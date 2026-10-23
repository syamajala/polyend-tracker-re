# Documentation

This site is built from `docs/` in the repository with
[MkDocs](https://www.mkdocs.org/) and the
[Material](https://squidfunk.github.io/mkdocs-material/) theme, and published to GitHub
Pages by `.github/workflows/docs.yml`.

## Edit and preview

Docs change in the same pull request as the feature they describe.

```sh
python3 -m venv .venv && .venv/bin/pip install "mkdocs<2" mkdocs-material mike
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
- once the fork makes releases, each release tag will add its version, and the version
  picker at the top will switch between them.
