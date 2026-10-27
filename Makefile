# Local Flatpak builds, without waiting for CI (assets/flatpak/README.md).
# The source is the working tree as it is: uncommitted edits and new files
# that git doesn't ignore are in, unlike CI's `git archive HEAD`.
#
#   make flatpak          build and install it for this user
#   make run              start it (the launcher)
#   make wine             a game.exe folder with whisper and patch_d2.mpq in build/ (macOS: Gcenx's Wine too; Linux: the system's)
#   make run og           the original game.exe under that Wine, hidden (OG_FLAGS ?= -w -ns: windowed, no sound; OG_HIDE=0 to show it)
#   make run og host NAME   ... and on into a hosted TCP/IP game as d2d's save NAME (DIFF=normal|nightmare|hell)
#   make bundle           build d2decomped-x86_64.flatpak, to copy to a Deck
#   make deck             the bundle, copied to the Deck and installed (DECK=deck@steamdeck)
#   make clean            drop the build and the bundle (keeps the module cache)
#   make distclean        clean, and the module cache too (the next build is a full one)

APP      := io.github.psi29a.d2decomped
MANIFEST := .github/flatpak/ci-manifest.yml
SOURCE   := build/flatpak-source
BUILDDIR := build-flatpak
REPO     := build/flatpak-repo
BUNDLE   := d2decomped-x86_64.flatpak
DECK     ?= deck@steamdeck
BUILDER  := flatpak-builder --force-clean --ccache

# macOS: Gcenx's Wine, fetched (Homebrew's wine-stable cask is disabled: not notarized).
# Linux: the system's Wine (installing it needs sudo, so that's left to you).
ifeq ($(shell uname -s),Darwin)
WINE_URL := https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.0_1/wine-stable-11.0_1-osx64.tar.xz
WINE     := build/wine/Contents/Resources/wine/bin/wine
WINESERVER := $(dir $(WINE))wineserver
D2D_SAVES ?= $(HOME)/Library/Preferences/d2d/save
else
WINE     := $(shell command -v wine)
WINESERVER := $(or $(shell command -v wineserver),$(wildcard /usr/lib/wine/wineserver64 /usr/lib/wine/wineserver),wineserver)
D2D_SAVES ?= $(HOME)/.config/d2d/save
endif
ifeq ($(WINE),)
WINE     := wine
WINE_DEP := wine-missing
else ifeq ($(shell uname -s),Darwin)
WINE_DEP := $(WINE)
endif
PREFIX   := $(CURDIR)/build/wine-prefix
OG       := build/og
D2_MPQ_DIR ?= $(HOME)/Workspace/private/diablo2
D2_PATCH_INSTALLER ?= $(HOME)/Downloads/Diablo II + LoD/patch/LODPatch_114d.exe
OG_SAVES := $(PREFIX)/drive_c/users/$(shell id -un)/Saved Games/Diablo II
OG_FLAGS ?= -w -ns
OG_WINEDEBUG ?= -all
OG_HIDE  ?= 1
DIFF     ?= normal
WHISPER   = python3 apps/whisper/whisper.py 127.0.0.1 $$(head -1 $(OG)/whisper.txt)
# `make run og host NAME`: the words after `run` say what to run; they're goals too, so they do nothing.
RUN_WHAT := $(filter-out run,$(MAKECMDGOALS))
ifneq ($(filter run,$(MAKECMDGOALS)),)
$(RUN_WHAT):
	@:
endif

.PHONY: help source flatpak run bundle deck clean distclean wine wine-missing
.DEFAULT_GOAL := help

help:
	@sed -n 's/^#   //p' $(firstword $(MAKEFILE_LIST))

# The manifest builds from $(SOURCE) (its `dir` source).
source:
	rm -rf $(SOURCE) && mkdir -p $(SOURCE)
	git ls-files -z --cached --others --exclude-standard | tar --null -T - -cf - | tar -xf - -C $(SOURCE)

flatpak: source
	$(BUILDER) --user --install $(BUILDDIR) $(MANIFEST)

run: $(if $(filter og,$(firstword $(RUN_WHAT))),$(filter wine-missing,$(WINE_DEP)))
ifeq ($(firstword $(RUN_WHAT)),og)
	@test -f $(OG)/patch_d2.mpq || { echo "no $(OG)/patch_d2.mpq: make wine first"; exit 1; }
	@# One game.exe at a time (a second can't start its LNG manager).
	@WINEPREFIX=$(PREFIX) $(WINESERVER) -k 2>/dev/null; WINEPREFIX=$(PREFIX) $(WINESERVER) -w 2>/dev/null; true
