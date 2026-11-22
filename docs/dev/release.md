# Cutting a release

A release is a set of files built from one clean commit: one merged flash image per board, the TF card pack, the web tool for offline use, checksums and notes. The tooling writes them into a local folder; nothing is uploaded by the tools.

## Before you start
1. Set the version: the first line of `version.txt` is the version, a date such as `2026.11.22` (a second release on the same day gets a suffix, for example `2026.11.22.1`).
2. Add the version's section to `CHANGELOG.md` (heading `## <version> ...`). The section is copied into the release notes. State known gaps honestly (the "Known limitations" list in `README.md` is the starting point).
3. Commit everything. The release tool refuses a dirty tree, a stale build, and two builds from different commits.
4. Run the gates: `FAST=1 make test`, `make lint`, `make lint-app`, `make sim-test`, `make sim-diff`, `make webcheck`, `make size` and `make size-fakepod` (the internal-RAM ceilings in `tools/check/diram_budget.txt` must hold), `make fonts-check`.

## Build the release files
Write a notes file with the three languages as JSON (`{"zh-TW": "...", "zh-CN": "...", "en": "..."}`), then:

```
make release NOTES=notes.json
```

This builds both boards (`make build`, `make build-fakepod`), publishes the web tool into `dist/` (`tools/web_publish.py`) and runs `tools/release.py`, which writes `release/<version>/`:

| File | What it is |
|---|---|
| `jukedial-<version>-round.bin`, `jukedial-<version>-fakepod.bin` | one merged flash image per board (bootloader at 0x2000, partition table at 0x8000, app at 0x10000, fonts at 0xC00000), written at offset 0; each is re-split and verified (hashes, layout, board marker) |
| `jukedial-<version>-card.zip` | the TF card contents (unzip into the card's root) |
| `jukedial-<version>-webtool.zip` | the web tool for localhost use (`serve.sh`, `serve.bat`) |
| `SHA256SUMS`, `RELEASE_NOTES.md` | checksums and the notes (the JSON notes plus the changelog section) |

The release needs `fonts/ui-*.ttf` (they are in the repository). `tools/release.py` makes the card pack itself with `tools/gen/make_card_zip.py` unless you pass `--card FILE`; the HUD faces in it come from `make faces-zip` (needs Pillow and the fonts, see `docs/guide/en/20-build-flash.md`).

## Publish
Create a release on the project's release page (the same files on each host you use), tag it with the version, upload the files of `release/<version>/` as assets and paste `RELEASE_NOTES.md`. The hosted web tool (`make web-publish`, output in `dist/`) contains no firmware: users pick the merged image they downloaded.

Check the downloaded image once before announcing it: `sha256sum -c SHA256SUMS`, then flash it to a board you can recover (`make backup` first, see the guide) and boot it.
