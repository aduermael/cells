# Cells logo construction

Concept: **cell** + **prompt chevron (`>`)** + **BR selection handle**.

## Rules (locked)

- **Black only** (`#0A0A0A`) — recolor in context as needed  
- **Prompt chevron**, not a triangle (parallel-arm `>` cutout)  
- **Handle centered on the BR border corner** (spreadsheet engines: handle center = selection corner)  
- **Square viewBox** — full mark fits `0 0 96 96`  

## Grid

| Token | Value |
|-------|-------|
| viewBox | `0 0 96 96` |
| cell outer | `(12, 12)–(76, 76)` size **64** |
| BR corner **C** | `(76, 76)` |
| handle size | **12 × 12** |
| handle origin | `(70, 70)` — center = **C** |
| ink | `#0A0A0A` |

Content bbox including handle: `(12, 12)–(82, 82)`.

### Handle geometry (all options)

```
handle_center = cell_outer_BR = (76, 76)
handle_origin = center − size/2 = (70, 70)
```

Half the handle sits outside the cell, half overlaps the corner — same as Excel / Sheets / Numbers.

### Gap ring (options A, E, D-rotated)

Evenodd well centered on **C**, larger than the handle:

| | well | gap |
|--|------|-----|
| A | `(68,68)` 16×16 | 2u around handle |
| E | `(69,69)` 14×14 | 1u around handle |

### Outline (options B, D-outline)

Stroke **8** on centerline rect `(16,16)` 56×56 → outer edge `(12,12)–(76,76)`.  
Handle still centered on outer BR corner **C**.

### Chevron (prompt `>`)

Centered in cell (cell center = 44):

```
M32 28 L56 44 L32 60 V52 L44 44 L32 36 Z
```

## Files

| File | Idea |
|------|------|
| `option-a-solid.svg` | Solid + gap ring (2u) + centered handle |
| `option-e-tight-gap.svg` | Solid + tighter gap ring (1u) |
| `option-c-external-gap.svg` | Solid, centered handle, no gap ring (classic knob) |
| `option-d-outline.svg` | Outline cell + solid chevron + centered handle |
| `option-b-outline.svg` | Same as `option-d-outline` |
| `option-d-rotated.svg` | A geometry, −12° rigid |
