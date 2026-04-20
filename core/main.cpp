#include "console/Console_Manager.hpp"
#include "input/Input_Manager.hpp"
#include "clock/Clock_Manager.hpp"
#include "render/Render_Engine.hpp"
#include "render/tinyrenderer-master/model.h"
#include "ui/UI_system.hpp"

#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string>

// Modular entity system + shader effects
#include "render/module/Entity.h"
#include "render/module/Shader_Depth.h"
#include "render/module/Shader_Wave.h"
#include "render/module/Shader_Rotate.h"
#include "render/module/Shader_Splat.h"
#include "render/module/Light.h"

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

// Convert a TGA diffuse texture into a packed RGB+ANSI color map (caller must free)
// Each int: bits 24-31 = ANSI 16-color code, bits 16-23 = R, 8-15 = G, 0-7 = B
static int* tga_to_rgb_texture(const TGAImage& img, int* out_w, int* out_h) {
    int w = img.width(), h = img.height();
    *out_w = w;  *out_h = h;
    if (w == 0 || h == 0) return NULL;
    int* tex = (int*)malloc(w * h * sizeof(int));
    if (!tex) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            TGAColor c = img.get(x, y);
            int r = c[2], g = c[1], b = c[0]; // TGAColor is BGRA
            int ansi = rgb_to_ansi(r, g, b);
            tex[y * w + x] = (ansi << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return tex;
}

// ─── Scene builders ──────────────────────────────────────────────────────────

// Plane no Texture, just a flat gray face for shadow testing (shader will darken it further)
static void build_plane(float cx, float cy, float cz, float s,
                         int color, RFace* out) {
    float hs = s * 0.5f;
    Vec3f v[4] = {
        vec3f_make(cx-hs, cy, cz-hs),
        vec3f_make(cx+hs, cy, cz-hs),
        vec3f_make(cx+hs, cy, cz+hs),
        vec3f_make(cx-hs, cy, cz+hs),
    };
    // Wind counter-clockwise so the face normal points UP (+Y)
    rface_make_quad(v[0], v[3], v[2], v[1], color, '+', out);
}


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
    rface_make_quad(v[4], v[5], v[6], v[7], 36, 'A', faces +  0); // +Z front
    rface_make_quad(v[1], v[0], v[3], v[2], 31, 'B', faces +  2); // -Z back
    rface_make_quad(v[0], v[4], v[7], v[3], 32, 'C', faces +  4); // -X left
    rface_make_quad(v[5], v[1], v[2], v[6], 33, 'D', faces +  6); // +X right
    rface_make_quad(v[3], v[7], v[6], v[2], 34, 'E', faces +  8); // +Y top
    rface_make_quad(v[0], v[1], v[5], v[4], 35, 'F', faces + 10); // -Y bottom
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

// (Gaussian splat helpers moved to render/modul/Shader_Splat.h)

// ─── Shadow Resolution String (for UI) ────────────────────────────────────────

// Global that UI_system.cpp reads each frame to display current shadow resolution
const char* g_shadow_resolution_str = "HIGH";

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

    // ── Entities ─────────────────────────────────────────────────────────────
    // Face-Plane at the bottom - test shadow implementation (no shader, just a flat gray face)
    int ent_plane = entity_create();
    {
        RFace tmp[2];
        build_plane(0, -0.01f, -10.0f, 100, 90, tmp);
        entity_add_faces(ent_plane, tmp, 2);
        entity_move(ent_plane, 0.0f, -20.0f, 0.0f);
    }

    // Cube at origin — rotation shader: ASCII chars change by viewing angle
    int ent_cube = entity_create();
    {
        RFace tmp[12];
        build_solid_cube(0, 0, 0, 12, 31, tmp);
        entity_add_faces(ent_cube, tmp, 12);
        entity_set_shader(ent_cube, shader_rotate);
    }

    // Dot sphere to the right — depth shader: chars/colors fade with distance
    int ent_sphere = entity_create();
    {
        const int RINGS   = 12*2;
        const int SECTORS = 24*2;
        const int NDOTS   = (RINGS + 1) * SECTORS;  // 312
        RDot tmp[NDOTS];
        int n = build_sphere_dots(0, 0, 0, 7, RINGS, SECTORS, 32, 'O', tmp);
        entity_add_dots(ent_sphere, tmp, n);
        entity_set_pos(ent_sphere, 22.0f, 0.0f, 0.0f);
        entity_set_shader(ent_sphere, shader_depth);
    }

    // Diablo3 model to the left — textured (no extra shader needed)
    int* ansi_tex = NULL;
    int ansi_tw = 0, ansi_th = 0;
    int ent_diablo = entity_create();
    {
        std::string mdl_path = project_path("core\\render\\tinyrenderer-master\\obj\\diablo3_pose\\diablo3_pose.obj");
        Model diablo(mdl_path);
        int nfaces = diablo.nfaces();
        ansi_tex = tga_to_rgb_texture(diablo.diffuse(), &ansi_tw, &ansi_th);

        const float S = 15.0f;
        for (int i = 0; i < nfaces; i++) {
            vec4 v0 = diablo.vert(i, 0);
            vec4 v1 = diablo.vert(i, 1);
            vec4 v2 = diablo.vert(i, 2);
            vec2 uv0 = diablo.uv(i, 0);
            vec2 uv1 = diablo.uv(i, 1);
            vec2 uv2 = diablo.uv(i, 2);

            // Local space — entity position handles world offset
            Vec3f a  = vec3f_make((float)v0.x * S, (float)v0.y * S, (float)v0.z * S);
            Vec3f b  = vec3f_make((float)v1.x * S, (float)v1.y * S, (float)v1.z * S);
            Vec3f cc = vec3f_make((float)v2.x * S, (float)v2.y * S, (float)v2.z * S);

            if (ansi_tex)
                entity_add_face(ent_diablo, rface_make_textured(a, b, cc,
                    vec2f_make((float)uv0.x, (float)uv0.y),
                    vec2f_make((float)uv1.x, (float)uv1.y),
                    vec2f_make((float)uv2.x, (float)uv2.y),
                    ansi_tex, ansi_tw, ansi_th, '@'));
            else
                entity_add_face(ent_diablo, rface_make(a, b, cc, 95, '@'));
        }
        entity_set_pos(ent_diablo, -22.0f, -12.0f, 0.0f);
    }

    // Gaussian splats — one near each model, with pulsing animation
    srand(42);
    int ent_splat0 = entity_create();
    splat_build(ent_splat0, 300, 3.5f, 3.5f, 3.5f, 96);   // bright cyan
    entity_set_pos(ent_splat0, -13.0f, 9.0f, -9.0f);
    shader_splat_pulse_attach(ent_splat0, 0.5f, 2.0f);

    int ent_splat1 = entity_create();
    splat_build(ent_splat1, 300, 3.5f, 3.5f, 3.5f, 93);   // bright yellow
    entity_set_pos(ent_splat1, 30.0f, 9.0f, -9.0f);
    shader_splat_pulse_attach(ent_splat1, 0.5f, 2.0f);

    int ent_splat2 = entity_create();
    splat_build(ent_splat2, 300, 3.5f, 3.5f, 3.5f, 95);   // bright magenta
    entity_set_pos(ent_splat2, -33.0f, 6.0f, 9.0f);
    shader_splat_pulse_attach(ent_splat2, 0.5f, 2.0f);

    // ── Lights ────────────────────────────────────────────────────────────

    // Radial white light centered above the models
    int light0 = light_create();
    light_set_type(light0, LIGHT_RADIAL);
    light_set_pos(light0, 0.0f, 35.0f, 0.0f);
    light_set_color(light0, 255, 255, 255);
    light_set_intensity(light0, 1.8f);
    light_set_range(light0, 200.0f);

    // Register the lighting callback so the rasterizer uses it for faces
    render_set_light_fn(light_compute_callback);

    // ── Loop ─────────────────────────────────────────────────────────────────
    int clk = clock_create(144, "demo");

    // Flying camera state
    float cam_x = 0.0f, cam_y = 12.0f, cam_z = 45.0f;
    float cam_yaw = PI, cam_pitch = 0.0f;
    const float MOVE_SPEED = 0.5f;
    const float TURN_SPEED = 0.04f;

    // Mouse look state (Windows only — on Linux input_mouse_get returns -1)
    const int   MOUSE_CENTER_X  = 200;
    const int   MOUSE_CENTER_Y  = 200;
    const float MOUSE_SENS      = 0.005f;
    int mouse_ok = 0;
    {
        int mx, my;
        input_mouse_get(&mx, &my);
        if (mx >= 0 && my >= 0) {
            input_mouse_set(MOUSE_CENTER_X, MOUSE_CENTER_Y);
            mouse_ok = 1;
        }
    }

    while (1) {
        input_poll();  // drain stdin once per frame (Linux)
        if (input_key_held(VK_ESCAPE_)) break;
        if (!clock_sync(clk)) { sleep_ms(1); continue; }

        // ── Color mode toggle: ALT + C ──────────────────────────────────
        {
            static int alt_c_prev = 0;
            int alt_c_now = input_keys_held(2, VK_ALT_, VK_C_);
            if (alt_c_now && !alt_c_prev)
                render_cycle_color_mode();
            alt_c_prev = alt_c_now;
        }

        // ── Shadow resolution toggle: ALT + X ────────────────────────────
        {
            static int alt_x_prev = 0;
            int alt_x_now = input_keys_held(2, VK_ALT_, VK_X_);
            if (alt_x_now && !alt_x_prev) {
                light_cycle_shadow_resolution();
                g_shadow_resolution_str = light_get_shadow_resolution_str();
            }
            alt_x_prev = alt_x_now;
        }

        // Resize guard
        con_get_size(&w, &h);
        if (w != cam_get(cam)->buf_w || h != cam_get(cam)->buf_h) {
            cam_set_resolution(cam, w, h);
            con_clear();
        }

        // ── Mouse look (yaw + pitch) ────────────────────────────────────
        if (mouse_ok) {
            int mx, my;
            input_mouse_get(&mx, &my);
            int dx = mx - MOUSE_CENTER_X;
            int dy = my - MOUSE_CENTER_Y;
            if (dx || dy) {
                cam_yaw   -= dx * MOUSE_SENS;
                cam_pitch += dy * MOUSE_SENS;
                // Clamp pitch to ~85 degrees
                if (cam_pitch >  1.5f) cam_pitch =  1.5f;
                if (cam_pitch < -1.5f) cam_pitch = -1.5f;
            }
            input_mouse_set(MOUSE_CENTER_X, MOUSE_CENTER_Y);
        }

        // ── Keyboard rotation fallback: Q/E = yaw left/right ───────────
        if (input_key_held(VK_Q_)) cam_yaw   -= TURN_SPEED;
        if (input_key_held(VK_E_)) cam_yaw   += TURN_SPEED;

        // ── Forward/right vectors from yaw ──────────────────────────────
        float fwd_x = -sinf(cam_yaw);
        float fwd_z =  cosf(cam_yaw);
        float rgt_x =  cosf(cam_yaw);
        float rgt_z =  sinf(cam_yaw);

        // ── Movement: WASD = forward/left/back/right ────────────────────
        if (input_key_held(VK_W_)) { cam_x += fwd_x * MOVE_SPEED; cam_z += fwd_z * MOVE_SPEED; }
        if (input_key_held(VK_S_)) { cam_x -= fwd_x * MOVE_SPEED; cam_z -= fwd_z * MOVE_SPEED; }
        if (input_key_held(VK_A_)) { cam_x -= rgt_x * MOVE_SPEED; cam_z -= rgt_z * MOVE_SPEED; }
        if (input_key_held(VK_D_)) { cam_x += rgt_x * MOVE_SPEED; cam_z += rgt_z * MOVE_SPEED; }

        // ── Vertical: Space = up, Ctrl = down ───────────────────────────
        if (input_key_held(VK_SPACE_))   cam_y += MOVE_SPEED;
        if (input_key_held(VK_SHIFT_)) cam_y -= MOVE_SPEED;

        cam_set_position(cam, cam_x, cam_y, cam_z);
        cam_set_rotation(cam, cam_yaw, cam_pitch, 0.0f);
        cam_update(cam);

        // Slowly rotate the cube to show per-entity transforms
        entity_rotate(ent_cube, 0.01f, 0.005f, 0.0f);

        // Build shadow maps from all entity faces (faces only — dots/edges excluded)
        light_build_shadows();

        // Draw all entities (shaders → local-to-world → draw)
        entity_draw_all(1.0f / 60.0f);

        // HUD overlay (drawn into camera buffer at depth 0)
        ui_draw(cam, clk);

        render_present(cam);  // diff front vs back → ANSI output → swap
    }

    entity_destroy_all();
    light_destroy_all();
    free(ansi_tex);
    cam_destroy_all();
    con_cursor_show();
    con_clear();
    return 0;
}

