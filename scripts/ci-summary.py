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
        print(f"{suite.get('name')}: {tests} tests, {failed} failed, {skipped} skipped")
        for case in suite.iter("testcase"):
            for kind in ("failure", "error"):
                problem = case.find(kind)
                if problem is not None:
                    message = (problem.get("message") or "").splitlines()
                    print(f"  FAILED {case.get('name')}: {message[0][:300] if message else ''}")
            if case.find("skipped") is not None:
                print(f"  SKIPPED {case.get('name')}")
print(f"total: {totals[0]} tests, {totals[1]} failed, {totals[2]} skipped")
print("=== end of summary ===")
