# Rendering

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
