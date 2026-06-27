# pebble-ereader

EPUB reader for the (re)Pebble watch. Phone holds the book and streams text pages to the watch over AppMessage.

## Architecture

- **`ereader/src/c/ereader.c`** — watchapp. Scrollable text page, UP/DOWN to flip pages, SELECT to re-fetch. Persists current page index across launches.
- **`ereader/src/pkjs/index.js`** — PebbleKit JS running inside the Pebble mobile app. Holds the parsed book in `localStorage`, responds to page requests, opens the configuration page.
- **`config/config.html`** — configuration page launched from the Pebble app's settings. User picks an EPUB; the page extracts text with `epub.js`, paginates it, and ships the pages back through `pebblejs://close#<json>`.

The watch does not store the book — it just renders the current page sent by the phone. Page index syncs both ways: the phone is authoritative for last-read position; the watch echoes its current index on each request and persists it locally as a fallback when the phone is unreachable.

## Build & run

Prereqs: `pebble-tool` (installed via `uv tool install pebble-tool`), the SDK (`pebble sdk install latest`).

```
cd ereader
pebble build
pebble install --emulator basalt        # or your watch over Bluetooth
pebble logs --emulator basalt
```

## Hosting the config page

`Pebble.openURL()` needs a real URL. Easiest options:

1. Push `config/config.html` to a `gh-pages` branch and use the GitHub Pages URL.
2. For dev: `python3 -m http.server 8000 --directory config` plus an `ngrok http 8000` tunnel.

Then set `CONFIG_URL` near the top of `ereader/src/pkjs/index.js`.

## Known limits

- Whole book is held in the phone's `localStorage` (typically 5–10 MB). Books over a couple of MB of plain text may not fit. Long-term fix: move to IndexedDB or stream pages on demand from the source file.
- Plain text only. Images, formatting, and chapter navigation are dropped.
- Page size is fixed at upload time. Re-upload to change it.
