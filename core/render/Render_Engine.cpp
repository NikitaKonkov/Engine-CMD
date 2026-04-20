// ═══════════════════════════════════════════════════════════════════════════════
// Render_Engine.cpp — Camera system + differential ANSI renderer
// ═══════════════════════════════════════════════════════════════════════════════

#include "Render_Engine.hpp"
#include "../console/Console_Manager.hpp"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <climits>

#if !defined(_WIN32)
  #include <unistd.h>   // write(), STDOUT_FILENO
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Camera Pool ─────────────────────────────────────────────────────────────

static Camera  cameras[RENDER_MAX_CAMERAS];
static int     active_cam  = -1;
static int     pool_ready  = 0;

static void pool_init(void) {
    if (pool_ready) return;
    for (int i = 0; i < RENDER_MAX_CAMERAS; i++) {
        cameras[i].id    = -1;
        cameras[i].front = NULL;
        cameras[i].back  = NULL;
    }
    pool_ready = 1;
}

// ─── Color Mode ──────────────────────────────────────────────────────────────

static int g_color_mode = RENDER_COLOR_16;  // default: classic 16-color

// ─── Lighting Callback ──────────────────────────────────────────────────────

static RenderLightFn g_light_fn = NULL;

void render_set_light_fn(RenderLightFn fn) {
    g_light_fn = fn;
}

void render_set_color_mode(int mode) {
    if (mode < 0 || mode > 2) return;
    g_color_mode = mode;
}

int render_get_color_mode(void) {
    return g_color_mode;
}

void render_cycle_color_mode(void) {
    g_color_mode = (g_color_mode + 1) % 3;
    // Force full redraw by invalidating all back buffers
    for (int i = 0; i < RENDER_MAX_CAMERAS; i++) {
        if (cameras[i].id >= 0 && cameras[i].back) {
            int count = cameras[i].buf_w * cameras[i].buf_h;
            for (int j = 0; j < count; j++) {
                cameras[i].back[j].valid = 0;
                cameras[i].back[j].depth = 1e30f;
            }
        }
    }
}

// ANSI 16-color code → RGB lookup (same table as main.cpp's rgb_to_ansi)
static const unsigned char g_ansi16_rgb[][3] = {
    {  0,  0,  0}, // 30 black
    {170,  0,  0}, // 31 red
    {  0,170,  0}, // 32 green
    {170, 85,  0}, // 33 brown
    {  0,  0,170}, // 34 blue
    {170,  0,170}, // 35 magenta
    {  0,170,170}, // 36 cyan
    {170,170,170}, // 37 light gray
    { 85, 85, 85}, // 90 dark gray
    {255, 85, 85}, // 91 bright red
    { 85,255, 85}, // 92 bright green
    {255,255, 85}, // 93 bright yellow
    { 85, 85,255}, // 94 bright blue
    {255, 85,255}, // 95 bright magenta
    { 85,255,255}, // 96 bright cyan
    {255,255,255}, // 97 bright white
};

void render_ansi16_to_rgb(int code, unsigned char* r, unsigned char* g, unsigned char* b) {
    int idx = -1;
    if (code >= 30 && code <= 37) idx = code - 30;
    else if (code >= 90 && code <= 97) idx = code - 90 + 8;
    if (idx >= 0 && idx < 16) {
        *r = g_ansi16_rgb[idx][0];
        *g = g_ansi16_rgb[idx][1];
        *b = g_ansi16_rgb[idx][2];
    } else {
        *r = 170; *g = 170; *b = 170;  // fallback gray
    }
}

// Reverse map: find the nearest ANSI 16-color code for a given RGB.
static int render_rgb_to_ansi16(unsigned char r, unsigned char g, unsigned char b) {
    int best = 37;  // default: light gray
    int best_dist = INT_MAX;
    for (int i = 0; i < 16; i++) {
        int dr = (int)r - g_ansi16_rgb[i][0];
        int dg = (int)g - g_ansi16_rgb[i][1];
        int db = (int)b - g_ansi16_rgb[i][2];
        int d  = dr * dr + dg * dg + db * db;
        if (d < best_dist) {
            best_dist = d;
            best = (i < 8) ? (30 + i) : (90 + i - 8);
        }
    }
    return best;
}

int render_rgb_to_256(unsigned char r, unsigned char g, unsigned char b) {
    // Check if it's close enough to a grayscale ramp (232-255)
    if (r == g && g == b) {
        if (r < 8) return 16;   // black
        if (r > 248) return 231; // white
        return 232 + (int)((r - 8.0f) / 247.0f * 23.0f + 0.5f);
    }
    // Map to the 6×6×6 color cube (indices 16-231)
    int ri = (int)(r / 255.0f * 5.0f + 0.5f);
    int gi = (int)(g / 255.0f * 5.0f + 0.5f);
    int bi = (int)(b / 255.0f * 5.0f + 0.5f);
    return 16 + 36 * ri + 6 * gi + bi;
}

