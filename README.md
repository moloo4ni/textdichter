# Textdichter

A light, simple and clean Markdown editor for the Linux desktop.
CommonMark only.

## Principles

- **Plain text stays plain.** Pure `.md` files, no hidden metadata. Encoding
  and line endings are kept as they were.
- **Honest CommonMark.** Rendered by the reference implementation,
  [cmark](https://github.com/commonmark/cmark). Tables, task lists and other
  extensions stay plain text, as the spec says.
- **One document, one window.** No tabs, no extra windows: opening a file
  replaces the current one. Friendly to tiling window managers.
- **You decide when to save.** No autosave; unsaved changes are marked with `•`.
- **Native and light.** Qt Widgets and cmark, no web engine.

## Interface

A menu bar and a single column of text, 80 characters wide, in two modes
(`Ctrl+/`):

- **Code** — the Markdown source, with the markup dimmed.
- **Preview** — the rendered document, read-only.

Switching keeps your place in the document.

## Editing

- The **Format** menu and the right-click menu hold bold, italic, code, links,
  headings, quotes, lists and code blocks. Applying a format again removes it.
- With nothing selected, a format inserts a pair of markers to type between;
  applied again at their end, it steps out of them.
- Lists and quotes continue on Enter; Tab and Shift+Tab nest list items.
- Pasting a URL over a selection turns it into a link.
- Find and replace: `Ctrl+F`, `Ctrl+H`.

## Building

Qt 6.5+ (Widgets, PrintSupport; LinguistTools for translations), cmark and
CMake 3.21+. Qt Svg is needed at runtime for the icon.

```sh
# Arch Linux
sudo pacman -S --needed qt6-base qt6-svg qt6-tools cmark cmake

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
./build/textdichter [file.md]
```

`sudo cmake --install build` adds the editor to the application menu and
makes it open `.md` files.

## Status

Pre-release: everything above works, but expect rough edges.

## License

[MPL-2.0](LICENSE)
