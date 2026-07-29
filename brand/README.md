# Brand assets

## Source of truth

| File | Role |
|------|------|
| [`logo.svg`](logo.svg) | Canonical geometry (monochrome `#0A0A0A`) |
| [`exports/`](exports/) | PNG renders for decks / docs |

Production app icon and favicons live under `apps/shared/`, using the same
paths as `logo.svg` with brand green `#0a9208`.

`apps/shared/favicons/favicon.svg` is a symlink to `../icon.svg` (one SVG for
app icon + favicon).

## Production paths

| Asset | Path |
|-------|------|
| App icon SVG | `apps/shared/icon.svg` |
| Favicons | `apps/shared/favicons/*` |
| Header mark | Inline in `apps/wasm/static/index.html` (`#header-logo`, `currentColor`) |
| UI primary | `--color-primary` / `PRIMARY_COLOR` = `#0a9208` |

Header markup stays inline so it can use `currentColor` for theme; keep its path
data identical to `logo.svg`.

## Geometry

Outline cell + prompt chevron (`>`) + BR selection handle (gap from the frame).

## Regenerating favicons / PNGs

```bash
# From repo root (requires rsvg-convert + ImageMagick)
SRC=apps/shared/icon.svg
rsvg-convert -w 96  -h 96  "$SRC" -o apps/shared/favicons/favicon-96x96.png
rsvg-convert -w 180 -h 180 -b white "$SRC" -o apps/shared/favicons/apple-touch-icon.png
rsvg-convert -w 192 -h 192 "$SRC" -o apps/shared/favicons/web-app-manifest-192x192.png
rsvg-convert -w 512 -h 512 "$SRC" -o apps/shared/favicons/web-app-manifest-512x512.png
# favicon.ico: convert 16/32/48 PNG renders into one .ico
```
