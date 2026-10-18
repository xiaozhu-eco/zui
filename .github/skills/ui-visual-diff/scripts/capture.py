"""Capture a page through the difftest browser driver.

visual-diff compares our render against a real browser. Edge's `--screenshot` flag
photographs the page as soon as its load event fires, which is too early for a scripted
page: `example/applications/reader` fills its shelf from `fetch("books/catalog.json")`
and only then drops its booting state, so the command line capture came back with an
empty page and every percentage it produced was meaningless. `--virtual-time-budget`
does not help either - virtual time races through the page's animation frames and the
fetches never settle.

This helper drives the same CDP `Browser` the difftest already uses: navigate, wait for
a selector to appear, let the page settle, then read the surface. Usage:

    python capture.py --url http://localhost:8765/reader.html --out shot.png \
        --width 1100 --height 720 --selector ".books .card" --wait-ms 15000
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "tests" / "difftest" / "chrome"))
sys.path.insert(0, str(REPO / "tests" / "difftest"))

from cdp import CdpError  # noqa: E402
from driver import DEFAULT_ENV, Browser, find_browser, load_env  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="capture a page with the difftest driver")
    parser.add_argument("--url", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--width", type=int, default=1100)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--selector", default="")
    parser.add_argument("--wait-ms", type=int, default=15000)
    parser.add_argument("--env", default=str(DEFAULT_ENV))
    parser.add_argument("--browser", default=None)
    args = parser.parse_args(argv)

    env = load_env(Path(args.env))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    browser = Browser(find_browser(env, args.browser), env, (args.width, args.height))
    try:
        browser.start()
    except (CdpError, RuntimeError, OSError) as exc:
        print(f"capture: browser did not start: {exc}", file=sys.stderr)
        return 1
    try:
        browser.set_viewport(args.width, args.height)
        browser.session.call("Page.navigate", {"url": args.url})
        deadline = time.monotonic() + max(1.0, args.wait_ms / 1000.0)
        seen = 0
        while time.monotonic() < deadline:
            state = browser.evaluate("document.readyState")
            if state == "complete":
                if not args.selector:
                    break
                seen = browser.evaluate(
                    f"document.querySelectorAll({json.dumps(args.selector)}).length"
                )
                if isinstance(seen, int) and seen > 0:
                    break
            time.sleep(0.15)
        else:
            print(
                f"capture: {args.selector or 'page'} did not appear in {args.wait_ms} ms "
                f"(last count {seen})",
                file=sys.stderr,
            )
        browser.settle(3)
        out.write_bytes(browser.capture_png())
    finally:
        browser.stop()

    print(f"capture: {out} ({out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
