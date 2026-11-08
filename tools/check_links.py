#!/usr/bin/env python3
"""Checks the relative links and anchors in the repository's markdown: every link to a file must
resolve, and every #anchor must name a heading of that file (GitHub's slugs). Web links are not
fetched. Run by tools/kb.py check.

    python tools/check_links.py [ROOT]
    python tools/check_links.py --selftest

Exit status: 0 every link resolves, 1 a broken one (each printed), 2 usage.
"""
import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote

SKIP_DIRS = {".git", ".pio", ".venv", "build", "vendor", "_deps", "node_modules"}
SKIP_PATHS = {Path("docs/temp")}  # gitignored notes
LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def slug(heading):
    """GitHub's anchor for a heading: lower case, punctuation dropped, spaces to hyphens."""
    s = re.sub(r"[`*~]", "", heading.strip().lower())
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def anchors(md):
    seen, out = {}, set()
    for line in strip_code(md.read_text(encoding="utf-8")).splitlines():
        m = re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", line)
        if m:
            base = slug(m.group(1))
            n = seen.get(base, 0)
            seen[base] = n + 1
            out.add(base if n == 0 else f"{base}-{n}")
    return out


def strip_code(text):
    text = re.sub(r"^```.*?^```", "", text, flags=re.S | re.M)
    return re.sub(r"`[^`\n]*`", "", text)


def markdown_files(root):
    for p in sorted(root.rglob("*.md")):
        rel = p.relative_to(root)
        if SKIP_DIRS & set(rel.parts[:-1]) or any(rel.is_relative_to(s) for s in SKIP_PATHS):
            continue
        yield p


def broken(root):
    cache = {}
    for md in markdown_files(root):
        for target in LINK.findall(strip_code(md.read_text(encoding="utf-8"))):
            if re.match(r"[a-z]+:", target):  # http:, https:, mailto:
                continue
            path, _, anchor = unquote(target).partition("#")
            dest = (md.parent / path).resolve() if path else md
            if not dest.exists():
                yield f"{md.relative_to(root)}: {target}: no such file"
            elif anchor and dest.suffix == ".md":
                if dest not in cache:
                    cache[dest] = anchors(dest)
                if anchor not in cache[dest]:
                    yield f"{md.relative_to(root)}: {target}: no such heading"


def selftest():
    assert slug("HIL on the PC and the board") == "hil-on-the-pc-and-the-board"
    assert slug("kb.py") == "kbpy"
    assert slug("`timing_check.py`") == "timing_checkpy"
    assert slug("Options and environment") == "options-and-environment"
    assert strip_code("a `[x](y)` b\n```\n[z](w)\n```\n") == "a  b\n\n"
    print("check_links: selftest ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--selftest", action="store_true", help="check the slugs and exit")
    a = p.parse_args()
    if a.selftest:
        selftest()
        return 0
    bad = list(broken(a.root.resolve()))
    for b in bad:
        print(b)
    print(f"check_links: {len(bad)} broken" if bad else "check_links: ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
