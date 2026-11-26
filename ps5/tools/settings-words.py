#!/usr/bin/env python3
# PSGopher - the words of BENCH's settings, checked (docs/dev/settings-words.md).
#
#   settings-words.py check [dump.jsonl]   fail on a word that breaks the naming rules
#   settings-words.py list  [dump.jsonl]   every row: key, label kind, label
#
# The dump is written by the menu host test (ps5/tests/test_menu.cpp) from the rows frontend.cpp
# builds: one JSON line per section and per row. The inventory (ps5/tools/settings-words.json) says,
# for each key, whether its label is Gopher64's word for the setting ("exact", "composed" when the
# port or the activator is added) or written for PSGopher, and where Gopher64's text is.
# SPDX-License-Identifier: GPL-3.0-or-later
import fnmatch
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
INVENTORY = os.path.join(ROOT, "ps5", "tools", "settings-words.json")
DEFAULT_DUMP = os.path.join(ROOT, "build", "host-tests", "menu-work", "settings-words.jsonl")
GOPHER = os.path.join(ROOT, "vendor", "gopher64")

VERBS = {"Enables", "Determines", "Selects", "Sets", "Shows", "Controls", "Uses", "Applies", "Reduces", "Allows",
         "Prevents", "Forces", "Assigns", "Creates", "Deletes", "Searches", "Closes", "When",
         "Checks", "Downloads", "Stops", "Replaces", "Skips", "Puts"}
SMALL = {"ms", "a", "an", "the", "of", "for", "to", "in", "on", "and", "or", "as", "with", "from", "at", "by"}

# (pattern, why); applied to the text with its "double quoted" parts taken out
BANNED = [
    (r"PSGopher", "PSGopher"), (r"\bour\b", "our"), (r"\bown\b", "own"), (r"\bas before\b", "as before"),
    (r"\bnew\b", "new"), (r"\bnow\b", "now"), (r"\bv\d+(\.\d+)*\b|\b\d+\.\d+\.\d+\b", "a version number"),
    (r"\b(measured|was|were|previously|formerly)\b", "history"), (r"\bthis console\b", "on this console"),
    (r"parallel-rdp|\bRADV\b|Granite|Crux2|OGBox|\bMesa\b|wave32|speciali[sz]|\bTokio\b", "an implementation name"),
    (r"\b\w+\.(ini|json|log|elf|cpp|rs|slint|txt|sav|srm|gb|gbc|z64|zip)\b", "a file name"),
    (r"Experimental", "Experimental"), (r"next start|takes effect", "when it takes effect"), (r"PS5's", "PS5's"),
    (r"colour|\bcentre|\bgrey|licence|behaviour|favour|\bmaths\b|optimis|organis|customis|recognis|initialis|"
     r"normalis|synchronis|visualis|summaris|programme|\bmetre|travell|cancell|labell|\bdefence|\boffence",
     "a British spelling"),
]


def quoted_out(text):
    return re.sub(r'"[^"]*"', "", text)


def gopher_strings(source):
    """The quoted strings (@tr texts too, a trailing colon left off) on the lines of 'file:a' or 'file:a-b'."""
    name, _, lines = source.rpartition(":")
    first, _, last = lines.partition("-")
    first, last = int(first), int(last or first)
    with open(os.path.join(GOPHER, name), encoding="utf-8") as f:
        text = f.read().splitlines()
    found = set()
    for line in text[first - 1:last]:
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', line):
            found.add(s)
            found.add(s.rstrip(":"))
    return found


def gopher_default(field, index):
    """A config.rs default: the value of 'field:' in its Default (bool as 0/1, an array's index)."""
    with open(os.path.join(GOPHER, "src/ui/config.rs"), encoding="utf-8") as f:
        text = f.read()
    start = text.index("pub fn new() -> Config")
    m = re.search(r"\b" + re.escape(field) + r":\s*(\[[^\]]*\]|[\w.]+)", text[start:])
    if not m:
        return None
    value = m.group(1)
    if value.startswith("["):
        value = [v.strip() for v in value[1:-1].split(",")][index or 0]
    return {"true": 1, "false": 0}.get(value, int(value) if value.lstrip("-").isdigit() else None)


