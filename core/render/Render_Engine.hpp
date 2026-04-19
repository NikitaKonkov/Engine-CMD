#if !defined(RENDER_ENGINE_HPP)
#define RENDER_ENGINE_HPP

// ═══════════════════════════════════════════════════════════════════════════════
// Render_Engine.hpp — Cross-platform ASCII 3D render system
//
// Camera-centric design: every camera owns a dynamically allocated double
// buffer.  Differential ANSI output compares front vs back and only redraws
// changed pixels — the same technique as the original engine but without
// static 50 MB arrays or platform headers.
//
// No Windows/Linux headers leak into this file.
// ═══════════════════════════════════════════════════════════════════════════════

#define RENDER_MAX_CAMERAS   16
#define RENDER_DEFAULT_FOV   90.0f
#define RENDER_MAX_PITCH     1.5f     // ~85 degrees — prevents gimbal flip

// ─── Vec3f ───────────────────────────────────────────────────────────────────

struct Vec3f {
    float x, y, z;
};

Vec3f vec3f_make   (float x, float y, float z);
Vec3f vec3f_add    (Vec3f a, Vec3f b);
Vec3f vec3f_sub    (Vec3f a, Vec3f b);
Vec3f vec3f_scale  (Vec3f v, float s);
float vec3f_dot    (Vec3f a, Vec3f b);
Vec3f vec3f_cross  (Vec3f a, Vec3f b);
float vec3f_length (Vec3f v);
Vec3f vec3f_normalize(Vec3f v);

// ─── Pixel ───────────────────────────────────────────────────────────────────

struct Pixel {
    char  ascii;       // character to draw
    int   color;       // ANSI color code (31 = red, 32 = green, …)
    float depth;       // Z-buffer value  (lower = closer)
    int   valid;       // 1 if occupied, 0 if empty
};

// ─── Primitives ──────────────────────────────────────────────────────────────

// UV coordinate pair for texture mapping
struct Vec2f {
    float u, v;
};

Vec2f vec2f_make(float u, float v);

// Single point in 3D space
struct RDot {
    Vec3f pos;
    char  ascii;
    int   color;
};

// Line segment between two vertices
struct REdge {
    Vec3f start, end;
    char  ascii;
    int   color;
};

// Triangle with per-vertex UVs, optional texture, and normal
struct RFace {
    Vec3f   verts[3];        // 3 vertices (always triangles — quads split externally)
    Vec2f   uvs[3];          // per-vertex texture coordinates
    Vec3f   normal;          // face normal  (set by rface_calc_normal, or manually)
    int*    texture;         // pointer to ANSI-color palette array (NULL = no texture)
    int     tex_w, tex_h;    // texture dimensions
    int     color;           // flat color if no texture
    char    ascii;           // ASCII character for the face
};

// Build helpers
RDot  rdot_make  (Vec3f pos, char ascii, int color);
REdge redge_make (Vec3f start, Vec3f end, char ascii, int color);
RFace rface_make (Vec3f v0, Vec3f v1, Vec3f v2, int color, char ascii);
RFace rface_make_textured(Vec3f v0, Vec3f v1, Vec3f v2,
                          Vec2f uv0, Vec2f uv1, Vec2f uv2,
                          int* texture, int tex_w, int tex_h, char ascii);

// Build a quad as two triangles.  Writes into out[0] and out[1].
void  rface_make_quad(Vec3f v0, Vec3f v1, Vec3f v2, Vec3f v3,
                      int color, char ascii, RFace out[2]);
void  rface_make_quad_textured(Vec3f v0, Vec3f v1, Vec3f v2, Vec3f v3,
                               int* texture, int tex_w, int tex_h,
                               char ascii, RFace out[2]);

// Compute and store the face normal from its 3 vertices (cross product).
void  rface_calc_normal(RFace* f);

// ─── Camera ──────────────────────────────────────────────────────────────────

struct Camera {
    int  id;           // slot index (0 … RENDER_MAX_CAMERAS-1), -1 = unused

    // ── Transform ────────────────────────────────────────────────────────
    Vec3f pos;         // world position
    float yaw;         // rotation around Y  (radians, left / right)
    float pitch;       // rotation around X  (radians, up / down)
    float roll;        // rotation around Z  (radians, tilt)

