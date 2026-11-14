from __future__ import annotations

import contextlib
import io
import unittest

from audit_primary_hal_p2_factory_callback_xrefs import (
    read_arm_word_at,
    verify_factory_destructor_clears_setup_callback_globals,
    verify_setup_callback_global_slots,
)


class SetupCallbackGlobalGotTests(unittest.TestCase):
    def setUp(self) -> None:
        self.disassembly = "\n".join(
            (
                "   ce9b8: 000c4248 andeq r4, ip, r8, asr #4",
                "   ce9bc: 000c4238 andeq r4, ip, r8, lsr r2",
                "   cf26c: 000c3978 andeq r3, ip, r8, ror r9",
                "   cf270: 000c3968 andeq r3, ip, r8, ror #18",
            )
        ) + "\n"

    def test_reads_objdump_word_with_short_address(self) -> None:
        self.assertEqual(read_arm_word_at(self.disassembly, 0x000CE9B8), 0x000C4248)

    def test_resolves_registration_and_dispatch_to_same_got_slots(self) -> None:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            verify_setup_callback_global_slots(self.disassembly)
        self.assertIn("SETUP_CALLBACK_GLOBAL_GOT_ASSERTIONS: PASS", output.getvalue())
        self.assertIn("SETUP_CALLBACK_GLOBAL_CALLBACK_GOT=0x00192b80", output.getvalue())
        self.assertIn("SETUP_CALLBACK_GLOBAL_COOKIE_GOT=0x00192b7c", output.getvalue())

    def test_rejects_changed_literal_word(self) -> None:
        broken = self.disassembly.replace("000c4248", "000c4249")
        with self.assertRaises(RuntimeError):
            verify_setup_callback_global_slots(broken)


class FactoryDestructorGlobalClearTests(unittest.TestCase):
    def test_confirms_cookie_then_callback_clear_order(self) -> None:
        report = "\n".join(
            (
                "000c56e4 <android::AudioALSAHardware::~AudioALSAHardware()>:",
                " c5750: e59f0100 ldr r0, [pc, #256]",
                " c575c: e79f0000 ldr r0, [pc, r0]",
                " c5760: e5806000 str r6, [r0]",
                " c5764: e59f00f0 ldr r0, [pc, #240]",
                " c5768: e79f0000 ldr r0, [pc, r0]",
                " c576c: e5806000 str r6, [r0]",
                " c5770: e59f00e8 ldr r0, [pc, #232]",
                " c5774: e79f0000 ldr r0, [pc, r0]",
                " c5778: e5806000 str r6, [r0]",
                " c5858: 000cd414 andeq sp, ip, r4, lsl r4",
                " c585c: 000cd40c andeq sp, ip, ip, lsl #8",
                " c5860: 000cd404 andeq sp, ip, r4, lsl #8",
            )
        ) + "\n"
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            verify_factory_destructor_clears_setup_callback_globals(report)
        self.assertIn("SETUP_CALLBACK_GLOBAL_CLEAR_ASSERTIONS: PASS", output.getvalue())
        self.assertIn("cookie(0x00192b7c),callback(0x00192b80)", output.getvalue())


if __name__ == "__main__":
    unittest.main()
