# Textdichter

A light, simple and clean Markdown editor for the Linux desktop.
CommonMark only — out of the box.

## Principles

- **Plain text stays plain.** Files are pure `.md`: no front matter, no hidden
  metadata. Encoding and line endings are preserved.
- **Honest CommonMark.** Rendering follows the spec via its reference
  implementation, [cmark](https://github.com/commonmark/cmark). GFM tables, task
  lists and the like show up as plain text — exactly as the spec says.
- **One document, one window.** Opening another file replaces the current one:
  no tabs, no extra windows. Friendly to tiling window managers.
- **You decide when to save.** No autosave — `Ctrl+S` writes the file.
  Unsaved changes are marked with `•`.
- **Native and light.** Qt Widgets and cmark. No Chromium, no web engine.

## Interface

A classic menu bar and a single centered column of text, 72 characters wide.
Two modes, switched with `Ctrl+/` or *View → Preview*:

- **Code** — the Markdown source in a monospace font.
- **Preview** — the rendered document, read-only, in the system font.

Switching modes keeps your place in the document. Shortcuts work on any
keyboard layout.

## Editing

- **Format menu** — bold, italic, code, links, headings (`Ctrl+1…6`), quotes,
  lists and code blocks. Applying a format again removes it.
- **Lists and quotes continue** on Enter; an empty item ends the list. Tab and
  Shift+Tab nest and unnest list items the way CommonMark expects.
- **Pasting a URL over a selection** turns it into a link.
- **Find and replace** in a bar at the bottom of the window (`Ctrl+F`, `Ctrl+H`).
- **Source styling** — headings, emphasis, code and links stand out, the markup
  itself is dimmed. Font size stays the same everywhere.

## Building

Requirements: Qt 6 (Widgets, PrintSupport, LinguistTools), cmark, CMake 3.21+.

```sh
# Arch Linux
sudo pacman -S --needed qt6-base qt6-tools cmark cmake

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/textdichter [file.md]
```

## Status

Early development, not ready for daily use yet.

## License

[MPL-2.0](LICENSE).
