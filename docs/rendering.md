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
│                    Grid Renderer (C/C++)                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Viewport    │  │ Layout      │  │ Draw Commands           │  │
│  │ Manager     │──│ Engine      │──│ Generator               │  │
│  │             │  │             │  │                         │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└────────────────────────────────────┬────────────────────────────┘
                                     │
                         ┌───────────┴───────────┐
                         ▼                       ▼
              ┌─────────────────┐     ┌─────────────────┐
              │ Native Backend  │     │ WebGL/Canvas    │
              │ (Metal/DX/Vulkan│     │ Backend         │
              │  or 2D APIs)    │     │                 │
              └─────────────────┘     └─────────────────┘
```

## Core Concepts

### Viewport

The visible area of the spreadsheet:

```c
typedef struct Viewport {
    // Scroll position (in pixels)
    double scroll_x;
    double scroll_y;

    // Visible size (in pixels)
    double width;
    double height;

    // Visible cell range (calculated from scroll + size)
    uuid_t first_visible_col;
    uuid_t last_visible_col;
    uuid_t first_visible_row;
    uuid_t last_visible_row;

    // Overscan (render extra cells for smooth scrolling)
    int overscan_cols;
    int overscan_rows;

    // Zoom level
    double zoom;
} Viewport;
```

### Cell Layout

Convert data model to pixel coordinates:

```c
typedef struct CellLayout {
    uuid_t cell_id;
    double x, y;              // Top-left corner
    double width, height;     // Cell dimensions
    bool selected;
    bool editing;
} CellLayout;

typedef struct GridLayout {
    // Column positions (cumulative widths)
    double* col_positions;    // col_positions[i] = x offset of column i
    int col_count;

    // Row positions (cumulative heights)
    double* row_positions;
    int row_count;

    // Visible cells with layout info
    CellLayout* visible_cells;
    int visible_count;

    // Grid lines
    double* horizontal_lines; // y positions
    double* vertical_lines;   // x positions
    int h_line_count;
    int v_line_count;
} GridLayout;
```

### Layout Calculation

```c
GridLayout* calculate_layout(Sheet* sheet, Viewport* vp) {
    GridLayout* layout = layout_new();

    // 1. Calculate column positions
    double x = 0;
    Axis* col = axis_get(sheet, 0, sheet->dimensions[0].first_axis);
    while (col && x < vp->scroll_x + vp->width + overscan_width(vp)) {
        if (x + col->size >= vp->scroll_x - overscan_width(vp)) {
            // Column is visible
            layout_add_col(layout, col->id, x, col->size);
        }
        x += col->size + col->gap_to_next * DEFAULT_COL_WIDTH;
        col = axis_get(sheet, 0, col->next_axis);
    }

    // 2. Calculate row positions (similar)
    // ...

    // 3. Find visible cells
    for (int c = 0; c < layout->col_count; c++) {
        for (int r = 0; r < layout->row_count; r++) {
            uuid_t col_id = layout->cols[c].id;
            uuid_t row_id = layout->rows[r].id;

            Cell* cell = cell_at(sheet, col_id, row_id);
            if (cell) {
                layout_add_cell(layout, cell,
                               layout->cols[c].x, layout->rows[r].y,
                               layout->cols[c].width, layout->rows[r].height);
            }
        }
    }

    return layout;
}
```

## Draw Commands

Abstract drawing operations (platform-independent):

```c
typedef enum DrawCmdType {
    DRAW_RECT,
    DRAW_TEXT,
    DRAW_LINE,
    DRAW_IMAGE,
    DRAW_CLIP_PUSH,
    DRAW_CLIP_POP,
} DrawCmdType;

typedef struct DrawCmd {
    DrawCmdType type;
    union {
        struct {
            double x, y, w, h;
            uint32_t fill_color;    // ARGB
            uint32_t stroke_color;
            double stroke_width;
            double corner_radius;
        } rect;

        struct {
            double x, y;
            const char* text;
            uint32_t color;
            const char* font;
            double size;
            int align;              // left/center/right
        } text;

        struct {
            double x1, y1, x2, y2;
            uint32_t color;
            double width;
        } line;
    };
} DrawCmd;

