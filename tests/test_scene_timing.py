"""tools/scene_timing.py: the per-window frame-rate and boot-digest checks. Standard library only, no game files."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools import scene_timing as st  # noqa: E402

LOG = """\
[rt] present: frame cap 60.00 fps (LEONIDA_FPS_CAP, 16.666 ms)
[rt] submit phases t=5s: 40 submissions in 5.0 s (8.00/s, 40 in all), 6.50 frames/s; ms per submission (max): replay 10.00 (50) wait 1.00 (5); 40% of the wall time
[rt] submit phases t=65s: 30 submissions in 5.0 s (6.00/s, 70 in all), 5.50 frames/s; ms per submission (max): replay 12.00 (80) wait 2.00 (9); 55% of the wall time
[rt] submit phases t=125s: 20 submissions in 5.0 s (4.00/s, 90 in all), 3.50 frames/s; ms per submission (max): replay 20.00 (120) wait 3.00 (12); 70% of the wall time
"""

DIGEST_OK = """\
presents: PresentX ok 2299, failed 0
faults:   none
frames:   39 captured, 39 non-black; last mean rgb 0.40 0.41 0.39; brightest 0.90 0.88 0.80
"""

DIGEST_BAD = """\
presents: PresentX ok 100, failed 3
faults:   device lost (VK_ERROR_DEVICE_LOST)
frames:   10 captured, 0 non-black
"""


class SceneTiming(unittest.TestCase):
    def test_parses_the_window_lines(self):
        rows = st.parse_log(LOG)
        self.assertEqual([r["t"] for r in rows], [5, 65, 125])
        self.assertAlmostEqual(rows[0]["fps"], 6.5)
        self.assertAlmostEqual(rows[1]["subs_per_s"], 6.0)
        self.assertAlmostEqual(rows[2]["wall_percent"], 70.0)

    def test_parses_the_digest(self):
        d = st.parse_digest(DIGEST_OK)
        self.assertEqual((d["presents_ok"], d["presents_failed"]), (2299, 0))
        self.assertEqual(d["faults"].lower(), "none")
        self.assertEqual((d["captured"], d["non_black"]), (39, 39))

    def test_a_healthy_run_passes(self):
        checks = st.check(st.parse_log(LOG), st.parse_digest(DIGEST_OK), 60, 3.0, 1)
        self.assertTrue(all(ok for ok, _ in checks), checks)

    def test_a_low_frame_rate_fails(self):
        checks = st.check(st.parse_log(LOG), None, 60, 4.0, 0)
        self.assertFalse(checks[0][0])
        self.assertIn("t=125s", checks[0][1])

    def test_the_window_starts_at_start_at(self):
        # The 6.5 fps window at t=5 is before start-at 60 and must not mask the slow 3.5 at t=125.
        checks = st.check(st.parse_log(LOG), None, 0, 5.0, 0)
        self.assertFalse(checks[0][0])

    def test_faults_and_failed_presents_fail(self):
        checks = st.check(st.parse_log(LOG), st.parse_digest(DIGEST_BAD), 60, 0.0, 1)
        failed = [msg for ok, msg in checks if not ok]
        self.assertTrue(any("PresentX failures" in m for m in failed), failed)
        self.assertTrue(any(m.startswith("faults:") for m in failed), failed)
        self.assertTrue(any("non-black captures" in m for m in failed), failed)

    def test_main_exit_codes(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "run.log")
            digest = os.path.join(tmp, "run.digest.txt")
            with open(log, "w", encoding="utf-8", newline="\n") as f:
                f.write(LOG)
            with open(digest, "w", encoding="utf-8", newline="\n") as f:
                f.write(DIGEST_OK)
            self.assertEqual(st.main([log, "--digest", digest, "--start-at", "60", "--min-fps", "3",
                                      "--require-frames", "1"]), 0)
            self.assertEqual(st.main([log, "--digest", digest, "--start-at", "60", "--min-fps", "9"]), 1)
            empty = os.path.join(tmp, "empty.log")
            with open(empty, "w", encoding="utf-8", newline="\n") as f:
                f.write("[rt] nothing here\n")
            self.assertEqual(st.main([empty]), 2)


if __name__ == "__main__":
    unittest.main()
