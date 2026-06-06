# Changelog

All notable changes to hako-edit (binary: `hake`). Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project follows semver (`v0.x.y` is pre-1.0; expect breaking changes between minor versions).

## [v0.1.4] — 2026-06-05

### Fixed — bundled agent discovery
- **`hakoFindBinary` now finds `hako-bundled`.** `BUNDLE_HAKO=1 make install` drops the agent as `hako-bundled` (to avoid clobbering a standalone `hako`), but the finder only looked for `hako` — so a bundle-only install was invisible and the Rei pane showed "not found". The finder now tries `hako` then `hako-bundled` at each location (exe dir → cwd → `~/.local/bin` → `/usr/local/bin` → PATH), preferring a standalone `hako`.

### Changed — suite alignment (hako-code v0.1.8 / flattened engine)
- The embedded agent runs as a `--pipe` subprocess and is the *same* binary as the standalone `hako`: `:pull hako-sho` / `:model` / the `hakm` engine all work identically inside the editor. No ollama, no in-process engine link (the old `BUNDLE_HAKO` engine-link concern is moot — the agent spawns `hakm` as a subprocess).
- Sharper "agent not found" guidance in the Rei pane: `hako.sh` installer, bundled-rebuild hint, and the `:pull hako-sho` next step.

### Removed
- Duplicate `LICENSE.txt` (GPL-3.0 lives in `LICENSE`).

## [v0.1.3] — 2026-05-25

### Fixed — clipboard fidelity
- **`hkSystemClipboardWrite` / `hkSystemClipboardRead`** — OS-aware transport replaces the old `xclip … 2>/dev/null || pbcopy …` chain. macOS picks `pbcopy`, Wayland picks `wl-copy`, X11 picks `xclip -selection clipboard -i`, Windows picks `clip`. The old shell `||` chain leaked the first chunk of large yanks on macOS (xclip-not-found path) and dropped non-newline trailing bytes via `fgets`-based reader. Tabs / indents / multi-line code now survive round-trip yank-out → external paste verbatim.
- **`editorPasteFromSystemClipboard`** — rewritten to walk bytes verbatim (no more `fgets` line buffering). Tab characters preserved; raw control bytes silently dropped.

### Added — naming rename
- **CLI: `hako` → `hake`.** Repo: `mithraeums/hako` → `mithraeums/hako-edit` (GitHub auto-redirects old URL). Source file: `hako.c` → `hake.c`. Macros: `HAKO_VERSION` / `HAKO_HELP_TEXT` / `HAKO_HL_*` / `HAKO_TRACE` → `HAKE_*`. Status bar / splash / `--help` / `--version` updated. Splash gains an `EDIT` line under the block-letter logo (above the kanji 箱).
- **Subprocess wire renamed.** Internal `claw*` symbols → `hako*` (the wire targets the v0.1.6 `hako` agent, not the legacy `claw` name). Binary lookup chain now searches for `hako` on `PATH` / `./hako` / `~/.local/bin/hako` / `/usr/local/bin/hako`.
- **`BUNDLE_HAKO` Makefile flag** (was `BUNDLE_CLAW`). Default `1` — `make` compiles `../hako-code/hako.c` into a bundled `hako` agent next to `hake`. `make BUNDLE_HAKO=0` builds the editor alone.

### Notes
- `~/.hakorc` is shared with the `hako` agent (the agent moved off `~/.hakocrc` in v0.1.6). Editor reads its own keys; AI keys are read by the spawned agent.
- The v0.1.6 hako-code companion ships an auto-migrator for `~/.hakoc/` → `~/.hako/`. The editor needs no migrator since it never wrote to either path.

## [v0.1.2]

### Added
- **Mithraeum default theme** — void / paper / gold / rust palette matches the site banners + claw/hako icons. Sets `THEME_MITHRAEUM` as the boot default; classic `dark` and the other 16 presets remain available via `:theme <name>` or `theme=<name>` in `.hakorc`.
- **`BUNDLE_CLAW` Makefile flag** (default `1`) — `make` builds both `hako` and `hakoc` from `../hakoCLAW/hakoCLAW.c`; `make BUNDLE_CLAW=0` builds the editor alone. Bundled `hakoc` installs alongside hako; the Rei pane finds it at runtime.