ifeq ($(word 2,$(RUN_WHAT)),host)
	@# Only NAME in game.exe's save folder, fresh from d2d's: char select's first slot is it.
	@test -f "$(D2D_SAVES)/$(word 3,$(RUN_WHAT)).d2s" || { echo "no $(D2D_SAVES)/$(word 3,$(RUN_WHAT)).d2s"; exit 1; }
	@mkdir -p "$(OG_SAVES)" && rm -f "$(OG_SAVES)"/* && cp "$(D2D_SAVES)/$(word 3,$(RUN_WHAT))".* "$(OG_SAVES)/"
endif
	@# macOS: hidden, never in front (OG_HIDE=0: shown, for playing it or whisper's realclick). A watchdog (as long as Wine's "wine" app lives) hides it the moment
	@# it shows (a dialog unhides it) and hands the front back to the app you were in. It keeps drawing,
	@# so whisper screenshots still work. Wine's output goes to $(OG)/wine.log (OG_WINEDEBUG=+file etc.).
	@# ponytail: Linux leaves the window where Wine puts it; xdotool windowminimize if that bothers you.
	@front=$$(osascript -e 'tell application "System Events" to get bundle identifier of first application process whose frontmost is true' 2>/dev/null); \
	cd $(OG) && WINEPREFIX=$(PREFIX) WINEDEBUG=$(OG_WINEDEBUG) $(abspath $(WINE)) game.exe $(OG_FLAGS) > wine.log 2>&1 & \
	[ "$(OG_HIDE)" = 1 ] && command -v osascript >/dev/null && osascript \
	  -e 'on run argv' -e 'tell application "System Events"' \
	  -e 'repeat 200 times' -e 'if exists application process "wine" then exit repeat' -e 'delay 0.05' -e 'end repeat' \
	  -e 'repeat while exists application process "wine"' \
	  -e 'if visible of application process "wine" then' \
	  -e 'set visible of application process "wine" to false' \
	  -e 'if (item 1 of argv) is not "" then tell application id (item 1 of argv) to activate' \
	  -e 'end if' -e 'delay 0.1' -e 'end repeat' -e 'end tell' -e 'end run' "$$front" >/dev/null 2>&1 & \
	up=; for i in $$(seq 30); do python3 -c 'import socket; socket.create_connection(("127.0.0.1", 4100), 1)' 2>/dev/null && { up=1; break; }; sleep 1; done; \
	test -n "$$up" || { echo "whisper never answered on 127.0.0.1:4100"; WINEPREFIX=$(PREFIX) $(WINESERVER) -k; exit 1; }; \
	echo "game.exe up (whisper 127.0.0.1:4100; macOS: hidden, OG_HIDE=0 to see and play it)"; \
	$(if $(filter host,$(word 2,$(RUN_WHAT))),sleep 3; $(WHISPER) host $(DIFF);) \
	wait
else
	flatpak run $(APP)
endif

wine-missing:
	@echo "no wine on PATH: install it (it needs sudo), e.g."
	@echo "  Debian/Ubuntu/SteamOS: sudo dpkg --add-architecture i386 && sudo apt install wine wine32"
	@echo "  Fedora: sudo dnf install wine    Arch: sudo pacman -S wine"
	@exit 1

build/wine/Contents/Resources/wine/bin/wine:
	mkdir -p build/wine
	curl -fL $(WINE_URL) | tar -xJf - -C build/wine --strip-components=1
	xattr -dr com.apple.quarantine build/wine 2>/dev/null || true

# The MPQs linked, not copied; bin/'s PEs copied without LODPatch's 24-byte prefix (MZ at 0); game.exe's ijl11.dll kept as ijl11_real.dll behind whisper's; whisper.txt (token, port) kept once made.
wine: $(WINE_DEP)
	@test -f $(D2_MPQ_DIR)/bin/game.exe || { echo "no $(D2_MPQ_DIR)/bin/game.exe (set D2_MPQ_DIR)"; exit 1; }
	@# D2.LNG: the install's language DLL (1.5 KB, accelerators + messages); without it entering a game
	@# stops at "Unable to start LNG manager". The install disc's SETUP.MPQ has it, unnamed (File00000025).
	@test -f $(D2_MPQ_DIR)/bin/D2.LNG || { echo "no $(D2_MPQ_DIR)/bin/D2.LNG: copy it from a Diablo II install"; exit 1; }
	mkdir -p $(OG)
	ln -sf $(D2_MPQ_DIR)/*.mpq $(OG)/
	for f in $(D2_MPQ_DIR)/bin/*; do \
	  if [ "$$(head -c 2 $$f)" = MZ ]; then cp $$f $(OG)/; else tail -c +25 $$f > $(OG)/$$(basename $$f); fi; \
	done
	mv $(OG)/ijl11.dll $(OG)/ijl11_real.dll
	cp apps/whisper/ijl11.dll $(OG)/
	cmake --build build --target mpq-cat
	D2_PATCH_INSTALLER="$(D2_PATCH_INSTALLER)" build/tools/mpq-cat/mpq-cat -p $(OG)/patch_d2.mpq $(D2_MPQ_DIR)
	test -f $(OG)/whisper.txt || printf '%s\n4100\n' "$$(openssl rand -hex 12)" > $(OG)/whisper.txt
	WINEPREFIX=$(PREFIX) WINEDEBUG=-all $(WINE) wineboot -i
	@echo "ready: make run og; token: $$(head -1 $(OG)/whisper.txt)"

bundle: source
	$(BUILDER) --repo=$(REPO) $(BUILDDIR) $(MANIFEST)
	flatpak build-bundle $(REPO) $(BUNDLE) $(APP)

# Needs sshd on the Deck (Desktop Mode: `sudo systemctl start sshd`, a password set with `passwd`).
deck: bundle
	scp $(BUNDLE) $(DECK):/tmp/$(BUNDLE)
	ssh $(DECK) 'flatpak install --user --reinstall -y --bundle /tmp/$(BUNDLE) && rm /tmp/$(BUNDLE)'

clean:
	rm -rf $(SOURCE) $(BUILDDIR) $(REPO) $(BUNDLE)

distclean: clean
	rm -rf .flatpak-builder
