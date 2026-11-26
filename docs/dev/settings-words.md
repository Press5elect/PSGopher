# The words of BENCH's settings

BENCH's labels, descriptions and choice names follow the owner's naming rules (Crux2's
`settings-inventory.md`, "The new words", with Gopher64 in PCSX2's place).

- **Labels are Gopher64's, exactly,** wherever Gopher64 has the setting: matched by its config key
  (`video.upscale`, `emulation.rewind`...), never by label. Its `@tr("...")` texts are in
  `vendor/gopher64/src/ui/gui/*.slint` (a trailing colon left off), the N64's input names in
  `src/ui/input_profile.rs`. Port rows add the port to Gopher64's word ("Port 1 · Enabled").
  Choice names are Gopher64's too where it names them (Resolution: 1x 2x 4x 8x).
- **Descriptions are written** (Gopher64 has none): one or two sentences, three at most, present
  tense, starting with a verb (Enables, Determines, Selects, Sets, Shows, Controls, Uses, Applies,
  Reduces, Allows, Prevents, Forces) or "When enabled, ..."; the cost or risk stated plainly; American
  spelling; labels and choice names in Title Case; the standard choice of a row with more than an
  Off/On pair marked "(Default)".
- **Never** in a label, description, choice or value: PSGopher (except in quotes), our, own, as
  before, new, now, version numbers, history, implementation names (parallel-rdp, RADV, Tokio...),
  file names, "Experimental", "next start", "PS5's". A row that applies at the next game shows no
  words about it (the board has no marker).

## The inventory and the check

`ps5/tools/settings-words.json` maps each row key (a pattern such as `input.transfer_pak.*` is
allowed) to its label kind: `exact` (Gopher64's word), `composed` (Gopher64's word with the port or
activator added) or `written`; `label_source` is Gopher64's file and line (`file:line` or
`file:first-last`); `choices` is `exact` (with `choices_source`), `written` or `data` (profile names,
DualSense control names); `default_field` is the `config.rs` field the row's default is checked
against; `shared_description` lets two rows share a description on purpose.

The menu host test (`ps5/tests/test_menu.cpp`) dumps every row BENCH builds
(`build/host-tests/menu-work/settings-words.jsonl`, one JSON line each: section, key, kind, label,
description, value, default, choices) and `run-host-tests.sh` runs

```
ps5/tools/settings-words.py check [dump.jsonl]
```

which fails when: a row has no inventory entry (or an entry has no row); an exact label or choice
is not on its Gopher64 source line; a banned word or a British spelling appears (words in double
quotes are left out); a description is empty, does not start with a verb, is longer than three
sentences, or is another row's by accident; a label or choice is not in Title Case; a "(Default)"
choice is not the row's default, or the default differs from Gopher64's `config.rs`.
`settings-words.py list` prints key, kind and label of every row.

A new row: give it a key in `bench()` (`frontend.cpp`), its default, and an entry in the JSON.

A row that shows only in some state (the updater's Download, Cancel, Install, Skip and Roll Back in
Console) is built every time and carries `shown`; the dump has it, the panel leaves it out while it
does not apply. Its label and description are fixed words (no version numbers: the version goes in
the value, filled in at run time).
