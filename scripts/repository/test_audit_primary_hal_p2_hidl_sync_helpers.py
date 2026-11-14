from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import patch

import audit_primary_hal_p2_hidl_sync_helpers as auditor


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

    def test_unresolved_target_is_reported_without_inventing_a_range(self) -> None:
        lines = auditor.unresolved_report_lines(
            0x284DC,
            RuntimeError("no FUNC symbol contains helper target 0x000284dc"),
        )

        self.assertIn("CALL_TARGET 0x000284dc -> UNRESOLVED", lines)
        self.assertTrue(any("no FUNC symbol contains" in line for line in lines))
        self.assertTrue(any("No guessed function boundary" in line for line in lines))
        self.assertFalse(any("FULL_FUNCTION_RANGE" in line for line in lines))


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
