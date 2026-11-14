from __future__ import annotations

import lzma
import struct
import unittest
from pathlib import Path
from unittest.mock import patch

import audit_primary_hal_p2_hidl_sync_helpers as auditor


class GnuDebugDataTests(unittest.TestCase):
    @staticmethod
    def make_elf_with_sections(section_names: bytes, payload: bytes | None) -> bytes:
        section_count = 3 if payload is not None else 2
        section_table_offset = 52
        names_offset = section_table_offset + section_count * 40
        payload_offset = names_offset + len(section_names)
        data = bytearray(payload_offset + (len(payload) if payload is not None else 0))
        data[:4] = b"\x7fELF"
        data[4] = 1
        data[5] = 1
        struct.pack_into("<I", data, 32, section_table_offset)
        struct.pack_into("<HHH", data, 46, 40, section_count, 1)
        names_index = section_names.index(b".shstrtab")
        struct.pack_into(
            "<IIIIII", data, section_table_offset + 40,
            names_index, 3, 0, 0, names_offset, len(section_names),
        )
        if payload is not None:
            debug_name_index = section_names.index(b".gnu_debugdata")
            struct.pack_into(
                "<IIIIII", data, section_table_offset + 80,
                debug_name_index, 1, 0, 0, payload_offset, len(payload),
            )
            data[payload_offset:payload_offset + len(payload)] = payload
        data[names_offset:names_offset + len(section_names)] = section_names
        return bytes(data)

    def test_extracts_xz_compressed_gnu_debugdata(self) -> None:
        mini_elf = b"\x7fELF\x01\x01mini-debug-elf"
        names = b"\0.shstrtab\0.gnu_debugdata\0"
        binary = self.make_elf_with_sections(names, lzma.compress(mini_elf))

        self.assertEqual(auditor.extract_gnu_debugdata(binary), mini_elf)

    def test_returns_none_when_gnu_debugdata_is_absent(self) -> None:
        names = b"\0.shstrtab\0"
        binary = self.make_elf_with_sections(names, None)

        self.assertIsNone(auditor.extract_gnu_debugdata(binary))

    def test_rejects_non_elf_input(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "invalid ELF header"):
            auditor.extract_gnu_debugdata(b"not-an-elf")


