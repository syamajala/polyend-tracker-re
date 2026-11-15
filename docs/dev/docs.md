# Documentation

This site is built from `docs/` in the repository with
[MkDocs](https://www.mkdocs.org/) and the
[Material](https://squidfunk.github.io/mkdocs-material/) theme, and published to GitHub
Pages by `.github/workflows/docs.yml`.

## Edit and preview

Docs change in the same pull request as the feature they describe.

```sh
python3 -m venv .venv && .venv/bin/pip install "mkdocs<2" mkdocs-material mike
python3 tools/docs_synth_pages.py  # the Synths pages, from ../opentracker-synths
.venv/bin/mkdocs serve            # http://127.0.0.1:8000, reloads on save
.venv/bin/mkdocs build --strict   # what CI runs: broken links fail
```

Pages are listed in `mkdocs.yml` (`nav:`). Images go in `docs/images/`.

## Screenshots

Screenshots come from the simulator, so they can be regenerated whenever a screen
changes:

```sh
make testsongs && make -C sim
tools/docs_screens.sh            # all of them, or: tools/docs_screens.sh song performance
```

Each shot in `tools/docs_screens.sh` is a short simulator script run on a fresh scratch
card: either the generated test songs with "BG World 1" open, or an empty project with a
generated example DX7 bank. No third-party content goes into the images.

## Versions

Publishing uses [mike](https://github.com/jimporter/mike), which keeps one copy of the
site per version on the `gh-pages` branch:

- every push to `main` updates **dev**;
- every release tag (`v*`, the same tags that make a release) adds its own version,
  named after the tag, and moves the **latest** alias to it. The site opens on
  **latest**, so readers get the docs for the firmware they can download; the version
  picker at the top switches to **dev** or an older release.

A release's Synths pages come from the synth repository's `main` at the time of the tag,
as its card modules do (`release.yml`).

## Synth help files

Each synth's help lives next to its code in the module repository,
`modules/<synth>/help/<NAME>.md` (`NAME` = the descriptor's name, e.g. `BTYPE`): a guide
for the docs, then the `# Settings` reference that the Edit Patch help box reads
(format in `src/core/modules/synth_help.h`, user view in
[Edit Patch and synth help](../guide/edit-patch.md)).

- `make help` (and `make firmware`) writes the card copies, the reference only, to
  `build/<profile>/Help/Synths/` (`tools/synth_help_card.py`); the release puts them in
  `Modules.zip`.
- `tools/docs_synth_pages.py [MODULES_DIR]` writes the **Synths** pages of this site,
  `docs/synths/` (not in git): the whole file per synth, the reference one heading level
  down, and an overview. The docs workflow runs it on the synth repository's `main`;
  run it once before `mkdocs serve`. A new synth also needs its line in `mkdocs.yml`.
- `make host-test` (`tests/synth_help_test.cpp`) checks every help file of the profile's
  modules against the descriptor: names, full coverage, no markup, the widest font fits.
- Config's help box texts work the same way: `help/Config.md` in this repository (`##` a
  category, `###` an option), on the card as `Help/Config.md`; `tests/config_screen_test.cpp`
  checks it against the Config menu.
- Describe what a setting does. Wording about how something sounds goes in only after it
  has been heard on the device.
