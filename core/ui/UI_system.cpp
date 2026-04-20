// ═══════════════════════════════════════════════════════════════════════════════
// UI_system.cpp — HUD overlay drawn into the camera's pixel buffer
// ═══════════════════════════════════════════════════════════════════════════════

#include "UI_system.hpp"
#include "../render/Render_Engine.hpp"
#include "../clock/Clock_Manager.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Helpers: draw into camera buffer at depth 0 (always on top) ─────────────

static void ui_put(int cam_id, int x, int y, char ch, int color) {
    cam_set_pixel(cam_id, x, y, ch, color, 0.0f);
}

static void ui_text(int cam_id, int x, int y, const char* str, int color) {
    for (int i = 0; str[i]; i++)
        ui_put(cam_id, x + i, y, str[i], color);
}

// Bresenham line into the camera buffer
static void ui_line(int cam_id, int x0, int y0, int x1, int y1, char ch, int color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int abs_dx = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;
    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;
    int err = abs_dx - abs_dy;

    for (;;) {
        ui_put(cam_id, x0, y0, ch, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -abs_dy) { err -= abs_dy; x0 += sx; }
        if (e2 <  abs_dx) { err += abs_dx; y0 += sy; }
    }
}

// Pick a line-drawing character based on the overall slope
static char pick_line_char(int dx, int dy) {
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    if (ay < ax / 3)  return '-';                          // mostly horizontal
    if (ax < ay / 3)  return '|';                          // mostly vertical
    return (dx > 0) == (dy > 0) ? '\\' : '/';              // diagonal
}

// ─── Axis Gizmo ──────────────────────────────────────────────────────────────
// Projects world X/Y/Z unit vectors through the camera's view rotation,
// then draws them as colored lines in a fixed corner of the screen.

static void ui_draw_gizmo(int cam_id, Camera* c) {
    int w = c->buf_w;
    int h = c->buf_h;
    if (w < 24 || h < 12) return;   // terminal too small for gizmo

    // Gizmo center: bottom-right area
    int gx = w - 14;
    int gy = h - 8;
    int arm = 5;                     // arm length in rows

    // Precomputed trig from camera (already cached by cam_update)
    float cy = c->cos_yaw,   sy = c->sin_yaw;
    float cp = c->cos_pitch, sp = c->sin_pitch;

    // For each world axis, rotate by camera view transform: -yaw then -pitch
    // cos(-a) = cos(a), sin(-a) = -sin(a)
    //
    // Step 1 — rotate by -yaw around Y:
    //   x' =  vx * cos_yaw + vz * sin_yaw
    //   z' = -vx * sin_yaw + vz * cos_yaw
    //   y' =  vy
    //
    // Step 2 — rotate by -pitch around X:
    //   y'' =  y' * cos_pitch + z' * sin_pitch
    //   z'' = -y' * sin_pitch + z' * cos_pitch
    //   x'' =  x'
    //
    // View space: x'' = screen right, y'' = screen up, z'' = into screen
    // Screen:     px = x'',  py = -y''  (terminal Y grows downward)

    struct { float ax, ay, az; int color; char label; } axes[3] = {
        { 1, 0, 0,  91, 'X' },   // bright red
        { 0, 1, 0,  92, 'Y' },   // bright green
        { 0, 0, 1,  94, 'Z' },   // bright blue
    };

    // Terminal chars are ~2× taller than wide — stretch X to compensate
    float aspect = 2.0f;

    // Draw a dim center crosshair for orientation
    ui_put(cam_id, gx, gy, '+', 90);

    for (int i = 0; i < 3; i++) {
        float vx = axes[i].ax, vy = axes[i].ay, vz = axes[i].az;

        // Rotate by -yaw (Y-axis)
        float rx =  vx * cy + vz * sy;
        float rz = -vx * sy + vz * cy;
        float ry =  vy;

        // Rotate by -pitch (X-axis)
        float fy =  ry * cp + rz * sp;
        // float fz = -ry * sp + rz * cp;  // depth — unused for orthographic
        float fx  = rx;

        // Screen offsets (aspect-compensated, Y inverted)
        int dx = (int)(fx * (float)arm * aspect + 0.5f);
        int dy = (int)(-fy * (float)arm + 0.5f);

        int ex = gx + dx;
        int ey = gy + dy;

        // Choose line character from direction
        char ch = pick_line_char(dx, dy);

        // Draw axis line
        ui_line(cam_id, gx, gy, ex, ey, ch, axes[i].color);

        // Axis label at the tip (overwrite last pixel)
        ui_put(cam_id, ex, ey, axes[i].label, axes[i].color);
    }
}

