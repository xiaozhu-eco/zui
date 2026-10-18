"""Regenerate tests/interaction_census.json.

The id lists below are the output of `AppInteract.DumpInteractionCensus`
(`tests/app_interact/interaction_census_test.cpp`), which loads each page and prints
`frame::Access::interactive_ids`. Rerun that test, paste the ids here, then run this script to
refresh the id -> covering-test map by scanning the handler test sources. The guard
`AppInteract.InteractionCensusMatchesEveryPage` fails when a page's live set drifts from the file.

    .\\build\\windows-x64\\zui_app_interact.exe --gtest_filter=AppInteract.DumpInteractionCensus
    python tests/tools/interaction_census.py
"""

from __future__ import annotations

import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

# page rel path -> (id list from DumpInteractionCensus, test files to search)
PAGES: dict[str, tuple[str, list[str], list[str]]] = {
    "album": (
        "example/applications/album/index.html",
        ["add-album", "album-form-cancel", "album-name", "confirm-cancel", "confirm-ok",
         "delete-album", "light-album", "light-close", "light-delete", "light-next", "light-note",
         "light-prev", "lightbox", "restore", "stage", "zoom-in", "zoom-out"],
        ["tests/app_interact/album_handlers_test.cpp"],
    ),
    "calendar": (
        "example/applications/calendar/index.html",
        ["color-clay", "color-gold", "color-green", "color-sea", "confirm-cancel", "confirm-ok",
         "create", "day-0", "day-1", "day-2", "day-3", "day-4", "day-5", "day-6", "dow-0", "dow-1",
         "dow-2", "dow-3", "dow-4", "dow-5", "dow-6", "editor-cancel", "editor-remove", "next",
         "prev", "restore", "today", "view-month", "view-week"],
        ["tests/app_interact/calendar_handlers_test.cpp"],
    ),
    "chat": (
        "example/applications/chat/index.html",
        ["confirm-cancel", "confirm-ok", "create", "creator-cancel", "draft", "jump", "log", "q",
         "remove", "restore"],
        ["tests/app_interact/chat_handlers_test.cpp"],
    ),
    "contacts": (
        "example/applications/contacts/index.html",
        ["confirm-cancel", "confirm-ok", "create", "d-email", "d-phone", "editor-cancel", "g-family",
         "g-friend", "g-other", "g-work", "q", "restore", "star-only"],
        ["tests/app_interact/contacts_handlers_test.cpp"],
    ),
    "kanban": (
        "example/applications/kanban/index.html",
        ["add-column", "add-{{id}}", "board", "card-{{id}}", "col-{{id}}", "confirm-cancel",
         "confirm-ok", "editor-cancel", "editor-remove", "name-{{id}}", "restore"],
        ["tests/app_interact/kanban_handlers_test.cpp"],
    ),
    "particle": (
        "example/applications/particle/index.html",
        [],
        ["tests/app_interact/particle_handlers_test.cpp"],
    ),
    "writer": (
        "example/applications/writer/index.html",
        [],
        ["tests/app_interact/writer_handlers_test.cpp"],
    ),
    "zdb": (
        "example/applications/zdb/index.html",
        ["clear-sql", "detail-close", "export-db", "limit-100", "limit-200", "limit-25", "limit-50",
         "mode-data", "mode-query", "mode-structure", "obj-{{index}}", "object-filter", "open-file",
         "open-sample", "page-next", "page-prev", "run-sql", "sql-input", "struct-parent"],
        ["tests/app_interact/zdb_handlers_test.cpp"],
    ),
    # zmd's page is the prototype's markup, copied verbatim: almost every control declares its
    # action as `data-act` / `data-fmt` / `data-set` plus one delegated listener, so it is outside
    # the census (which enumerates id-bearing `on*` handlers) and outside the binder too. The two
    # controls the page does mark with `oninput` are the Markdown source and the find field, and
    # those two are what the census sees. Recorded in example/applications/zmd/README.md.
    "zmd": (
        "example/applications/zmd/index.html",
        ["mdc-a-source", "zmd-find-q"],
        ["tests/app_interact/zmd_page_handlers_test.cpp"],
    ),
    "reader": (
        "example/applications/reader/reader.html",
        ["bookmark_btn", "chapter_next", "ch{{index}}", "clear_btn", "find_query", "flow_wrap",
         "open{{index}}", "remove{{index}}", "resume", "shelf_query", "sort_btn", "toc_query",
         "toc_toggle", "tray_scrim"],
        ["tests/app_interact/reader_handlers_test.cpp"],
    ),
    "hello": (
        "example/tests/hello/hello.html",
        ["inc", "name", "nav_home", "nav_projects", "nav_settings", "save"],
        ["tests/app_interact/example_tests_handlers_test.cpp",
         "tests/app_interact/example_programs_test.cpp"],
    ),
    "studio": (
        "example/tests/studio/studio.html",
        ["nav_inspect", "nav_log", "open", "reshuffle", "theme_dawn", "theme_night"],
        ["tests/app_interact/example_tests_handlers_test.cpp",
         "tests/app_interact/example_programs_test.cpp"],
    ),
    # No id-bearing handlers (no session, or handlers bound without ids); the guard keeps them so a
    # later handler addition shows up as a census diff.
    "checks": ("example/tests/checks/checks.html", [], ["tests/app_interact/example_tests_handlers_test.cpp"]),
    "transition": ("example/tests/transition/transition.html", [], ["tests/app_interact/example_tests_handlers_test.cpp"]),
    "flow": ("example/tests/flow/flow.html", [], ["tests/app_interact/example_programs_test.cpp"]),
    "style": ("example/tests/style/style.html", [], ["tests/app_interact/example_programs_test.cpp"]),
    "table": ("example/tests/table/table.html", [], ["tests/app_interact/example_programs_test.cpp"]),
    "text": ("example/tests/text/text.html", [], ["tests/app_interact/example_programs_test.cpp"]),
    # Ids are scanned from the page (handler ids, or radios when the page has no on* handlers).
    "share": (
        "example/applications/share/index.html",
        [],
        ["tests/app_interact/share_handlers_test.cpp"],
    ),
    "file-explorer": (
        "example/applications/file-explorer/index.html",
        [],
        ["tests/app_interact/file_explorer_handlers_test.cpp"],
    ),
}