// ─── Vec3f ───────────────────────────────────────────────────────────────────

Vec2f vec2f_make(float u, float v) {
    Vec2f r; r.u = u; r.v = v; return r;
}

Vec3f vec3f_make(float x, float y, float z) {
    Vec3f v; v.x = x; v.y = y; v.z = z; return v;
}

Vec3f vec3f_add(Vec3f a, Vec3f b) {
    Vec3f v; v.x = a.x + b.x; v.y = a.y + b.y; v.z = a.z + b.z; return v;
}

Vec3f vec3f_sub(Vec3f a, Vec3f b) {
    Vec3f v; v.x = a.x - b.x; v.y = a.y - b.y; v.z = a.z - b.z; return v;
}

Vec3f vec3f_scale(Vec3f v, float s) {
    Vec3f r; r.x = v.x * s; r.y = v.y * s; r.z = v.z * s; return r;
}

float vec3f_dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f vec3f_cross(Vec3f a, Vec3f b) {
    Vec3f v;
    v.x = a.y * b.z - a.z * b.y;
    v.y = a.z * b.x - a.x * b.z;
    v.z = a.x * b.y - a.y * b.x;
    return v;
}

float vec3f_length(Vec3f v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3f vec3f_normalize(Vec3f v) {
    float len = vec3f_length(v);
    if (len < 0.0001f) return vec3f_make(0, 0, 0);
    return vec3f_scale(v, 1.0f / len);
}

// ─── Primitive Builders ──────────────────────────────────────────────────────

RDot rdot_make(Vec3f pos, char ascii, int color) {
    RDot d;
    d.pos   = pos;
    d.ascii = ascii;
    d.color = color;
    return d;
}

REdge redge_make(Vec3f start, Vec3f end, char ascii, int color) {
    REdge e;
    e.start = start;
    e.end   = end;
    e.ascii = ascii;
    e.color = color;
    return e;
}

RFace rface_make(Vec3f v0, Vec3f v1, Vec3f v2, int color, char ascii) {
    RFace f;
    f.verts[0] = v0;  f.verts[1] = v1;  f.verts[2] = v2;
    f.uvs[0] = vec2f_make(0, 0);
    f.uvs[1] = vec2f_make(1, 0);
    f.uvs[2] = vec2f_make(0, 1);
    f.normal  = vec3f_make(0, 0, 0);
    f.texture = NULL;
    f.tex_w   = 0;
    f.tex_h   = 0;
    f.color   = color;
    f.ascii   = ascii;
    rface_calc_normal(&f);
    return f;
}

RFace rface_make_textured(Vec3f v0, Vec3f v1, Vec3f v2,
                          Vec2f uv0, Vec2f uv1, Vec2f uv2,
                          int* texture, int tex_w, int tex_h, char ascii) {
    RFace f;
    f.verts[0] = v0;  f.verts[1] = v1;  f.verts[2] = v2;
    f.uvs[0]   = uv0; f.uvs[1]   = uv1; f.uvs[2]   = uv2;
    f.normal   = vec3f_make(0, 0, 0);
    f.texture  = texture;
    f.tex_w    = tex_w;
    f.tex_h    = tex_h;
    f.color    = 37;   // white fallback
    f.ascii    = ascii;
    rface_calc_normal(&f);
    return f;
}

void rface_make_quad(Vec3f v0, Vec3f v1, Vec3f v2, Vec3f v3,
                     int color, char ascii, RFace out[2]) {
    out[0] = rface_make(v0, v1, v2, color, ascii);
    out[1] = rface_make(v0, v2, v3, color, ascii);
}

void rface_make_quad_textured(Vec3f v0, Vec3f v1, Vec3f v2, Vec3f v3,
                              int* texture, int tex_w, int tex_h,
                              char ascii, RFace out[2]) {
    out[0] = rface_make_textured(v0, v1, v2,
                vec2f_make(0, 0), vec2f_make(1, 0), vec2f_make(1, 1),
                texture, tex_w, tex_h, ascii);
    out[1] = rface_make_textured(v0, v2, v3,
                vec2f_make(0, 0), vec2f_make(1, 1), vec2f_make(0, 1),
                texture, tex_w, tex_h, ascii);
}

void rface_calc_normal(RFace* f) {
    Vec3f e1 = vec3f_sub(f->verts[1], f->verts[0]);
    Vec3f e2 = vec3f_sub(f->verts[2], f->verts[0]);
    f->normal = vec3f_normalize(vec3f_cross(e1, e2));
}

// ─── Camera Lifecycle ────────────────────────────────────────────────────────

int cam_create(int buf_width, int buf_height) {
    pool_init();

    if (buf_width <= 0 || buf_height <= 0) return -1;

    // Find a free slot
    int slot = -1;
    for (int i = 0; i < RENDER_MAX_CAMERAS; i++) {
        if (cameras[i].id == -1) { slot = i; break; }
    }
    if (slot < 0) return -1;

    int count = buf_width * buf_height;
    Pixel* front = (Pixel*)calloc(count, sizeof(Pixel));
    Pixel* back  = (Pixel*)calloc(count, sizeof(Pixel));
    if (!front || !back) {
        free(front);
        free(back);
        return -1;
    }

    Camera* c       = &cameras[slot];
    c->id           = slot;

    // Transform
    c->pos          = vec3f_make(0, 0, 0);
    c->yaw          = 0.0f;
    c->pitch        = 0.0f;
    c->roll         = 0.0f;

    // Projection
    c->fov          = RENDER_DEFAULT_FOV;
    c->zoom         = 1.0f;
    c->near_plane   = 0.1f;
    c->far_plane    = 10000.0f;
    c->aspect_w     = 1.0f;
    c->aspect_h     = 2.0f;

    // Movement
    c->move_speed   = 0.1f;
    c->turn_speed   = 0.1f;

    // Derived
    c->forward      = vec3f_make(0, 0, 1);
    c->right        = vec3f_make(1, 0, 0);
    c->up           = vec3f_make(0, 1, 0);

    // Trig cache
    c->cos_yaw      = 1.0f;  c->sin_yaw   = 0.0f;
    c->cos_pitch    = 1.0f;  c->sin_pitch  = 0.0f;
    c->cos_roll     = 1.0f;  c->sin_roll   = 0.0f;
    c->cache_dirty  = 1;

    // Buffers
    c->buf_w        = buf_width;
    c->buf_h        = buf_height;
    c->front        = front;
    c->back         = back;

    // First camera becomes active
    if (active_cam < 0) {
        active_cam = slot;
    }

    return slot;
}

void cam_destroy(int id) {
    if (id < 0 || id >= RENDER_MAX_CAMERAS) return;
    Camera* c = &cameras[id];
    if (c->id == -1) return;

    free(c->front);
    free(c->back);
    c->front = NULL;
    c->back  = NULL;
    c->id    = -1;

    if (active_cam == id) active_cam = -1;
}

void cam_destroy_all(void) {
    for (int i = 0; i < RENDER_MAX_CAMERAS; i++) cam_destroy(i);
    active_cam = -1;
    pool_ready = 0;
}

// ─── Camera Access ───────────────────────────────────────────────────────────

Camera* cam_get(int id) {
    if (id < 0 || id >= RENDER_MAX_CAMERAS) return NULL;
    if (cameras[id].id == -1) return NULL;
    return &cameras[id];
}

void cam_set_active(int id) {
    if (!cam_get(id)) return;
    active_cam = id;
}

int cam_get_active_id(void) {
    return active_cam;
}

Camera* cam_get_active(void) {
    if (active_cam < 0) return NULL;
    return cam_get(active_cam);
}

// ─── Camera Configuration ────────────────────────────────────────────────────

void cam_set_position(int id, float x, float y, float z) {
    Camera* c = cam_get(id);
    if (!c) return;
    c->pos = vec3f_make(x, y, z);
    c->cache_dirty = 1;
}

void cam_set_rotation(int id, float yaw, float pitch, float roll) {
    Camera* c = cam_get(id);
    if (!c) return;
    c->yaw   = yaw;
    c->pitch = pitch;
    c->roll  = roll;
    c->cache_dirty = 1;
}

void cam_set_fov(int id, float fov_degrees) {
    Camera* c = cam_get(id);
    if (!c) return;
    if (fov_degrees < 1.0f)   fov_degrees = 1.0f;
    if (fov_degrees > 179.0f) fov_degrees = 179.0f;
    c->fov = fov_degrees;
}

void cam_set_zoom(int id, float zoom) {
    Camera* c = cam_get(id);
    if (!c) return;
    if (zoom < 0.01f) zoom = 0.01f;
    c->zoom = zoom;
}

void cam_set_clip(int id, float near_plane, float far_plane) {
    Camera* c = cam_get(id);
    if (!c) return;
    if (near_plane < 0.001f) near_plane = 0.001f;
    if (far_plane <= near_plane) far_plane = near_plane + 1.0f;
    c->near_plane = near_plane;
    c->far_plane  = far_plane;
}

void cam_set_aspect(int id, float w_scale, float h_scale) {
    Camera* c = cam_get(id);
    if (!c) return;
    c->aspect_w = w_scale;
    c->aspect_h = h_scale;
}

void cam_set_speed(int id, float move_speed, float turn_speed) {
    Camera* c = cam_get(id);
    if (!c) return;
    c->move_speed = move_speed;
    c->turn_speed = turn_speed;
}

void cam_set_resolution(int id, int width, int height) {
    Camera* c = cam_get(id);
    if (!c) return;
    if (width <= 0 || height <= 0) return;

    int count = width * height;
    Pixel* new_front = (Pixel*)calloc(count, sizeof(Pixel));
    Pixel* new_back  = (Pixel*)calloc(count, sizeof(Pixel));
    if (!new_front || !new_back) {
        free(new_front);
        free(new_back);
        return;   // keep old buffers on failure
    }

    free(c->front);
    free(c->back);
    c->front = new_front;
    c->back  = new_back;
    c->buf_w = width;
    c->buf_h = height;
}

// ─── Per-Frame Operations ────────────────────────────────────────────────────

void cam_update(int id) {
    Camera* c = cam_get(id);
    if (!c) return;

    // Clamp pitch
    if (c->pitch >  RENDER_MAX_PITCH) c->pitch =  RENDER_MAX_PITCH;
    if (c->pitch < -RENDER_MAX_PITCH) c->pitch = -RENDER_MAX_PITCH;

    // Trig cache
    c->cos_yaw   = cosf(c->yaw);
    c->sin_yaw   = sinf(c->yaw);
    c->cos_pitch = cosf(c->pitch);
    c->sin_pitch = sinf(c->pitch);
    c->cos_roll  = cosf(c->roll);
    c->sin_roll  = sinf(c->roll);

    // Forward: where the camera is looking (includes pitch)
    c->forward.x = -c->sin_yaw * c->cos_pitch;
    c->forward.y = -c->sin_pitch;
    c->forward.z =  c->cos_yaw * c->cos_pitch;

    // Right: perpendicular to forward, always horizontal
    c->right.x = c->cos_yaw;
    c->right.y = 0.0f;
    c->right.z = c->sin_yaw;

    // Up: cross product of right × forward, normalized
    c->up = vec3f_normalize(vec3f_cross(c->right, c->forward));

    c->cache_dirty = 0;
}

void cam_swap(int id) {
    Camera* c = cam_get(id);
    if (!c) return;

    // Pointer swap — no copying
    Pixel* tmp = c->back;
    c->back    = c->front;
    c->front   = tmp;

    // Clear new front buffer
    int count = c->buf_w * c->buf_h;
    for (int i = 0; i < count; i++) {
        c->front[i].valid = 0;
        c->front[i].depth = 1e30f;
        c->front[i].r = 0;
        c->front[i].g = 0;
        c->front[i].b = 0;
    }
}

int cam_set_pixel(int id, int x, int y, char ascii, int color, float depth) {
    Camera* c = cam_get(id);
    if (!c) return 0;
    if (x < 0 || y < 0 || x >= c->buf_w || y >= c->buf_h) return 0;

    int idx = y * c->buf_w + x;
    Pixel* p = &c->front[idx];

    // Z-test: only write if closer or slot is empty
    if (!p->valid || depth < p->depth) {
        p->ascii = ascii;
        p->color = color;
        p->depth = depth;
        p->valid = 1;
        // Derive RGB from the ANSI 16-color code
        render_ansi16_to_rgb(color, &p->r, &p->g, &p->b);
        return 1;
    }
    return 0;
}

int cam_set_pixel_rgb(int id, int x, int y, char ascii, int color,
                      unsigned char r, unsigned char g, unsigned char b,
                      float depth) {
    Camera* c = cam_get(id);
    if (!c) return 0;
    if (x < 0 || y < 0 || x >= c->buf_w || y >= c->buf_h) return 0;

    int idx = y * c->buf_w + x;
    Pixel* p = &c->front[idx];

    if (!p->valid || depth < p->depth) {
        p->ascii = ascii;
        p->color = color;
        p->r = r;
        p->g = g;
        p->b = b;
        p->depth = depth;
        p->valid = 1;
        return 1;
    }
    return 0;
}

Vec3f cam_project(int id, Vec3f world_pos) {
    Camera* c = cam_get(id);
    Vec3f fail = vec3f_make(-1.0f, -1.0f, -1.0f);
    if (!c) return fail;

    // Auto-update cache if dirty
    if (c->cache_dirty) cam_update(id);

    // ── Translate to camera space ────────────────────────────────────────
    float dx = world_pos.x - c->pos.x;
    float dy = world_pos.y - c->pos.y;
    float dz = world_pos.z - c->pos.z;

    // ── Rotate by -yaw (Y-axis) ─────────────────────────────────────────
    // cos(-a) = cos(a),  sin(-a) = -sin(a)
    float tx =  dx * c->cos_yaw + dz * c->sin_yaw;
    float tz = -dx * c->sin_yaw + dz * c->cos_yaw;
    dx = tx;
    dz = tz;

    // ── Rotate by -pitch (X-axis) ───────────────────────────────────────
    float ty =  dy * c->cos_pitch + dz * c->sin_pitch;
    tz       = -dy * c->sin_pitch + dz * c->cos_pitch;
    dy = ty;
    dz = tz;

    // ── Near-plane clamp ─────────────────────────────────────────────────
    if (dz < c->near_plane) dz = c->near_plane;

    // ── Perspective divide ───────────────────────────────────────────────
    float half_w  = c->buf_w * 0.5f;
    float half_h  = c->buf_h * 0.5f;
    float fov_tan = tanf((c->fov * 0.5f) * (float)M_PI / 180.0f);
    float scale   = c->zoom / fov_tan;

    float sx =  (dx / dz) * half_w * scale * c->aspect_w + half_w;
    float sy = -(dy / dz) * half_h * scale * c->aspect_h + half_h;

    return vec3f_make(sx, sy, dz);
}

// ─── Drawing Primitives ──────────────────────────────────────────────────────

// Helper: clamp int to [lo, hi]
static int clamp_i(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

// Helper: clamp float to [lo, hi]
static float clamp_f(float val, float lo, float hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

// Helper: distance from camera to a world-space point
static float cam_distance(Camera* c, Vec3f p) {
    return vec3f_length(vec3f_sub(p, c->pos));
}

// ── View-space helpers (for near-plane clipping) ─────────────────────────────

// Transform a world-space point into camera/view space (before projection).
static Vec3f cam_to_view(Camera* c, Vec3f world_pos) {
    float dx = world_pos.x - c->pos.x;
    float dy = world_pos.y - c->pos.y;
    float dz = world_pos.z - c->pos.z;

    // Rotate by -yaw (Y-axis)
    float tx =  dx * c->cos_yaw + dz * c->sin_yaw;
    float tz = -dx * c->sin_yaw + dz * c->cos_yaw;
    dx = tx;  dz = tz;

    // Rotate by -pitch (X-axis)
    float ty =  dy * c->cos_pitch + dz * c->sin_pitch;
    tz       = -dy * c->sin_pitch + dz * c->cos_pitch;

    return vec3f_make(dx, ty, tz);
}

// Project a view-space point to screen space (no near-plane clamping).
static Vec3f cam_project_view(Camera* c, Vec3f vp) {
    float dz = vp.z;
    if (dz < 0.001f) dz = 0.001f;  // safety against div-by-zero

    float half_w  = c->buf_w * 0.5f;
    float half_h  = c->buf_h * 0.5f;
    float fov_tan = tanf((c->fov * 0.5f) * (float)M_PI / 180.0f);
    float scale   = c->zoom / fov_tan;

    float sx =  (vp.x / dz) * half_w * scale * c->aspect_w + half_w;
    float sy = -(vp.y / dz) * half_h * scale * c->aspect_h + half_h;

    return vec3f_make(sx, sy, dz);
}

// Interpolate a UV coordinate
static Vec2f lerp_uv(Vec2f a, Vec2f b, float t) {
    Vec2f r;
    r.u = a.u + (b.u - a.u) * t;
    r.v = a.v + (b.v - a.v) * t;
    return r;
}

void draw_dot(RDot d) {
    int cid = cam_get_active_id();
    Camera* c = cam_get(cid);
    if (!c) return;

    // Far-plane culling
    if (cam_distance(c, d.pos) > c->far_plane) return;

    Vec3f proj = cam_project(cid, d.pos);

    // Behind camera
    if (proj.z < c->near_plane) return;

    int sx = (int)proj.x;
    int sy = (int)proj.y;
    cam_set_pixel(cid, sx, sy, d.ascii, d.color, proj.z);
}

void draw_edge(REdge e) {
    int cid = cam_get_active_id();
    Camera* c = cam_get(cid);
    if (!c) return;

    // Far-plane culling on midpoint
    Vec3f mid = vec3f_scale(vec3f_add(e.start, e.end), 0.5f);
    if (cam_distance(c, mid) > c->far_plane) return;

    Vec3f p0 = cam_project(cid, e.start);
    Vec3f p1 = cam_project(cid, e.end);

    // Both behind camera → skip
    if (p0.z < c->near_plane && p1.z < c->near_plane) return;

    // Bresenham's line algorithm with depth interpolation
    int x0 = (int)p0.x, y0 = (int)p0.y;
    int x1 = (int)p1.x, y1 = (int)p1.y;

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    // Total pixel steps for depth interpolation
    int steps = (dx > dy) ? dx : dy;
    if (steps == 0) steps = 1;
    int step = 0;

    while (1) {
        // Interpolate depth along the edge
        float t = (float)step / (float)steps;
        float depth = p0.z * (1.0f - t) + p1.z * t;

        cam_set_pixel(cid, x0, y0, e.ascii, e.color, depth);

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
        step++;
    }
}

// Internal: rasterize a single triangle (3 projected screen verts + depths).
// wp0/wp1/wp2 = world-space positions for lighting interpolation.
// face_normal  = face normal for diffuse lighting.
// Shared by draw_face.
static void rasterize_triangle(int cid, Camera* c,
    Vec3f sp0, Vec3f sp1, Vec3f sp2,   // screen-space {x, y, camZ}
    Vec3f wp0, Vec3f wp1, Vec3f wp2,   // world-space positions (for lighting)
    Vec3f face_normal,                  // face normal (for lighting)
    Vec2f uv0, Vec2f uv1, Vec2f uv2,
    int* texture, int tex_w, int tex_h,
    int flat_color, char ascii)
{
    // Reciprocal depths for perspective-correct interpolation
    float w0 = 1.0f / sp0.z;
    float w1 = 1.0f / sp1.z;
    float w2 = 1.0f / sp2.z;

    // Bounding box
    int min_x = (int)fminf(fminf(sp0.x, sp1.x), sp2.x);
    int max_x = (int)fmaxf(fmaxf(sp0.x, sp1.x), sp2.x);
    int min_y = (int)fminf(fminf(sp0.y, sp1.y), sp2.y);
    int max_y = (int)fmaxf(fmaxf(sp0.y, sp1.y), sp2.y);

    // Clamp to framebuffer
    min_x = clamp_i(min_x, 0, c->buf_w - 1);
    max_x = clamp_i(max_x, 0, c->buf_w - 1);
    min_y = clamp_i(min_y, 0, c->buf_h - 1);
    max_y = clamp_i(max_y, 0, c->buf_h - 1);

    // Signed area × 2
    float area = (sp1.x - sp0.x) * (sp2.y - sp0.y)
               - (sp2.x - sp0.x) * (sp1.y - sp0.y);
    if (fabsf(area) < 0.001f) return;  // degenerate

    float inv_area = 1.0f / area;

    // Pre-check if lighting is enabled
    int use_lighting = (g_light_fn != NULL);

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            // Barycentric coordinates
            float l1 = ((sp2.y - sp0.y) * (x - sp0.x) + (sp0.x - sp2.x) * (y - sp0.y)) * inv_area;
            float l2 = ((sp0.y - sp1.y) * (x - sp0.x) + (sp1.x - sp0.x) * (y - sp0.y)) * inv_area;
            float l0 = 1.0f - l1 - l2;

            if (l0 < 0 || l1 < 0 || l2 < 0) continue;

            // Perspective-correct weights
            float w_interp = l0 * w0 + l1 * w1 + l2 * w2;
            float depth = 1.0f / w_interp;

            float pc0 = (l0 * w0) / w_interp;
            float pc1 = (l1 * w1) / w_interp;
            float pc2 = (l2 * w2) / w_interp;

            // Base pixel color (before lighting)
            unsigned char base_r, base_g, base_b;
            int pixel_ansi = flat_color;

            // Perspective-correct texture mapping
            if (texture && tex_w > 0 && tex_h > 0) {
                float u = pc0 * uv0.u + pc1 * uv1.u + pc2 * uv2.u;
                float v = pc0 * uv0.v + pc1 * uv1.v + pc2 * uv2.v;

                u = clamp_f(u, 0.0f, 1.0f);
                v = clamp_f(v, 0.0f, 1.0f);

                int tx = clamp_i((int)(u * tex_w), 0, tex_w - 1);
                int ty = clamp_i((int)(v * tex_h), 0, tex_h - 1);
                int texel = texture[ty * tex_w + tx];

                base_r = (unsigned char)((texel >> 16) & 0xFF);
                base_g = (unsigned char)((texel >> 8) & 0xFF);
                base_b = (unsigned char)(texel & 0xFF);
                pixel_ansi = (texel >> 24) & 0xFF;
                if (pixel_ansi == 0) pixel_ansi = flat_color;
            } else {
                render_ansi16_to_rgb(flat_color, &base_r, &base_g, &base_b);
            }

            // Apply lighting if enabled
            if (use_lighting) {
                // Interpolate world position (perspective-correct)
                Vec3f world_pos;
                world_pos.x = pc0 * wp0.x + pc1 * wp1.x + pc2 * wp2.x;
                world_pos.y = pc0 * wp0.y + pc1 * wp1.y + pc2 * wp2.y;
                world_pos.z = pc0 * wp0.z + pc1 * wp1.z + pc2 * wp2.z;

                float lit_r, lit_g, lit_b;
                g_light_fn(world_pos, face_normal,
                           (float)base_r, (float)base_g, (float)base_b,
                           &lit_r, &lit_g, &lit_b);

                base_r = (unsigned char)(lit_r < 0 ? 0 : (lit_r > 255 ? 255 : lit_r));
                base_g = (unsigned char)(lit_g < 0 ? 0 : (lit_g > 255 ? 255 : lit_g));
                base_b = (unsigned char)(lit_b < 0 ? 0 : (lit_b > 255 ? 255 : lit_b));

                // Update ANSI-16 code to match the lit RGB so 16-color mode reflects lighting
                pixel_ansi = render_rgb_to_ansi16(base_r, base_g, base_b);
            }

            cam_set_pixel_rgb(cid, x, y, ascii, pixel_ansi,
                              base_r, base_g, base_b, depth);
        }
    }
}

void draw_face(RFace f) {
    int cid = cam_get_active_id();
    Camera* c = cam_get(cid);
    if (!c) return;

    // Far-plane culling on centroid
    Vec3f center = vec3f_scale(
        vec3f_add(vec3f_add(f.verts[0], f.verts[1]), f.verts[2]), 1.0f / 3.0f);
    if (cam_distance(c, center) > c->far_plane) return;

    // Auto-update cache if dirty
    if (c->cache_dirty) cam_update(cid);

    // Recompute face normal from world-space verts (entity may have rotated them)
    Vec3f fn_e1 = vec3f_sub(f.verts[1], f.verts[0]);
    Vec3f fn_e2 = vec3f_sub(f.verts[2], f.verts[0]);
    Vec3f face_n = vec3f_normalize(vec3f_cross(fn_e1, fn_e2));

    // Transform all 3 vertices to view (camera) space
    Vec3f vs[3];
    for (int i = 0; i < 3; i++)
        vs[i] = cam_to_view(c, f.verts[i]);

    // Classify vertices: in front of or behind the near plane
    float np = c->near_plane;
    int front_idx[3], back_idx[3];
    int nfront = 0, nback = 0;
    for (int i = 0; i < 3; i++) {
        if (vs[i].z >= np)
            front_idx[nfront++] = i;
        else
            back_idx[nback++] = i;
    }

    // All behind → discard
    if (nfront == 0) return;

    // All in front → no clipping needed
    if (nfront == 3) {
        Vec3f sp[3];
        for (int i = 0; i < 3; i++)
            sp[i] = cam_project_view(c, vs[i]);

        rasterize_triangle(cid, c,
            sp[0], sp[1], sp[2],
            f.verts[0], f.verts[1], f.verts[2], face_n,
            f.uvs[0], f.uvs[1], f.uvs[2],
            f.texture, f.tex_w, f.tex_h,
            f.color, f.ascii);
        return;
    }

    // ── Near-plane clipping ──────────────────────────────────────────────
    // Interpolate where an edge (from behind-vert A to front-vert B)
    // crosses the near plane z = np.

    if (nfront == 1) {
        // 1 vertex in front, 2 behind → clip to 1 smaller triangle
        int fi  = front_idx[0];
        int bi0 = back_idx[0];
        int bi1 = back_idx[1];

        float t0 = (np - vs[bi0].z) / (vs[fi].z - vs[bi0].z);
        float t1 = (np - vs[bi1].z) / (vs[fi].z - vs[bi1].z);

        Vec3f c0 = vec3f_add(vs[bi0], vec3f_scale(vec3f_sub(vs[fi], vs[bi0]), t0));
        Vec3f c1 = vec3f_add(vs[bi1], vec3f_scale(vec3f_sub(vs[fi], vs[bi1]), t1));

        Vec2f uv_c0 = lerp_uv(f.uvs[bi0], f.uvs[fi], t0);
        Vec2f uv_c1 = lerp_uv(f.uvs[bi1], f.uvs[fi], t1);

        // Interpolate world positions at clip points
        Vec3f wc0 = vec3f_add(f.verts[bi0], vec3f_scale(vec3f_sub(f.verts[fi], f.verts[bi0]), t0));
        Vec3f wc1 = vec3f_add(f.verts[bi1], vec3f_scale(vec3f_sub(f.verts[fi], f.verts[bi1]), t1));

        Vec3f sp_f  = cam_project_view(c, vs[fi]);
        Vec3f sp_c0 = cam_project_view(c, c0);
        Vec3f sp_c1 = cam_project_view(c, c1);

        rasterize_triangle(cid, c,
            sp_f, sp_c0, sp_c1,
            f.verts[fi], wc0, wc1, face_n,
            f.uvs[fi], uv_c0, uv_c1,
            f.texture, f.tex_w, f.tex_h,
            f.color, f.ascii);
    }
    else {  // nfront == 2
        // 2 vertices in front, 1 behind → clip to 2 triangles (a quad)
        int fi0 = front_idx[0];
        int fi1 = front_idx[1];
        int bi  = back_idx[0];

        float t0 = (np - vs[bi].z) / (vs[fi0].z - vs[bi].z);
        float t1 = (np - vs[bi].z) / (vs[fi1].z - vs[bi].z);

        Vec3f c0 = vec3f_add(vs[bi], vec3f_scale(vec3f_sub(vs[fi0], vs[bi]), t0));
        Vec3f c1 = vec3f_add(vs[bi], vec3f_scale(vec3f_sub(vs[fi1], vs[bi]), t1));

        Vec2f uv_c0 = lerp_uv(f.uvs[bi], f.uvs[fi0], t0);
        Vec2f uv_c1 = lerp_uv(f.uvs[bi], f.uvs[fi1], t1);

        // Interpolate world positions at clip points
        Vec3f wc0 = vec3f_add(f.verts[bi], vec3f_scale(vec3f_sub(f.verts[fi0], f.verts[bi]), t0));
        Vec3f wc1 = vec3f_add(f.verts[bi], vec3f_scale(vec3f_sub(f.verts[fi1], f.verts[bi]), t1));

        Vec3f sp_f0 = cam_project_view(c, vs[fi0]);
        Vec3f sp_f1 = cam_project_view(c, vs[fi1]);
        Vec3f sp_c0 = cam_project_view(c, c0);
        Vec3f sp_c1 = cam_project_view(c, c1);

        // Triangle 1: fi0, fi1, clip0
        rasterize_triangle(cid, c,
            sp_f0, sp_f1, sp_c0,
            f.verts[fi0], f.verts[fi1], wc0, face_n,
            f.uvs[fi0], f.uvs[fi1], uv_c0,
            f.texture, f.tex_w, f.tex_h,
            f.color, f.ascii);

        // Triangle 2: fi1, clip1, clip0
        rasterize_triangle(cid, c,
            sp_f1, sp_c1, sp_c0,
            f.verts[fi1], wc1, wc0, face_n,
            f.uvs[fi1], uv_c1, uv_c0,
            f.texture, f.tex_w, f.tex_h,
            f.color, f.ascii);
    }
}

// ─── Render Output ───────────────────────────────────────────────────────────

int render_diff(int cam_id, char* out, int out_size) {
    Camera* c = cam_get(cam_id);
    if (!c || !out || out_size < 1) return 0;

    int pos = 0;
    int w = c->buf_w;
    int h = c->buf_h;
    int mode = g_color_mode;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            Pixel cur  = c->front[idx];
            Pixel prev = c->back[idx];

            // Only emit ANSI if this cell changed
            int changed = (cur.valid != prev.valid) ||
                          (cur.valid && (cur.ascii != prev.ascii ||
                                        cur.color != prev.color ||
                                        cur.r != prev.r ||
                                        cur.g != prev.g ||
                                        cur.b != prev.b));

            if (!changed) continue;

            // Leave room for the longest escape sequence (~50 bytes for 24-bit)
            if (pos >= out_size - 60) break;

            if (cur.valid) {
                if (mode == RENDER_COLOR_24BIT) {
                    pos += snprintf(out + pos, out_size - pos,
                        "\x1b[%d;%dH\x1b[38;2;%d;%d;%dm%c",
                        y + 1, x + 1, cur.r, cur.g, cur.b, cur.ascii);
                } else if (mode == RENDER_COLOR_256) {
                    int idx256 = render_rgb_to_256(cur.r, cur.g, cur.b);
                    pos += snprintf(out + pos, out_size - pos,
                        "\x1b[%d;%dH\x1b[38;5;%dm%c",
                        y + 1, x + 1, idx256, cur.ascii);
                } else {
                    pos += snprintf(out + pos, out_size - pos,
                        "\x1b[%d;%dH\x1b[%dm%c",
                        y + 1, x + 1, cur.color, cur.ascii);
                }
            } else {
                pos += snprintf(out + pos, out_size - pos,
                    "\x1b[%d;%dH ", y + 1, x + 1);
            }
        }
    }

    if (pos < out_size) out[pos] = '\0';
    return pos;
}

void render_present(int cam_id) {
    Camera* c = cam_get(cam_id);
    if (!c) return;

    // Allocate output buffer: worst case ~50 bytes per changed pixel (24-bit mode)
    // +32 bytes for sync markers
    int max_bytes = c->buf_w * c->buf_h * 50 + 32;
    if (max_bytes < 4096) max_bytes = 4096;

    char* buf = (char*)malloc(max_bytes);
    if (!buf) return;

    // Begin synchronized output (DEC private mode 2026):
    // Terminal holds all rendering until the end marker, preventing tearing.
    int pos = 0;
    pos += snprintf(buf + pos, max_bytes - pos, "\x1b[?2026h");

    int len = render_diff(cam_id, buf + pos, max_bytes - pos);
    pos += len;

    // End synchronized output — terminal paints the whole frame at once
    pos += snprintf(buf + pos, max_bytes - pos, "\x1b[?2026l");

    if (pos > 0) {
#if defined(_WIN32)
        con_print(buf);
#else
        // Bypass stdio buffering — single write() syscall for minimal tearing
        fflush(stdout);
        (void)write(STDOUT_FILENO, buf, pos);
#endif
    }

    free(buf);
    cam_swap(cam_id);
}
