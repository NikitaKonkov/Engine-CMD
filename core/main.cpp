#include "console/Console_Manager.hpp"
#include "input/Input_Manager.hpp"
#include "clock/Clock_Manager.hpp"
#include "render/Render_Engine.hpp"
#include "render/tinyrenderer-master/model.h"

#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string>

// Cross-platform sleep
#if defined(_WIN32)
  #include <windows.h>
  #define sleep_ms(ms) Sleep(ms)
#else
  #include <unistd.h>
  #define sleep_ms(ms) usleep((ms) * 1000)
#endif

static const float PI = 3.14159265f;

// ─── Path resolution ─────────────────────────────────────────────────────────

// Build an absolute path relative to the project root (exe's parent directory).
// The exe lives in exe64/ or exe32/, so project root is one level up.
static std::string project_path(const char* relative) {
#if defined(_WIN32)
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    // Strip exe filename → exe64 dir
    char* slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';
    // Strip exe64 folder → project root
    slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';
    std::string root(buf);
    root += "\\";
    root += relative;
    return root;
#else
    // On Linux/Termux: CWD = project root; convert backslashes to forward slashes
    std::string path(relative);
    for (char& c : path) if (c == '\\') c = '/';
    return path;
#endif
}

// ─── ANSI color mapping ──────────────────────────────────────────────────────

// Map an RGB pixel to the nearest 16-color ANSI foreground code (30-37, 90-97)
static int rgb_to_ansi(int r, int g, int b) {
    static const int codes[] = {30,31,32,33,34,35,36,37,90,91,92,93,94,95,96,97};
    static const int lut[][3] = {
        {  0,  0,  0}, // 30 black
        {170,  0,  0}, // 31 red
        {  0,170,  0}, // 32 green
        {170, 85,  0}, // 33 dark yellow / brown
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
    int best = 0, best_d = INT_MAX;
    for (int i = 0; i < 16; i++) {
        int dr = r - lut[i][0], dg = g - lut[i][1], db = b - lut[i][2];
        int d = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return codes[best];
}

// Convert a TGA diffuse texture into an int* ANSI color map (caller must free)
static int* tga_to_ansi_texture(const TGAImage& img, int* out_w, int* out_h) {
    int w = img.width(), h = img.height();
    *out_w = w;  *out_h = h;
    if (w == 0 || h == 0) return NULL;
    int* tex = (int*)malloc(w * h * sizeof(int));
    if (!tex) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            TGAColor c = img.get(x, y);
            tex[y * w + x] = rgb_to_ansi(c[2], c[1], c[0]); // TGAColor is BGRA
        }
    }
    return tex;
}

// ─── Scene builders ──────────────────────────────────────────────────────────

// Solid cube: 6 quads → 12 triangles
static void build_solid_cube(float cx, float cy, float cz, float s,
                              int color, RFace faces[12]) {
    float hs = s * 0.5f;
    Vec3f v[8] = {
        vec3f_make(cx-hs, cy-hs, cz-hs), // 0 left-bottom-front
        vec3f_make(cx+hs, cy-hs, cz-hs), // 1 right-bottom-front
        vec3f_make(cx+hs, cy+hs, cz-hs), // 2 right-top-front
        vec3f_make(cx-hs, cy+hs, cz-hs), // 3 left-top-front
        vec3f_make(cx-hs, cy-hs, cz+hs), // 4 left-bottom-back
        vec3f_make(cx+hs, cy-hs, cz+hs), // 5 right-bottom-back
        vec3f_make(cx+hs, cy+hs, cz+hs), // 6 right-top-back
        vec3f_make(cx-hs, cy+hs, cz+hs), // 7 left-top-back
    };
    rface_make_quad(v[4], v[5], v[6], v[7], color, '#', faces +  0); // +Z front
    rface_make_quad(v[1], v[0], v[3], v[2], color, '#', faces +  2); // -Z back
    rface_make_quad(v[0], v[4], v[7], v[3], color, '#', faces +  4); // -X left
    rface_make_quad(v[5], v[1], v[2], v[6], color, '#', faces +  6); // +X right
    rface_make_quad(v[3], v[7], v[6], v[2], color, '#', faces +  8); // +Y top
    rface_make_quad(v[0], v[1], v[5], v[4], color, '#', faces + 10); // -Y bottom
}

