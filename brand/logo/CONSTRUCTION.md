# Cells logo construction

## Rules (locked)

- **Black only** (`#0A0A0A`)
- **Prompt chevron `>`** (not a triangle)
- **Main mark is a square** — outer bounds `(8,8)–(88,88)` size 80
- **Handle stays inside that square** (does not stick out)
- **Gap around the handle** (white channel in the BR corner)
- Handle sits in the **BR border zone** (like centered on the selection border, inset so it fits with gap)

## Grid

| Token | Value |
|-------|-------|
| viewBox | `0 0 96 96` |
| outer square | `(8, 8)–(88, 88)` size **80** |
| well (BR pocket) | `(68, 68)–(88, 88)` size **20** (opt A / outline) |
| handle | `(72, 72)` size **12** → ends `(84, 84)` |
| gap | **4** on all sides of handle within the well |
| handle center | `(78, 78)` — BR border zone (between body corner 68 and outer 88) |
| ink | `#0A0A0A` |

### Tight gap (option E)

| Token | Value |
|-------|-------|
| well | `(70, 70)–(88, 88)` size 18 |
| handle | `(73, 73)` size 12 |
| gap | **3** |

### Chevron (prompt `>`)

```
M28 24 L52 40 L28 56 V48 L40 40 L28 32 Z
```

(Outline uses the same chevron, slightly recentered as `M32 28…` in the hollow.)

### Solid (A, E)

```
evenodd: outer square − chevron − well
+ handle rect
```

### Outline (D / B)

Four stroke bars (thickness 8) that stop at the well — same pocket and handle as A:

- top `(8,8)` 80×8  
- left `(8,8)` 8×80  
- right `(80,8)` 8×60 (stops at y=68)  
- bottom `(8,80)` 60×8 (stops at x=68)  
- handle `(72,72)` 12×12  

## Files

| File | Idea |
|------|------|
| `option-a-solid.svg` | Solid square, gap 4, handle inside BR |
| `option-e-tight-gap.svg` | Solid, gap 3 |
| `option-d-outline.svg` | Outline square, same handle/gap |
| `option-b-outline.svg` | Same as D |
| `option-d-rotated.svg` | A geometry, −12° |
