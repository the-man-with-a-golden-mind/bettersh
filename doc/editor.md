# Terminal editor

`std/edit` provides a modal editor built on the character grid in `std/ui`. It runs inside a terminal, switches the terminal to raw mode, and restores the previous terminal state when it exits. A failed save leaves the editor open so the buffer can still be recovered.

A program can open the editor with:

```btr
import std/edit
edit.run("notes.txt")
```

The editor has normal and insert modes. Insert mode accepts text; Escape returns to normal mode. Long files scroll vertically, long lines scroll horizontally, and the gutter grows with the line count. Cursor movement and editing operate on Unicode scalar values.

## Normal-mode keys

| Key | Action |
| --- | --- |
| `h`, `j`, `k`, `l` | Move the cursor. |
| `w`, `b`, `e` | Move by subword, including camel-case boundaries. |
| `i` | Enter insert mode. |
| `O` | Open a line above the current line. |
| `J` | Join the next line. |
| `>>`, `<<` | Indent or unindent by one tab or up to four spaces. |
| `gc` or Ctrl-/ | Add or remove a leading `# ` comment. |
| `~` | Swap the ASCII case of the current character. |
| Ctrl-t | Transpose two characters. |
| `%` | Jump to the matching bracket, ignoring strings and `#` comments. |
| `*` | Find the next occurrence of the word under the cursor. |
| Ctrl-d | Duplicate the current line. |
| Ctrl-n, Ctrl-p | Move the current line down or up. |
| Ctrl-z | Undo. |
| Ctrl-r or Ctrl-y | Redo. |
| `K` | Request hover information from the language server. |
| `gd` | Go to a definition in the current buffer. |

In insert mode, Tab inserts a tab and Ctrl-w deletes the preceding subword. Ctrl-x cycles through language-server completion candidates, and Ctrl-u inserts the selected candidate.

## Themes and syntax highlighting

`BETTERSH_EDITOR` may name a configuration file. When the variable is unset, the editor uses `$HOME/.bettersh/editor.conf`. Setting it to an empty string disables configuration loading. Each non-empty line is one of these forms:

```text
theme name dark
theme keyword bright-blue
server rs rust-analyzer
root /path/to/workspace
```

`theme name NAME` selects a built-in theme. Available names are `plain`, `dark`, `light`, `nord`, `gruvbox`, `dracula`, `catppuccin`, `solarized`, `monokai`, and `tokyonight`. A `theme ROLE COLOR` line overrides one role. Blank lines and lines beginning with `#` are ignored. An invalid line causes the configuration load to fail.

Syntax highlighting is selected by file extension for JavaScript, TypeScript, Rust, shell, BetterSh, Python, and Lua. The highlighter recognizes comments, strings, keywords, numbers, functions, types, constants, macros, and shell variables. The plain-text frame returned by the UI library does not contain color escape sequences.

## Language servers

A `server EXT COMMAND` line assigns a language-server command to a file extension. The editor starts that command for matching files and communicates with it through `std/lsp`. `root PATH` sets the workspace root.

The client sends the complete document when it synchronizes. Hover, definition, and completion requests are available through the normal-mode keys above. A definition in the current file moves the cursor. A definition in another file is reported as `path:line:column`.

The language-server integration is optional. Editing, saving, themes, and built-in syntax highlighting continue to work without a server configuration.
