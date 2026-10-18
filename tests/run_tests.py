"""One entry point for the test mechanism: ctest, the kernel plane, and the five verdicts.

The pieces already existed (`ctest`, `tests/difftest/run.py`, `tests/concern_report.py`) but each
needed its own arguments and a matching `--run-id`; this wires them the way
docs/TEST_ARCHITECTURE.md "频率" describes, so the mechanism can be run with one command:

    PR Debug   python tests/run_tests.py
    Nightly    python tests/run_tests.py --kernel --run-id <id>
    Full       python tests/run_tests.py --kernel --browser --run-id <id>

`--require-difftest-report` is passed whenever the kernel ran, so a missing, stale, or mismatched
report is `job=fail` instead of silently `not-run`. The exit code is the aggregation's, and a
ctest failure is kept as a failure even when the aggregation itself is happy.
"""

from __future__ import annotations

import argparse
import datetime
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent


def _run(command: list[str], cwd: Path) -> int:
    print("+ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=str(cwd)).returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=str(REPO / "build" / "windows-x64"),
                        help="configured build directory that ctest runs in")
    parser.add_argument("--kernel", action="store_true",
                        help="also run the difftest kernel plane (tests/difftest/run.py)")
    parser.add_argument("--browser", action="store_true",
                        help="kernel runs the L0 Chrome probe too (implies --kernel)")
    parser.add_argument("--run-id", default=None,
                        help="stamp shared by the kernel report and the aggregation")
    parser.add_argument("--case", action="append", default=None,
                        help="limit the kernel to these cases (repeatable)")
    parser.add_argument("--skip-ctest", action="store_true",
                        help="reuse the last junit file instead of running ctest")
    args = parser.parse_args(argv)

    build = Path(args.build).resolve()
    junit = build / "junit-all.xml"
    run_id = args.run_id or "local-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    ctest_failed = False

    if not args.skip_ctest:
        if not (build / "CTestTestfile.cmake").exists():
            print(f"[run_tests] no ctest build in {build}; configure a preset first", file=sys.stderr)
            return 2
        code = _run(["ctest", "--output-junit", str(junit)], build)
        ctest_failed = code != 0
        if ctest_failed:
            # The job still aggregates: a red suite must show up in the verdict lines, not just here.
            print(f"[run_tests] ctest exited {code}; aggregating the JUnit anyway\n", flush=True)
    elif not junit.exists():
        print(f"[run_tests] --skip-ctest but {junit} does not exist", file=sys.stderr)
        return 2

    kernel = args.kernel or args.browser
    report_json = REPO / "build" / "difftest" / "run" / "report.json"
    subset = bool(args.case)
    if kernel:
        command = [sys.executable, str(HERE / "difftest" / "run.py"), "check",
                   "--run-id", run_id]
        if not args.browser:
            command.append("--engine-only")
        if not args.case:
            # A full kernel run also asserts the TESTPLAN scenario matrix: a feature that is
            # declared "aligned" but has no golden case fails cmd_check instead of only showing up
            # as a smaller covered-count. Subset runs skip it (they cannot be complete by design).
            command.append("--require-coverage")
        for case in args.case or []:
            command += ["--case", case]
        code = _run(command, REPO)
        # cmd_check's exit code is ignored on purpose: the kernel gate is the known_red difference,
        # which the aggregation computes from the report this run just wrote.
        print(f"[run_tests] kernel check exited {code}", flush=True)
        if subset:
            # known_red is a full-suite delta: every case that this run did not cover would read as
            # "waiver no longer red, delete it". A --case subset is a debugging run, so it is not fed
            # to the aggregation at all rather than being fed as a misleading failure.
            print("[run_tests] --case subset: kernel stays not-run (the waiver list needs the full run)\n",
                  flush=True)
        else:
            print("", flush=True)

    aggregate = [sys.executable, str(HERE / "concern_report.py"), "--junit", str(junit)]
    if kernel and not subset:
        aggregate += ["--difftest-report", str(report_json), "--expect-run-id", run_id,
                      "--require-difftest-report"]
    verdict = _run(aggregate, REPO)
    if verdict != 0 or ctest_failed:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
