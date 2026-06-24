<p align="center">
  <a href="https://mithraeums.github.io">
    <img src="https://mithraeums.github.io/assets/banner-hake-dark.svg" alt="hake — a quiet box around the cursor" width="100%"/>
  </a>
</p>

<p align="center">
  <em>A minimalistic modal text environment, containing your code without distraction.</em>
</p>

<p align="center">
  <a href="https://github.com/mithraeums/hako-edit/releases"><img src="https://img.shields.io/badge/version-v0.1.5-b89656?style=flat-square&labelColor=14130f" alt="v0.1.5"/></a>
  <img src="https://img.shields.io/badge/license-GPL--3.0-c8c2b2?style=flat-square&labelColor=14130f" alt="GPL-3.0"/>
  <img src="https://img.shields.io/badge/C99-single%20file-c8c2b2?style=flat-square&labelColor=14130f" alt="C99 single file"/>
  <img src="https://img.shields.io/badge/themes-17-c8c2b2?style=flat-square&labelColor=14130f" alt="17 themes"/>
  <img src="https://img.shields.io/badge/languages-40%2B-c8c2b2?style=flat-square&labelColor=14130f" alt="40+ languages"/>
</p>

<p align="center">
  <sub><a href="https://mithraeums.github.io">site</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-edit/releases">releases</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums/hako-code">hako-code</a> &nbsp;·&nbsp; <a href="https://github.com/mithraeums">org</a></sub>
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
      <img src="https://github.com/mithraeums/mithraeums.github.io/blob/main/assets/readme-screenshots/hako-edit/rei.png?raw=true" alt="Rei AI pane" width="100%"/><br/>
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
██ ███ ██ Rei: v1.1
█████████ Provider: You Choose
▀█▀▀█▀▀█▀ Model: You Choose
```

<p align="center"><sub><b>—— I ——</b></sub></p>

## Overview

- **Modal Editing**: Vim-inspired normal, insert, visual, and visual-line modes
- **Full Motion Set**: counts, named registers, text objects (`diw`, `ci"`, `da(`), marks, jumplist, dot-repeat, `:s/` substitution, bracket match
- **Multi-Pane Support**: Split horizontally and vertically, resize with `Ctrl-W +/-/</>`
- **File Explorer**: 箱 Kami - directory navigation, hidden-file toggle, dirs-first sort
- **AI Assistant**: 箱 Rei - powered by [hako-code](https://github.com/mithraeums/hako-code) in a split pane. Local hako models (`hako-sho` 3B / `hako-koi` 7B) or any of its 13 cloud providers, function-calling tool loop, SSE streaming, per-project trust and history
- **Skills**: drop `~/.hako/skills/*.md` into the system prompt, install from any URL with `/skill install`
- **Syntax Highlighting**: 40+ languages, search-match highlighting persists across edits
- **Undo/Redo**: time-bounded undo blocks, configurable depth
- **Visual Selection**: character and line modes with yank/delete/change, count feedback
- **Clipboard**: bracketed paste, system clipboard via `Ctrl-C`/`Ctrl-V`
- **Fast Search**: incremental with `n`/`N`, all matches highlighted
- **Line Numbers**: absolute or relative, dynamic width
- **Mouse Support**: click to position, scroll wheel moves cursor
- **17 Themes**: dark, light, gruvbox, nord, dracula, monokai, solarized, tokyonight, catppuccin, onedark, material, everforest, rosepine, github-dark, github-light, ayu, kanagawa
- **Customizable**: every setting in `.hakorc`, model/provider choices persist in `~/.hako/state`

<p align="center"><sub><b>—— II ——</b></sub></p>

## Build

```sh
# One-liner - works on Linux, macOS, Windows (mingw)
gcc hake.c -o hake -lpthread

# Or use the Makefile to also embed the executable icon where the OS allows
make

# Build flavors
make BUNDLE_HAKO=1   # default — also builds the hako agent next to hake
make BUNDLE_HAKO=0   # editor only; :install-agent fetches the agent later
make NO_AGENT=1      # editor with no agent path at all (Rei pane stays inert)
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
# Curl one-liner (Linux / macOS / Windows-MinGW)
curl -fsSL https://raw.githubusercontent.com/mithraeums/hako-edit/main/install.sh | sh
```

Verifies sha256, installs to `/usr/local/bin` if writable else `~/.local/bin`. macOS quarantine xattr stripped post-install. Set `VERIFY=0` to skip sha verify, `PREFIX=/opt` to relocate.

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
apk add gcc make musl-dev curl
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
|`:e [filename]`     |Open file   |
|`:q`     |Quit                   |
|`:wq`    |Save and quit          |
|`Ctrl-F` |Page forward           |
|`Ctrl-B` |Page backward          |

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
|`Ctrl-W c`|Close pane      |

### File Explorer (箱 Kami)

|Key        |Action             |
|-----------|-------------------|
|`:e,:explorer`|Toggle explorer |
|`j,k`      |Navigate files     |
|`h,l`      |Parent/open        |
|`g,G`      |Top/bottom of list |
|`.`        |Toggle hidden files|
|`r`        |Refresh            |
|`q,Esc`    |Close explorer     |

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

### Slash commands (inside prompt — forwarded to the agent):

|Command                 |Action                                       |
|------------------------|---------------------------------------------|
|`/help`                 |List commands                                |
|`/provider <name>`      |ollama, anthropic, openai, deepseek, mistral, together, fireworks, openrouter, groq, xai (auto-fills endpoint) |
|`/model <id>`           |Switch model (persists to `~/.hako/state`)   |
|`/tools on` / `off`         |Toggle function calling                      |
|`/trust` / `/trust revoke` | Grant or revoke file-ops in this project  |
|`/skills [reload]`      |List / reload `~/.hako/skills/*.md`          |
|`/skill install <url>`  |Download a skill into `~/.hako/skills/`      |
|`/history local` / `global`|Show path, or move to `<cwd>/.hako/history` |
|`/file <path>`          |Inject a local file into context             |
|`/clear`                |Wipe visible history                         |
|`/usage`                |Show provider/model/trust + token totals     |
|`/sessions`             |List up to 16 prior sessions                 |
|`/session [new]`        |Show session info; `new` resets              |
|`/resume <id>`          |Switch to prior session                      |
|`/quit`                 |Close the panel                              |

**Tools** (when trusted): `read_file`, `list_dir`, `read_open_file`, `list_open_files`, `write_file` (path constrained to project; staged when `ai_autowrite=0`), `run_shell` (project must be trusted).

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
mouse_enabled=1
theme=dark
explorer_width=30

# AI panel (Rei)
ai_provider=deepseek          # or anthropic | openai | ollama | mistral | ...
ai_api_key=sk-...             # not needed for ollama
ai_model=deepseek-chat
ai_max_tokens=2048
ai_tools_enabled=1
ai_stream=1
ai_autowrite=1                # 0 stages writes to <path>.hako-pending
# ai_endpoint=                # auto-set for known providers
```

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

### v0.1.5 (Latest)
Interface polish, an on-brand theme, a unified config — plus two crash fixes.

- **Popup menus** — `:help`, a bare `:theme` (live-preview picker), and `:registers` open scrollable popups (mouse + keyboard).
- **Mithraeum theme on-brand** — matches the site palette; gold `#b89656` is the primary UI accent (borders, splash, popups, status, line numbers).
- **One unified `~/.hakorc`** — `:config` merges and preserves the agent's `ai_*` and other tools' keys; defaults `theme=mithraeum`.
- **Agent integration** — in-pane tool-permission prompts (`[y]/[n]/[a]`), `:install-agent` / `:update-agent`, agent auto-respawn, and a no-agent build (`make NO_AGENT=1`).
- **Crash fixes** — closing the Rei pane (`Ctrl-C`) no longer use-after-frees the agent reader thread; search no longer reads out of bounds at end of line.

Full history → [CHANGELOG.md](./CHANGELOG.md).

<p align="center"><sub><b>—— VII ——</b></sub></p>

## Roadmap
- [ ] Windows parity
- [ ] Diff-render (fewer redraws)
- [ ] In-editor slash menus for themes and settings
- [ ] Write-file diff preview with confirm
- [ ] OpenAI/Ollama tool parity
- [ ] Buffer list (`:ls`, `:b`)

<p align="center"><sub><b>—— VIII ——</b></sub></p>

## Contributing
If you share the belief that simplicity empowers creativity, feel free to contribute.

### Contribution is welcome in the form of:
- Forking this repo
- Submitting a Pull Request
- Bug reports and feature requests

Please ensure your code follows the existing style. I realize the single file is a personal choice, though if you choose to assist, I have divided the code.<br>

Sections of codebase are as follows:
1. Includes
2. Defines
3. Enums
4. Struct Declarations
5. Function Prototypes
6. Syntax Highlighting
7. Terminal
8. Buffer
9. Helpers
10. Color
11. Clipboard
12. Pane Management
13. Row Operations
14. Editor Operations
15. Cursor Movement
16. Scrolling
17. Visual Mode
18. Undo/Redo
19. Search
20. File I/O
21. Drawing
22. Input
23. Mode and Commands
24. Main Drawing Functions
25. Input Processing
26. Splash Screen
27. Explorer
28. AI Assistant
29. Init
30. Main

### Thank you for your attention.
This project started out of curiosity and a simple C text editor tutorial. If you hit any issues, feel free to open an issue on GitHub.
Pull requests, suggestions, or even thoughtful discussions are welcome.

<p align="center"><sub><a href="LICENSE">— SEE LICENSE —</a> &nbsp;·&nbsp; GPL-3.0</sub></p>

<p align="center"><sub><em>— deus sol invictus mithras —</em></sub></p>

