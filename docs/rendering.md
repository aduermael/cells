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
| Formula reference highlights | Implemented |
| Presence cursors (collab) | Implemented |
| Zoom (10%-400%) | Implemented |
| Fill handle and fill preview | Implemented |
| Spill range highlight | Implemented |
| Text wrapping | Implemented |
| Cell borders | Implemented |

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

### Canvas2D Operations

The TypeScript renderer draws directly to Canvas2D (no intermediate draw commands):

| Method | Purpose |
|--------|---------|
| `fillRect()` | Cell backgrounds, selection highlight |
| `fillText()` | Cell content, headers |
| `strokeRect()` | Cell borders, selection borders |
| `moveTo()/lineTo()` | Grid lines |
| `save()/clip()/restore()` | Clipping for cell content overflow |

The C++ core provides data (cell values, formatted text, styles) but does not generate draw commands.

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
2. **Cell background** - Fill cell area with theme background color
3. **Grid lines** - Vertical and horizontal lines in cell area
4. **Style range backgrounds** - Background colors for styled empty cell ranges
5. **Cell backgrounds** - Background colors for individual cells
6. **Cell borders** - Custom borders with edge deduplication
7. **Cell values** - Text content with clipping per cell
8. **Formula highlights** - Colored boxes for formula references during editing
9. **Spill range highlight** - Blue dashed border for dynamic array spill ranges
10. **Column/row selection** - Highlight if column or row selected
11. **Cell/Range selection** - Border and fill for selected cells
12. **Fill handle** - Small square at selection corner for drag-fill
13. **Fill preview** - Dashed border during fill handle drag
14. **Column headers** - A, B, C... with selection highlight
15. **Row headers** - 1, 2, 3... with selection highlight
16. **Corner** - Top-left fixed area
17. **Header borders** - Lines separating headers from cells

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

Theme-aware colors via CSS variables with fallback values:

```typescript
const COLORS = {
    gridLine: '#f0f0f0',      // Subtle grid lines
    headerBg: '#f8f9fa',      // Header background
    headerBorder: '#dee2e6',  // Header border
    headerSeparator: 'rgba(0, 0, 0, 0.06)', // Subtle separators
    headerText: '#495057',    // Header text
    cellText: '#212529',      // Cell text
    cellBg: '#ffffff',        // Cell background
    selectionBorder: '#058601', // Selection border (green)
    selectionBg: 'rgba(5, 134, 1, 0.1)', // Selection fill
    cornerBg: '#e9ecef'       // Corner background
} as const;
```

Colors are dynamically loaded from CSS variables at render time to support dark mode.

### Formula Reference Colors

When editing formulas, each reference gets a unique color for visual identification:

```typescript
const FORMULA_REF_COLORS = [
  { border: '#4285f4', bg: 'rgba(66, 133, 244, 0.15)' },  // Blue
  { border: '#ea4335', bg: 'rgba(234, 67, 53, 0.15)' },   // Red
  { border: '#fbbc04', bg: 'rgba(251, 188, 4, 0.15)' },   // Yellow
  { border: '#34a853', bg: 'rgba(52, 168, 83, 0.15)' },   // Green
  { border: '#ff6d00', bg: 'rgba(255, 109, 0, 0.15)' },   // Orange
  { border: '#ab47bc', bg: 'rgba(171, 71, 188, 0.15)' },  // Purple
  { border: '#00acc1', bg: 'rgba(0, 172, 193, 0.15)' },   // Cyan
  { border: '#8d6e63', bg: 'rgba(141, 110, 99, 0.15)' },  // Brown
];
```

### Zoom Support

Zoom is implemented via dimension scaling during rendering (not CSS transform):

```typescript
// Zoom range: 10% - 400%
renderer.setZoomScale(150);  // Set to 150%

// Zoom affects all dimensions:
// - Header width/height
// - Column widths and row heights
// - Cell padding
// - Font sizes
```

Zoom state is stored in SheetInfo and persisted with the sheet.

### Future Improvements

- **Dirty region tracking** - Currently full redraw every frame
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
- `id`: 8-char base62 UUID (column or row identifier)
- `size`: Pixel width/height of this axis
- `subtree_total`: Sum of sizes in this subtree (used for O(log n) offset lookups)
- Red-black tree pointers (left, right, parent) and color

Position is implicit from tree structure (in-order traversal order), not stored in nodes. The `subtree_total` augmentation enables O(log n) pixel-to-axis lookups by walking down the tree and tracking cumulative offsets.

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
| `apps/wasm/src/grid-renderer.ts` | Main GridRenderer class with render() method |
| `apps/wasm/src/grid-constants.ts` | Constants, colors, zoom helpers, shared types |
| `apps/wasm/src/grid-header-renderer.ts` | Column/row header rendering |
| `apps/wasm/src/grid-selection-renderer.ts` | Selection, fill handle, spill range rendering |
| `apps/wasm/src/grid-presence-renderer.ts` | Remote user cursors and selections |
| `apps/wasm/src/grid-formula-renderer.ts` | Formula reference highlight rendering |
| `apps/wasm/src/grid-events.ts` | Mouse/keyboard event handling |
| `apps/wasm/src/grid-utils.ts` | Cell bounds calculation utilities |
| `core/cells/ostree.h/cc` | Generic Order-Statistic Tree implementation |
| `core/cells/axis_index.h/cc` | AxisIndex wrapping OSTree for column/row indexing |
| `core/cells/viewport_index.h/cc` | ViewportIndex combining two AxisIndexes + cell HashMap |
