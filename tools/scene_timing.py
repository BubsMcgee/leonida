"""Scene timing checks for a windows-boot run: the per-window frame rate the runtime prints
(`[rt] submit phases ... N frames/s ...`) and the boot digest's presents, faults and captures.

Reads only numbers from the logs, which is what may leave the machine (AGENTS.md, "No game data"). Standard library
only; `python -I`.

    python -I tools/scene_timing.py <name>.log --digest <name>.digest.txt --start-at 60 --min-fps 5

Exit 0 when every check passes, 1 when one fails, 2 when the log has no timing lines.
"""
import argparse
import json
import re
import sys

PHASES = re.compile(
    r"^\[rt\] submit phases t=(\d+)s: (\d+) submissions in ([\d.]+) s "
    r"\(([\d.]+)/s, (\d+) in all\), ([\d.]+) frames/s;.*?([\d.]+)% of the wall time")

PRESENTS = re.compile(r"^presents:\s*PresentX ok (\d+), failed (\d+)")
FAULTS = re.compile(r"^faults:\s*(.+)$")
FRAMES = re.compile(r"^frames:\s*(\d+) captured, (\d+) non-black")


def parse_log(text):
    """[(t_seconds, submissions, fps, subs_per_s, wall_percent)], in file order."""
    rows = []
    for line in text.splitlines():
        m = PHASES.match(line)
        if m:
            rows.append({
                "t": int(m.group(1)),
                "submissions": int(m.group(2)),
                "fps": float(m.group(6)),
                "subs_per_s": float(m.group(4)),
                "wall_percent": float(m.group(7)),
            })
    return rows


def parse_digest(text):
    out = {"presents_ok": 0, "presents_failed": 0, "faults": "", "captured": 0, "non_black": 0}
    for line in text.splitlines():
        m = PRESENTS.match(line)
        if m:
            out["presents_ok"], out["presents_failed"] = int(m.group(1)), int(m.group(2))
            continue
        m = FAULTS.match(line)
        if m:
            out["faults"] = m.group(1).strip()
            continue
        m = FRAMES.match(line)
        if m:
            out["captured"], out["non_black"] = int(m.group(1)), int(m.group(2))
    return out


def check(rows, digest, start_at, min_fps, require_frames):
    """A list of (ok, message) for the checks; the reasons are in the messages."""
    checks = []
    window = [r for r in rows if r["t"] >= start_at]
    if not window:
        checks.append((False, f"no timing window at or after t={start_at}s"))
    else:
        worst = min(window, key=lambda r: r["fps"])
        mean = sum(r["fps"] for r in window) / len(window)
        checks.append((worst["fps"] >= min_fps,
                       f"lowest fps after t={start_at}s is {worst['fps']:.2f} at t={worst['t']}s "
                       f"(need >= {min_fps}); mean {mean:.2f}"))
    if digest:
        checks.append((digest["presents_failed"] == 0,
                       f"PresentX failures: {digest['presents_failed']} (need 0)"))
        no_faults = digest["faults"].strip().lower() in ("none", "")
        checks.append((no_faults, f"faults: {digest['faults'] or 'not recorded'} (need none)"))
        if require_frames:
            checks.append((digest["non_black"] >= require_frames,
                           f"non-black captures: {digest['non_black']} of {digest['captured']} "
                           f"(need >= {require_frames})"))
    return checks


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--digest", default=None)
    ap.add_argument("--start-at", type=int, default=60, help="ignore timing windows before this second")
    ap.add_argument("--min-fps", type=float, default=5.0, help="lowest allowed frames/s in the window")
    ap.add_argument("--require-frames", type=int, default=0, help="non-black captures required (0: no check)")
    ap.add_argument("--json", action="store_true", help="print the parsed data and checks as JSON")
    args = ap.parse_args(argv)

    with open(args.log, encoding="utf-8", errors="replace") as f:
        rows = parse_log(f.read())
    digest = None
    if args.digest:
        with open(args.digest, encoding="utf-8", errors="replace") as f:
            digest = parse_digest(f.read())

    if not rows:
        print(f"no '[rt] submit phases' timing lines in {args.log} (is LEONIDA_SUBMIT_PHASES_SECONDS 0?)",
              file=sys.stderr)
        return 2

    checks = check(rows, digest, args.start_at, args.min_fps, args.require_frames)
    if args.json:
        print(json.dumps({"windows": rows, "digest": digest,
                          "checks": [{"ok": ok, "message": msg} for ok, msg in checks]}, indent=2))
    else:
        print("   t(s)   fps   subs/s   wall%")
        for r in rows:
            print(f"{r['t']:6d} {r['fps']:6.2f} {r['subs_per_s']:8.2f} {r['wall_percent']:6.0f}")
        print()
        for ok, msg in checks:
            print(f"{'PASS' if ok else 'FAIL'} {msg}")
    return 0 if all(ok for ok, _ in checks) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
