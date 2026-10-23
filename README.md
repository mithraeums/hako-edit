<p align="center">
  <a href="https://mithraeums.github.io">
    <img src="https://mithraeums.github.io/assets/banner-hake.png" alt="hake — a quiet box around the cursor" width="100%"/>
  </a>
</p>

<p align="center">
  <em>A minimalistic modal text environment, containing your code without distraction.</em>
</p>

<p align="center">
  <a href="https://github.com/mithraeums/hako-edit/releases"><img src="https://img.shields.io/badge/version-v0.1.8-e2402e?style=flat-square&labelColor=14130f" alt="v0.1.8"/></a>
  <img src="https://img.shields.io/badge/license-GPL--3.0-2636c4?style=flat-square&labelColor=14130f" alt="GPL-3.0"/>
  <img src="https://img.shields.io/badge/C99-single%20file-2636c4?style=flat-square&labelColor=14130f" alt="C99 single file"/>
  <img src="https://img.shields.io/badge/themes-19-2636c4?style=flat-square&labelColor=14130f" alt="19 themes"/>
  <img src="https://img.shields.io/badge/languages-40%2B-2636c4?style=flat-square&labelColor=14130f" alt="40+ languages"/>
</p>

<p align="center">
  <sub><a href="https://mithraeums.github.io">site</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-edit/releases">releases</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-code">hako-code</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako">hako (engine)</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-studio">hako-studio</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums">org</a></sub>
</p>

<br>

<p align="center">
  <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/demo.gif?raw=true" alt="hake — splash, syntax, theme picker, help popup" width="88%"/>
</p>

<table align="center">
  <tr>
    <td align="center" width="50%">
      <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/explorer.png?raw=true" alt="Kami explorer" width="100%"/><br/>
      <sub><b>箱 Kami</b>: file explorer</sub>
    </td>
    <td align="center" width="50%">
      <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/rei.png?raw=true" alt="hake with the Rei pane: rei proposes an edit to main.c, it is allowed, and the open buffer reloads with the new code" width="100%"/><br/>
      <sub><b>箱 Rei</b> AI (powered by hako-code)</sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/split.png?raw=true" alt="Split panes" width="100%"/><br/>
      <sub>Multi-pane splits · resize with <code>Ctrl-W</code></sub>
    </td>
    <td align="center" width="50%">
      <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/splash.png?raw=true" alt="Launch splash" width="100%"/><br/>
      <sub>Launch splash · mithraeum theme</sub>
    </td>
  </tr>
</table>

<br>

箱 **Rei** lives inside hake; powered by <a href="https://github.com/mithraeums/hako-code">hako-code</a>:
```
 ▄█████▄
██ ███ ██ Rei: hako-code
█████████ Provider: You Choose
▀█▀▀█▀▀█▀ Model: You Choose
```

<p align="center"><sub><b>—— I ——</b></sub></p>

## Overview