class ParseFunctionsTests(unittest.TestCase):
    def test_parses_thumb_and_arm_function_symbols(self) -> None:
        output = "\n".join([
            "   12: 000284dd 48 FUNC GLOBAL DEFAULT 12 android::RefBase::incStrong(void const*)",
            "   13: 00028520 32 FUNC GLOBAL DEFAULT 12 android::RefBase::decStrong(void const*)",
            "   14: 00028540  0 FUNC GLOBAL DEFAULT 12 zero_sized",
            "   15: 00028550 16 OBJECT GLOBAL DEFAULT 12 not_a_function",
        ])

        functions = auditor.parse_functions(output)

        self.assertEqual(
            functions,
            [
                (0x284DC, 48, "android::RefBase::incStrong(void const*)", True),
                (0x28520, 32, "android::RefBase::decStrong(void const*)", False),
            ],
        )

    def test_resolves_smallest_containing_function_and_aliases(self) -> None:
        functions = [
            (0x28000, 0x200, "outer()", False),
            (0x28040, 0x40, "helper_alias_b()", True),
            (0x28040, 0x40, "helper_alias_a()", True),
        ]

        resolved = auditor.resolve_function(0x28050, functions)

        self.assertEqual(
            resolved,
            (0x28040, 0x40, "helper_alias_a() | helper_alias_b()", True),
        )

    def test_rejects_target_outside_all_function_ranges(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "no FUNC symbol contains"):
            auditor.resolve_function(0x30000, [(0x20000, 0x100, "unrelated()", False)])

    def test_all_verified_targets_report_complete_resolution(self) -> None:
        self.assertEqual(
            auditor.helper_target_resolution_status(
                list(auditor.HELPER_TARGETS),
                [],
            ),
            "COMPLETE",
        )

    def test_any_unresolved_target_keeps_resolution_incomplete(self) -> None:
        resolved = list(auditor.HELPER_TARGETS[:-1])
        unresolved = [auditor.HELPER_TARGETS[-1]]
        self.assertEqual(
            auditor.helper_target_resolution_status(resolved, unresolved),
            "INCOMPLETE",
        )

    def test_unresolved_target_is_reported_without_inventing_a_range(self) -> None:
        lines = auditor.unresolved_report_lines(
            0x284DC,
            RuntimeError("no FUNC symbol contains helper target 0x000284dc"),
        )

        self.assertIn("CALL_TARGET 0x000284dc -> UNRESOLVED", lines)
        self.assertTrue(any("no FUNC symbol contains" in line for line in lines))
        self.assertTrue(any("No guessed function boundary" in line for line in lines))
        self.assertFalse(any("FULL_FUNCTION_RANGE" in line for line in lines))


    def test_resolves_thumb_to_arm_veneer_across_executable_sections(self) -> None:
        names = b"\0.shstrtab\0.text\0.plt\0"
        section_table_offset = 52
        section_count = 4
        names_offset = section_table_offset + section_count * 40
        text_address = 0x1000
        text = bytearray(0x50)
        plt_address = 0x1050
        plt = bytearray(0x40)
        text_offset = names_offset + len(names)
        plt_offset = text_offset + len(text)
        target = 0x1020
        struct.pack_into(
            "<HHIII", text, target - text_address,
            0x4778, 0x46C0, 0xE59FC000, 0xE08CF00F, 0x20,
        )

        data = bytearray(plt_offset + len(plt))
        data[:4] = b"\x7fELF"
        data[4] = 1
        data[5] = 1
        struct.pack_into("<I", data, 32, section_table_offset)
        struct.pack_into("<HHH", data, 46, 40, section_count, 1)
        struct.pack_into(
            "<IIIIIIIIII", data, section_table_offset + 40,
            names.index(b".shstrtab"), 3, 0, 0, names_offset, len(names), 0, 0, 1, 0,
        )
        struct.pack_into(
            "<IIIIIIIIII", data, section_table_offset + 80,
            names.index(b".text"), 1, 0x6, text_address, text_offset, len(text), 0, 0, 4, 0,
        )
        struct.pack_into(
            "<IIIIIIIIII", data, section_table_offset + 120,
            names.index(b".plt"), 1, 0x6, plt_address, plt_offset, len(plt), 0, 0, 4, 0,
        )
        data[names_offset:names_offset + len(names)] = names
        data[text_offset:text_offset + len(text)] = text
        data[plt_offset:plt_offset + len(plt)] = plt

        with patch.object(Path, "read_bytes", return_value=bytes(data)):
            self.assertEqual(
                auditor.resolve_veneer_target(Path("bridge.so"), target),
                (0x1050, ".text", ".plt"),
            )

    def test_rejects_non_veneer_helper_target(self) -> None:
        names = b"\0.shstrtab\0.text\0"
        section_table_offset = 52
        section_count = 3
        names_offset = section_table_offset + section_count * 40
        text_address = 0x1000
        text_offset = names_offset + len(names)
        text = bytes(0x100)
        data = bytearray(text_offset + len(text))
        data[:4] = b"\x7fELF"
        data[4] = 1
        data[5] = 1
        struct.pack_into("<I", data, 32, section_table_offset)
        struct.pack_into("<HHH", data, 46, 40, section_count, 1)
        struct.pack_into(
            "<IIIIIIIIII", data, section_table_offset + 40,
            names.index(b".shstrtab"), 3, 0, 0, names_offset, len(names), 0, 0, 1, 0,
        )
        struct.pack_into(
            "<IIIIIIIIII", data, section_table_offset + 80,
            names.index(b".text"), 1, 0x6, text_address, text_offset, len(text), 0, 0, 4, 0,
        )
        data[names_offset:names_offset + len(names)] = names
        data[text_offset:text_offset + len(text)] = text

        with patch.object(Path, "read_bytes", return_value=bytes(data)):
            with self.assertRaisesRegex(RuntimeError, "not the expected Thumb-to-ARM veneer"):
                auditor.resolve_veneer_target(Path("bridge.so"), 0x1020)

    def test_resolves_plt_symbol_from_exact_entry_header(self) -> None:
        output = (
            "bridge.so: file format elf32-littlearm\n"
            "Disassembly of section .plt:\n"
            "000193fc <android::RefBase::RefBase()@plt>:\n"
            "  193fc: e28fc600 add ip, pc, #0, 12\n"
        )
        with patch.object(auditor, "run", return_value=output) as run:
            symbol = auditor.resolve_plt_symbol(Path("bridge.so"), 0x193FC)

        self.assertEqual(symbol, "android::RefBase::RefBase()@plt")
        command = run.call_args.args[0]
        self.assertIn("--start-address=0x193fc", command)
        self.assertIn("--stop-address=0x19400", command)

    def test_resolves_complete_demangled_template_plt_symbol(self) -> None:
        symbol_name = (
            "android::hardware::MessageQueue<unsigned char, "
            "(android::hardware::MQFlavor)1>::~MessageQueue()@plt"
        )
        output = (
            "bridge.so: file format elf32-littlearm\\n"
            "Disassembly of section .plt:\\n"
            f"0001942c <{symbol_name}>:\\n"
            "  1942c: e28fc600 add ip, pc, #0, 12\\n"
        )
        with patch.object(auditor, "run", return_value=output):
            resolved = auditor.resolve_plt_symbol(Path("bridge.so"), 0x1942C)

        self.assertEqual(resolved, symbol_name)

    def test_disassembles_bounded_thumb_target_window(self) -> None:
        with patch.object(auditor, "run", return_value="bounded disassembly") as run:
            output = auditor.disassemble_target_window(Path("bridge.so"), 0x284DC)

        self.assertEqual(output, "bounded disassembly")
        command = run.call_args.args[0]
        self.assertIn("-D", command)
        self.assertNotIn("-M", command)
        self.assertNotIn("force-thumb", command)
        self.assertIn("--start-address=0x284dc", command)
        self.assertIn("--stop-address=0x2853c", command)

    def test_disassembles_entire_function_extent(self) -> None:
        with patch.object(auditor, "run", return_value="disassembly") as run:
            output = auditor.disassemble(Path("bridge.so"), 0x28040, 0x180, True)

        self.assertEqual(output, "disassembly")
        command = run.call_args.args[0]
        self.assertIn("--start-address=0x28040", command)
        self.assertIn("--stop-address=0x281c0", command)
        self.assertIn("-M", command)
        self.assertIn("force-thumb", command)


if __name__ == "__main__":
    unittest.main()
