# Cells logo construction

Concept from sketch `IMG_3036.heic`: a **cell** (spreadsheet selection), a **chevron** (`>` CLI / scriptable), and a **bottom-right square handle** (selection drag handle).

## Grid

| Token | Value |
|-------|-------|
| viewBox | `0 0 96 96` |
| unit | 4 |
| cell origin | `(10, 10)` |
| cell size | `64 × 64` (ends `74, 74`) |
| handle size | `12 × 12` |
| handle origin (attached) | `(68, 68)` — centered on cell BR corner |
| handle origin (gap, opt E) | `(78, 78)` — 4-unit gap |
| brand green | `#058601` (matches `apps/shared/icon.svg`) |
| mono | `#0A0A0A` |

Content bounding box for attached handle: `(10,10)–(80,80)`. Extra SE padding balances handle mass.

## Chevron (CLI) — options A–E

Right-pointing thick chevron (arm thickness 8):

| Point | Role |
|-------|------|
| `(54, 42)` | tip |
| `(30, 26)` | top outer |
| `(30, 58)` | bottom outer |
| `(30, 50)` | bottom inner |
| `(40, 42)` | throat |
| `(30, 34)` | top inner |

Path: `M30 26 L54 42 L30 58 V50 L40 42 L30 34 Z`

Used as an **evenodd cutout** in the cell fill (A/B/C/E) or a **solid mark** on an outline cell (D).

## Bold chevron — option F

Thicker arms (12): `M28 24 L56 42 L28 60 V48 L38 42 L28 36 Z`

## Options

| File | Idea |
|------|------|
| `option-a-solid-cutout.svg` | Axis-aligned solid cell, chevron cutout, brand green |
| `option-b-mono-cutout.svg` | Same geometry, monochrome black |
| `option-c-rotated.svg` | Same mark, rigid −12° + scale 0.9 (sketch energy) |
| `option-d-outline.svg` | Outline cell + solid chevron + solid handle |
| `option-e-gap-handle.svg` | Solid cutout with detached handle (4u gap) |
| `option-f-bold-chevron.svg` | Solid cell + thicker chevron cutout |

## Non-goals (this folder)

Does not replace production `apps/shared/icon.svg` or favicons — options for review first.