### Changed
- **Rei pane decoupled from in-source AI.** All AI logic (HTTP, providers, tools, sessions, slash commands, history persistence) has moved to `hakoCLAW`. Hako spawns `hakoc --pipe` as a subprocess and talks JSONL over stdin/stdout. The Rei pane keeps render + key handling only; tool announcements, streaming responses, slash output all flow back as `message` / `tool_start` / `tool_end` / `done` events.
- Hako source dropped from ~10,240 → ~8,555 LOC (≈1,700 lines moved out).
- Rei pane no longer touches `.hako/` on disk. All session + skill + log state now lives under `.hakoc/` (hakoc's domain). Removes the dual-state-dir confusion.
- `~/.hakorc` AI keys (`ai_provider`, `ai_api_key`, `ai_model`, etc.) are now silently ignored by hako — they belong in `~/.hakocrc` (read by hakoc). Old configs still load cleanly.

### Fixed
- **`SIGPIPE` no longer kills the editor** when hakoc dies mid-turn. Hako installs `SIG_IGN`; `clawSendLine` detects `EPIPE` and marks the pipe dead so further sends no-op.
- `clawShutdown` reaps the subprocess cleanly — graceful drain loop, then `SIGTERM` + blocking `waitpid` fallback. No zombies on Rei pane close.

### Removed
- In-source AI transport: `aiBuildCurlCommand`, `aiBuildMessagesJson`, `aiWorkerThread`, `HK_TOOLS[]`, `hkExecTool`, `hkFnToolExecAll`, `hkProviderName/Parse/ApplyAlias`, `hkLoadSession`, `hkSaveSession`, `hkLoadHistoryTail`, `hkLoadSkills`, `hkLogMessage`, `hkProjectTrusted/Grant`, `hkUpdateUsage`, all `aiProvider` / `aiMessage` structs, all `E.ai_*` and `E.session_*` config fields.
- Trust y/N popup on first `:rei` — trust state now lives in hakoc; granted via `/trust` slash inside the Rei pane.

## [v0.1.1]

Windows + iSh fixes + splash polish.

- **Windows build fixed** — `detectTerminalType` definition moved outside the `#ifndef _WIN32` block (forward-decl was visible, body wasn't — MinGW link error). Unblocks `v0.1*` tag artifacts.
- **iSh hardening** — `gridResize` caps `w`/`h` at 4096 with `size_t` math + alloc-failure rollback; `editorUpdateWindowSize` clamps `rows`/`cols` (24×80 floor, 4096 ceil). Defensive fix for iPad memlock on startup.
- **Skill loader bounded** — checked `realloc`, `fread` advance uses `got` not requested size, 64-skill ceiling, 1 MiB cumulative prompt cap, `fclose` on every early break path.
- **`editorLoadConfig` early-return bug** — no `.hakorc` previously short-circuited before `hkLoadSession()`; fresh setups never resumed. Now loads session even when config absent.
- **Splash gained `# NEW` + `# TIPS`** sections (renders when screen has the vertical room).
- New mithraeum-aesthetic icon (`icon/hako.svg`): kanji `箱` on void, phosphor-green cursor block, gold corner ticks. `make icons` target regenerates `.icns` / `.ico` / `.png`.

## [v0.1.0]

Polish + portability + safety pass.

### Editor / panes
- Cell-grid diff renderer: front/back buffers, minimal escape emission, no flicker.
- Squished-terminal vertical pane stack: AI auto-stacks below editor when too narrow (≥ 16 rows).
- Cross-pane close: `:q rei` / `:q kami` (also `:close <name>`) closes named side from any focused pane.
- Explorer border fix; mouse hit-testing covers all panes including stacked.
- Themes: `:theme <name>` / `:colorscheme <name>` runtime switch (17 presets).
- Binary-file refuse: NUL-byte detection in first 4 KB → no garbage spew on image opens.

### Rei (AI panel)
- Cursor in prompt: `←/→/↑/↓/Home/End`, BS/DEL splice at cursor, mid-text edits.
- Visual mode in chat history: `j/k/gg/G/Ctrl-D/Ctrl-F/Ctrl-U/Ctrl-B/V/y/Esc/Ctrl-C/Ctrl-W`.
- Provider aliases: `deepseek`, `mistral`, `together`, `fireworks`, `openrouter`, `groq`, `xai/grok`; endpoint auto-filled.
- Token tracking: `/usage` shows `last N in / M out  total ... (cap K)` across Anthropic / OpenAI / Ollama.
- Blockquote tint: AI lines starting `> ` use comment color.
- `ai_autowrite=0` stages writes to `<path>.hako-pending` (line / byte diff returned).
- Sessions: `/sessions`, `/resume <id>`, `/session [new]`; auto-resume hint on launch.
- Universal tools (Anthropic native + Ollama / OpenAI function-calling fallback).
- Per-call announcements (`→ read_file(test.txt)` / `← N bytes` / `error: ...`).
- Spinner: `⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏ thinking...`.
- Streaming: `\n` sanitized live, multi-line split on stream end.

### Build / release
- GitHub Actions release workflow (linux / macos / windows on tag `v0.1*`).
- README documents iSh build (`apk add gcc make musl-dev curl && make`).
- Makefile unchanged for one-liner: `gcc -O2 -Wall hako.c -o hako -lpthread`.

## [v0.0.9]

HAKO goes from editor-with-AI-panel to modal editing environment with a real agent inside.

### Editor
- Full vim-motion set: count prefix (`5j`, `3dd`), named registers (`"ayy`, `"ap`), text objects (`diw`, `ci"`, `da(`), marks (`ma`, `'a`), jumplist (`Ctrl-O` / `Tab`), dot-repeat (`.`), substitution (`:s/foo/bar/g`), bracket match (`%`), word-under-cursor search (`*`, `#`).
- Search highlighting persists across edits, all matches lit (not just current).
- Line-delete feedback in status (`N lines deleted`).
- `:e` expands `~/`, resolves relative and absolute paths so `:w` saves where you meant.
- `:q` closes the active pane, not the whole program.
- `dw`, `cw`, `:w file.txt` confirmed working.
- 17 theme presets (dark, light, gruvbox, nord, dracula, monokai, solarized, tokyonight, catppuccin, onedark, material, everforest, rosepine, github-dark, github-light, ayu, kanagawa).
- Responsive layout; side panels auto-collapse on narrow terminals.
- Explorer entries always sorted: directories first, then files, case-insensitive.
- Status bar now uses the border color so panels feel like one chassis.

### Rei 零
- Multi-provider: Anthropic, OpenAI, Ollama (swap with `/provider`).
- Function-calling tool loop (Anthropic): `read_file`, `list_dir`, `write_file`, `run_shell`.
- SSE streaming for text responses.
- Project trust: first `:ai` asks before enabling file tools in a directory; grants stored in `<project>/.hako/trust`.
- Per-project chat history in `<project>/.hako/history` when trusted, falls back to `~/.hako/history`.
- Skills loader: `~/.hako/skills/*.md` injected into system prompt; `/skill install <url>` downloads a skill.
- Multi-line input: Enter inserts newline in INSERT, sends in NORMAL. Input bar wraps and stays visible.
- Slash commands: `/help`, `/provider`, `/model`, `/tools`, `/trust`, `/skills`, `/skill install`, `/history`, `/file`, `/clear`, `/quit`.
- Model + provider choice persists across restarts (`~/.hako/state`).
- Configurable mascot (`ai_mascot=path/to/art.txt`), lucky-cat default.

### Fixes
- Pane focus no longer dangles when a side panel is closed from inside it.
- Ctrl-W back from Rei does not lock the terminal.
- Inline prompt reads (`"a`, `di"`, `ma`, `r`) skip mouse-motion events instead of consuming them as the expected letter.
- Splash logo + status bar now theme-aware (match border color).
- Cursor lands in the selected explorer entry and in the Rei input bar instead of the status line.
- Window-size changes resize panes immediately instead of drifting until the next SIGWINCH.

## [v0.0.8]

Major stability and usability update.

### Features
- Bracketed paste mode support; no more staircase indentation on paste.
- Rapid-input paste detection fallback for terminals without bracketed paste.
- System clipboard integration (Ctrl-C to copy, Ctrl-V to paste).
- New vim motions: I, a, A, o, O (insert variants), x (delete char), D (delete to EOL), C (change to EOL).
- Visual mode navigation: w/b, 0/$, gg/G, Ctrl-F/Ctrl-B for fast selection.
- `dd` now yanks the deleted line into the paste buffer (vim behavior).
- Scroll wheel moves cursor naturally (like j/k) instead of jumping the viewport.
- Mouse click works in insert mode without producing artifacts.
- Dynamic line number width; scales for 10,000+ line files.
- Improved terminal type detection (Apple Terminal 256-color support).
- Improved ANSI color fallback for basic terminals (visual selection visible everywhere).

### Bug fixes
- Fixed file save writing to wrong location when opened from a path.
- Fixed pane tree memory leaks on close and exit.
- Fixed right panel (AI) not being freed on cleanup.
- Reduced screen flicker via buffer optimization (geometric growth in append buffer).
- Consistent naming across codebase (紙 Kami = explorer, 角 Kaku = AI).
- Explicit smart_indent initialization.

## [v0.0.7]

- New splash screen.
- Complete visual mode implementation (v/V).
- Full copy/paste system with yank buffer.
- Additional vim motions (w/b, 0/$, r, J).
- Screen splitting.
- 紙 Kami explorer panel.
- 角 Kaku AI assistant panel (scaffolding).
- Enhanced help system.
- Improved terminal cleanup.
- Themes in `.hakorc`.
- Config generation via `:config` command.
- Mouse support with click-to-position and scroll wheel.

## [v0.0.6]

### Features
- Full visual mode implementation (v for character, V for line selection).
- Complete copy/paste system with yank buffer (y to copy, p/P to paste).
- Additional vim motions: w/b (word movement), 0/$ (line start/end).
- Character replacement with r, line joining with J.
- Enhanced help system (`:help` in editor, `-h` / `--help` from command line).
- New file creation — editing non-existent files creates them on save.
- Improved terminal cleanup on exit.

### Bug fixes
- Fixed undo/redo segfault when undoing paste operations.
- Resolved terminal color persistence after exit.
- Corrected visual selection boundary handling.
- Fixed edge cases with empty line selections.

## [v0.0.5]

### Features
- More vi-like functionality with undo (u in normal) / redo (Ctrl+R in normal) blocks.
- Undo blocks function with time, action, and mode based boundaries.
- Configurable undo levels in `.hakorc` (default = 100).
- Options for number and relative number mode (replacing e.g. `~` with `1`) in `.hakorc`.
- Number mode currently supports up to 4 characters (e.g. 9999 lines).

### Bug fixes
- Fixed terminal crashing from invalid line jumps.
- Corrected cursor positioning and edge cases pertaining to empty file navigation.
- Removed duplicate code and minor debug output.

## [v0.0.4]

- Added familiar vi-inspired modes & commands (normal & insert, `/`, `:`, `:w`, `:wq`, `:q`).
- Line jump via `:NUMBER` (e.g. `:120` goes to line 120).
- `Ctrl+F` pages forward, `Ctrl+B` pages back.

## [v0.0.3]

- Implemented `tt` (go to line 1) and `bb` (go to last line) normal-mode shortcuts.
- Improved fuzzy find triggered by `/`; use arrow keys or scroll to move between hits, enter/esc to exit.
- Language support added for C++, C#, Java, Rust, SQL, HTML/CSS, and JavaScript (in addition to Python & C).

## [v0.0.2]

- Established default font color theme, found in `.hakorc`.
- Alternate-screen & clean exit, wiping the terminal.

## [v0.0.1]

- Initial push.
- Basic text editing.
- Ability to save and load files.
