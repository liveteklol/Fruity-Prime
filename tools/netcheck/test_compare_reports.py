"""Regression checks for pair-scoped netcheck report comparisons."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


COMPARER = Path(__file__).with_name("compare-reports.py")


def report(player, hunter, observer, whole_run_seen, pair_seen):
    return "\n".join((
        f"netcheck-hunter {player} {hunter}",
        "netcheck-needed spawn 1 single",
        "netcheck-parity spawn 0 0",
        f"netcheck {player} mine {player} spawn 10",
        f"netcheck {player} mine-with {observer} spawn 5",
        f"netcheck {player} saw {observer} spawn {whole_run_seen}",
        f"netcheck {player} saw-with {observer} spawn {pair_seen}",
        f"netcheck-shots {player} from {observer} received 1 fired 1 stale 0 pushed 0 "
        "abandoned 0 waiting 0 overdue 0 gaps 0 recovered 0 lost 0 pending 0 late 0",
        "",
    ))


class CompareReportsTests(unittest.TestCase):
    def test_prefers_observer_counter_from_the_pair_window(self):
        with tempfile.TemporaryDirectory() as temporary:
            first = Path(temporary, "first.txt")
            second = Path(temporary, "second.txt")
            first.write_text(report("Alice", "Samus", "Bob", 20, 5), encoding="utf-8")
            second.write_text(report("Bob", "Sylux", "Alice", 20, 5), encoding="utf-8")

            result = subprocess.run(
                [sys.executable, str(COMPARER), str(first), str(second)],
                check=False,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("within pair-scoped windows", result.stdout)
        self.assertNotIn("MISMATCH", result.stdout)
        self.assertIn("RESULT: PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
