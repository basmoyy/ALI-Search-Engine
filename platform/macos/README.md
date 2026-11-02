# ALI — macOS Application

A native macOS browser and local search engine powered by the ALI C++ core.

## Requirements

- macOS 13 or later (arm64 / Apple Silicon)
- Xcode Command Line Tools
- Homebrew + OpenSSL 3

```sh
xcode-select --install
brew install cmake openssl@3
```

## Build and Launch

```sh
cd platform/macos
./build.sh run
```

That's it. The script:
1. Builds the C++ ALI core in Release mode
2. Compiles the Swift app
3. Assembles `ALI.app`
4. Opens it

To rebuild after source changes:
```sh
./build.sh
open ALI.app
```

To clean:
```sh
./build.sh clean
```

## What it does

- **Search bar** — type a query to search ALI's local index. Results appear with titles, URLs and excerpts.
- **Grammar discovery** — for queries like "who invented the microwave?", ALI constructs candidate URLs from 50+ authoritative sources (Wikipedia, arXiv, Stack Overflow, etc.) and fetches them in the background.
- **In-app browser** — click any result to open it inside ALI using WKWebView. Full JavaScript, cookies and sessions work normally.
- **Tabs** — Cmd+T opens a new tab. Click × to close.
- **History** — every page visit is recorded locally and fed into ALI's discovery frontier.
- **Bookmarks** — star icon bookmarks the current page.
- **Stats** — bar chart icon shows live engine stats: documents indexed, segments, sources tracked, grammars loaded.

## Data storage

All ALI data lives in:
```
~/Library/Application Support/ALI/
  index/      ← binary segment files
  meta.db     ← SQLite metadata, URL frontier, source registry
  history.json
  bookmarks.json
```

No data leaves the device. No external search provider is used.

## First-time use

On the first launch ALI has an empty index. Try:

1. Type **"who invented the microwave"** — ALI uses the Grammar Registry to construct the Wikipedia URL and fetch it. After a few seconds, search results appear.
2. Type **"Stack Overflow Python error"** — triggers discovery of Stack Overflow.
3. Navigate to any website using the address bar — browsing automatically feeds URLs into the discovery system.

After a few minutes of searching and browsing, the local index grows and results appear faster.

## Troubleshooting

**App won't open**: right-click → Open → Open (bypasses Gatekeeper on unsigned apps)

**"dylib not found"**: run `./build.sh clean && ./build.sh` to rebuild from scratch

**No search results**: the index starts empty. Search queries trigger background discovery — wait 5–10 seconds for the first results.
