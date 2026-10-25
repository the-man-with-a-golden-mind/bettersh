# Terminal editor

`std/edit` is a text editor for the terminal that works like Sublime Text. There are no modes: typing inserts text, and commands are key chords, menus, and the command palette. Every edit applies to all selections at once.

`edit notes.txt` in `btr` or `bsh` opens a file. `edit a.txt b.txt` opens each one in a tab, `edit src` opens the folder in the side bar, and `edit` with no paths opens the files of the last session, or an empty buffer. `bettersh -e notes.txt`, `bettersh edit notes.txt`, and `btr edit notes.txt` do the same from the command line. `bettersh -- edit` still runs a file named `edit`. A path that cannot be read stops the editor before it starts and prints why.

A program can also open the editor:

```btr
import std/edit
edit.run_files(["notes.txt"])
```

The editor puts the terminal in raw mode on the alternate screen and restores both when it exits, including after a signal. A save writes a new file and moves it into place, so a failed write leaves the old file as it was. A symlink keeps pointing at its target, and the file's mode is kept.

## The screen

The top row is a menu bar: File, Edit, Selection, Find, View, Goto, and Tools. F10 opens it, and a click opens one menu. Under it is one tab for each open file. A tab shows `â` when the file has unsaved changes and `Ã` otherwise; a click on `Ã`, or a middle click, closes the tab. Two files with the same name also show their folder.

The text area has line numbers, indent guides, a highlighted current line, and a scroll bar on the right. The bracket next to the caret and its match are underlined. When the selection is one word, other copies of it are underlined too. Secondary carets are drawn as blocks; the terminal cursor is the primary caret.

The last row is the status bar: the line and column of the primary caret, what is selected, messages, and on the right the indentation, the line endings, and the syntax. Find, replace, and file prompts open just above it.

Tabs, wide characters, and combining marks take their real width on screen. Control characters in a file are drawn as visible symbols, so a file cannot send escape sequences to the terminal.

## Keys

Shift with any motion extends the selection. Ctrl-K starts a two-key chord such as Ctrl-K Ctrl-U.

