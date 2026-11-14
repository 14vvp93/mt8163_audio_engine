import unittest

import audit_primary_hal_p2_hidl_device_lifetime as auditor


class DirectCallParserTests(unittest.TestCase):
    def test_parses_thumb_bl_to_refbase_interworking_veneer(self) -> None:
        line = (
            "   1b8e2: f00c fc7b bl 281dc "
            "<__aeabi_llsl@@Base+0x13c>"
        )

        self.assertEqual(
            auditor.parse_direct_call(line),
            (0x1B8E2, 0x281DC, "__aeabi_llsl@@Base+0x13c"),
        )

    def test_parses_arm_blx_and_normalizes_thumb_target_bit(self) -> None:
        line = "   00010220: e12fff33 blx 00020441 <callback_thunk>"

        self.assertEqual(
            auditor.parse_direct_call(line),
            (0x10220, 0x20440, "callback_thunk"),
        )

    def test_ignores_non_call_and_unresolved_indirect_branch(self) -> None:
        self.assertIsNone(auditor.parse_direct_call("   1b8e0: 2000 movs r0, #0"))
        self.assertIsNone(auditor.parse_direct_call("   1b8e2: 4798 blx r3"))

    def test_parses_relocation_offset(self) -> None:
        self.assertEqual(
            auditor.parse_relocation_offset("0001b8e2  00000017 R_ARM_RELATIVE"),
            0x1B8E2,
        )
        self.assertIsNone(auditor.parse_relocation_offset("Relocation section '.rel.dyn'"))


if __name__ == "__main__":
    unittest.main()
