"""Prints a short JUnit summary at the end of a CI job, so the result can be
read from the last lines of the log. Usage: python3 scripts/ci-summary.py DIR..."""

import pathlib
import sys
import xml.etree.ElementTree as ElementTree

print("=== test summary ===")
totals = [0, 0, 0]
for directory in sys.argv[1:]:
    for path in sorted(pathlib.Path(directory).rglob("TEST-*.xml")):
        suite = ElementTree.parse(path).getroot()
        tests = int(suite.get("tests", 0))
        failed = int(suite.get("failures", 0)) + int(suite.get("errors", 0))
        skipped = int(suite.get("skipped", 0))
        totals = [totals[0] + tests, totals[1] + failed, totals[2] + skipped]
        # Instrumented results put every class in one file named after the
        # first, so each failure prints its own class.
        print(f"{path.name}: {tests} tests, {failed} failed, {skipped} skipped")
        for case in suite.iter("testcase"):
            for kind in ("failure", "error"):
                problem = case.find(kind)
                if problem is not None:
                    text = (problem.get("message") or problem.text or "").strip().splitlines()
                    print(f"  FAILED {case.get('classname', '').split('.')[-1]}.{case.get('name')}")
                    for line in text[:8]:
                        print(f"    {line[:240]}")
            if case.find("skipped") is not None:
                print(f"  SKIPPED {case.get('classname', '').split('.')[-1]}.{case.get('name')}")
print(f"total: {totals[0]} tests, {totals[1]} failed, {totals[2]} skipped")
for log in sys.argv[1:]:
    crash = pathlib.Path(log) / "logcat.txt"
    if crash.is_file():
        print("--- logcat (errors and the app's log) ---")
        print(crash.read_text(errors="replace")[-12000:])
print("=== end of summary ===")
