# Brand assets

## Canonical logo

| File | Role |
|------|------|
| [`logo.svg`](logo.svg) | Geometry source (monochrome `#0A0A0A`) |
| [`exports/`](exports/) | PNG renders for decks / docs |
| [`logo/`](logo/) | Exploration options (archive) |

Production app icon and favicons live under `apps/shared/` (sourced from this geometry, brand green `#0a9208`).

## Production paths

| Asset | Path |
|-------|------|
| App icon SVG | `apps/shared/icon.svg` |
| Favicons | `apps/shared/favicons/*` |
| Header mark | Inline in `apps/wasm/static/index.html` (`#header-logo`) |
| UI primary | `--color-primary` / `PRIMARY_COLOR` = `#0a9208` |

## Geometry

Outline cell + prompt chevron (`>`) + BR selection handle inside the square with a gap from the frame. See `logo.svg`.

## Regenerating favicons / PNGs

```bash
# From repo root (requires rsvg-convert + ImageMagick)
SRC=apps/shared/icon.svg
rsvg-convert -w 96  -h 96  "$SRC" -o apps/shared/favicons/favicon-96x96.png
rsvg-convert -w 180 -h 180 -b white "$SRC" -o apps/shared/favicons/apple-touch-icon.png
rsvg-convert -w 192 -h 192 "$SRC" -o apps/shared/favicons/web-app-manifest-192x192.png
rsvg-convert -w 512 -h 512 "$SRC" -o apps/shared/favicons/web-app-manifest-512x512.png
# favicon.ico: multi-size via convert on 16/32/48 PNG renders
```
