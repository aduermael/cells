# Rendering

## Overview

Rendering is handled by **TypeScript Canvas2D** in the browser. The C++ core provides viewport queries (via Order Statistic Tree) but does not generate draw commands.

| Component | Status |
|-----------|--------|
| Canvas2D grid renderer | Implemented |
| Grid lines, cells, headers | Implemented |
| Selection rendering | Implemented |
| Column/row resize preview | Implemented |
| Drag-and-drop ghost | Implemented |
| Formula bar highlights | Implemented |
| Presence cursors (collab) | Implemented |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    TypeScript UI (Main Thread)                   │
│   Event handling, state management, requestAnimationFrame        │
└────────────────────────────────────┬────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    GridRenderer (TypeScript)                     │
│   Canvas2D rendering, virtual scrolling, selection rendering     │
└────────────────────────────────────┬────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    WASM Core (Web Worker)                        │
│   Viewport queries, cell data, formatted values                  │
└─────────────────────────────────────────────────────────────────┘
```

## Design Goals

1. **60 FPS**: Smooth scrolling with virtual viewport
2. **Memory efficient**: Only render visible cells
3. **Simple**: Canvas2D is well-understood, easy to debug

## Core Concepts

### Viewport

The visible area of the spreadsheet:
- Scroll position (x, y in pixels)
- Visible size (width, height)
- Visible cell range (first/last visible col/row)
- Overscan (extra cells for smooth scrolling)
- Zoom level

### Layout Calculation

1. Calculate column positions (cumulative widths)
2. Calculate row positions (cumulative heights)
3. Find visible cells based on scroll position
4. Generate cell layout info (position, size, selection state)

### Draw Commands

Abstract platform-independent drawing operations:

| Command | Purpose |
|---------|---------|
| `DRAW_RECT` | Cell backgrounds, selection highlight |
| `DRAW_TEXT` | Cell content |
| `DRAW_LINE` | Grid lines |
| `DRAW_CLIP_PUSH/POP` | Clipping for cell overflow |

The core generates a draw list; platform backends interpret it.

## Rendering Pipeline

1. **Layout**: Calculate which cells are visible
2. **Background**: Draw grid background
3. **Grid lines**: Vertical and horizontal lines
4. **Cell contents**: Text, styled backgrounds
5. **Selection**: Highlight selected range
6. **Headers**: Column (A, B, C) and row (1, 2, 3) headers

## Virtual Scrolling

For large spreadsheets with millions of rows:
- Order Statistic Tree provides O(log n) pixel-to-axis mapping
- Only visible cells are queried from WASM core
- Cell positions calculated on-demand during render

## Selection Rendering

| Selection Type | Visual |
|----------------|--------|
| Single cell | Bold border |
| Range | Light fill, bold border |
| Multi-range | Multiple highlights |
| Row/Column | Full row/column highlight |

Plus: active cell indicator, resize handle (fill handle).

## Performance Targets

| Scenario | Target |
|----------|--------|
| Initial render (100×100 visible) | < 16ms |
| Scroll (continuous) | < 8ms/frame |
| Cell edit | < 5ms |
| Large recalc (10k cells) | < 100ms |
| Max visible cells | 10,000+ |

---

## Current Implementation

The actual implementation is in TypeScript at `apps/wasm/src/grid-renderer.ts` using Canvas2D.

### Architecture (Simplified)

```
┌──────────────────────────────────────────────────────────────┐
│                   index.html (Main UI)                        │
│   - Event handling (mouse, keyboard)                         │
│   - State management (selection, scroll position)            │
│   - Coordinates GridRenderer updates                         │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│              GridRenderer Class (TypeScript)                  │
│   - Receives state via setStateRefs()                        │
│   - render() draws everything to Canvas2D                    │
│   - Bundled with esbuild into client.js                      │
└──────────────────────────────────────────────────────────────┘
```

### Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `HEADER_HEIGHT` | 24px | Column header row height |
| `HEADER_WIDTH` | 50px | Row header column width |
| `DEFAULT_COL_WIDTH` | 100px | Default column width |
| `DEFAULT_ROW_HEIGHT` | 24px | Default row height |
| `CELL_PADDING` | 4px | Text padding inside cells |

### Rendering Pipeline (Actual)

The `render()` method draws in this order:

1. **Clear canvas** - Full clear each frame (no dirty tracking)
2. **Grid lines** - Vertical and horizontal lines in cell area
3. **Cell values** - Text content with clipping per cell
4. **Column selection** - Highlight if column selected
5. **Row selection** - Highlight if row selected
6. **Cell/Range selection** - Border and fill for selected cells
7. **Column headers** - A, B, C... with selection highlight
8. **Row headers** - 1, 2, 3... with selection highlight
9. **Corner** - Top-left fixed area
10. **Header borders** - Lines separating headers from cells

### High-DPI Support

Canvas automatically scales for device pixel ratio:
```typescript
const dpr = window.devicePixelRatio || 1;
canvas.width = container.clientWidth * dpr;
canvas.height = container.clientHeight * dpr;
ctx.scale(dpr, dpr);
```

### Drag and Drop Rendering

Special handling for column/row reordering:
- `getDragAdjustedColX()` / `getDragAdjustedRowY()` - Calculate positions with gap for dragged item
- `drawDragGhost()` - Semi-transparent ghost following cursor
- Source column/row is hidden during drag, gap appears at target position

### Resize Preview

- `drawResizePreview()` - Dashed line showing new column/row boundary
- Called after main `render()` when resizing is active

### Color Palette

```typescript
const COLORS = {
    gridLine: '#f0f0f0',      // Subtle grid
    headerBg: '#f8f9fa',      // Header background
    headerBorder: '#dee2e6',  // Header border
    headerText: '#495057',    // Header text
    cellText: '#212529',      // Cell text
    selectionBorder: '#0d6efd', // Selection border (blue)
    selectionBg: 'rgba(13, 110, 253, 0.1)', // Selection fill
    cornerBg: '#e9ecef'       // Corner background
} as const;
```

### Future Improvements

- **Dirty region tracking** - Currently full redraw every frame
- **Zoom** - Not yet supported
- **Smooth scrolling easing** - Direct scroll position updates

---

## Viewport Indexing Architecture

The viewport query system uses Order-Statistic Trees (augmented red-black trees) for O(log n) spatial lookups, replacing the previous quadtree implementation.

### Components

```
┌─────────────────────────────────────────────────────────────────┐
│                    ViewportIndex                                 │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  │
│  │ Column AxisIndex│  │ Row AxisIndex   │  │ Cell HashMap    │  │
│  │ (OS Tree)       │  │ (OS Tree)       │  │ (by cellId)     │  │
│  └────────┬────────┘  └────────┬────────┘  └─────────────────┘  │
│           │                    │                                 │
│           ▼                    ▼                                 │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │              Order-Statistic Tree (OSTree)                  ││
│  │  - Red-black tree with subtree_total augmentation           ││
│  │  - O(log n) insert, delete, lookup by pixel offset          ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

