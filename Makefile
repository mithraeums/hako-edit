# hake (hako-edit) — minimal cross-platform build with icon
#
# Windows: icon embedded into .exe via windres (real OS icon).
# macOS:   icon attached via Rez/SetFile if Xcode CLT installed (best-effort);
#          otherwise icon/hake.icns is shipped alongside.
# Linux:   ELF can't embed icons; icon/hake.png is shipped alongside for use
#          with a .desktop entry.
#
# BUNDLE_HAKO=1 (default): also build the `hako` agent binary from ../hako-code/hako.c.
#   hake will find and spawn it at runtime for the Rei panel.
# BUNDLE_HAKO=0: build hake only. Rei pane shows install guidance (:install-agent)
#   until a hako binary is on the system; the editor can install it in place.
# NO_AGENT=1: build an editor with NO agent path at all. The Rei pane stays inert and
#   says "this build ships without an agent" — it never errors. Implies BUNDLE_HAKO=0.

CC          ?= gcc
CFLAGS      ?= -O2 -Wall
LDLIBS      ?= -lpthread
BUNDLE_HAKO ?= 1
NO_AGENT    ?= 0
HAKO_SRC    ?= ../hako-code/hako.c

ifeq ($(NO_AGENT),1)
    CFLAGS      += -DHAKE_NO_AGENT
    BUNDLE_HAKO := 0
endif

ICON_DIR = icon
BIN      = hake

ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    BIN     := hake.exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM = macos
    else ifeq ($(UNAME_S),FreeBSD)
        PLATFORM = freebsd
    else
        PLATFORM = linux
    endif
endif

# macOS universal2 toggle: make UNIVERSAL=1 → fat binary (arm64 + x86_64).
ifeq ($(PLATFORM),macos)
    ifeq ($(UNIVERSAL),1)
        CFLAGS += -arch arm64 -arch x86_64
    endif
endif

ifeq ($(BUNDLE_HAKO),1)
TARGETS = $(BIN) hako
else
TARGETS = $(BIN)
endif

.PHONY: all clean icons install uninstall
all: $(TARGETS)

# Build the `hako` agent alongside hake when BUNDLE_HAKO=1.
hako: $(HAKO_SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

# ---------- icons ----------
# Regenerate icon/hake.{icns,ico,png} from icon/hake.svg.
# Requires rsvg-convert or ImageMagick. iconutil (macOS) → .icns, magick → .ico.
icons:
	@cd $(ICON_DIR) && bash build-icons.sh

# ---------- Windows: embed icon via resource (optional — skip if .ico missing) ----------
ifeq ($(PLATFORM),windows)

HAS_ICO := $(wildcard $(ICON_DIR)/hake.ico)

ifeq ($(HAS_ICO),)
$(BIN): hake.c
	$(CC) $(CFLAGS) hake.c -o $@ $(LDLIBS)
else
hake.rc:
	@printf 'IDI_ICON1 ICON "$(ICON_DIR)/hake.ico"\n' > $@

hake.res: hake.rc $(ICON_DIR)/hake.ico
	windres $< -O coff -o $@

$(BIN): hake.c hake.res
	$(CC) $(CFLAGS) hake.c hake.res -o $@ $(LDLIBS)
endif

endif

# ---------- macOS: build, then attach icon if tools exist ----------
ifeq ($(PLATFORM),macos)

$(BIN): hake.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
	@if command -v Rez >/dev/null 2>&1 && command -v SetFile >/dev/null 2>&1; then \
		printf 'read %c%s%c (-16455) "%s/hake.icns";\n' "'" "icns" "'" "$(ICON_DIR)" > .hake.r; \
		Rez -append .hake.r -o $(BIN) && SetFile -a C $(BIN) && \
		echo "icon attached to $(BIN)" || echo "icon attach failed (non-fatal)"; \
		rm -f .hake.r; \
	else \
		echo "Rez/SetFile not found — install Xcode CLT to attach icon (xcode-select --install)"; \
	fi

endif

# ---------- Linux: plain build, icon shipped alongside ----------
ifeq ($(PLATFORM),linux)

$(BIN): hake.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
	@echo "built $(BIN). For a desktop icon: copy $(ICON_DIR)/hake.png to ~/.local/share/icons/ and create a .desktop entry."

endif

# ---------- FreeBSD: plain build ----------
ifeq ($(PLATFORM),freebsd)

$(BIN): hake.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

endif

clean:
	rm -f hake hake.exe hako hake.rc hake.res .hake.r

# ---------- install / uninstall ----------
PREFIX ?=
ICONS  ?= 1

_uname_s := $(shell uname -s 2>/dev/null)
_resolve_prefix = $(if $(PREFIX),$(PREFIX),$(if $(shell test -w /usr/local/bin && echo y),/usr/local,$(HOME)/.local))

install: $(BIN)
	@dest="$(_resolve_prefix)"; \
	mkdir -p "$$dest/bin"; \
	install -m 0755 $(BIN) "$$dest/bin/$(BIN)"; \
	echo "installed: $$dest/bin/$(BIN)"; \
	if [ -f hako ]; then \
		install -m 0755 hako "$$dest/bin/hako-bundled"; \
		echo "installed: $$dest/bin/hako-bundled (BUNDLE_HAKO subprocess wire)"; \
	fi; \
	if [ "$(_uname_s)" = "Darwin" ] && command -v xattr >/dev/null 2>&1; then \
		xattr -d com.apple.quarantine "$$dest/bin/$(BIN)" 2>/dev/null || true; \
	fi; \
	if [ "$(_uname_s)" = "Linux" ] && [ "$(ICONS)" = "1" ] && [ -f $(ICON_DIR)/hake.png ]; then \
		mkdir -p "$$HOME/.local/share/applications" "$$HOME/.local/share/icons/hicolor/256x256/apps"; \
		install -m 0644 $(ICON_DIR)/hake.png "$$HOME/.local/share/icons/hicolor/256x256/apps/hake.png"; \
		printf "[Desktop Entry]\nType=Application\nName=hake\nComment=Mithraeum modal terminal editor\nExec=$$dest/bin/$(BIN)\nIcon=hake\nTerminal=true\nCategories=Development;TextEditor;\n" > "$$HOME/.local/share/applications/hake.desktop"; \
		echo "installed: icon + .desktop entry"; \
	fi; \
	case ":$$PATH:" in *":$$dest/bin:"*) ;; *) echo "note: $$dest/bin not in PATH";; esac

uninstall:
	@for prefix in $(PREFIX) /usr/local $$HOME/.local /opt/local /opt; do \
		[ -z "$$prefix" ] && continue; \
		for b in $(BIN) hako-bundled; do \
			path="$$prefix/bin/$$b"; \
			if [ -e "$$path" ] || [ -L "$$path" ]; then rm -f "$$path" && echo "removed: $$path"; fi; \
		done; \
	done
	@rm -f "$$HOME/.local/share/applications/hake.desktop" 2>/dev/null || true
	@for d in $$HOME/.local/share/icons/hicolor/*/apps; do \
		[ -d "$$d" ] && rm -f "$$d/hake.png" 2>/dev/null; \
	done
