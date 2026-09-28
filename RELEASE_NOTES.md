First release of Textdichter, a light, simple and clean Markdown editor for
the Linux desktop.

## What's inside

- **Code and Preview modes** — the Markdown source with the markup dimmed and
  the rendered document, switched with `Ctrl+/`. Your place in the document is
  kept.
- **CommonMark only**, rendered by [cmark](https://github.com/commonmark/cmark).
- **Format and right-click menus** — bold, italic, code, links, headings,
  quotes, lists and code blocks. Applying a format again removes it or steps
  out of it.
- **Lists and quotes continue** on Enter; Tab and Shift+Tab nest list items.
- **Find and replace**, **HTML export**, **printing and PDF export**.
- **Your data is safe** — changes made to the file by another program are
  noticed, and unsaved changes survive a crash.
- English and Russian interface.

If you tried the pre-release: settings now live in `~/.config/textdichter`,
so the list of recent files starts empty.

## Installing

On Arch Linux, from the AUR: `paru -S textdichter`. Elsewhere, the tarball
below runs with Qt 6.8 or newer and Qt SVG from your distribution; see
[README](https://github.com/moloo4ni/textdichter#installing).
