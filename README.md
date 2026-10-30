<p align="center">
  <a href="https://mithraeums.github.io">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://mithraeums.github.io/assets/banner-hake-night.png"/>
      <img src="https://mithraeums.github.io/assets/banner-hake.png" alt="hake — a quiet box around the cursor" width="100%"/>
    </picture>
  </a>
</p>

<p align="center">
  <em>A minimalistic modal text environment, containing your code without distraction.</em>
</p>

<p align="center">
  <a href="https://github.com/mithraeums/hako-edit/releases"><img src="https://img.shields.io/badge/version-v0.2.0-e2402e?style=flat-square&labelColor=14130f" alt="v0.2.0"/></a>
  <img src="https://img.shields.io/badge/license-GPL--3.0-2636c4?style=flat-square&labelColor=14130f" alt="GPL-3.0"/>
  <img src="https://img.shields.io/badge/C99-single%20file-2636c4?style=flat-square&labelColor=14130f" alt="C99 single file"/>
  <img src="https://img.shields.io/badge/themes-19-2636c4?style=flat-square&labelColor=14130f" alt="19 themes"/>
  <img src="https://img.shields.io/badge/languages-40%2B-2636c4?style=flat-square&labelColor=14130f" alt="40+ languages"/>
</p>

<p align="center">
  <sub><a href="https://mithraeums.github.io">site</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-edit/releases">releases</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-code">hako-code</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako">hako (engine)</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-studio">hako-studio</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums">org</a></sub>
</p>

<br>

<!-- terminal media: real sessions through a pty, drawn in the night terminal at 2x (Code/mithraeums/.claude/web/look/media.py).
     one image per row, width only, never a height: GitHub can shrink them but never stretch them -->
<p align="center">
  <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/tour.gif?raw=true" alt="hake: the start screen, main.c, a split with notes.md, the theme picker" width="100%"/>
</p>

<p align="center">
  <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/rei.png?raw=true" alt="hake with the Rei pane: rei proposes an edit to main.c, it is allowed, and the open buffer reloads with the new code" width="100%"/><br/>
  <sub>箱 <b>Rei</b>: <a href="https://github.com/mithraeums/hako-code">hako-code</a> in a split pane, its tools and approvals drawn by the editor</sub>
</p>

<p align="center">
  <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/markdown.png?raw=true" alt="a markdown file: front matter, headings, emphasis, task boxes, a table and a fenced C block highlighted as C" width="100%"/><br/>
  <sub>a highlighter that reads tokens: markdown with fenced code in its own language</sub>
</p>

<p align="center">
  <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/nano.png?raw=true" alt="hake --nano with the two rows of shortcuts at the bottom" width="100%"/><br/>
  <sub><code>hake --nano</code> (or <code>--emacs</code>): the same panes and <code>:</code> commands, other keys</sub>
</p>

<br>

<p align="center"><sub><b>—— I ——</b></sub></p>

## Overview