| Keys | Action |
| --- | --- |
| Arrows, Home, End, Page Up, Page Down | Move. Home goes to the first non-blank character, then to column 1. Up and Down keep the column. |
| Ctrl-Left, Ctrl-Right | Move by word. Alt-Left and Alt-Right move by part of a word, such as `Bar` in `fooBar`. |
| Ctrl-Home, Ctrl-End | Start or end of the file. |
| Ctrl-Up, Ctrl-Down | Scroll without moving the caret. |
| Ctrl-N, Ctrl-O, Ctrl-S | New file, open, save. Ctrl-Shift-S is Save As. Ctrl-Alt-S saves every file. |
| Ctrl-W, Ctrl-Shift-T, Ctrl-Q | Close the tab, reopen the last closed one, quit. |
| Ctrl-Page Up, Ctrl-Page Down, Alt-1 to Alt-9 | Switch tabs. |
| Ctrl-Z, Ctrl-Shift-Z or Ctrl-Y | Undo, redo. Typing undoes a word at a time. |
| Ctrl-X, Ctrl-C, Ctrl-V | Cut, copy, paste. With nothing selected they work on whole lines. |
| Ctrl-A, Ctrl-L | Select all. Select the line; again adds the next line. |
| Ctrl-D | Select the word, then add the next copy of the selection. Ctrl-K Ctrl-D skips one. Alt-F3 selects every copy. |
| Ctrl-Alt-Up, Ctrl-Alt-Down | Add a caret on the line above or below. Alt-Shift-Up and Alt-Shift-Down do the same. |
| Ctrl-Shift-L | Split the selection into one per line. |
| Escape | Go back to one selection, or close what is open. |
| Ctrl-Shift-D, Ctrl-Shift-K | Duplicate the line or selection. Delete the line. |
| Ctrl-Shift-Up, Ctrl-Shift-Down | Move lines. Alt-Up and Alt-Down do the same. |
| Ctrl-J, Ctrl-Enter, Ctrl-Shift-Enter | Join lines. Insert a line after or before. |
| Ctrl-/ | Toggle a line comment. |
| Tab, Shift-Tab, Ctrl-] | Indent and unindent. Tab inserts an indent unless the selection spans lines. |
| Ctrl-Backspace, Ctrl-Delete | Delete a word. Alt-Backspace and Alt-D do the same. Ctrl-K Ctrl-K deletes to the end of the line. |
| Ctrl-K Ctrl-U, Ctrl-K Ctrl-L, Ctrl-T, F9 | Upper case, lower case, swap characters, sort lines. |
| Ctrl-M, Ctrl-Shift-M | Jump to the matching bracket. Select inside the brackets; again includes them. |
| Ctrl-F, Ctrl-H | Find, replace. Where the terminal sends the same byte for Ctrl-H and Ctrl-Backspace, that byte deletes a word and Ctrl-Alt-F opens replace. F3 and Shift-F3 find the next and previous match. Ctrl-F3 finds the word under the caret. |
| Ctrl-Shift-F | Find in files. |
| Ctrl-P | Goto Anything: open files, files in the folder, `:` for a line, `@` for a symbol, and `name:line`. |
| Ctrl-G, Ctrl-R | Goto Line, Goto Symbol. |
| F12, Alt-Minus | Goto Definition. Jump back. |
| Ctrl-Shift-P or F1 | Command palette. Every command is listed with its key. |
| Ctrl-Space | Show completions. Ctrl-K Ctrl-I shows the signature or hover text. |
| Ctrl-K Ctrl-B, Ctrl-0 | Toggle the side bar. Move the keys to the side bar. |
| Alt-Z | Toggle word wrap for the file. |
| Ctrl-Shift-[, Ctrl-Shift-] | Fold, unfold. Alt-{ and Alt-} do the same. Ctrl-K Ctrl-1 to Ctrl-K Ctrl-5 fold every block at that level, and Ctrl-K Ctrl-J unfolds everything. |
| Ctrl-B, Ctrl-` or Ctrl-\ | Run the file in the panel. Open or close the panel. |

Terminals report many Ctrl-Shift chords and Ctrl-Enter only with the kitty keyboard protocol or xterm's modifyOtherKeys, which the editor turns on. Where a terminal does not send them, the Alt chords in the table work instead, and every command is in the palette and the menus. Alt-B and Alt-F also move by word, which is what some macOS terminals send for Option-Left and Option-Right. A Cmd chord that the terminal passes on works like the Ctrl chord.

Typing a bracket or a quote also types its closer when the next character is a blank or a closer. Typing the closer steps over it, and Backspace between an empty pair deletes both. A bracket or quote typed with text selected wraps the selection. Enter keeps the indent of the line, adds a level after an opening bracket or a Python `:`, and moves a closing bracket to its own line.

## Word wrap, folding, and the minimap

Alt-Z wraps long lines of the current file at the edge of the text area, breaking after a blank where it can. Up and Down then move by screen line, and a resize rewraps. `set word_wrap true` wraps every file.

Ctrl-Shift-[ folds the indented block around the caret, or the selected lines. The first line stays on screen with `â¯` after it and `â¸` in the gutter, and the rest is hidden. Motions skip hidden lines, moving a folded line moves what it hides, and a caret that lands inside a fold, from a search or a jump, opens it. Editing the folded line keeps the fold; editing inside it opens it. A click on `â¸` unfolds.

View, then Toggle Minimap, or `set minimap true`, shows an outline of the file on the right, with the lines on screen lit. A click or drag there scrolls.

## Find and replace

Ctrl-F opens the find row with the selected text, or the last search. Typing moves to the first match from where the search started, and the row shows how many matches there are. Enter and Shift-Enter go to the next and previous match, Alt-Enter selects every match, and Escape closes the row and keeps the match selected. Alt-C turns case matching on or off, and Alt-W matches whole words. Ctrl-D and Alt-F3 also find text that spans lines.

Ctrl-H adds a replace row. Tab moves between the two rows. Enter in the replace row replaces the current match and finds the next one, and Alt-Enter replaces every match. Undo takes back a replace-all in one step.

Ctrl-Shift-F asks for text and searches the side bar's folder, or the working directory. The results open in a tab. Enter or a double click on a result line opens that file at the match.

## Goto Anything and the palette

Ctrl-P lists the open tabs and the files in the folder, matched as you type: `mbt` finds `main.btr`. In a git work tree the list is `git ls-files`; elsewhere the folder is walked, skipping hidden and build directories. `:42` goes to line 42, `@name` lists the functions and types in the file, and `util.btr:10` opens a file at a line. Up and Down choose. Goto Line and Goto Symbol move the caret while you choose, and Escape puts it back.

Ctrl-Shift-P lists every command with its key. It also switches the theme, the syntax, the indentation, and the line endings of the current file.

## Files

Ctrl-O asks for paths on the bottom row. The files in the typed directory are listed above it; Up and Down choose one and Tab writes it. A path with spaces stays one path when it is quoted or when the spaces are escaped with `\`. Several paths separated by spaces open several tabs, and a folder opens in the side bar. A path that does not exist opens an empty tab that is written on the first save.

Ctrl-S on a new buffer asks for a path. Saving over another existing file asks first. Closing a tab with unsaved changes, or quitting with any, asks for each file: Save, Don't Save, or Cancel (or the keys `s`, `n`, and Escape).

Files keep their line endings: a file whose lines end in CR LF is saved that way, and a missing final newline stays missing. Indentation is detected when a file opens: tabs, or spaces and their width. The status bar shows what was found, and the palette can change or convert it.

Quitting remembers the open files, where each caret was, and the side bar, and `edit` with no paths opens them again. The session is `$HOME/.bettersh/session`, or the file `BETTERSH_SESSION` names. Unsaved changes are not kept there: quitting still asks about them. `set restore_session false` turns this off.

When the terminal reports focus, coming back to the editor checks the open files. A file that changed on disk is reloaded when it has no unsaved changes. Otherwise the status bar warns.

## Selections and the mouse

A click places the caret. A double click selects a word and a triple click selects a line; dragging after either one extends by words or lines. A click in the line numbers selects lines. Shift-click extends the selection, and Alt-click or Ctrl-click adds a caret. A middle-button drag selects a box of columns, one selection per line. Dragging past the top or bottom scrolls. The wheel scrolls the view, and Shift with the wheel scrolls sideways. A click or drag in the scroll bar jumps there. A right click opens a menu with Cut, Copy, Paste, and more.

Copy and cut also send the text to the system clipboard: with OSC 52, which most terminals and tmux pass on, and with `pbcopy`, `wl-copy`, `xclip`, or `xsel` when one is installed. Paste reads the system clipboard the same way, so text copied in another program pastes. Pasting several pieces into the same number of selections puts one piece in each.

## The panel and the side bar

Ctrl-B runs the current file in the panel under the text: a `.sh` file with `sh`, anything else with bettersh. Each run uses a private temporary file. The panel also has a bettersh REPL, which remembers earlier lines so a later line can use an earlier `let`, and a shell that keeps its directory after `cd`. Tab switches between Shell, REPL, and Run. Up and Down recall earlier lines. Escape returns the keys to the text and leaves the panel open; Ctrl-` closes it.

