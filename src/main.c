#include <sdk.h>

/* Constants */
#define MAX_WINDOWS_PER_WORKSPACE 6
#define WORKSPACE_COUNT 4
#define TOP_BAR_HEIGHT 24
#define DEFAULT_GAP_SIZE 4
#define DEFAULT_BORDER_SIZE 2
#define FOCUSED_BORDER_MULTIPLIER 3
#define SYMBOL_WIDTH 8
#define SYMBOL_HEIGHT 8
#define MASTER_RATIO_DEFAULT 50
#define MASTER_RATIO_MASTER_STACK 60

/* Colors (ARGB) */
#define COLOR_BORDER_NORMAL  0x928374
#define COLOR_WINDOW_BG      0x282828
#define COLOR_BAR_BG         0x1d2021
#define COLOR_EMPTY_DESKTOP  0x3c3836

/* Layout types */
typedef enum {
    LAYOUT_HORIZONTAL,
    LAYOUT_VERTICAL,
    LAYOUT_GRID,
    LAYOUT_FULLSCREEN,
    LAYOUT_MASTER_STACK,
    LAYOUT_COUNT
} layout_type_t;

/* Window metadata */
typedef struct {
    char title[32];
    uint32_t pid;
    uint8_t is_open;
} window_t;

/* Layout configuration */
typedef struct {
    layout_type_t type;
    uint32_t gap_size;
    uint32_t border_size;
    uint32_t border_color;
    uint32_t master_ratio;
} layout_config_t;

/* Calculated window position on screen */
typedef struct {
    uint32_t x, y, width, height;
    uint32_t pid;
} window_position_t;

/* Workspace state */
typedef struct {
    window_t windows[MAX_WINDOWS_PER_WORKSPACE];
    uint32_t window_count;
    layout_config_t layout;
    uint32_t focused_window_index;
} workspace_t;

/* Global state */
static struct kernel_api* g_api = NULL;
static workspace_t g_workspaces[WORKSPACE_COUNT];
static uint32_t g_active_workspace = 0;

/* Framebuffer info */
static uint32_t* g_framebuffer = NULL;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint32_t g_fb_pitch_pixels = 0;

/* Previous state for incremental redraw */
static window_position_t g_prev_positions[MAX_WINDOWS_PER_WORKSPACE];
static uint32_t g_prev_window_count = 0;
static layout_type_t g_prev_layout = LAYOUT_GRID;
static uint32_t g_prev_focused = 0;

/* Predefined layouts */
static const layout_config_t DEFAULT_LAYOUTS[LAYOUT_COUNT] = {
    [LAYOUT_HORIZONTAL] = {
        .type = LAYOUT_HORIZONTAL,
        .gap_size = DEFAULT_GAP_SIZE,
        .border_size = DEFAULT_BORDER_SIZE,
        .border_color = COLOR_BORDER_NORMAL,
        .master_ratio = MASTER_RATIO_DEFAULT
    },
    [LAYOUT_VERTICAL] = {
        .type = LAYOUT_VERTICAL,
        .gap_size = DEFAULT_GAP_SIZE,
        .border_size = DEFAULT_BORDER_SIZE,
        .border_color = COLOR_BORDER_NORMAL,
        .master_ratio = MASTER_RATIO_DEFAULT
    },
    [LAYOUT_GRID] = {
        .type = LAYOUT_GRID,
        .gap_size = DEFAULT_GAP_SIZE,
        .border_size = DEFAULT_BORDER_SIZE,
        .border_color = COLOR_BORDER_NORMAL,
        .master_ratio = MASTER_RATIO_DEFAULT
    },
    [LAYOUT_FULLSCREEN] = {
        .type = LAYOUT_FULLSCREEN,
        .gap_size = DEFAULT_GAP_SIZE,
        .border_size = DEFAULT_BORDER_SIZE,
        .border_color = COLOR_BORDER_NORMAL,
        .master_ratio = MASTER_RATIO_DEFAULT
    },
    [LAYOUT_MASTER_STACK] = {
        .type = LAYOUT_MASTER_STACK,
        .gap_size = DEFAULT_GAP_SIZE,
        .border_size = DEFAULT_BORDER_SIZE,
        .border_color = COLOR_BORDER_NORMAL,
        .master_ratio = MASTER_RATIO_MASTER_STACK
    }
};

