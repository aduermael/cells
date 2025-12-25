# Rendering

## Implementation Status

**Current state (December 2024):** Canvas2D rendering in the browser only.

| Component | Status |
|-----------|--------|
| Canvas2D backend (Web) | ✅ Implemented |
| Grid lines, cells, headers | ✅ Implemented |
| Selection rendering | ✅ Implemented |
| Column/row resize preview | ✅ Implemented |
| Drag-and-drop ghost | ✅ Implemented |
| WebGL backend | ❌ Not started |
| Native backends (Metal, DirectX) | ❌ Not started |
| Dirty region tracking | ❌ Not implemented |
| Frozen panes | ❌ Not implemented |

The architecture below describes the full vision. See "Current Implementation" section at the end for actual implementation details.

---

## Design Goals

1. **60 FPS**: Smooth scrolling even with millions of cells
2. **Memory efficient**: Only visible cells in memory
3. **Cross-platform**: Same rendering logic for native/web
4. **GPU accelerated**: When available

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Platform UI Layer                             │
│   (Native: AppKit/UIKit/Win32, Web: HTML/Canvas)                │
└────────────────────────────────────┬────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Grid Renderer (C++)                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Viewport    │  │ Layout      │  │ Draw Commands           │  │
│  │ Manager     │──│ Engine      │──│ Generator               │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└────────────────────────────────────┬────────────────────────────┘
                                     │
                         ┌───────────┴───────────┐
                         ▼                       ▼
              ┌─────────────────┐     ┌─────────────────┐
              │ Native Backend  │     │ WebGL/Canvas    │
              │ (Metal/DX/etc)  │     │ Backend         │
              └─────────────────┘     └─────────────────┘
```

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

## Platform Backends

| Platform | Backend | Notes |
|----------|---------|-------|
| macOS | Metal or Core Graphics | SwiftUI Canvas |
| iOS | Metal or Core Graphics | SwiftUI Canvas |
| Windows | Direct2D or DirectX | WinUI 3 |
| Web | Canvas2D or WebGL | WebGL for large grids |

## Dirty Region Tracking

Only re-render changed areas:
- Track dirty regions (cell bounds)
- On change, add cell to dirty list
- Render only cells overlapping dirty regions
- Full redraw on scroll or zoom

## Text Rendering

Options:
1. **Platform APIs** (CoreText, DirectWrite): Best quality, platform-specific
2. **Font Atlas**: Fast, consistent, limited fonts
3. **SDF Rendering**: Smooth at any zoom, GPU accelerated

Recommendation: Platform APIs for quality, fallback to atlas for WebGL.

## Scrolling

### Smooth Scrolling

Interpolate between current and target position with easing.
Apply momentum decay for touch/trackpad scrolling.

### Virtual Scrolling

For millions of rows:
- Calculate position mathematically (index × default height)
- Use cached cumulative heights for variable row heights
- Binary search to find row at position

## Selection Rendering

| Selection Type | Visual |
|----------------|--------|
| Single cell | Bold border |
| Range | Light fill, bold border |
| Multi-range | Multiple highlights |
| Row/Column | Full row/column highlight |

Plus: active cell indicator, resize handle (fill handle).

## Frozen Panes

Split viewport into four regions:
1. **Scrollable** (bottom-right): Normal scrolling
2. **Frozen columns** (left): Scrolls vertically only
3. **Frozen rows** (top): Scrolls horizontally only
4. **Corner** (top-left): Fixed

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

### Not Implemented

- **Dirty region tracking** - Full redraw every frame
- **WebGL acceleration** - Canvas2D only
- **Frozen panes** - Not supported
- **Zoom** - Not supported
- **Smooth scrolling easing** - Direct scroll position updates
