# Ember brand artwork

Ember's logo and menu background are original artwork drawn for the project (2026-10-13). The logo is a simple fire character, 火, inside an open brush circle. The background is an ink landscape with a brush arc on the right, which leaves the left side quiet behind the menu.

| File | What it is |
|---|---|
| `source/ember-mark.svg` | Editable logo, 512 x 512, transparent |
| `source/ember-menu-background.svg` | Editable background, 2048 x 1152 |
| `ember-menu-background.png` | Background export, 2048 x 1152 RGB, loaded at runtime and cropped to cover |
| `../../src/ui/ember.png` | Logo export, 1254 x 1254 RGBA |
| `../../src/ui/ember.ico` | Windows icon, 16 to 256 px |
| `../../src/ui/ember.rgba` | 128 x 128 straight RGBA, embedded in the font atlas at build time |

SHA-256:
- `ember-menu-background.png`: `B02FF71A65E7EFCF71CAB28ED87002AEFB0609DD458279D6FF67AC145AAFB032`
- `ember.png`: `37384030C4F17595E8F588CE3216C85F1A38E2915B8010046A7C5C857D2F6A58`

## Rebuilding

- **From the PNG.** `scripts/build-ember-brand.py` (Pillow) rebuilds `ember.ico` and `ember.rgba` from `ember.png`, with the same sizes and Lanczos resize the original exports used. Use it after any change to the PNG.
- **From the SVGs.** Render each SVG with Inkscape at the sizes above, then run the script.
- **Website.** `scripts/build-web-assets.py` regenerates the embernetplay.link favicon, header emblem and background from these files.

Normal product builds use the checked-in exports and need neither Python nor Inkscape.