Ctrl-K Ctrl-B shows the side bar with the folder given on the command line, opened with Ctrl-O, or the working directory. Ctrl-0 moves the keys there. Up and Down choose, Enter opens a folder or a file, Right opens a folder, and Left closes it. A click opens what it hits.

The side bar also manages files. With the keys there, or from its menu (a right click, or `m`):

| Key | Action |
| --- | --- |
| `n` | New file in the picked folder, or beside the picked file. A name such as `lib/util.btr` also creates the folders. |
| `N` | New folder, with any folders on the way. |
| `r`, F2 | Rename or move. The prompt holds the path; a path to an existing folder moves the item into it. Open tabs follow. |
| `D` | Duplicate, as `name copy.ext` unless you type another path. |
| `d`, Delete | Delete, after asking. It goes to the Trash with the system's `trash` or `gio trash`, and is deleted otherwise; `BETTERSH_TRASH=off` always deletes. Clean tabs of deleted files close. |
| `c`, `x`, `v` | Copy or cut the picked item, then paste it into the picked folder. A copy that would clash gets a `copy` name. |
| `y` | Copy the full path to the clipboard. |
| `u`, Backspace | Go to the parent folder. |
| `.` | Make the picked folder the root. |
| `g` | Go to any folder, with path completion; `~` is the home folder. |
| `R`, F5 | Read the folder again. |

