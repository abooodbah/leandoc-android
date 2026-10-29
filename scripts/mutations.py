"""Mutation check: changes one load-bearing line at a time, runs the tests
that should notice, records the first failing test, and restores the file.

    python3 scripts/mutations.py mutations/reader.json jvm
    python3 scripts/mutations.py mutations/reader.json device   # needs an emulator

Each entry names a file, an exact text to replace (it must occur once), the
replacement, and which suite should catch it ("jvm" or "device"). The result
is a Markdown table on standard output."""

import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(__file__).resolve().parent.parent
SUITES = {
    "jvm": (["./gradlew", "--no-daemon", "-q", "testDebugUnitTest", "--rerun-tasks"],
            "app/build/test-results/testDebugUnitTest"),
    "device": (["./gradlew", "--no-daemon", "-q", "connectedDebugAndroidTest"],
               "app/build/outputs/androidTest-results/connected"),
}


def failures(results):
    found = []
    for path in sorted((ROOT / results).rglob("TEST-*.xml")):
        suite = ElementTree.parse(path).getroot()
        for case in suite.iter("testcase"):
            if case.find("failure") is not None or case.find("error") is not None:
                found.append(f"{suite.get('name').split('.')[-1]}.{case.get('name')}")
    return found


def main():
    entries = json.loads((ROOT / sys.argv[1]).read_text())
    wanted = sys.argv[2]
    print("| Mutation | File | First failing test | Failing tests |")
    print("| --- | --- | --- | ---: |")
    for entry in entries:
        if entry["suite"] != wanted:
            continue
        path = ROOT / entry["file"]
        original = path.read_text()
        if original.count(entry["old"]) != 1:
            print(f"| {entry['name']} | {entry['file']} | pattern not found once | - |", flush=True)
            continue
        command, results = SUITES[wanted]
        subprocess.run(["rm", "-rf", str(ROOT / results)])
        path.write_text(original.replace(entry["old"], entry["new"]))
        try:
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            failed = failures(results)
            if not failed and run.returncode != 0:
                failed = ["(build failed: " + run.stderr.strip().splitlines()[-1][:120] + ")"] if run.stderr.strip() else ["(build failed)"]
        finally:
            path.write_text(original)
        first = failed[0] if failed else "NOT CAUGHT"
        print(f"| {entry['name']} | `{entry['file'].split('/')[-1]}` | {first} | {len(failed)} |", flush=True)


main()