- **Modal Editing**: vim keys by default (normal, insert, visual, visual-line); emacs or nano keys with `--emacs` / `--nano` or `edit.keys=` in the rc
- **Barebones by default**: one window looks like vim (`~` rows, a ruler on the last line, no status bar, no line numbers); bars and titles appear with a second pane
- **Full Motion Set**: counts, named registers, text objects (`diw`, `ci"`, `da(`), marks, jumplist, dot-repeat, `:s/` substitution, bracket match
- **Multi-Pane Support**: Split horizontally and vertically, resize with `Ctrl-W +/-/</>`, jump with `Ctrl-W 1-9`; with more than one pane each gets a title bar (`pane_titles=0` turns it off)
- **File Explorer**: 箱 Kami - directory navigation, hidden-file toggle, dirs-first sort
- **AI Assistant**: 箱 Rei - [hako-code](https://github.com/mithraeums/hako-code) in a split pane, over its `--pipe` protocol. Local hako models (`hako-sho` 3B / `hako-koi` 7B) or any of its 14 cloud providers; its tools, approvals, pickers and sessions, rendered by the editor. A file Rei edits reloads in its pane
- **Skills**: the agent's [Agent Skills](https://agentskills.io) — `:skill install owner/repo` from the Rei pane
- **Syntax Highlighting**: 45 file types; markdown done properly (headings, emphasis, code spans and fenced blocks in their own language, lists and task boxes, links, quotes, tables, front matter); multi-line strings and comments, escapes and numbers in every language
- **Undo/Redo**: time-bounded undo blocks, configurable depth
- **Visual Selection**: character and line modes with yank/delete/change, count feedback
- **Clipboard**: bracketed paste, system clipboard via `Ctrl-C`/`Ctrl-V`
- **Fast Search**: incremental with `n`/`N`, all matches highlighted
- **Console**: `:console` opens a shell line under the runner pane, in the project dir; `console_sandbox=1` runs it under `sandbox-exec` (macOS) or `bwrap` (Linux), writes only to the project and `$TMPDIR`, no network
- **Runner**: `:build` / `:test` / `:run` stream into a bottom pane while you edit; `]e`/`[e` jump to `file:line` errors; `a` hands the error to Rei. `:!<cmd>` for quick one-shots
- **Line Numbers**: absolute or relative, dynamic width
- **Mouse Support**: click to position, scroll wheel moves cursor
- **20 Themes**: `terminal` (the default: your profile's own colors, nothing painted), mithraeum and washi (the suite's own, dark and light), dark, light, gruvbox, nord, dracula, monokai, solarized, tokyonight, catppuccin, onedark, material, everforest, rosepine, github-dark, github-light, ayu, kanagawa - live-preview picker via `:theme` (saved to the rc); your own as `theme.<name>.<slot>=`
- **Customizable**: one `.hakorc` for every suite tool, a project's own on top

<p align="center"><sub><b>—— II ——</b></sub></p>

## Build

```sh
# One-liner - works on Linux, macOS, Windows (mingw)
gcc hake.c -o hake -lpthread

# Or use the Makefile to also embed the executable icon where the OS allows
make                 # the editor; :install-agent fetches the agent later
make bundle          # the editor AND the hako agent, from ../hako-code/hako.c
make NO_AGENT=1      # editor with no agent path at all (Rei pane stays inert)
make test            # smoke test + the --pipe conformance check + theme sync
```

**Run**
```sh
./hake              # or  ./hake [filename]
```

**Install** (drop the binary into your PATH):
```sh
cp hake /usr/local/bin/hake          # Linux / macOS
```

> **Deps:** C standard library + POSIX/Win32 system headers + `pthread`. No third-party libraries linked. AI features shell out to `curl(1)` at runtime.

## Install

```sh
curl -fsSL https://mithraeums.github.io/hake.sh | sh       # the editor
curl -fsSL https://mithraeums.github.io/install.sh | sh    # the whole suite: editor, agent, engine
```

Verifies sha256, installs to `/usr/local/bin` if writable else `~/.local/bin`. macOS quarantine xattr stripped post-install. Where there is no release binary (iSh is 32-bit x86) it builds from source. `VERIFY=0` skips the sha check, `PREFIX=/opt` relocates, `SOURCE=1` forces a source build.

### Executable icon

The icon files live in `icon/`:

| Platform | File          | What `make` does                                                                |
|----------|---------------|---------------------------------------------------------------------------------|
| Windows  | `hake.ico`    | Embeds icon into `.exe` via `windres`; real OS icon.                           |
| macOS    | `hake.icns`   | Attaches icon as a resource fork via `Rez`+`SetFile` (Xcode CLT). Best-effort.  |
| Linux    | `hake.png`    | ELF can't embed icons; ship `hake.png` and reference it from a `.desktop` file. |

A plain `gcc hake.c -o hake` produces a working binary on every platform; the icon is purely cosmetic.

### iSh (iOS - experimental)

hake compiles on Alpine via [iSh](https://ish.app):

```sh
apk add build-base git curl
make
```

Notes:
- AI panel needs `curl` for HTTPS requests; `apk add curl` is required before using Rei.
- Narrow screens auto-stack the AI panel below the editor instead of squeezing horizontally.
- iSh runs single-threaded under emulation; expect slower stream rendering.

<p align="center"><sub><b>—— III ——</b></sub></p>

## Key Bindings & Commands

### Key modes

vim is the default. `hake --emacs` or `hake --nano` for one run, `edit.keys=emacs|nano` in the rc for good.
every pane, `:` command, the explorer, runner, console and Rei work in all three: nano reaches the `:` line with `^T`, emacs with `M-x`.

|nano       |                     |emacs      |                          |
|-----------|---------------------|-----------|--------------------------|
|`^O` `^S`  |write out, save      |`C-x C-s` `C-x C-w`|save, save as     |
|`^X`       |exit (asks to save)  |`C-x C-c`  |quit                      |
|`^W` `M-W` |where is, next       |`C-s` `C-r`|incremental search        |
|`^\`       |replace              |`M-%`      |replace                   |
|`^K` `^U`  |cut line, paste      |`C-k` `C-y`|kill line, yank           |
|`^^` `M-6` |mark, copy           |`C-SPC` `C-w` `M-w`|mark, kill, copy region|
|`^_` `^C`  |go to line, position |`M-g` `M-<` `M->`|go to line, top, bottom|
|`^R`       |insert a file        |`C-x C-f` `C-x b`|find file, buffers  |
|`M-U` `M-E`|undo, redo           |`C-/` `C-g`|undo, cancel              |
|`^G`       |help                 |`C-x 2 3 o 0 1`|windows               |

### Normal Mode

|Key      |Action                 |
|---------|-----------------------|
|`i`      |Enter insert mode      |
|`I`      |Insert at first non-blank|
|`a`      |Append after cursor    |
|`A`      |Append at end of line  |
|`o`      |Open line below        |
|`O`      |Open line above        |
|`v`      |Visual mode (character)|
|`V`      |Visual mode (line)     |
|`h,j,k,l`|Navigate (←↓↑→)        |
|`w,b`    |Next/previous word     |
|`0,$`    |Start/end of line      |
|`gg,G`   |Top/bottom of file     |
|`dd`     |Delete line (yanks)    |
|`D`      |Delete to end of line  |
|`x`      |Delete character       |
|`C`      |Change to end of line  |
|`yy`     |Yank (copy) line       |
|`p,P`    |Paste after/before     |
|`u`      |Undo                   |
|`Ctrl-R` |Redo                   |
|`r`      |Replace character      |
|`J`      |Join lines             |
|`/`      |Search                 |
|`n,N`    |Next/previous match    |
|`Ctrl-C` |Copy to system clipboard|
|`Ctrl-V` |Paste from system clipboard|
|`:w`     |Save                   |
|`:e <file\|dir>`|Open file, or explorer at a directory (`:e ..`)|
|`:q`     |Quit                   |
|`:wq`    |Save and quit          |
|`Ctrl-F` |Page forward           |
|`Ctrl-B` |Page backward          |
|`]e,[e`  |Next/previous runner error|
|`:!<cmd>`|Run shell command (popup)|
|`:run <cmd>`|Run in the runner pane (async)|
|`:build,:test`|Run `build_cmd` / `test_cmd` from `.hakorc`|

### Visual Mode

|Key       |Action                 |
|----------|-----------------------|
|`h,j,k,l` |Extend selection      |
|`w,b`     |Word-wise selection    |
|`0,$`     |Select to start/end   |
|`gg,G`    |Select to top/bottom   |
|`Ctrl-F`  |Page forward selection |
|`Ctrl-B`  |Page backward selection|
|`y`       |Yank selection         |
|`d,x`     |Delete selection       |
|`c`       |Change selection       |
|`Ctrl-C`  |Copy to system clipboard|
|`Esc`     |Exit visual mode       |

### Window Management

|Key       |Action          |
|----------|----------------|
|`Ctrl-W s`|Split horizontal|
|`Ctrl-W v`|Split vertical  |
|`Ctrl-W w`|Switch pane     |
|`Ctrl-W 1-9`|Jump to editor pane N|
|`Ctrl-W r` / `k`|Jump to Rei / Kami|
|`Ctrl-W + - < >`|Resize       |
|`Ctrl-W c`|Close pane (`Ctrl-W !` discards unsaved changes)|

### File Explorer (箱 Kami)

|Key        |Action             |
|-----------|-------------------|
|`:kami`, `:ex`|Toggle explorer (`:e <dir>` opens it on a folder) |
|`j,k`      |Navigate files     |
|`h,l`      |Parent/open        |
|`g,G`      |Top/bottom of list |
|`.`        |Toggle hidden files|
|`r`        |Refresh            |
|`q,Esc`    |Close explorer     |

### Console

`:console` (or `:term`) opens a shell line under the runner's output, in the project dir. `enter` runs the line,
`up`/`down` walk its history, `ctrl-c` stops a command, `cd` moves it, `clear` and `exit` do what they say, `esc` goes
back to the file. With `console_sandbox=1` in your own rc (a project's rc may not set it) every line runs under
`sandbox-exec` on macOS or `bwrap` on Linux: writes only to the project and `$TMPDIR`, no network; with neither, it
refuses rather than running unsandboxed.

### Runner (bottom pane)

`:build`, `:test`, `:run <cmd>` stream a command's output into a bottom pane while you
keep editing. Lines that look like `file:line[:col]` are highlighted and jumpable —
the compile/fix loop without leaving the editor.

|Key        |Action                                  |
|-----------|----------------------------------------|
|`j,k`      |Select line                             |
|`Enter`    |Jump to `file:line[:col]` under selection|
|`],[`      |Next / previous error line              |
|`a`        |Ask the agent to fix the selected error |
|`r`        |Rerun                                   |
|`q`        |Stop if running, close otherwise        |

### AI Assistant (箱 Rei)

|Key        |Action                              |
|-----------|------------------------------------|
|`:ai`      |Toggle AI panel                     |
|`i`        |Enter prompt mode                   |
|`Enter`    |In INSERT: newline. In NORMAL: send |
|`:` `/`    |hako's commands, as a menu; `up`/`down` pick, `tab` completes, `enter` runs, `esc` closes it|
|`Esc`      |Back to NORMAL                      |
|`v`        |Visual select (copy-only in history)|
|`j,k`      |Navigate history                    |
|`Ctrl-W w` |Switch pane                         |
|`Ctrl-C`   |Close panel                         |
|`:q`       |Close panel                         |

### Pane commands (editor-owned — the `:` line, not sent to the agent)

|Command           |Action                                                        |
|------------------|--------------------------------------------------------------|
|`:auto` / `:noauto`|Skip / restore per-tool permission prompts for the session   |
|`:install-agent`  |Install the hako agent in place when none is found, then connect|
|`:update-agent`   |Self-update the agent (`hako --update`) and relaunch it        |
|`:clear`          |Wipe visible history · `:w` saves the chat to `ai_chat.txt`   |

When the agent asks to write a file or run a command, the pane prompts inline —
`● write_file  foo.c` then `[y] once  [n] no  [a] always`. `:auto` skips the prompts.

### Agent commands (inside the prompt — forwarded to hako)

Anything starting with `:` or `/` that the pane does not own goes to the agent as a
command, not to the model: `:model`, `:provider`, `:sessions`, `:theme`, `:login`,
`:skill install`, `:use`, `:usage`, `:help` and the rest. Bare `:model` / `:provider` /
`:sessions` open the editor's own picker; enter picks, esc cancels. The full list is in
the [hako-code README](https://github.com/mithraeums/hako-code#commands-inside-prompt).

**Tools** (once the project is trusted with `:trust`): the agent's — `read_file`, `list_dir`, `grep`, `glob`, `write_file`, `edit_file`, `edit_lines`, `run_shell`, `web_fetch` — each behind the `[y]/[n]/[a]` prompt above, with the output shown in the pane.

**Pane control** (any focused pane): `:q rei` / `:q kami` (also `:close <name>`) closes the named side panel.

<p align="center"><sub><b>—— IV ——</b></sub></p>

## Configuration

one rc for every tool in the suite. hake reads, in order:
- the first that exists of `$HAKORC`, `~/.hakorc`, `${XDG_CONFIG_HOME:-~/.config}/hako/hakorc` (writes go there)
- `./.hakorc`, on top: a project's own settings. it may not set what runs a command or reaches the network (`ai_endpoint`, `ai_api_key`, `ai_auto_approve`, `editor`, `console_sandbox`, `*_cmd` except `build_cmd`, `test_cmd`, `run_cmd`); hake says so once when it skips one

`edit.` before a key makes it hake's alone and beats the plain key; `code.` and `studio.` keys are the other tools'.
`:theme` and `:config key=value` change the one line that holds the key and keep the rest of the file;
`:config` opens the rc; `:set key=value` lasts for the session.

```
# ~/.hakorc
theme=terminal           # the default: your profile's colors
edit.theme=gruvbox       # hake only
edit.keys=vim            # or emacs, nano
show_line_numbers=1      # 2: relative
status_line=1            # 0 never, 1 with more than one pane, 2 always
word_wrap=1
tab_stop=8
mouse=1
build_cmd=make           # :build; test_cmd, run_cmd too
console_sandbox=1        # :console in a sandbox

theme.mine.base=nord     # a theme of your own: #rrggbb, ansi0-15 or default
theme.mine.bg=default
theme.mine.comment=#7a7a7a
```

slots: `bg fg comment keyword type string number line_number status_bg status_fg visual_bg visual_fg accent err`.
the old `theme_bg=R,G,B` keys still work on top of the active theme. the agent's keys (`ai_provider`, `ai_model` ...)
live in the same file; keys are better kept out of it: `:login <provider>` in the Rei pane stores them in `~/.hako/credentials`.

<p align="center"><sub><b>—— V ——</b></sub></p>

## Supported Languages

hake colors 45 file types (`hake --hl-dump <file>` shows what it sees):

**Systems**: C, C++, Rust, Go, Zig, Assembly  
**Scripting**: Python, Ruby, Perl, Lua, Shell, PHP  
**Web**: JavaScript, TypeScript, CSS, SCSS  
**JVM**: Java, Kotlin, Scala, Clojure  
**Functional**: Haskell, OCaml, F#, Elixir  
**Modern**: Swift, Dart, Julia, Nim  
**Writing**: Markdown (with fenced code in its own language), HTML, XML  
**Config**: YAML, TOML, INI, JSON, Dockerfile, Makefile, diff, `.hakorc`

<p align="center"><sub><b>—— VI ——</b></sub></p>

## Change Log

### Latest — v0.2.0
- **Lighter, like vim.** No splash: with no file you get vim's start screen. One window has no status bar, title bar or line numbers, just a ruler on the last line; they come back with a second pane (or `status_line=`).
- **Emacs and nano keys.** `hake --emacs` or `hake --nano` for a run, `edit.keys=` in the rc for good. The panes and every `:` command work in all three.
- **`:console`.** A shell line under the runner, in the project dir; `console_sandbox=1` runs it with no network, writing only to the project and `$TMPDIR`.
- **One rc, layered.** The same file as hako and the studio, with a project `.hakorc` on top. `terminal` is the default theme, custom themes come from the rc, and `:theme` saves the pick there.
- **A new highlighter that reads tokens.** Multi-line strings, nested comments, escapes and numbers done right, and full markdown with fenced code in its own language.
- **The Rei pane looks like hako** and has hako's command menu on `:` and `/`.

Earlier versions, and the full detail → [CHANGELOG.md](./CHANGELOG.md).

<p align="center"><sub><b>—— VII ——</b></sub></p>

## Contributing
If you share the belief that simplicity empowers creativity, feel free to contribute.

### Contribution is welcome in the form of:
- Forking this repo
- Submitting a Pull Request
- Bug reports and feature requests

Please ensure your code follows the existing style. I realize the single file is a personal choice, though if you choose to assist, I have divided the code.<br>

The file is divided into sections marked `/*** name ***/` — includes, defines, structs,
syntax, terminal, the cell grid, panes, rows, cursor, undo, search, file i/o, drawing, input,
key modes, popups, explorer, runner, the AI pane, the agent pipe client, init, main. `grep -n '^/\*\*\*' hake.c`
lists them with line numbers.

### Thank you for your attention.
This project started out of curiosity and a simple C text editor tutorial. If you hit any issues, feel free to open an issue on GitHub.
Pull requests, suggestions, or even thoughtful discussions are welcome.

<p align="center"><sub><a href="LICENSE">— SEE LICENSE —</a> &nbsp;·&nbsp; GPL-3.0</sub></p>

<p align="center"><sub><em>— deus sol invictus mithras —</em></sub></p>