The command palette has the same commands under `Side Bar:`, and `Side Bar: Reveal Current File` opens the folders down to the file in the current tab.

The right edge of a row shows problems. `●3` means an open file has three diagnostics from its language server; a folder adds up what is under it. In a git work tree each file shows its state: `M` changed, `A` added, `D` deleted, `R` renamed, `U` conflicted, `?` untracked; a folder with changes under it shows `•`. The state is read when the side bar opens, after each file operation, and after each save.

## Completion

While you type, a list of completions opens under the caret. It holds words from the file, and in a `.btr` file also the built-in functions, the standard library, and the file's own `fn`, `let`, and `import` names. Up and Down choose, Enter or Tab inserts, and Escape closes. Typing `(` after a function name shows its signature in the status bar. Ctrl-Space opens the list on demand and also asks the language server, when one is configured.

In shell text inside a `.btr` file (after `$`, inside `$( )`, and inside `sh { }`), the list holds the file's shell functions, the shell's keywords and builtins, and the commands on `PATH` instead. Ctrl-K Ctrl-I there describes a builtin, a shell function, or where a command lives, and F12 goes to a shell function defined in the file.

## Configuration

`BETTERSH_EDITOR` may name a configuration file. Otherwise the editor reads `$HOME/.bettersh/editor.conf` when it exists. Without a file the theme is nord and no language server starts. Each line has one of these forms; blank lines and lines starting with `#` are ignored:

```text
theme name dracula
theme keyword blue
set tab_size 2
set translate_tabs_to_spaces false
server rs rust-analyzer
root /path/to/workspace
```

`theme name NAME` selects `plain`, `dark`, `light`, `nord`, `gruvbox`, `dracula`, `catppuccin`, `solarized`, `monokai`, or `tokyonight`, and `theme ROLE COLOR` changes one role. A theme with a colored status panel fills the whole screen with its colors; `dark` and `plain` use the terminal's own colors. Shell text in a `.btr` file is drawn on a tinted background, with `$`, `$(`, `)`, and the braces of `sh { }` in the macro color and an inserted `@{ value }` in the file's own colors. `theme shell COLOR` picks the tint; without it the tint comes from the function color. `dark` and `plain` show shell text in italics, or with the named color as its background. A file that sets only servers or settings uses the plain theme.

`set NAME VALUE` changes a setting. Booleans take `true` or `false`.

| Setting | Default | Meaning |
| --- | --- | --- |
| `tab_size` | 4 | Width of a tab and of one indent level, 1 to 16. |
| `translate_tabs_to_spaces` | true | Indent with spaces. |
| `detect_indentation` | true | Use the file's own indentation when it opens. |
| `auto_indent` | true | Keep the indent on Enter. |
| `auto_match` | true | Pair brackets and quotes. |
| `auto_complete` | true | Open the completion list while typing. |
| `highlight_line` | true | Highlight the caret's line. |
| `line_numbers` | true | Show line numbers. |
| `draw_indent_guides` | true | Draw indent guides. |
| `menu_bar` | true | Show the menu bar. |
| `mouse` | true | Use the mouse. |
| `scroll_past_end` | true | Let the last line scroll to the top. |
| `trim_trailing_white_space_on_save` | false | Remove blanks at line ends when saving. |
| `ensure_newline_at_eof_on_save` | false | End the file with a newline when saving. |
| `word_wrap` | false | Wrap long lines in every file. |
| `minimap` | false | Show the minimap. |
| `restore_session` | true | Reopen the last session's files when no paths are given. |

A line that cannot be read makes the editor use the defaults, and the status bar says which file had the problem.

Syntax highlighting is chosen by extension for BetterSh, shell, Python, JavaScript, TypeScript, Rust, C and C++, Go, Lua, and JSON. It marks comments, strings, keywords, numbers, functions, types, constants, macros, and shell variables. A string or block comment that spans lines is carried to the next line.

## Language servers

A language server gives the editor diagnostics, hover text, Goto Definition, and completions for a language. Install the server, then add a `server` line to `~/.bettersh/editor.conf`:

