# Books: reader and Project Gutenberg downloads `OK`

- [Books app](#books-app-txt-md-and-epub-reader)
- [Download from Project Gutenberg](#download-from-project-gutenberg)

## Books app (`.txt`, `.md` and `.epub` reader)

Copy `.txt`, `.md` or `.epub` files to the `/books` folder on the SD card (created automatically; files in the SD root are also listed), then open the **Books** app.

| Action | Result |
|---|---|
| Tap the right third / swipe left | Next page (with a slide animation) |
| Tap the left third / swipe right | Previous page |
| Tap the middle | Menu: a page slider; **Contents**, **Aa** (font), **Theme**, **Close**; **Justify** (full justification) and **Animation** (on/off) |
| Magnifier icon (library) | Search by title / author with the system keyboard; **x** clears |
| Latest / Title / Author chips | Change the list order |

- **Library:** EPUB covers (36×48 thumbnails, stored in `/books/.covers`), title + author from the EPUB metadata (cached in `/books/.nyxmeta`; EPUBs are processed once in the background), percent read, and a **Finished** marker once a book has been read to the end. A thin line under the title shows cover-preparation progress.
- **Justified text** (can be turned off in the menu). Lines that would become too loose are left left-aligned.
- **Fonts:** Sans, Serif, Mono, Small, Sans+, Serif+ (built-in GFX fonts, ASCII). The **Unicode fonts** `Sans U`, `Serif U`, `Mono U`, `Sans U+`, `Serif U+` appear on the **Aa** button when `.vlw` files exist in `/books/fonts`. They cover Latin-1/Latin Extended-A (accents), modern Greek, basic Cyrillic, curly quotes and dashes, the ellipsis and the euro. CJK, Arabic, Thai and similar scripts still show `?`. Without `.vlw` files, accented letters are folded to their base letters as before. Generate the files with `python3 tools/make_vlw.py <output-folder>` and copy them to `/books/fonts`.
- **Images in EPUB:** JPEG (baseline) and PNG images inside a chapter are shown as a full page; ornaments under 3 KB are skipped; progressive JPEG, GIF, SVG and WebP are not supported (the page gets a note).
- **Markdown (`.md`):** headings (`#`), lists, quotes, code blocks, tables, rules, bold/italic/code/links are cleaned into text; headings go into the table of contents. 1 MB limit. Markdown images appear as `[alt text]`.
- **Reading position** is saved per book in `/books/.nyxreader` (chapter + text offset, so it stays exact after changing font or orientation), together with the font, theme, justification, animation and order choices.
- **EPUB 2 and 3:** chapters are read one at a time from the ZIP (RAM-friendly); the table of contents comes from NCX or `nav`. ZIP64 and DRM-protected EPUBs are not supported.
- **TXT:** UTF-8, UTF-16 (with BOM), Latin-1/Windows-1252. Hard-wrapped text (Project Gutenberg style) is re-joined into paragraphs. Lines such as `Chapter 1`, `BAB IV` or `Prolog` are recognised as headings and go into the table of contents. 3 MB limit per file.
- **Testing:** `tests/ebook/` — `python3 tests/ebook/test_ebook.py` (inflate, EPUB, TXT, layout, fuzz), `python3 tests/ebook/test_ebook_v2.py` (UTF-8 + folding, EPUB images, covers/metadata, Markdown) and `python3 tests/ebook/run_ui_test.py` (UI flow with *mocked* LovyanGFX/SD/JPEGDEC/keyboard), all with AddressSanitizer. The owner has also tested the Books app directly on a device and it works well.

## Download from Project Gutenberg

Open it with the **Gutenberg** button in the Books library header. It needs WiFi and an SD card.

Flow: a popular list or search results, tap a book, the **Detail** screen, **Download**, the file goes into `/books`, then **Open**. Only public-domain books are shown.

| Chip | Function |
|---|---|
| Language | Cycles through: all, `en`, `id`, `fr`, `de`, `es`, `nl`, `pt`, `it`. Only for the Gutendex source. |
| Images | Without images (default, much smaller EPUBs) or with images. |
| Order | Popular or Latest. |
| Source | `Gutendex` (default) or `OPDS`. |

- **Two search sources:** Gutendex (`gutendex.com`, a community JSON API over the Gutenberg catalogue) and the official `gutenberg.org` OPDS feed (XML; only book number, title, author). If the chosen source fails, search is automatically retried on the other one. The source choice is not remembered after a restart.
- **Download URLs are built directly from the book number** for both sources, without redirects: `cache/epub/<N>/pg<N>.epub` (no images), `pg<N>-images-3.epub` (with images), or `pg<N>.txt` if there is no EPUB. URLs from the API are only a last resort if the direct URL fails (404/410).
- **Polite to the server:** one job at a time, at least 1.5 seconds between requests, and a book cannot be started twice (a per-book-number registry).
- **Safe against half-finished files:** data is written to `<name>.part`, then renamed to the book only after it passes verification. An interrupted download is resumed with `Range` from the last byte; **Try again** continues a `.part` that still matches.
- **Verification before it becomes a book:** the size on the SD card, the `PK` header, the SD card is read back and its CRC32 matched against the data that arrived from the network (files up to 16 MB), plus a check of the ZIP's end record and central directory. A file that fails is kept as `<name>.bad` for inspection on a computer.
- **SD write errors:** if the data on the SD card does not match the network, the download is repeated once from the start using a different write method (buffered or direct). Failing twice shows *SD write corrupted 2x. Check the SD card.*
- **Already there?** Before downloading, `/books` is searched by the `[PGnnn]` number. A complete book is not downloaded again; a corrupted old file is moved to `.bad`.
- **Testing:** `g++ -std=c++11 -o t tests/gutenberg/test_gutendex.cpp && ./t` and `g++ -std=c++11 -o t tests/gutenberg/test_gutdl.cpp && ./t` (pure logic), and `python3 tests/gutenberg/run_sim.py` (the download/verification flow from the real `.ino` code with Arduino stubs). The flow was tested by the owner on a device (about 5 EPUB books with images downloaded successfully); the OPDS source and the TXT format were not specifically confirmed.