/* ------------------------------------------------------------------------- */
/* Low-level drawing helpers                                                 */
/* ------------------------------------------------------------------------- */

static void set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < g_fb_width && y < g_fb_height) {
        uint32_t* pixel = &g_framebuffer[y * g_fb_pitch_pixels + x];
        *pixel = color;
    }
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            set_pixel(x + dx, y + dy, color);
        }
    }
}

static void clear_screen(void) {
    fill_rect(0, 0, g_fb_width, g_fb_height, COLOR_BAR_BG);
}

/* ------------------------------------------------------------------------- */
/* String utilities                                                          */
/* ------------------------------------------------------------------------- */

static uint32_t string_length(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static void string_copy(char* dest, const char* src) {
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

/* ------------------------------------------------------------------------- */
/* Layout calculation functions                                              */
/* ------------------------------------------------------------------------- */

static void calculate_horizontal_layout(
    window_position_t* positions,
    uint32_t count,
    uint32_t gap,
    uint32_t usable_height
) {
    uint32_t window_width = (g_fb_width - gap * (count + 1)) / count;
    for (uint32_t i = 0; i < count; i++) {
        positions[i].x = gap + i * (window_width + gap);
        positions[i].y = TOP_BAR_HEIGHT + gap;
        positions[i].width = window_width;
        positions[i].height = usable_height - gap;
    }
}

static void calculate_vertical_layout(
    window_position_t* positions,
    uint32_t count,
    uint32_t gap,
    uint32_t usable_height
) {
    uint32_t window_height = (usable_height - gap * (count + 1)) / count;
    for (uint32_t i = 0; i < count; i++) {
        positions[i].x = gap;
        positions[i].y = TOP_BAR_HEIGHT + gap + i * (window_height + gap);
        positions[i].width = g_fb_width - gap * 2;
        positions[i].height = window_height;
    }
}

static void calculate_grid_layout(
    window_position_t* positions,
    uint32_t count,
    uint32_t gap,
    uint32_t usable_height
) {
    uint32_t cols = 2;
    uint32_t rows = (count + 1) / 2;
    uint32_t cell_width = (g_fb_width - gap * (cols + 1)) / cols;
    uint32_t cell_height = (usable_height - gap * (rows + 1)) / rows;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t col = i % cols;
        uint32_t row = i / cols;
        positions[i].x = gap + col * (cell_width + gap);
        positions[i].y = TOP_BAR_HEIGHT + gap + row * (cell_height + gap);
        positions[i].width = cell_width;
        positions[i].height = cell_height;
    }
}

static void calculate_fullscreen_layout(
    window_position_t* positions,
    uint32_t count,
    uint32_t gap,
    uint32_t usable_height
) {
    for (uint32_t i = 0; i < count; i++) {
        positions[i].x = gap;
        positions[i].y = TOP_BAR_HEIGHT + gap;
        positions[i].width = g_fb_width - gap * 2;
        positions[i].height = usable_height - gap;
    }
}

static void calculate_master_stack_layout(
    window_position_t* positions,
    uint32_t count,
    uint32_t gap,
    uint32_t usable_height,
    uint32_t master_ratio
) {
    if (count == 1) {
        positions[0].x = gap;
        positions[0].y = TOP_BAR_HEIGHT + gap;
        positions[0].width = g_fb_width - gap * 2;
        positions[0].height = usable_height - gap;
    } else {
        uint32_t master_width = (g_fb_width * master_ratio / 100) - gap * 2;
        uint32_t stack_width = g_fb_width - master_width - gap * 3;

        positions[0].x = gap;
        positions[0].y = TOP_BAR_HEIGHT + gap;
        positions[0].width = master_width;
        positions[0].height = usable_height - gap;

        uint32_t stack_count = count - 1;
        uint32_t stack_height = (usable_height - gap * (stack_count + 1)) / stack_count;

        for (uint32_t i = 1; i < count; i++) {
            positions[i].x = master_width + gap * 2;
            positions[i].y = TOP_BAR_HEIGHT + gap + (i - 1) * (stack_height + gap);
            positions[i].width = stack_width;
            positions[i].height = stack_height;
        }
    }
}

static void compute_window_positions(
    window_position_t* positions,
    uint32_t count,
    const layout_config_t* config
) {
    uint32_t gap = config->gap_size;
    uint32_t usable_height = g_fb_height - TOP_BAR_HEIGHT - gap;

    for (uint32_t i = 0; i < count; i++) {
        positions[i].x = 0;
        positions[i].y = TOP_BAR_HEIGHT;
        positions[i].width = 0;
        positions[i].height = 0;
    }

    if (count == 0) return;

    switch (config->type) {
        case LAYOUT_HORIZONTAL:
            calculate_horizontal_layout(positions, count, gap, usable_height);
            break;
        case LAYOUT_VERTICAL:
            calculate_vertical_layout(positions, count, gap, usable_height);
            break;
        case LAYOUT_GRID:
            calculate_grid_layout(positions, count, gap, usable_height);
            break;
        case LAYOUT_FULLSCREEN:
            calculate_fullscreen_layout(positions, count, gap, usable_height);
            break;
        case LAYOUT_MASTER_STACK:
            calculate_master_stack_layout(positions, count, gap, usable_height,
                                          config->master_ratio);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing functions                                                         */
/* ------------------------------------------------------------------------- */

static void draw_top_bar(void) {
    fill_rect(0, 0, g_fb_width, TOP_BAR_HEIGHT, COLOR_BAR_BG);
}

static void draw_window_frame(
    const window_position_t* position,
    uint32_t border_size,
    uint32_t border_color,
    uint32_t is_focused
) {
    uint32_t x = position->x;
    uint32_t y = position->y;
    uint32_t w = position->width;
    uint32_t h = position->height;
    uint32_t border = is_focused ? border_size * FOCUSED_BORDER_MULTIPLIER : border_size;

    fill_rect(x + border, y + border, w - border * 2, h - border * 2, COLOR_WINDOW_BG);
    fill_rect(x, y, w, border, border_color);
    fill_rect(x, y + h - border, w, border, border_color);
    fill_rect(x, y, border, h, border_color);
    fill_rect(x + w - border, y, border, h, border_color);
}

static void draw_empty_desktop_indicator(void) {
    const char* text = "~";
    uint32_t text_width = string_length(text) * SYMBOL_WIDTH;
    uint32_t x = (g_fb_width - text_width) / 2;
    uint32_t y = g_fb_height / 2 - SYMBOL_HEIGHT / 2;

    for (uint32_t i = 0; i < string_length(text); i++) {
        fill_rect(x + i * SYMBOL_WIDTH, y, SYMBOL_WIDTH, SYMBOL_HEIGHT, COLOR_EMPTY_DESKTOP);
    }
}

static void redraw_incremental(void) {
    workspace_t* ws = &g_workspaces[g_active_workspace];

    if (ws->window_count != g_prev_window_count || ws->layout.type != g_prev_layout) {

        for (uint32_t i = 0; i < g_prev_window_count; i++) {
            fill_rect(g_prev_positions[i].x, g_prev_positions[i].y,
                     g_prev_positions[i].width, g_prev_positions[i].height,
                     COLOR_BAR_BG);
        }
        
        if (ws->window_count == 0) {
            draw_empty_desktop_indicator();
        } else {
            window_position_t positions[MAX_WINDOWS_PER_WORKSPACE];
            compute_window_positions(positions, ws->window_count, &ws->layout);
            
            for (uint32_t i = 0; i < ws->window_count; i++) {
                positions[i].pid = ws->windows[i].pid;
                draw_window_frame(&positions[i], ws->layout.border_size,
                                ws->layout.border_color,
                                i == ws->focused_window_index);
                g_prev_positions[i] = positions[i];
            }
        }
        
        g_prev_window_count = ws->window_count;
        g_prev_layout = ws->layout.type;
        g_prev_focused = ws->focused_window_index;
    }

    else if (ws->focused_window_index != g_prev_focused) {
        window_position_t positions[MAX_WINDOWS_PER_WORKSPACE];
        compute_window_positions(positions, ws->window_count, &ws->layout);

        draw_window_frame(&positions[g_prev_focused], ws->layout.border_size,
                         ws->layout.border_color, 0);

        draw_window_frame(&positions[ws->focused_window_index], ws->layout.border_size,
                         ws->layout.border_color, 1);

        g_prev_focused = ws->focused_window_index;
    }
}

/* ------------------------------------------------------------------------- */
/* Workspace and window management                                           */
/* ------------------------------------------------------------------------- */

static void initialize_workspaces(void) {
    for (uint32_t i = 0; i < WORKSPACE_COUNT; i++) {
        workspace_t* ws = &g_workspaces[i];
        ws->window_count = 0;
        ws->layout = DEFAULT_LAYOUTS[LAYOUT_GRID];
        ws->focused_window_index = 0;
        for (uint32_t j = 0; j < MAX_WINDOWS_PER_WORKSPACE; j++) {
            ws->windows[j].is_open = 0;
            ws->windows[j].title[0] = '\0';
        }
    }
}

static void add_window_to_current_workspace(const char* title) {
    workspace_t* ws = &g_workspaces[g_active_workspace];
    if (ws->window_count >= MAX_WINDOWS_PER_WORKSPACE) return;

    window_t* win = &ws->windows[ws->window_count];
    string_copy(win->title, title);
    win->is_open = 1;
    win->pid = ws->window_count;
    ws->window_count++;
    ws->focused_window_index = ws->window_count - 1;
    redraw_incremental();
}

static void close_current_window(void) {
    workspace_t* ws = &g_workspaces[g_active_workspace];
    if (ws->window_count == 0) return;

    uint32_t focused = ws->focused_window_index;
    for (uint32_t i = focused; i < ws->window_count - 1; i++) {
        ws->windows[i] = ws->windows[i + 1];
    }

    ws->window_count--;
    if (ws->window_count == 0) {
        ws->focused_window_index = 0;
    } else if (focused >= ws->window_count) {
        ws->focused_window_index = ws->window_count - 1;
    }

    redraw_incremental();
}

static void cycle_focus(int direction) {
    workspace_t* ws = &g_workspaces[g_active_workspace];
    if (ws->window_count == 0) return;

    if (direction > 0) {
        ws->focused_window_index = (ws->focused_window_index + 1) % ws->window_count;
    } else {
        ws->focused_window_index = (ws->focused_window_index + ws->window_count - 1) % ws->window_count;
    }
    redraw_incremental();
}

static void cycle_layout(void) {
    workspace_t* ws = &g_workspaces[g_active_workspace];
    layout_type_t current = ws->layout.type;
    layout_type_t next = (current + 1) % LAYOUT_COUNT;
    ws->layout = DEFAULT_LAYOUTS[next];
    redraw_incremental();
}

/* ------------------------------------------------------------------------- */
/* Keyboard callbacks                                                        */
/* ------------------------------------------------------------------------- */

static void on_cycle_focus_next(void* unused) {
    (void)unused;
    cycle_focus(1);
}

static void on_cycle_layout(void* unused) {
    (void)unused;
    cycle_layout();
}

static void on_new_window(void* unused) {
    (void)unused;
    add_window_to_current_workspace("template");
}

static void on_close_window(void* unused) {
    (void)unused;
    close_current_window();
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

void _start(struct kernel_api* kernel_api) {
    g_api = kernel_api;

    clear_screen();

    g_framebuffer = g_api->get_framebuffer();
    g_api->get_fb_dimensions(&g_fb_width, &g_fb_height, &g_fb_pitch_pixels);
    g_fb_pitch_pixels = g_api->get_fb_pitch_pixels();

    initialize_workspaces();

    g_prev_window_count = 0;
    g_prev_layout = LAYOUT_GRID;
    g_prev_focused = 0;

    redraw_incremental();

    g_api->keyboard_register_hotkey(0x20, 1, on_cycle_focus_next, NULL);
    g_api->keyboard_register_hotkey(0x26, 1, on_cycle_layout, NULL);
    g_api->keyboard_register_hotkey(0x10, 1, on_close_window, NULL);
    g_api->keyboard_register_hotkey(0x11, 1, on_new_window, NULL);
}