typedef struct DrawList {
    DrawCmd* commands;
    int count;
    int capacity;
} DrawList;
```

### Generate Draw Commands

```c
DrawList* render_grid(GridLayout* layout, Sheet* sheet, Selection* sel) {
    DrawList* dl = drawlist_new();

    // 1. Background
    drawlist_rect(dl, 0, 0, layout->total_width, layout->total_height,
                  COLOR_WHITE, 0, 0, 0);

    // 2. Grid lines
    for (int i = 0; i < layout->v_line_count; i++) {
        drawlist_line(dl,
                     layout->vertical_lines[i], 0,
                     layout->vertical_lines[i], layout->total_height,
                     COLOR_GRID_LINE, 1);
    }
    for (int i = 0; i < layout->h_line_count; i++) {
        drawlist_line(dl,
                     0, layout->horizontal_lines[i],
                     layout->total_width, layout->horizontal_lines[i],
                     COLOR_GRID_LINE, 1);
    }

    // 3. Cell contents
    for (int i = 0; i < layout->visible_count; i++) {
        CellLayout* cl = &layout->visible_cells[i];
        Cell* cell = cell_get(sheet, cl->cell_id);

        // Cell background (if styled)
        if (cell->style && cell->style->bg_color) {
            drawlist_rect(dl, cl->x, cl->y, cl->width, cl->height,
                         cell->style->bg_color, 0, 0, 0);
        }

        // Cell text
        char* display_text = cell_display_text(cell);
        drawlist_text(dl, cl->x + CELL_PADDING, cl->y + CELL_PADDING,
                     display_text,
                     cell->style ? cell->style->fg_color : COLOR_BLACK,
                     cell->style ? cell->style->font : DEFAULT_FONT,
                     cell->style ? cell->style->font_size : DEFAULT_FONT_SIZE,
                     cell->style ? cell->style->align : ALIGN_LEFT);
    }

    // 4. Selection highlight
    if (sel->type != SEL_NONE) {
        render_selection(dl, layout, sel);
    }

    // 5. Headers (if enabled)
    render_col_headers(dl, layout);
    render_row_headers(dl, layout);

    return dl;
}
```

## Platform Backends

### Metal (macOS/iOS)

```c
// Convert DrawList to Metal commands
void render_metal(DrawList* dl, id<MTLRenderCommandEncoder> encoder) {
    for (int i = 0; i < dl->count; i++) {
        DrawCmd* cmd = &dl->commands[i];
        switch (cmd->type) {
            case DRAW_RECT:
                metal_draw_rect(encoder, cmd->rect);
                break;
            case DRAW_TEXT:
                metal_draw_text(encoder, cmd->text);
                break;
            // ...
        }
    }
}
```

### Canvas2D (Web)

```javascript
// DrawList passed via WASM
function renderCanvas(ctx, drawListPtr) {
    const commands = parseDrawList(drawListPtr);

    for (const cmd of commands) {
        switch (cmd.type) {
            case DRAW_RECT:
                ctx.fillStyle = colorToCSS(cmd.fillColor);
                ctx.fillRect(cmd.x, cmd.y, cmd.w, cmd.h);
                break;
            case DRAW_TEXT:
                ctx.font = `${cmd.size}px ${cmd.font}`;
                ctx.fillStyle = colorToCSS(cmd.color);
                ctx.fillText(cmd.text, cmd.x, cmd.y);
                break;
            // ...
        }
    }
}
```

### WebGL (Web, High Performance)

For very large grids, use instanced rendering:

```glsl
// Vertex shader for cell rectangles
attribute vec2 a_position;      // Quad vertex
attribute vec4 a_instance;      // x, y, w, h per cell
attribute vec4 a_color;         // RGBA per cell

varying vec4 v_color;

uniform mat4 u_projection;

void main() {
    vec2 pos = a_position * a_instance.zw + a_instance.xy;
    gl_Position = u_projection * vec4(pos, 0.0, 1.0);
    v_color = a_color;
}
```

## Dirty Region Tracking

Only re-render changed areas:

```c
typedef struct DirtyRegion {
    double x, y, w, h;
} DirtyRegion;

typedef struct DirtyTracker {
    DirtyRegion* regions;
    int count;
    bool full_redraw;
} DirtyTracker;

void mark_cell_dirty(DirtyTracker* tracker, CellLayout* cl) {
    // Add cell bounds to dirty regions
    add_dirty_region(tracker, cl->x, cl->y, cl->width, cl->height);
}

void mark_range_dirty(DirtyTracker* tracker, GridLayout* layout,
                      uuid_t start_col, uuid_t end_col,
                      uuid_t start_row, uuid_t end_row) {
    // Calculate pixel bounds of range
    // Add to dirty regions
}

DrawList* render_dirty_only(GridLayout* layout, Sheet* sheet,
                            DirtyTracker* tracker) {
    if (tracker->full_redraw) {
        return render_grid(layout, sheet, NULL);
    }

    DrawList* dl = drawlist_new();
    for (int i = 0; i < tracker->count; i++) {
        // Only render cells overlapping dirty region
        render_region(dl, layout, sheet, &tracker->regions[i]);
    }
    return dl;
}
```

## Text Rendering

Text rendering is complex. Options:

### Option A: Platform Text APIs
Use native text rendering (CoreText, DirectWrite, Pango):
- Pro: Perfect font rendering, full Unicode
- Con: Platform-specific code

### Option B: Font Atlas
Pre-render glyphs to texture:
- Pro: Fast, consistent across platforms
- Con: Limited fonts, complex for Unicode

### Option C: SDF Text Rendering
Signed distance field fonts:
- Pro: Smooth at any zoom, GPU accelerated
- Con: Setup complexity

Recommendation: **Option A** for quality, with Option B fallback for WebGL

```c
// Platform text measurement (needed for layout)
typedef struct TextMetrics {
    double width;
    double height;
    double baseline;
} TextMetrics;