### Key Operations

| Operation | Complexity | Description |
|-----------|------------|-------------|
| `queryViewport(x1, y1, x2, y2)` | O(log n + k) | Find all cells in pixel rectangle (k = result size) |
| `pixelToAxis(offset)` | O(log n) | Find column/row containing pixel offset |
| `axisToPixel(axisId)` | O(log n) | Get pixel offset of column/row start |
| `resize(axisId, newSize)` | O(log n) | Update column/row size |
| `insert(axisId, position, size)` | O(log n) | Insert new column/row |
| `remove(axisId)` | O(log n) | Remove column/row |

### Order-Statistic Tree

Each node stores:
- `id`: Axis UUID (column or row)
- `size`: Pixel width/height of this axis
- `subtree_total`: Sum of sizes in this subtree (used for O(log n) offset lookups)
- `position`: Logical position (for ordering)
- Red-black tree pointers (left, right, parent) and color

The `subtree_total` augmentation enables O(log n) pixel-to-axis lookups by walking down the tree and tracking cumulative offsets.

### Integration with TypeScript

The WASM module exposes viewport queries via pixel coordinates:

```typescript
// Query cells visible in pixel rectangle
const cells = queryViewport(scrollX, scrollY, scrollX + width, scrollY + height);

// Get pixel offset for column/row (for rendering)
const colX = getColumnPixelOffset(colPosition);
const rowY = getRowPixelOffset(rowPosition);

// Get total dimensions
const totalWidth = getTotalWidth();
const totalHeight = getTotalHeight();
```

### Performance

- Build: O(n log n) where n = max(columns, rows)
- Query: O(log n + k) where k = cells in viewport
- Single update: O(log n)

Memory per axis: ~48 bytes (UUID + size + subtree_total + pointers + balance)
- 1M rows: ~48 MB
- 16K columns: ~768 KB

### Files

| File | Description |
|------|-------------|
| `core/cells/ostree.h/cc` | Generic Order-Statistic Tree implementation |
| `core/cells/axis_index.h/cc` | AxisIndex wrapping OSTree for column/row indexing |
| `core/cells/viewport_index.h/cc` | ViewportIndex combining two AxisIndexes + cell HashMap |