- **Modal Editing**: Vim-inspired normal, insert, visual, and visual-line modes
- **Full Motion Set**: counts, named registers, text objects (`diw`, `ci"`, `da(`), marks, jumplist, dot-repeat, `:s/` substitution, bracket match
- **Multi-Pane Support**: Split horizontally and vertically, resize with `Ctrl-W +/-/</>`, jump with `Ctrl-W 1-9`; each pane has a title bar (`pane_titles=0` turns it off)
- **File Explorer**: 箱 Kami - directory navigation, hidden-file toggle, dirs-first sort
- **AI Assistant**: 箱 Rei - [hako-code](https://github.com/mithraeums/hako-code) in a split pane, over its `--pipe` protocol. Local hako models (`hako-sho` 3B / `hako-koi` 7B) or any of its 13 cloud providers; its tools, approvals, pickers and sessions, rendered by the editor. A file Rei edits reloads in its pane
- **Skills**: the agent's [Agent Skills](https://agentskills.io) — `:skill install owner/repo` from the Rei pane
- **Syntax Highlighting**: 40+ languages, search-match highlighting persists across edits
- **Undo/Redo**: time-bounded undo blocks, configurable depth
- **Visual Selection**: character and line modes with yank/delete/change, count feedback
- **Clipboard**: bracketed paste, system clipboard via `Ctrl-C`/`Ctrl-V`
- **Fast Search**: incremental with `n`/`N`, all matches highlighted
- **Runner**: `:build` / `:test` / `:run` stream into a bottom pane while you edit; `]e`/`[e` jump to `file:line` errors; `a` hands the error to Rei. `:!<cmd>` for quick one-shots
- **Line Numbers**: absolute or relative, dynamic width
- **Mouse Support**: click to position, scroll wheel moves cursor
- **19 Themes**: mithraeum and washi (the suite's own, dark and light), dark, light, gruvbox, nord, dracula, monokai, solarized, tokyonight, catppuccin, onedark, material, everforest, rosepine, github-dark, github-light, ayu, kanagawa — live-preview picker via `:theme`
- **Customizable**: every setting in `.hakorc`, model/provider choices persist in `~/.hako/state`

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

Use supplied config, or generate one with `:config` inside the editor. HAKE looks for `.hakorc` in the current directory first, then your home directory.

```
# Example .hakorc
tab_stop=4
use_tabs=1
show_line_numbers=2
auto_indent=1
smart_indent=1
mouse=1          # shared with hako (where it defaults off); mouse_enabled still works
theme=mithraeum  # shared with hako and hako studio: the same 18 names
explorer_width=30
pane_titles=1

# Runner pane
# build_cmd=make
# test_cmd=make test
# run_cmd=./a.out

# AI panel (Rei) — these are the agent's keys; one ~/.hakorc serves both
ai_provider=mithraeum         # local hako models; or anthropic | openai | gemini | ollama | ...
ai_model=hako-sho
ai_max_tokens=4096
ai_tools_enabled=1
ai_stream=1
# ai_endpoint=                # auto-set for known providers
```

Keys are better kept out of the rc: `:login <provider>` in the Rei pane stores them in `~/.hako/credentials`.

Run `:config` to generate a fully documented `.hakorc` with all options.

<p align="center"><sub><b>—— V ——</b></sub></p>

## Supported Languages

hake provides syntax color for 40+ languages, some of which include:

**Systems**: C, C++, Rust, Go, Zig, Assembly  
**Scripting**: Python, Ruby, Perl, Lua, Shell, PHP  
**Web**: JavaScript, TypeScript, HTML, CSS, JSON  
**JVM**: Java, Kotlin, Scala, Clojure  
**Functional**: Haskell, OCaml, F#, Elixir  
**Modern**: Swift, Dart, Julia, Nim  
**Config**: YAML, TOML, Dockerfile, Makefile

<p align="center"><sub><b>—— VI ——</b></sub></p>

## Change Log

### Latest — v0.1.8
A small one, to keep the Rei pane in step with hako-code 0.2.5.

- **A menu pick leaves no trace in the chat.** A provider or model change made from a picker shows in the status line (hako's new `state` event) instead of as a line in the conversation.

Earlier versions, and the full detail → [CHANGELOG.md](./CHANGELOG.md).

<p align="center"><sub><b>—— VII ——</b></sub></p>

## Roadmap
- [ ] Windows parity (pipe agent + runner pane)
- [ ] Custom themes from `.hakorc` in the theme picker
- [ ] Buffer list (`:ls`, `:b`)
- [ ] `:e` tab completion

<p align="center"><sub><b>—— VIII ——</b></sub></p>

## Contributing
If you share the belief that simplicity empowers creativity, feel free to contribute.

### Contribution is welcome in the form of:
- Forking this repo
- Submitting a Pull Request
- Bug reports and feature requests

Please ensure your code follows the existing style. I realize the single file is a personal choice, though if you choose to assist, I have divided the code.<br>

The file is divided into sections marked `/*** name ***/` — includes, defines, structs,
syntax, terminal, panes, rows, cursor, undo, search, file i/o, drawing, input, popups, splash,
explorer, runner, the AI pane, the agent pipe client, init, main. `grep -n '^/\*\*\*' hake.c`
lists them with line numbers.

### Thank you for your attention.
This project started out of curiosity and a simple C text editor tutorial. If you hit any issues, feel free to open an issue on GitHub.
Pull requests, suggestions, or even thoughtful discussions are welcome.

<p align="center"><sub><a href="LICENSE">— SEE LICENSE —</a> &nbsp;·&nbsp; GPL-3.0</sub></p>

<p align="center"><sub><em>— deus sol invictus mithras —</em></sub></p>

