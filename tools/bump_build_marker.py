#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path
from datetime import datetime
import re

BUILD_LINE_RE = re.compile(r"^// Build Marker: .*$")
NOTE_LINE_RE = re.compile(r"^// Note: .*$")


def bump_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    build_line = f"// Build Marker: {stamp} (Local, Last Updated)"
    note_line = "// Note: Update this timestamp whenever agents modifies this file."

    changed = False
    # Find existing build marker
    idx = next((i for i, s in enumerate(lines) if BUILD_LINE_RE.match(s)), None)
    if idx is not None:
        if lines[idx] != build_line:
            lines[idx] = build_line
            changed = True
        # Ensure the next line is the correct note
        note_idx = idx + 1 if idx + 1 < len(lines) else None
        if note_idx is None or not NOTE_LINE_RE.match(lines[note_idx]):
            # Insert note and a blank line after it if missing
            insert_at = idx + 1
            lines[insert_at:insert_at] = [note_line, ""]
            changed = True
        else:
            if lines[note_idx] != note_line:
                lines[note_idx] = note_line
                changed = True
            # Ensure there is a blank line after note
            if note_idx + 1 >= len(lines) or lines[note_idx + 1] != "":
                lines.insert(note_idx + 1, "")
                changed = True
    else:
        # No build marker: insert at file start with a blank line after
        lines = [build_line, note_line, ""] + lines
        changed = True

    if changed:
        path.write_text("\n".join(lines) + ("\n" if text.endswith("\n") else ""), encoding="utf-8")
    return changed


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("Usage: bump_build_marker.py <file> [<file> ...]", file=sys.stderr)
        return 2
    rc = 0
    for arg in argv[1:]:
        p = Path(arg)
        if not p.exists():
            print(f"Not found: {p}", file=sys.stderr)
            rc = 1
            continue
        changed = bump_file(p)
        print(f"{p}: {'updated' if changed else 'no change'}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

