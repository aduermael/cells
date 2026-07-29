# Cells logo construction

Concept: **cell** + **prompt chevron (`>`)** + **BR selection handle with gap**.

## Rules (locked)

- **Black only** (`#0A0A0A`) — recolor in context as needed  
- **Prompt chevron**, not a triangle (parallel-arm `>` cutout)  
- **Gap** between cell body and handle  
- **Square bounds** — full mark fits a square viewBox; solid options use a square outer silhouette  

## Grid

| Token | Value |
|-------|-------|
| viewBox | `0 0 96 96` |
| outer square | `(8,8)–(88,88)` size **80** |
| brand / ink | `#0A0A0A` |

### Solid square options (A, E, D)

| Token | A (wide gap) | E (tight gap) |
|-------|--------------|---------------|
| well cutout | `(70,70)` 18×18 | `(72,72)` 16×16 |
| handle | `(76,76)` 12×12 flush BR | same |
| gap (cell → handle) | **6** | **4** |

Outer path uses `fill-rule="evenodd"`: outer square − chevron − well, then a filled handle rect flush to the outer BR corner.

### External-gap option (C)

Cell `(8,8)` 64×64, gap 4, handle `(76,76)` 12×12 — content still spans the same 80×80 square, handle sits outside the cell fill.

### Outline option (B)

Stroke-8 frame on cell outer 8–72; solid chevron; external handle with gap 4.

## Chevron (prompt `>`)

```
M28 24 L52 40 L28 56 V48 L40 40 L28 32 Z
```

| Point | Role |
|-------|------|
| `(52, 40)` | tip |
| `(28, 24)` / `(28, 56)` | outer arms |
| `(28, 32)` / `(28, 48)` | inner arms (thickness 8) |
| `(40, 40)` | throat |

## Files

| File | Idea |
|------|------|
| `option-a-solid.svg` | Square solid, 6u handle gap |
| `option-e-tight-gap.svg` | Square solid, 4u handle gap |
| `option-c-external-gap.svg` | Cell + detached handle (sketch-like) |
| `option-b-outline.svg` | Outline cell + solid chevron |
| `option-d-rotated.svg` | A geometry, −12° rigid |