// ─── Box border ──────────────────────────────────────────────────────────────

static void ui_draw_border(int cam_id, int x, int y, int bw, int bh, int color) {
    // Top & bottom horizontal edges
    for (int i = 0; i < bw; i++) {
        ui_put(cam_id, x + i, y,          '-', color);
        ui_put(cam_id, x + i, y + bh - 1, '-', color);
    }
    // Left & right vertical edges
    for (int j = 0; j < bh; j++) {
        ui_put(cam_id, x,          y + j, '|', color);
        ui_put(cam_id, x + bw - 1, y + j, '|', color);
    }
    // Corners
    ui_put(cam_id, x,          y,          '+', color);
    ui_put(cam_id, x + bw - 1, y,          '+', color);
    ui_put(cam_id, x,          y + bh - 1, '+', color);
    ui_put(cam_id, x + bw - 1, y + bh - 1, '+', color);
}

// ─── Public ──────────────────────────────────────────────────────────────────

void ui_draw(int cam_id, int clk_id) {
    Camera* c = cam_get(cam_id);
    if (!c) return;

    int w = c->buf_w;
    int h = c->buf_h;

    char buf[96];

    // ── Top-left info panel ──────────────────────────────────────────────
    double fps = clock_get_fps(clk_id);
    snprintf(buf, sizeof(buf), " FPS: %.0f ", fps);
    ui_text(cam_id, 2, 1, buf, 93);   // bright yellow

    snprintf(buf, sizeof(buf), " RES: %dx%d ", w, h);
    ui_text(cam_id, 2, 2, buf, 96);   // bright cyan

    snprintf(buf, sizeof(buf), " CAM: %.0f, %.0f, %.0f ",
             c->pos.x, c->pos.y, c->pos.z);
    ui_text(cam_id, 2, 3, buf, 97);   // bright white

    float yaw_deg   = c->yaw   * 180.0f / (float)M_PI;
    float pitch_deg = c->pitch * 180.0f / (float)M_PI;
    snprintf(buf, sizeof(buf), " YAW: %.0f  PIT: %.0f ",
             yaw_deg, pitch_deg);
    ui_text(cam_id, 2, 4, buf, 37);   // light gray

    // Color mode indicator
    {
        int cm = render_get_color_mode();
        const char* mode_str = (cm == 2) ? "24bit" : (cm == 1) ? "256" : "16";
        snprintf(buf, sizeof(buf), " COL: %s (ALT+C) ", mode_str);
        ui_text(cam_id, 2, 5, buf, 95);  // bright magenta
    }

    // Light count — query render_set_light_fn presence as proxy
    // (we count active lights via a simple external counter set in main)
    snprintf(buf, sizeof(buf), " LIT: shadows on ");
    ui_text(cam_id, 2, 6, buf, 93);  // bright yellow

    // Shadow resolution (set from main.cpp each frame)
    extern const char* g_shadow_resolution_str;
    snprintf(buf, sizeof(buf), " SHD: %s (ALT+X) ", g_shadow_resolution_str);
    ui_text(cam_id, 2, 7, buf, 92);  // bright green

    // Dim border around the info panel (expanded to 9 rows)
    int panel_w = 26;
    if ((int)strlen(buf) + 3 > panel_w) panel_w = (int)strlen(buf) + 3;
    ui_draw_border(cam_id, 1, 0, panel_w, 9, 90);

    // ── Bottom: controls hint ────────────────────────────────────────────
    if (h > 6) {
        const char* hint = " WASD=move QE=turn SPACE/SHIFT=up/down ESC=quit ";
        int hint_x = (w - (int)strlen(hint)) / 2;
        if (hint_x < 0) hint_x = 0;
        ui_text(cam_id, hint_x, h - 1, hint, 90);  // dark gray
    }

    // ── Axis gizmo (bottom-right) ────────────────────────────────────────
    ui_draw_gizmo(cam_id, c);
}
