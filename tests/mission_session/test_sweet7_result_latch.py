import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class Sweet7ResultLatchTests(unittest.TestCase):
    def test_failure_and_success_share_one_result_latch(self):
        script = (ROOT / "scm/scripts/SWEET7.txt").read_text(encoding="utf-8")
        self.assertIn(
            "591@ = 0 // shared mission result announced: 0 pending, 1 failed, 2 passed",
            script,
        )
        failure = re.search(
            r":SWEET7_32404\s+(.*?)(?=\s*:SWEET7_32422)", script, re.S
        ).group(1)
        success = re.search(
            r":SWEET7_32422\s+(.*?)(?=\s*:SWEET7_32503)", script, re.S
        ).group(1)
        for body, terminal_value in ((failure, "1"), (success, "2")):
            self.assertRegex(body, r"if\s+591@ <> 0\s+then\s+return\s+end")
            self.assertIn(f"591@ = {terminal_value}", body)
        self.assertLess(failure.index("591@ = 1"), failure.index("M_FAIL"))
        self.assertLess(success.index("591@ = 2"), success.index("M_PASSR"))


if __name__ == "__main__":
    unittest.main()
