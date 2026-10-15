from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "atmosphere_test"))
from audit_assets import resolve


class AtmosphereAssetTests(unittest.TestCase):
    def test_inheritance_replaces_curves_but_merges_nested_fields(self):
        parent = {"FogDensities": [{"Hour": 0, "Value": 0.2}], "FogOptions": {"IgnoreFogLimits": True}}
        child = {"Parent": "Parent", "FogDensities": [], "FogOptions": {"Other": 1}}
        result = resolve("Child", {"Parent": parent, "Child": child})
        self.assertEqual(result["FogDensities"], [])
        self.assertEqual(result["FogOptions"], {"IgnoreFogLimits": True, "Other": 1})
        self.assertEqual(parent["FogDensities"][0]["Value"], 0.2)

    def test_inheritance_cycles_are_reported(self):
        with self.assertRaisesRegex(ValueError, "inheritance cycle"):
            resolve("First", {"First": {"Parent": "Second"}, "Second": {"Parent": "First"}})


if __name__ == "__main__":
    unittest.main()