TextMetrics measure_text(const char* text, const char* font, double size);
```

## Scrolling

### Smooth Scrolling

```c
typedef struct ScrollState {
    double target_x, target_y;    // Where we're scrolling to
    double current_x, current_y;  // Current position
    double velocity_x, velocity_y; // For momentum
} ScrollState;

void scroll_update(ScrollState* state, double dt) {
    // Smooth interpolation
    double ease = 1.0 - pow(0.001, dt);
    state->current_x += (state->target_x - state->current_x) * ease;
    state->current_y += (state->target_y - state->current_y) * ease;

    // Momentum decay
    state->velocity_x *= pow(0.95, dt * 60);
    state->velocity_y *= pow(0.95, dt * 60);

    state->target_x += state->velocity_x * dt;
    state->target_y += state->velocity_y * dt;
}
```

### Virtual Scrolling

For millions of rows, calculate position mathematically:

```c
// Given a row index, find y position without iterating all rows
double row_position_fast(Sheet* sheet, int row_index) {
    // If all rows are default height:
    if (sheet->all_rows_default_height) {
        return row_index * DEFAULT_ROW_HEIGHT;
    }

    // Otherwise, use cached cumulative heights
    return sheet->row_cumulative_heights[row_index];
}

// Given y position, find row index
int row_at_position(Sheet* sheet, double y) {
    if (sheet->all_rows_default_height) {
        return (int)(y / DEFAULT_ROW_HEIGHT);
    }

    // Binary search in cumulative heights
    return binary_search(sheet->row_cumulative_heights,
                        sheet->row_count, y);
}
```

## Selection Rendering

```c
typedef enum SelectionType {
    SEL_NONE,
    SEL_CELL,           // Single cell
    SEL_RANGE,          // Rectangular range
    SEL_MULTI_RANGE,    // Multiple ranges (Ctrl+click)
    SEL_ROW,            // Entire row(s)
    SEL_COL,            // Entire column(s)
} SelectionType;

typedef struct Selection {
    SelectionType type;

    // For SEL_CELL
    uuid_t active_cell;

    // For SEL_RANGE
    uuid_t start_col, end_col;
    uuid_t start_row, end_row;

    // For SEL_MULTI_RANGE
    Range* ranges;
    int range_count;
} Selection;

void render_selection(DrawList* dl, GridLayout* layout, Selection* sel) {
    // 1. Highlight selected cells (light blue background)
    // 2. Bold border around selection range
    // 3. Active cell indicator (darker border)
    // 4. Resize handle (bottom-right corner for fill)
}
```

## Frozen Panes

Freeze rows/columns (like Excel's freeze panes):

```c
typedef struct FrozenPanes {
    int frozen_cols;          // Number of frozen columns (from left)
    int frozen_rows;          // Number of frozen rows (from top)
} FrozenPanes;

DrawList* render_with_frozen(GridLayout* layout, Sheet* sheet,
                             FrozenPanes* frozen) {
    DrawList* dl = drawlist_new();

    // 1. Render scrollable area (bottom-right)
    render_region(dl, layout, REGION_SCROLLABLE);

    // 2. Render frozen columns (left side, scrolls vertically only)
    render_region(dl, layout, REGION_FROZEN_COLS);

    // 3. Render frozen rows (top, scrolls horizontally only)
    render_region(dl, layout, REGION_FROZEN_ROWS);

    // 4. Render corner (frozen both ways)
    render_region(dl, layout, REGION_CORNER);

    // 5. Render split lines
    render_split_lines(dl, frozen);

    return dl;
}
```

## Performance Targets

| Scenario | Target |
|----------|--------|
| Initial render (100x100 visible) | < 16ms |
| Scroll (continuous) | < 8ms per frame |
| Cell edit | < 5ms |
| Large recalc (10k cells) | < 100ms |
| Maximum visible cells | 10,000+ |

## Benchmarking

```c
typedef struct RenderStats {
    double layout_time_ms;
    double draw_gen_time_ms;
    double backend_time_ms;
    int cells_rendered;
    int draw_commands;
} RenderStats;

RenderStats* benchmark_render(Sheet* sheet, Viewport* vp) {
    RenderStats* stats = stats_new();

    double t0 = time_now();
    GridLayout* layout = calculate_layout(sheet, vp);
    stats->layout_time_ms = time_now() - t0;

    double t1 = time_now();
    DrawList* dl = render_grid(layout, sheet, NULL);
    stats->draw_gen_time_ms = time_now() - t1;

    stats->cells_rendered = layout->visible_count;
    stats->draw_commands = dl->count;

    return stats;
}
```