// Sphere as a dot cloud on the surface
// Returns the number of dots written (= (rings+1)*sectors)
static int build_sphere_dots(float cx, float cy, float cz, float r,
                              int rings, int sectors,
                              int color, char ch, RDot* out) {
    int idx = 0;
    for (int i = 0; i <= rings; i++) {
        float phi = PI * i / rings;
        for (int j = 0; j < sectors; j++) {
            float theta = 2.0f * PI * j / sectors;
            float x = cx + r * sinf(phi) * cosf(theta);
            float y = cy + r * cosf(phi);
            float z = cz + r * sinf(phi) * sinf(theta);
            out[idx++] = rdot_make(vec3f_make(x, y, z), ch, color);
        }
    }
    return idx;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
    con_init();
    con_cursor_hide();
    con_clear();

    // ── Camera ───────────────────────────────────────────────────────────────
    int w, h;
    con_get_size(&w, &h);
    int cam = cam_create(w, h);
    cam_set_aspect(cam, 1.0f, 2.0f);   // compensate for tall console chars
    cam_set_clip  (cam, 0.5f, 500.0f);
    cam_set_fov   (cam, 90.0f);

    // ── Scene ────────────────────────────────────────────────────────────────
    // Red solid cube at origin
    RFace cube[12];
    build_solid_cube(0, 0, 0, 12, 31, cube);   // 31 = red

    // Green dot sphere to the right of the cube
    const int RINGS   = 12;
    const int SECTORS = 24;
    const int NDOTS   = (RINGS + 1) * SECTORS;  // 312
    RDot sphere[NDOTS];
    int dot_count = build_sphere_dots(22, 0, 0, 7, RINGS, SECTORS, 32, 'O', sphere); // 32 = green

    // ── Diablo3 model ────────────────────────────────────────────────────────
    std::string mdl_path = project_path("core\\render\\tinyrenderer-master\\obj\\diablo3_pose\\diablo3_pose.obj");
    Model diablo(mdl_path);
    int nfaces_mdl = diablo.nfaces();

    // Convert diffuse TGA → ANSI color texture
    int ansi_tw = 0, ansi_th = 0;
    int* ansi_tex = tga_to_ansi_texture(diablo.diffuse(), &ansi_tw, &ansi_th);

    // Build RFace array from model triangles, scaled & positioned
    const float MDL_SCALE = 15.0f;
    const float MDL_X = -22.0f;  // left of the cube
    const float MDL_Y = -12.0f;  // feet roughly at ground level
    const float MDL_Z = 0.0f;

    RFace* mdl_faces = NULL;
    if (nfaces_mdl > 0) {
        mdl_faces = new RFace[nfaces_mdl];
        for (int i = 0; i < nfaces_mdl; i++) {
            vec4 v0 = diablo.vert(i, 0);
            vec4 v1 = diablo.vert(i, 1);
            vec4 v2 = diablo.vert(i, 2);
            vec2 uv0 = diablo.uv(i, 0);
            vec2 uv1 = diablo.uv(i, 1);
            vec2 uv2 = diablo.uv(i, 2);

            Vec3f a = vec3f_make(MDL_X + (float)v0.x * MDL_SCALE,
                                 MDL_Y + (float)v0.y * MDL_SCALE,
                                         (float)v0.z * MDL_SCALE + MDL_Z);
            Vec3f b = vec3f_make(MDL_X + (float)v1.x * MDL_SCALE,
                                 MDL_Y + (float)v1.y * MDL_SCALE,
                                         (float)v1.z * MDL_SCALE + MDL_Z);
            Vec3f cc = vec3f_make(MDL_X + (float)v2.x * MDL_SCALE,
                                  MDL_Y + (float)v2.y * MDL_SCALE,
                                          (float)v2.z * MDL_SCALE + MDL_Z);

            if (ansi_tex) {
                mdl_faces[i] = rface_make_textured(a, b, cc,
                    vec2f_make((float)uv0.x, (float)uv0.y),
                    vec2f_make((float)uv1.x, (float)uv1.y),
                    vec2f_make((float)uv2.x, (float)uv2.y),
                    ansi_tex, ansi_tw, ansi_th, '#');
            } else {
                mdl_faces[i] = rface_make(a, b, cc, 31, '#'); // red fallback
            }
        }
    }

    // ── Loop ─────────────────────────────────────────────────────────────────
    int clk = clock_create(60, "demo");
    const float ORBIT_R = 45.0f;
    const float ORBIT_H = 12.0f;
    float angle = 0.0f;

    while (!input_key_held(VK_ESCAPE_)) {
        if (!clock_sync(clk)) { sleep_ms(1); continue; }

        // Resize guard
        con_get_size(&w, &h);
        if (w != cam_get(cam)->buf_w || h != cam_get(cam)->buf_h) {
            cam_set_resolution(cam, w, h);
            con_clear();
        }

        // Orbit around origin
        angle += 0.015f;
        float cx = ORBIT_R * cosf(angle);
        float cz = ORBIT_R * sinf(angle);
        cam_set_position(cam, cx, ORBIT_H, cz);

        // ── Yaw: look toward origin ──────────────────────────────────────
        // forward.x = -sin(yaw), forward.z = cos(yaw).
        // To face origin from (cx, H, cz): forward ∝ (-cx, -, -cz)
        //   -sin(yaw) = -cos(angle)  →  sin(yaw) = cos(angle)
        //    cos(yaw) = -sin(angle)
        // → yaw = π/2 + angle
        float yaw   = PI * 0.5f + angle;

        // ── Pitch: tilt down toward origin ───────────────────────────────
        // Positive pitch → forward.y = -sin(pitch) goes negative (down).
        float pitch = atan2f(ORBIT_H, ORBIT_R);   // ~0.26 rad

        cam_set_rotation(cam, yaw, pitch, 0.0f);
        cam_update(cam);

        // Draw scene (no manual cam_swap — render_present handles it)
        for (int i = 0; i < 12;         i++) draw_face(cube[i]);
        for (int i = 0; i < dot_count;  i++) draw_dot(sphere[i]);
        for (int i = 0; i < nfaces_mdl; i++) draw_face(mdl_faces[i]);

        // HUD
        Camera* c = cam_get(cam);
        con_move(1, 1);
        con_printf(COL_BR_YELLOW "cam(%.0f,%.0f,%.0f) yaw=%.2f  faces=%d  ESC=quit" COL_RESET,
                   c->pos.x, c->pos.y, c->pos.z, c->yaw, nfaces_mdl);

        render_present(cam);  // diff front vs back → ANSI output → swap
    }

    delete[] mdl_faces;
    free(ansi_tex);
    cam_destroy_all();
    con_cursor_show();
    con_clear();
    return 0;
}