_TAG = re.compile(r"<([A-Za-z][\w:-]*)\b([^<>]*)>", re.S)
_ATTR = re.compile(
    r"""([:@A-Za-z_][\w:.-]*)\s*=\s*("[^"]*"|'[^']*'|[^\s"'=<>`]+)"""
)


def _attr_map(blob: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for match in _ATTR.finditer(blob):
        value = match.group(2)
        if value[:1] in "\"'":
            value = value[1:-1]
        out[match.group(1).lower()] = value
    return out


def _tags(html: str):
    return [(match.group(1).lower(), _attr_map(match.group(2))) for match in _TAG.finditer(html)]


def handler_ids(html: str) -> list[str]:
    found: list[str] = []
    for _tag, attrs in _tags(html):
        ident = attrs.get("id", "")
        if not ident:
            continue
        if any(key.startswith("on") and len(key) > 2 for key in attrs):
            found.append(ident)
    return sorted(set(found))


def radio_ids(html: str) -> list[str]:
    found: list[str] = []
    for tag, attrs in _tags(html):
        if tag == "input" and attrs.get("type", "").lower() == "radio" and attrs.get("id"):
            found.append(attrs["id"])
    return sorted(set(found))


def unkeyed_handlers(html: str) -> list[str]:
    found: list[str] = []
    for _tag, attrs in _tags(html):
        if attrs.get("id"):
            continue
        for key, value in attrs.items():
            if key.startswith("on") and len(key) > 2 and value:
                found.append(value)
    return sorted(set(found))


def test_bodies(paths: list[str]) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for rel in paths:
        src = (REPO / rel).read_text(encoding="utf-8")
        marks = [(m.group(1), m.start()) for m in re.finditer(r"TEST\(AppInteract,\s*(\w+)\)", src)]
        for i, (name, pos) in enumerate(marks):
            end = marks[i + 1][1] if i + 1 < len(marks) else len(src)
            out.append((name, src[pos:end]))
    return out


def prefix_of(identifier: str) -> str:
    m = re.match(r"[A-Za-z_]+-?[A-Za-z_]*", identifier)
    return m.group(0) if m else identifier


# Sibling controls that share one handler with a control a test drives literally. The id is driven
# by the same handler case; the mapping is by handler identity, read off the page's `on*` attribute.
OVERRIDES: dict[tuple[str, str], list[str]] = {
    ("calendar", "color-gold"): ["CalendarColorRadioPaintsTheSavedEvent"],
    ("calendar", "color-green"): ["CalendarColorRadioPaintsTheSavedEvent"],
    ("calendar", "color-sea"): ["CalendarColorRadioPaintsTheSavedEvent"],
    ("contacts", "g-family"): ["ContactsFilterChipNarrowsToTheGroup"],
    ("contacts", "g-friend"): ["ContactsFilterChipNarrowsToTheGroup"],
    ("contacts", "g-other"): ["ContactsFilterChipNarrowsToTheGroup"],
    ("contacts", "g-work"): ["ContactsFilterChipNarrowsToTheGroup"],
    ("zdb", "open-file"): ["ZdbOpenAndExportAreWiredToTheHostPickers"],
    ("zdb", "export-db"): ["ZdbOpenAndExportAreWiredToTheHostPickers"],
    ("reader", "toc_toggle"): ["ReaderOnTocShowsTheTray"],
    ("reader", "toc_query"): ["ReaderOnTypeKeyEscapeClosesTheOverlays"],
    ("reader", "find_query"): ["ReaderOnHitJumpsToTheMatch"],
    ("reader", "shelf_query"): ["ReaderOnTypeKeyEscapeClosesTheOverlays"],
    ("reader", "clear_btn"): ["ReaderOnClearResetsTheReadingProgress"],
    ("reader", "resume"): ["ReaderOnResumeOpensTheMostRecentBook"],
    ("reader", "bookmark_btn"): ["ReaderOnBookmarkTogglesTheMark"],
    ("reader", "flow_wrap"): ["ReaderOnReadingTapTogglesTheChrome"],
    # zmd's two id-bearing handlers read a control the test drives by its id constant rather
    # than by the literal string, so the text scan cannot find them.
    ("zmd", "mdc-a-source"): ["ZmdSourceTypingUpdatesTheCountsRow"],
    ("zmd", "zmd-find-q"): ["ZmdFindFieldCountsTheMatches"],
}


def covering_tests(identifier: str, bodies: list[tuple[str, str]]) -> list[str]:
    hits = [name for name, body in bodies if f'"{identifier}"' in body]
    if hits:
        return sorted(set(hits))
    pre = prefix_of(identifier)
    templated = "{{" in identifier or identifier[-1].isdigit()
    if templated and pre:
        return sorted({name for name, body in bodies if f'"{pre}' in body})
    return []


def main() -> int:
    census: dict[str, dict] = {}
    unmatched: list[tuple[str, str]] = []
    for app, (rel, ids, test_files) in PAGES.items():
        scan = app in ("share", "particle", "writer", "file-explorer")
        html = (REPO / rel).read_text(encoding="utf-8") if scan else ""
        if app in ("share", "particle", "writer"):
            ids = handler_ids(html)
        elif app == "file-explorer":
            ids = radio_ids(html)
        bodies = test_bodies(test_files)
        entry: dict = {"file": rel, "ids": {}}
        if app == "file-explorer":
            entry["controls"] = "radio"
        for identifier in ids:
            hits = OVERRIDES.get((app, identifier)) or covering_tests(identifier, bodies)
            if not hits:
                unmatched.append((app, identifier))
            entry["ids"][identifier] = hits
        if app == "share":
            entry["unkeyed"] = {}
            for name in unkeyed_handlers(html):
                hits = sorted({test for test, body in bodies if f'"{name}"' in body})
                if not hits:
                    unmatched.append((app, name))
                entry["unkeyed"][name] = hits
        census[app] = entry
    out = REPO / "tests" / "interaction_census.json"
    out.write_text(json.dumps(census, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} ({sum(len(e['ids']) for e in census.values())} ids)")
    if unmatched:
        print("UNMATCHED (add a covering test or the guard will fail):")
        for app, identifier in unmatched:
            print(f"  {app}: {identifier}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
