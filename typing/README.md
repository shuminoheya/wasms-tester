# ro-maji-wasm-converter

Japanese romaji typing engine running in the browser with WebAssembly.

## Files

- `index.html` — demo / typing game
- `romaji_engine.wasm` — compiled WebAssembly engine
- `romaji_engine.cpp` — WASM source code
- `README.md` — this document

## Put these 4 files together

The HTML loads the WASM with:

```js
fetch("./romaji_engine.wasm")
```

So `index.html` and `romaji_engine.wasm` must be in the same directory.

## GitHub

You can place these four files directly in a repository. For GitHub Pages,
serve the repository as a static site. The HTML first tries
`WebAssembly.instantiateStreaming()` and falls back to `fetch()` +
`WebAssembly.instantiate()` if streaming compilation is not available or
fails.

## Local test

A local HTTP server is recommended instead of opening `index.html` with
`file://`:

```bash
python3 -m http.server 8000
```

Then open:

```text
http://localhost:8000/
```

## Source

The WASM source is `romaji_engine.cpp`.

The browser JavaScript inside `index.html` is only the WASM/UI bridge:
keyboard input, WASM calls, and screen updates.
