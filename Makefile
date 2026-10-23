# Local Flatpak builds, without waiting for CI (assets/flatpak/README.md).
# The source is the working tree as it is: uncommitted edits and new files
# that git doesn't ignore are in, unlike CI's `git archive HEAD`.
#
#   make flatpak          build and install it for this user
#   make run              start it (the launcher)
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

.PHONY: help source flatpak run bundle deck clean distclean
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
	flatpak run $(APP)

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