    // ── Projection ───────────────────────────────────────────────────────
    float fov;         // horizontal field-of-view in degrees
    float zoom;        // multiplier (1.0 = normal, 2.0 = 2× tighter)
    float near_plane;  // near clip distance
    float far_plane;   // far  clip distance
    float aspect_w;    // console char width  scale (default 1.0)
    float aspect_h;    // console char height scale (default 2.0)

    // ── Movement ─────────────────────────────────────────────────────────
    float move_speed;
    float turn_speed;

    // ── Derived (recomputed by cam_update) ───────────────────────────────
    Vec3f forward;     // unit vector: where the camera looks
    Vec3f right;       // unit vector: camera's right
    Vec3f up;          // unit vector: camera's up

    // ── Trig cache (recomputed by cam_update) ────────────────────────────
    float cos_yaw,   sin_yaw;
    float cos_pitch,  sin_pitch;
    float cos_roll,   sin_roll;
    int   cache_dirty; // 1 = needs recompute

    // ── Double buffer (dynamically allocated, owned by this camera) ──────
    int    buf_w;      // framebuffer width  in characters
    int    buf_h;      // framebuffer height in characters
    Pixel* front;      // current frame  (being drawn into)
    Pixel* back;       // previous frame (used for diff comparison)
};

// ─── Camera Lifecycle ────────────────────────────────────────────────────────

// Create a camera with the given framebuffer resolution.
// Returns the camera id (>= 0) or -1 on failure.
// The first camera created becomes the active camera automatically.
int      cam_create      (int buf_width, int buf_height);

// Destroy a single camera and free its buffers.
void     cam_destroy     (int id);

// Destroy every camera and reset the pool.
void     cam_destroy_all (void);

// ─── Camera Access ───────────────────────────────────────────────────────────

// Get a camera by id.  Returns NULL if id is invalid or slot is empty.
Camera*  cam_get          (int id);

// Set / get the active camera (one at a time).
void     cam_set_active   (int id);
int      cam_get_active_id(void);
Camera*  cam_get_active   (void);

// ─── Camera Configuration ────────────────────────────────────────────────────

void cam_set_position   (int id, float x, float y, float z);
void cam_set_rotation   (int id, float yaw, float pitch, float roll);
void cam_set_fov        (int id, float fov_degrees);
void cam_set_zoom       (int id, float zoom);
void cam_set_clip       (int id, float near_plane, float far_plane);
void cam_set_aspect     (int id, float w_scale, float h_scale);
void cam_set_speed      (int id, float move_speed, float turn_speed);

// Resize the camera's framebuffers.  Old contents are discarded.
void cam_set_resolution (int id, int width, int height);

// ─── Per-Frame Operations ────────────────────────────────────────────────────

// Recompute forward/right/up vectors and trig cache.
// Call once per frame after changing position or rotation.
void  cam_update    (int id);

// Swap front ↔ back and clear the new front buffer.
// Call at the start of each frame before drawing.
void  cam_swap      (int id);

// Write a pixel into the camera's front buffer with Z-test.
// Returns 1 if the pixel was written, 0 if it was occluded or out of bounds.
int   cam_set_pixel (int id, int x, int y, char ascii, int color, float depth);

// Project a world-space point through this camera's view + perspective.
// Returns screen-space {x, y, depth}.  depth < near_plane means behind camera.
Vec3f cam_project   (int id, Vec3f world_pos);

// ─── Drawing Primitives ──────────────────────────────────────────────────────
// All draw functions operate on the active camera (cam_get_active_id()).
// They project, clip, Z-test, and rasterize directly into the camera's front
// buffer.  Call between cam_swap() and render_present().

// Draw a single dot (one pixel after projection).
void draw_dot  (RDot d);

// Draw a line using Bresenham's algorithm with per-pixel depth interpolation.
void draw_edge (REdge e);

// Draw a filled triangle with perspective-correct barycentric rasterization.
// If the face has a texture, perspective-correct UV interpolation is used.
void draw_face (RFace f);

// ─── Render Output ───────────────────────────────────────────────────────────

// Build a diff string comparing front vs back.  Only changed pixels produce
// ANSI cursor-move + color + char sequences.  Writes into `out` (up to
// `out_size` bytes).  Returns the number of bytes written (excluding '\0').
int  render_diff    (int cam_id, char* out, int out_size);

// Convenience: render_diff → con_print → cam_swap, all in one call.
// Requires Console_Manager to be initialized.
void render_present (int cam_id);

#endif // RENDER_ENGINE_HPP