```text
server EXT COMMAND
```

EXT is a file extension without the dot. COMMAND runs through `/bin/sh`, so it can have arguments and use `PATH`, and it must speak LSP on standard input and output; many servers need `--stdio` for that. Each extension needs its own line, so `.ts` and `.tsx` are two lines. When two lines name the same extension, the last one wins. The editor starts the server when a file of that type opens and stops it when the tab closes.

### Rust

```sh
rustup component add rust-analyzer
```

```text
server rs rust-analyzer
```

Open a file inside a Cargo project. rust-analyzer indexes the crate first, so diagnostics can take a few seconds to appear.

### TypeScript and JavaScript

```sh
npm install -g typescript typescript-language-server
```

```text
server ts typescript-language-server --stdio
server tsx typescript-language-server --stdio
server js typescript-language-server --stdio
server jsx typescript-language-server --stdio
```

The server needs TypeScript 5 or older, which includes `tsserver`. It uses the project's own `node_modules/typescript` when there is one, and the global install otherwise. A `tsconfig.json` or `jsconfig.json` in the project sets the compiler options.

### Python

```sh
npm install -g pyright
```

```text
server py pyright-langserver --stdio
```

`pip install python-lsp-server` installs another server; its line is `server py pylsp`. Pyright reads `pyrightconfig.json` or `pyproject.toml` for its settings and uses the virtual environment they name.

### Go and C

```text
server go gopls
server c clangd
server h clangd
server cpp clangd
```

`go install golang.org/x/tools/gopls@latest` installs gopls. clangd comes with LLVM, and it reads the project's `compile_commands.json` for compiler flags.

### Shell text in .btr files

The server line for `sh` also serves the shell text of a `.btr` file:

```text
server sh bash-language-server start
```

`npm install -g bash-language-server` installs it; it runs ShellCheck when that is installed. The server sees a `sh` file named after the `.btr` file with `.sh` added. It holds every `$`, `$( )`, and `sh { }` in its place, every other column as a space, and each `@{ value }` as a variable of the same width, so line and column numbers match. Its diagnostics are shown with an `sh:` prefix, except ones on an inserted value. Hover, completion, and F12 ask it when the caret is in shell text, and the server for `btr`, if there is one, otherwise.

### The workspace

A server works on a workspace: the folder of the project. Without a `root` line, the editor walks up from the file to the nearest folder that holds `Cargo.toml`, `package.json`, `tsconfig.json`, `jsconfig.json`, `pyproject.toml`, `setup.py`, `setup.cfg`, `requirements.txt`, `go.mod`, `compile_commands.json`, or `.git`, and uses the file's own folder when there is none. `root PATH` sets one workspace for every file instead.

### What the editor does with it

The editor sends the whole document when it changed, before each request and between frames. Diagnostics are underlined, the line gets a `â` in the gutter, and the message shows in the status bar when the caret is on that line. Ctrl-K Ctrl-I shows hover text, F12 goes to a definition (opening the file when it is elsewhere), and Ctrl-Space adds the server's completions to the list. Without a server, F12 in a `.btr` file goes to a `fn` in the same file, and in other files to a matching symbol.

A server that is slow to start keeps starting in the background, and the status bar says so. A server that refuses to start shows its own reason there, such as a missing TypeScript install. Each open file starts its own copy of the server, so a large workspace opened in many tabs uses that much more memory.

When nothing seems to happen, run the COMMAND by hand in a shell: it should start and wait for input. To see what the server logs, redirect its error output in the config, for example `server rs rust-analyzer 2>/tmp/rust-analyzer.log`.

## Scripting and tests

`edit.app(cols, rows)` makes an editor that is not attached to a terminal, and `edit.app_with(cols, rows, cfg)` uses a loaded configuration. `edit.load(e, text, path)` opens text as a tab without reading the disk, and `edit.open_path(e, path)` reads a file. `edit.key(e, "ctrl-shift-up")` handles one key, or an event map such as a mouse report, and `edit.type_text(e, text)` types characters. `edit.text(e)`, `edit.sels(e)`, `edit.frame(e)`, `edit.paint(e)`, and `edit.cursor(e)` return the text, the selections, the screen as plain text, the screen with colors, and where the terminal cursor would be.
