# ro-maji-wasm-converter

Japanese romaji typing engine compiled to WebAssembly (WASM).

## Contents

- `index.html` — browser demo/game UI
- `romaji_engine.wasm` — compiled WASM engine
- `romaji_engine.cpp` — WASM source code

## Features

- Multiple valid romaji input forms
- `x` / `l` small-kana input
- Youon (拗音) handling
- Sokuon `っ` handling
- `ん` ambiguity handling
- Foreign-sound kana support
- Actual accepted input reflected in the sample romaji
- Browser-side WASM execution

## Run locally

Serve the directory over HTTP. Opening `index.html` directly with `file://`
may be blocked by the browser's WASM/fetch security rules.

Example:

```bash
python3 -m http.server 8000
```

Then open:

`http://localhost:8000/`

## GitHub Pages

This repository can be published with GitHub Pages.

Keep these two files in the same directory:

- `index.html`
- `romaji_engine.wasm`

## Source / Build

The WASM engine source is in `romaji_engine.cpp`.

The browser-facing JavaScript is intentionally kept thin: it handles
keyboard events, calls the WASM exports, and updates the HTML UI.
The romaji input state/logic lives in the WASM engine.

## License

No license file is included by default. Add the license you prefer before
publishing if you want to grant reuse rights explicitly.
