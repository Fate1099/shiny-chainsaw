#!/ usr / bin / env python3
"""Regenerate tests/spec/commonmark.txt from the official CommonMark spec.

The fixture is the spec source verbatim: the CommonMark spec.txt already uses
the 32-backtick example fences the harness reads, so no transformation is
needed. Pinning the tag makes the fixture reproducible and lets a bump be
checked with a diff instead of a hand edit.

Usage:
    python3 scripts/gen-commonmark-spec.py [output] [--version 0.31.2]
"""

import argparse
import hashlib
import sys
import urllib.request

FENCE = "`" * 32
DEFAULT_VERSION = "0.31.2"
URL = "https://raw.githubusercontent.com/commonmark/commonmark-spec/{}/spec.txt"


def fetch(url):
    with urllib.request.urlopen(url) as response:
        return response.read().decode("utf-8")


def count_examples(text):
    return sum(1 for line in text.splitlines() if line.strip() == FENCE + " example")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default="tests/spec/commonmark.txt")
    parser.add_argument("--version", default=DEFAULT_VERSION)
    args = parser.parse_args()

    text = fetch(URL.format(args.version))
    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write(text)

    print(
        "wrote %d examples (%s) to %s"
        % (count_examples(text), hashlib.sha256(text.encode()).hexdigest()[:16], args.output)
    )


if __name__ == "__main__":
    main()