def load(dump):
    sections, rows = [], []
    with open(dump, encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            (rows if "key" in d else sections).append(d)
    return sections, rows


def entry_for(inventory, key):
    if key in inventory:
        return inventory[key]
    for pattern, e in inventory.items():
        if fnmatch.fnmatchcase(key, pattern):
            return e
    return None


def title_case_problem(label):
    for word in re.split(r"[ ,/]+", label):
        word = word.strip("()")
        if not word or not word[0].isalpha() or word.lower() in SMALL:
            continue
        if not word[0].isupper():
            return word
    return None


def check(dump):
    if not os.path.exists(dump):
        print("settings-words: no dump at %s (the menu host test writes it)" % dump, file=sys.stderr)
        return 2
    with open(INVENTORY, encoding="utf-8") as f:
        inventory = json.load(f)
    sections, rows = load(dump)
    errors = []
    used = set()
    seen_help = {}

    def bad(where, what):
        errors.append("%s: %s" % (where, what))

    def banned(where, text):
        plain = quoted_out(text)
        for pattern, why in BANNED:
            if re.search(pattern, plain, re.I if why in ("PSGopher", "a British spelling") else 0):
                bad(where, "%s in \"%s\"" % (why, text))

    for s in sections:
        where = "section %s" % s["section"]
        banned(where, s["section"])
        banned(where, s["text"])
        if title_case_problem(s["section"]):
            bad(where, "the name is not in Title Case")
        if s["text"].split(" ")[0].strip(",") not in VERBS:
            bad(where, "the text does not start with a verb: \"%s\"" % s["text"])
    for r in rows:
        key, where = r["key"], "%s (%s)" % (r["key"], r["label"])
        e = entry_for(inventory, key)
        if e is None:
            bad(where, "no entry in settings-words.json")
            continue
        used.add(next(k for k, v in inventory.items() if v is e))
        kind = e["label"]
        # the label
        if kind in ("exact", "composed"):
            strings = gopher_strings(e["label_source"])
            if kind == "exact" and r["label"] not in strings:
                bad(where, "the label is not Gopher64's (%s has %s)" % (e["label_source"], sorted(strings)))
            if kind == "composed" and not any(s and s in r["label"] for s in strings):
                bad(where, "the label does not carry Gopher64's word (%s)" % e["label_source"])
        elif kind == "written":
            if title_case_problem(r["label"]):
                bad(where, "the label is not in Title Case (\"%s\")" % title_case_problem(r["label"]))
        else:
            bad(where, "label kind %r is not exact, composed or written" % kind)
        # the choices
        names = [c[0] for c in r["choices"]]
        if e.get("choices") == "exact":
            strings = gopher_strings(e["choices_source"])
            for n in names:
                if n.replace(" (Default)", "") not in strings:
                    bad(where, "choice \"%s\" is not Gopher64's (%s)" % (n, e["choices_source"]))
        data = e.get("choices") == "data"
        # the words
        banned(where, r["label"])
        if not data:
            for n in names:
                banned(where, n)
                if title_case_problem(n):
                    bad(where, "choice \"%s\" is not in Title Case" % n)
        banned(where, r["value"])
        if not r["help"].strip():
            bad(where, "the description is empty")
        else:
            banned(where, r["help"])
            if r["kind"] != "info" and r["help"].split(" ")[0] not in VERBS:
                bad(where, "the description does not start with a verb: \"%s\"" % r["help"])
            if r["help"].count(". ") + r["help"].count(".\n") > 2 or len(r["help"]) > 290:
                bad(where, "the description is longer than three sentences or the box")
            if r["help"] in seen_help and not e.get("shared_description"):
                bad(where, "the description is also %s's" % seen_help[r["help"]])
            seen_help.setdefault(r["help"], key)
        # (Default)
        marked = [c for c in r["choices"] if "(Default)" in c[0]]
        plain_switch = names == ["Off", "On"]
        if r["default"] is None:
            if marked:
                bad(where, "has a (Default) choice but no known default")
        elif plain_switch:
            if marked:
                bad(where, "a plain Off/On switch has no (Default) mark")
        elif r["choices"]:
            if len(marked) != 1 or marked[0][1] != r["default"]:
                bad(where, "the (Default) choice is not the row's default %s: %s" % (r["default"], names))
        # the default against Gopher64's config
        if "default_field" in e and r["default"] is not None:
            index = int(key.rsplit(".", 1)[1]) if key[-1].isdigit() else None
            want = gopher_default(e["default_field"], index)
            if want is not None and want != r["default"]:
                bad(where, "the default %s is not Gopher64's %s (config.rs %s)" % (r["default"], want, e["default_field"]))
    for key in inventory:
        if key not in used:
            bad("settings-words.json", "the key %s matches no row (stale entry)" % key)
    for line in errors:
        print("settings-words: " + line, file=sys.stderr)
    if errors:
        print("settings-words: %d problem(s)" % len(errors), file=sys.stderr)
        return 1
    print("settings-words: %d rows, %d sections: the words are in order" % (len(rows), len(sections)))
    return 0


def listing(dump):
    with open(INVENTORY, encoding="utf-8") as f:
        inventory = json.load(f)
    for r in load(dump)[1]:
        e = entry_for(inventory, r["key"]) or {}
        print("%s\t%s\t%s" % (r["key"], e.get("label", "?"), r["label"]))
    return 0


def main(argv):
    if len(argv) < 2 or argv[1] not in ("check", "list"):
        print(__doc__ or "usage: settings-words.py check|list [dump.jsonl]", file=sys.stderr)
        return 2
    dump = argv[2] if len(argv) > 2 else DEFAULT_DUMP
    return check(dump) if argv[1] == "check" else listing(dump)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
