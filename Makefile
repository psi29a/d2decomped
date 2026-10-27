# Local Flatpak builds, without waiting for CI (assets/flatpak/README.md).
# The source is the working tree as it is: uncommitted edits and new files
# that git doesn't ignore are in, unlike CI's `git archive HEAD`.
#
#   make flatpak          build and install it for this user
#   make run              start it (the launcher)
#   make wine             Gcenx's Wine + a game.exe folder with whisper in build/ (D2_MPQ_DIR: MPQs + bin/game.exe)
#   make run og           the original game.exe under that Wine, behind your windows (OG_FLAGS ?= -w -ns: windowed, no sound)
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

# Gcenx's macOS Wine (Homebrew's wine-stable cask is disabled: not notarized).
WINE_URL := https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.0_1/wine-stable-11.0_1-osx64.tar.xz
WINE     := build/wine/Contents/Resources/wine/bin/wine
PREFIX   := $(CURDIR)/build/wine-prefix
OG       := build/og
D2_MPQ_DIR ?= $(HOME)/Workspace/private/diablo2
OG_FLAGS ?= -w -ns
# `make run og`: what to run is the goal after `run`.
RUN_WHAT := $(filter-out run,$(MAKECMDGOALS))

.PHONY: help source flatpak run bundle deck clean distclean wine og
.DEFAULT_GOAL := help

help:
	@sed -n 's/^#   //p' $(firstword $(MAKEFILE_LIST))

# The manifest builds from $(SOURCE) (its `dir` source).
source:
	rm -rf $(SOURCE) && mkdir -p $(SOURCE)
	git ls-files -z --cached --others --exclude-standard | tar --null -T - -cf - | tar -xf - -C $(SOURCE)

flatpak: source
	$(BUILDER) --user --install $(BUILDDIR) $(MANIFEST)

run:
ifeq ($(RUN_WHAT),og)
	@# Wine's window takes the front: once whisper listens, the app you were in gets it back.
	@front=$$(osascript -e 'tell application "System Events" to get bundle identifier of first application process whose frontmost is true'); \
	cd $(OG) && WINEPREFIX=$(PREFIX) WINEDEBUG=-all $(CURDIR)/$(WINE) game.exe $(OG_FLAGS) > /dev/null 2>&1 & \
	for i in $$(seq 30); do nc -z -G 1 127.0.0.1 4100 2>/dev/null && break; sleep 1; done; \
	sleep 1; osascript -e "tell application id \"$$front\" to activate"; \
	echo "game.exe up (whisper 127.0.0.1:4100); closing its window ends it"; wait
else
	flatpak run $(APP)
endif

og:
	@:

$(WINE):
	mkdir -p build/wine
	curl -fL $(WINE_URL) | tar -xJf - -C build/wine --strip-components=1
	xattr -dr com.apple.quarantine build/wine 2>/dev/null || true

# The MPQs linked, not copied; bin/'s PEs copied without LODPatch's 24-byte prefix (MZ at 0); game.exe's ijl11.dll kept as ijl11_real.dll behind whisper's; whisper.txt (token, port) kept once made.
wine: $(WINE)
	@test -f $(D2_MPQ_DIR)/bin/game.exe || { echo "no $(D2_MPQ_DIR)/bin/game.exe (set D2_MPQ_DIR)"; exit 1; }
	mkdir -p $(OG)
	ln -sf $(D2_MPQ_DIR)/*.mpq $(OG)/
	for f in $(D2_MPQ_DIR)/bin/*; do \
	  if [ "$$(head -c 2 $$f)" = MZ ]; then cp $$f $(OG)/; else tail -c +25 $$f > $(OG)/$$(basename $$f); fi; \
	done
	mv $(OG)/ijl11.dll $(OG)/ijl11_real.dll
	cp apps/whisper/ijl11.dll $(OG)/
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
