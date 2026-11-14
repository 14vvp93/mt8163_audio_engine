from __future__ import annotations

import contextlib
import io
import unittest

from audit_primary_hal_p2_factory_callback_xrefs import (
    read_arm_word_at,
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
        self.assertIn("SETUP_CALLBACK_GLOBAL_CALLBACK_GOT=0x001a2b80", output.getvalue())
        self.assertIn("SETUP_CALLBACK_GLOBAL_COOKIE_GOT=0x001a2b7c", output.getvalue())

    def test_rejects_changed_literal_word(self) -> None:
        broken = self.disassembly.replace("000c4248", "000c4249")
        with self.assertRaises(RuntimeError):
            verify_setup_callback_global_slots(broken)


if __name__ == "__main__":
    unittest.main()
