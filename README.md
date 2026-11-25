# ScoopStar 7.1.2

A WordStar 7.0 for DOS work-alike for Windows 11, written in C.

ScoopStar looks and works like WordStar 7.0: the same editing screen, the same
classic control-key commands (^K, ^Q, ^O, ^P, ^M), the same dot commands, and
pull-down menus laid out as in WordStar 7. Documents are saved as Rich Text
Format (.RTF), so Microsoft Word, LibreOffice and WordPad open them directly,
and ScoopStar keeps a private copy of its own formatting inside each file so
nothing is lost when you reopen it.

ScoopStar was written with Claude, Anthropic's AI model, working from the
WordStar 7.0 for DOS manuals. Scoop140 directed the design, chose the
features, and tested each version.

## Features

- Classic WordStar editing: control-key commands with pop-up command menus,
  blocks and column blocks, markers, find and replace, undo, two windows
- Dot commands for margins, first-line indent, tabs, line spacing (1 to 9 in
  half steps, so `.ls 1.5` works), page length, headers, footers
  and page numbering, with up to five header and footer lines and separate
  even-page and odd-page versions
- Bold, italic, underline, strikeout, superscript, subscript and any TrueType
  font installed in Windows
- Footnotes, endnotes and annotations
- Newspaper-style columns, column breaks, vertical centering
- Paragraph numbering in compound (1.1.1.) or outline (I. A. 1. a.) style
- Graphics (PNG, JPEG, BMP, GIF, TIFF) shown in Preview, printed, and
  embedded in the RTF file
- Spelling in English (Windows spell checker), Latin and German (Hunspell),
  with personal dictionaries, and an English thesaurus
- Automatic hyphenation for English and German, and soft hyphens
- Index and table of contents generation
- Paper sizes (Letter, Legal, Statement, Executive, A4, A5, #10 envelope or
  any custom size with `.pw` and `.pl`), portrait or landscape (`.pr or=l`)
- Page Preview of the real paper, and printing to any Windows printer on the
  document's own paper size and orientation
- Opens WordStar for DOS files and converts them to RTF
- Help levels 0 to 4, from pull-down menus to no menus at all
- Color schemes: WordStar blue, black, monochrome, amber and green
- Starts in full screen (can be turned off in Screen Settings)
- Protect and unprotect files, make directories, and file lists in natural
  order (chapter2 before chapter10)

## What's new in 7.1.2

- Fixed a crash in spell check on misspelled words with ten or more
  suggestions.
- The status line shows ColBlock while column block mode (^KN) is on.
- If ScoopStar ever crashes, it saves your changed documents as
  "name (recovered).rtf" and writes a report to %APPDATA%\ScoopStar\crash.txt.

## What's new in 7.1.1

- Printing now uses the document's paper size on printers whose drivers
  keep their own paper settings (such as Canon inkjets). ScoopStar picks
  the paper from the printer's own list and opens the printer with it.

## What's new in 7.1

- `.pw` paper width and `.pr or=l` / `.pr or=p` orientation dot commands
- Paper Size list in Layout > Page (^OY)
- Preview draws the actual paper; printing sets the paper size and orientation
- RTF files keep the paper size and landscape setting
- Starts in full screen by default

## Installing

1. Download the `release` folder (or the whole repository).
2. Keep `ScoopStar.exe` and the `Dictionaries` folder together.
3. Run `ScoopStar.exe`.

No installation is needed. Settings and personal spelling dictionaries are
kept in `%APPDATA%\ScoopStar`.

## Documentation

- `docs/manual.txt`: commands, dot commands, spelling, footnotes and settings
- `docs/new-features.txt`: endnotes, columns, column blocks, hyphenation,
  paragraph numbers, graphics, help levels, headers and footers, index and
  table of contents
- Inside the program: F1 or the Help menu

## Building from source

ScoopStar is cross-compiled with MinGW-w64. On Linux (or WSL):

```sh
sudo apt install mingw-w64
sh build.sh
```

`build.sh` builds the Hunspell and libhyphen libraries once, then compiles the
program into a single static `ScoopStar.exe`. `tools/mkfont.py` regenerates the
screen font header (`src/font8x16.h`) from GNU Unifont.

## Layout

| Folder | Contents |
|---|---|
| `src/` | ScoopStar source code |
| `hunspell/` | Hunspell spelling library source |
| `hyphen/` | libhyphen hyphenation library source |
| `tools/` | font generator |
| `docs/` | user documentation |
| `release/` | ready-to-run program and dictionaries |

## Credits and licenses

ScoopStar's own code is under the MIT License (see `LICENSE`). It includes:

| Component | Use | License |
|---|---|---|
| [Hunspell](https://github.com/hunspell/hunspell) | Latin and German spelling | MPL 1.1 / GPL 2 / LGPL 2.1 |
| [libhyphen](https://github.com/hunspell/hyphen) | hyphenation | MPL 1.1 / GPL 2 / LGPL 2.1 |
| [GNU Unifont](https://unifoundry.com/unifont/) | 8x16 screen font | SIL Open Font License 1.1 |
| English dictionary (SCOWL) | spelling | BSD-style, see `README_en_US.txt` |
| Latin dictionary | spelling | GPL 2 or later, see `README_la.txt` |
| German dictionary (frami) | spelling | GPL 2 or 3, see `README_de_DE_frami.txt` |
| English and German hyphenation patterns | hyphenation | see `README_hyph_*.txt` |
| English thesaurus (WordNet) | thesaurus | WordNet license, see `WordNet_license.txt` |

The dictionaries are LibreOffice's, distributed as separate data files with
their license files in `release/Dictionaries`.

WordStar is a trademark of its respective owner. ScoopStar is an independent
work-alike and is not affiliated with or endorsed by the owners of WordStar.
