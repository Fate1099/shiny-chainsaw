#!/usr/bin/env python3
"""Regenerate tests/spec/gfm.txt from the official GFM spec page.

The vendored spec file in the repository had mangled emphasis examples
(nested <strong>/<em> pairs collapsed), so this script rebuilds the
example blocks from https://github.github.com/gfm/ which is the
canonical rendering of the GitHub Flavored Markdown spec (0.29-gfm).

Usage:
    python3 scripts/gen-gfm-spec.py [output]
"""

import html
import re
import sys
import urllib.request

URL = "https://github.github.com/gfm/"
FENCE = "`" * 32
HEADER_RE = re.compile(r'<h([2-4])[^>]*>(.*?)</h\1>', re.S)
EXAMPLE_RE = re.compile(
    r'<div class="example" id="example-(\d+)">(.*?)</div>\s*</div>', re.S
)
CODE_RE = re.compile(
    r'<pre><code class="language-(markdown|html)">(.*?)</code></pre>', re.S
)


def clean(text):
    text = html.unescape(text)
    # The rendered page wraps literal spaces in spans for visibility.
    text = text.replace('<span class="space"> </span>', " ")
    text = re.sub(r"<span[^>]*>", "", text)
    text = text.replace("</span>", "")
    return text.replace("\u2192", "\t")


def strip_tags(text):
    return clean(re.sub(r"<[^>]+>", " ", text)).strip()


# Sections that belong to a GFM extension get a flag on their example fence,
# exactly like the cmark test harness does.  Everything else is plain GFM core.
EXTENSION_FLAGS = {
    "task list items": "tasklist",
    "tables": "table",
    "strikethrough": "strikethrough",
    "autolinks": "autolink",
    "disallowed raw html": "tagfilter",
}


def extension_flag(section):
    if not section.endswith("(extension)"):
        return None
    name = section[: -len(" (extension)")].strip().lower()
    name = re.sub(r"^\d+(\.\d+)*\s+", "", name)
    return EXTENSION_FLAGS.get(name)


def fetch(url):
    with urllib.request.urlopen(url) as response:
        return response.read().decode("utf-8")


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "tests/spec/gfm.txt"
    page = fetch(URL)

    # Positions of section headers so each example can be attributed to one.
    headers = [(m.start(), strip_tags(m.group(2))) for m in HEADER_RE.finditer(page)]
    examples = []
    for m in EXAMPLE_RE.finditer(page):
        number = int(m.group(1))
        body = m.group(2)
        blocks = CODE_RE.findall(body)
        if len(blocks) < 2:
            continue
        section = ""
        for pos, title in headers:
            if pos < m.start():
                section = title
        markdown = clean(blocks[0][1]).rstrip("\n")
        expected = clean(blocks[1][1]).rstrip("\n")
        examples.append((number, section, markdown, expected))

    lines = [
        "---",
        "title: GitHub Flavored Markdown Spec",
        "version: 0.29-gfm",
        "date: '2019-04-06'",
        "license: '[CC-BY-SA 4.0](http://creativecommons.org/licenses/by-sa/4.0/)'",
        "...",
        "",
        "Extracted from https://github.github.com/gfm/ by scripts/gen-gfm-spec.py.",
        "",
    ]
    last_section = None
    for number, section, markdown, expected in examples:
        if section != last_section:
            lines.append("## " + section)
            lines.append("")
            last_section = section
        flag = extension_flag(section)
        lines.append(FENCE + " example" + (" " + flag if flag else ""))
        lines.append(markdown)
        lines.append(".")
        if expected:
            lines.append(expected)
        lines.append(FENCE)
        lines.append("")

    with open(out_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    print("wrote %d examples to %s" % (len(examples), out_path))


if __name__ == "__main__":
    main()
