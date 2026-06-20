# CoreS3 firmware — sprite asset pipeline + drift gate.
#
# sprites.cpp is GENERATED from assets/sprites/*.png by
# scripts/generate_sprites_png.py. Regenerate whenever the PNG art changes and
# commit the PNGs + the regenerated sprites.cpp together.

PY        ?= python3
GEN       := scripts/generate_sprites_png.py
PREVIEW   := scripts/preview_ui.py
SPRITES   := lib/walkie_ui_logic/walkie_ui_logic/sprites.cpp

.PHONY: sprites previews sprites-check

# Regenerate placeholder PNGs (only the missing ones) + sprites.cpp.
sprites:
	$(PY) $(GEN)

# Regenerate the docs/ui_previews/*.png design previews from the PNG art.
previews:
	$(PY) $(PREVIEW)

# Drift gate: fail if sprites.cpp is out of sync with assets/sprites/*.png.
# Uses --check so it does NOT silently create placeholders: a missing or
# misnamed real-art PNG fails the gate instead of passing vacuously.
# Wire this into CI / pre-commit to prevent PNG <-> array drift.
sprites-check:
	$(PY) $(GEN) --check
	git diff --exit-code $(SPRITES) \
	  || (echo "ERROR: $(SPRITES) is stale — run 'make sprites' and commit it." && exit 1)
