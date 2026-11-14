import unittest

import audit_android9_refbase_lifetime as auditor


class RefBaseImplementationGateTests(unittest.TestCase):
    def test_complete_symbol_coverage_passes(self) -> None:
        missing = {
            "SYSTEM LIBUTILS": set(),
            "VNDK-SP-28 LIBUTILS": set(),
        }
        status = auditor.refbase_implementation_status(True, missing)
        self.assertEqual(status, "COMPLETE")
        self.assertEqual(auditor.refbase_implementation_exit_code(status), 0)

    def test_missing_symbol_in_either_candidate_fails(self) -> None:
        missing = {
            "SYSTEM LIBUTILS": set(),
            "VNDK-SP-28 LIBUTILS": {"RefBase::decStrong"},
        }
        status = auditor.refbase_implementation_status(True, missing)
        self.assertEqual(status, "INCOMPLETE")
        self.assertEqual(auditor.refbase_implementation_exit_code(status), 1)

    def test_missing_bridge_dependency_fails(self) -> None:
        missing = {
            "SYSTEM LIBUTILS": set(),
            "VNDK-SP-28 LIBUTILS": set(),
        }
        status = auditor.refbase_implementation_status(False, missing)
        self.assertEqual(status, "INCOMPLETE")
        self.assertEqual(auditor.refbase_implementation_exit_code(status), 1)

    def test_empty_library_inventory_fails_closed(self) -> None:
        status = auditor.refbase_implementation_status(True, {})
        self.assertEqual(status, "INCOMPLETE")
        self.assertEqual(auditor.refbase_implementation_exit_code(status), 1)


class ParseFunctionSymbolsTests(unittest.TestCase):
    def test_parses_arm_and_thumb_refbase_symbols(self) -> None:
        output = "\n".join([
            "   12: 00012341 48 FUNC GLOBAL DEFAULT 12 android::RefBase::createWeak(void const*) const",
            "   13: 00012400 96 FUNC GLOBAL DEFAULT 12 android::RefBase::weakref_type::attemptIncStrong(void const*)",
            "   14: 00012520 64 FUNC GLOBAL DEFAULT 12 android::RefBase::decStrong(void const*) const",
            "   15: 00012600 72 FUNC GLOBAL DEFAULT 12 android::RefBase::weakref_type::decWeak(void const*)",
        ])

        symbols = auditor.parse_function_symbols(output)

        self.assertEqual(
            symbols,
            [
                auditor.FunctionSymbol(
                    0x12340, 48, True,
                    "android::RefBase::createWeak(void const*) const",
                ),
                auditor.FunctionSymbol(
                    0x12400, 96, False,
                    "android::RefBase::weakref_type::attemptIncStrong(void const*)",
                ),
                auditor.FunctionSymbol(
                    0x12520, 64, False,
                    "android::RefBase::decStrong(void const*) const",
                ),
                auditor.FunctionSymbol(
                    0x12600, 72, False,
                    "android::RefBase::weakref_type::decWeak(void const*)",
                ),
            ],
        )

    def test_ignores_undefined_and_zero_sized_symbols(self) -> None:
        output = "\n".join([
            "   1: 00000000  0 FUNC GLOBAL DEFAULT UND android::RefBase::createWeak(void const*) const",
            "   2: 00012340  0 FUNC GLOBAL DEFAULT 12 android::RefBase::createWeak(void const*) const",
            "   3: 00012350 24 FUNC GLOBAL DEFAULT 12 android::RefBase::createWeak(void const*) const",
        ])

        self.assertEqual(
            auditor.parse_function_symbols(output),
            [
                auditor.FunctionSymbol(
                    0x12350, 24, False,
                    "android::RefBase::createWeak(void const*) const",
                )
            ],
        )

    def test_finds_target_symbol_by_exact_demangled_fragment(self) -> None:
        symbols = [
            auditor.FunctionSymbol(
                0x1000, 32, False,
                "android::RefBase::createWeak(void const*) const",
            ),
            auditor.FunctionSymbol(
                0x2000, 48, True,
                "android::RefBase::weakref_type::attemptIncStrong(void const*)",
            ),
        ]

        self.assertEqual(
            auditor.find_target_symbols(
                symbols,
                auditor.TARGETS["weakref_type::attemptIncStrong"],
            ),
            [symbols[1]],
        )

    def test_disassembles_exact_function_extent(self) -> None:
        from pathlib import Path
        from unittest.mock import patch

        symbol = auditor.FunctionSymbol(0x1000, 0x40, True, "sample()")
        with patch.object(auditor, "run", return_value="disassembly") as run:
            result = auditor.disassemble_function(Path("libutils.so"), symbol)

        self.assertEqual(result, "disassembly")
        command = run.call_args.args[0]
        self.assertIn("--start-address=0x1000", command)
        self.assertIn("--stop-address=0x1040", command)
        self.assertIn("force-thumb", command)


if __name__ == "__main__":
    unittest.main()
