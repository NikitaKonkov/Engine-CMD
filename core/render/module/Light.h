#ifndef LIGHT_H
#define LIGHT_H

// ═══════════════════════════════════════════════════════════════════════════════
// Light.h — Light source pool with shadow mapping for the ASCII 3D engine
//
// Two light types:
//   LIGHT_RADIAL  — omnidirectional point light (spherical shadow map)
//   LIGHT_FOCUSED — spotlight with direction + cone angle (perspective shadow map)
//
// Only faces cast and receive shadows.  Dots and edges are excluded entirely.
//
// Usage:
//   int lid = light_create();
//   light_set_type(lid, LIGHT_RADIAL);
//   light_set_pos(lid, 0, 20, 0);
//   light_set_color(lid, 255, 240, 200);
//   light_set_intensity(lid, 1.0f);
//   light_set_range(lid, 100.0f);
//   ...
//   light_shadow_clear_all();
//   entity_shadow_pass();        // rasterize all entity faces into shadow maps
//   entity_draw_all(dt);         // normal render (lighting applied inside rasterizer)
//   light_destroy_all();
//
// Include from ONE .cpp file only (contains static pool storage).
// ═══════════════════════════════════════════════════════════════════════════════

#include "../Render_Engine.hpp"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Constants ───────────────────────────────────────────────────────────────

#define LIGHT_MAX           16
#define LIGHT_RADIAL        1     // omnidirectional point light
#define LIGHT_FOCUSED       0     // spotlight with direction + cone

#define LIGHT_AMBIENT       0.15f // minimum ambient light (shadow areas)

// ─── Dynamic Shadow Resolution ────────────────────────────────────────────────
// Global shadow map dimensions (can be changed dynamically with cycle function)

static int g_shadow_cube_size   = 128;  // per-face for radial (128, 64, 32)
static int g_shadow_map_w       = 128;  // for focused (128, 64, 32)
static int g_shadow_map_h       = 64;


static const int g_shadow_preset_count = 5;
// Resolution presets: {cube_size, focused_w, focused_h}
static const int g_shadow_presets[][g_shadow_preset_count] = {
    {512, 512, 256},  // superhot (not recommended — very slow)
    {256, 256, 128},  // extreme (not recommended — very slow)
    {128, 128, 64},   // high
    {64,  64,  32},   // medium
    {32,  32,  16},   // low
};

static int g_shadow_preset_idx = 0;  // start at high

// ─── Light Struct ────────────────────────────────────────────────────────────

struct Light {
    int   id;            // pool slot, -1 = unused
    int   enabled;

    int   type;          // LIGHT_RADIAL or LIGHT_FOCUSED
    Vec3f pos;           // world position
    Vec3f dir;           // normalized direction (LIGHT_FOCUSED only)
    float cone_angle;    // half-angle in radians (LIGHT_FOCUSED only)

    float intensity;     // brightness multiplier (0..N, typically 1.0)
    float range;         // max light distance

    unsigned char r, g, b; // light color

    // ── Shadow map ───────────────────────────────────────────────────────
    // Radial:  spherical depth map  (theta,phi) → depth
    // Focused: perspective depth map from the light's POV
    float* shadow_map;
    int    shadow_w, shadow_h;

    // ── Focused: cached projection params ────────────────────────────────
    float dir_yaw, dir_pitch;
    float cos_yaw, sin_yaw;
    float cos_pitch, sin_pitch;
};

// ─── Pool ────────────────────────────────────────────────────────────────────

static Light  g_lights[LIGHT_MAX];
static int    g_lights_ready = 0;

static void light_pool_init(void) {
    if (g_lights_ready) return;
    for (int i = 0; i < LIGHT_MAX; i++) {
        g_lights[i].id = -1;
        g_lights[i].shadow_map = NULL;
    }
    g_lights_ready = 1;
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

static int light_create(void) {
    light_pool_init();
    for (int i = 0; i < LIGHT_MAX; i++) {
        if (g_lights[i].id != -1) continue;
        Light* l = &g_lights[i];
        memset(l, 0, sizeof(Light));
        l->id         = i;
        l->enabled    = 1;
        l->type       = LIGHT_RADIAL;
        l->intensity  = 1.0f;
        l->range      = 100.0f;
        l->r = l->g = l->b = 255;
        l->dir        = vec3f_make(0, -1, 0);
        l->cone_angle = 0.5f; // ~28 degrees
        l->shadow_w   = g_shadow_map_w;
        l->shadow_h   = g_shadow_map_h;
        int alloc_size = (l->type == LIGHT_RADIAL) 
            ? (6 * g_shadow_cube_size * g_shadow_cube_size)
            : (g_shadow_map_w * g_shadow_map_h);
        l->shadow_map = (float*)malloc(alloc_size * sizeof(float));
        if (l->shadow_map) {
            for (int j = 0; j < alloc_size; j++)
                l->shadow_map[j] = 1e30f;
        }
        return i;
    }
    return -1;
}

static void light_destroy(int id) {
    if (id < 0 || id >= LIGHT_MAX) return;
    Light* l = &g_lights[id];
    if (l->id == -1) return;
    free(l->shadow_map);
    l->shadow_map = NULL;
    l->id = -1;
}

static void light_destroy_all(void) {
    for (int i = 0; i < LIGHT_MAX; i++) light_destroy(i);
    g_lights_ready = 0;
}

static Light* light_get(int id) {
    if (id < 0 || id >= LIGHT_MAX) return NULL;
    if (g_lights[id].id == -1) return NULL;
    return &g_lights[id];
}

// ─── Configuration ───────────────────────────────────────────────────────────

static void light_set_type(int id, int type) {
    Light* l = light_get(id);
    if (l) l->type = type;
}

static void light_set_pos(int id, float x, float y, float z) {
    Light* l = light_get(id);
    if (l) l->pos = vec3f_make(x, y, z);
}

static void light_set_direction(int id, float dx, float dy, float dz) {
    Light* l = light_get(id);
    if (!l) return;
    l->dir = vec3f_normalize(vec3f_make(dx, dy, dz));
    // Cache yaw/pitch for focused shadow projection
    l->dir_yaw   = atan2f(-l->dir.x, l->dir.z);
    l->dir_pitch = asinf(-l->dir.y);
    l->cos_yaw   = cosf(l->dir_yaw);
    l->sin_yaw   = sinf(l->dir_yaw);
    l->cos_pitch = cosf(l->dir_pitch);
    l->sin_pitch = sinf(l->dir_pitch);
}

static void light_set_cone(int id, float half_angle_deg) {
    Light* l = light_get(id);
    if (l) l->cone_angle = half_angle_deg * (float)M_PI / 180.0f;
}

static void light_set_intensity(int id, float intensity) {
    Light* l = light_get(id);
    if (l) l->intensity = intensity;
}

static void light_set_range(int id, float range) {
    Light* l = light_get(id);
    if (l) l->range = range;
}

static void light_set_color(int id, unsigned char r, unsigned char g, unsigned char b) {
    Light* l = light_get(id);
    if (!l) return;
    l->r = r; l->g = g; l->b = b;
}

static void light_set_enabled(int id, int en) {
    Light* l = light_get(id);
    if (l) l->enabled = en;
}

// ─── Shadow Map: Clear ───────────────────────────────────────────────────────

static void light_shadow_clear(int id) {
    Light* l = light_get(id);
    if (!l || !l->shadow_map) return;
    int count = (l->type == LIGHT_RADIAL)
        ? (6 * g_shadow_cube_size * g_shadow_cube_size)
        : (g_shadow_map_w * g_shadow_map_h);
    for (int i = 0; i < count; i++)
        l->shadow_map[i] = 1e30f;
}

static void light_shadow_clear_all(void) {
    light_pool_init();
    for (int i = 0; i < LIGHT_MAX; i++) {
        if (g_lights[i].id != -1 && g_lights[i].enabled)
            light_shadow_clear(i);
    }
}

// ─── Shadow Map: Projection & Rasterization ─────────────────────────────────

// ── Cube shadow map helpers (LIGHT_RADIAL) ──────────────────────────────────
// 6-face cube map eliminates the spherical-projection distortion that caused
// circular shadow artifacts.  Each face is a standard 90° perspective view,
// so triangle edges remain straight in projected space.
//
// Faces: 0=+X  1=-X  2=+Y  3=-Y  4=+Z  5=-Z

static int light_cube_face(float dx, float dy, float dz) {
    float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
    if (ax >= ay && ax >= az) return dx > 0 ? 0 : 1;
    if (ay >= ax && ay >= az) return dy > 0 ? 2 : 3;
    return dz > 0 ? 4 : 5;
}

// Project a direction (dx,dy,dz from light) onto one cube face.
// Returns 1 if the point is in front of the face, 0 if behind.
static int light_cube_project(int face, float dx, float dy, float dz,
                               float* out_u, float* out_v, float* out_depth) {
    float depth, sc, tc;
    switch (face) {
        case 0: depth =  dx; sc = -dz; tc = -dy; break;  // +X
        case 1: depth = -dx; sc =  dz; tc = -dy; break;  // -X
        case 2: depth =  dy; sc =  dx; tc =  dz; break;  // +Y
        case 3: depth = -dy; sc =  dx; tc = -dz; break;  // -Y
        case 4: depth =  dz; sc =  dx; tc = -dy; break;  // +Z
        case 5: depth = -dz; sc = -dx; tc = -dy; break;  // -Z
        default: return 0;
    }
    if (depth < 0.001f) return 0;
    float inv_d = 1.0f / depth;
    *out_u = (sc * inv_d + 1.0f) * 0.5f * (float)(g_shadow_cube_size - 1);
    *out_v = (tc * inv_d + 1.0f) * 0.5f * (float)(g_shadow_cube_size - 1);
    *out_depth = depth;
    return 1;
}

static void shadow_cube_write(Light* l, int face, int su, int sv, float depth) {
    if (su < 0 || sv < 0 || su >= g_shadow_cube_size || sv >= g_shadow_cube_size) return;
    int idx = face * g_shadow_cube_size * g_shadow_cube_size + sv * g_shadow_cube_size + su;
    if (depth < l->shadow_map[idx])
        l->shadow_map[idx] = depth;
}

// ── Focused perspective projection (LIGHT_FOCUSED) ──────────────────────────

static void light_focused_project(Light* l, Vec3f wp,
                                   float* out_u, float* out_v, float* out_depth) {
    float dx = wp.x - l->pos.x;
    float dy = wp.y - l->pos.y;
    float dz = wp.z - l->pos.z;

    // Rotate by -dir_yaw (Y-axis)
    float tx =  dx * l->cos_yaw + dz * l->sin_yaw;
    float tz = -dx * l->sin_yaw + dz * l->cos_yaw;
    dx = tx; dz = tz;

    // Rotate by -dir_pitch (X-axis)
    float ty =  dy * l->cos_pitch + dz * l->sin_pitch;
    tz       = -dy * l->sin_pitch + dz * l->cos_pitch;

    if (tz < 0.01f) { *out_u = -1; *out_v = -1; *out_depth = 0; return; }

    float fov_tan = tanf(l->cone_angle);
    if (fov_tan < 0.001f) fov_tan = 0.001f;
    float half_w = (float)l->shadow_w * 0.5f;
    float half_h = (float)l->shadow_h * 0.5f;

    *out_u = (dx / tz) / fov_tan * half_w + half_w;
    *out_v = -(ty / tz) / fov_tan * half_h + half_h;
    *out_depth = tz;
}

// Write depth to a flat shadow map (LIGHT_FOCUSED).
static void shadow_write(Light* l, int su, int sv, float depth) {
    if (su < 0 || sv < 0 || su >= l->shadow_w || sv >= l->shadow_h) return;
    int idx = sv * l->shadow_w + su;
    if (depth < l->shadow_map[idx])
        l->shadow_map[idx] = depth;
}

// ── Rasterize one triangle into a light's shadow map ────────────────────────

static void light_shadow_rasterize(Light* l, Vec3f v0, Vec3f v1, Vec3f v2) {
    if (!l || !l->shadow_map) return;

    if (l->type == LIGHT_RADIAL) {
        // Cube shadow map: project triangle onto each visible cube face
        float dx0 = v0.x - l->pos.x, dy0 = v0.y - l->pos.y, dz0 = v0.z - l->pos.z;
        float dx1 = v1.x - l->pos.x, dy1 = v1.y - l->pos.y, dz1 = v1.z - l->pos.z;
        float dx2 = v2.x - l->pos.x, dy2 = v2.y - l->pos.y, dz2 = v2.z - l->pos.z;

        for (int face = 0; face < 6; face++) {
            float fu0, fv0, fd0, fu1, fv1, fd1, fu2, fv2, fd2;
            if (!light_cube_project(face, dx0, dy0, dz0, &fu0, &fv0, &fd0)) continue;
            if (!light_cube_project(face, dx1, dy1, dz1, &fu1, &fv1, &fd1)) continue;
            if (!light_cube_project(face, dx2, dy2, dz2, &fu2, &fv2, &fd2)) continue;

            int min_su = (int)fminf(fminf(fu0, fu1), fu2);
            int max_su = (int)fmaxf(fmaxf(fu0, fu1), fu2);
            int min_sv = (int)fminf(fminf(fv0, fv1), fv2);
            int max_sv = (int)fmaxf(fmaxf(fv0, fv1), fv2);
            if (min_su < 0) min_su = 0;
            if (min_sv < 0) min_sv = 0;
            if (max_su >= g_shadow_cube_size) max_su = g_shadow_cube_size - 1;
            if (max_sv >= g_shadow_cube_size) max_sv = g_shadow_cube_size - 1;

            float area = (fu1 - fu0) * (fv2 - fv0) - (fu2 - fu0) * (fv1 - fv0);
            if (fabsf(area) < 0.001f) continue;
            float inv_area = 1.0f / area;

            for (int sy = min_sv; sy <= max_sv; sy++) {
                for (int sx = min_su; sx <= max_su; sx++) {
                    float px = (float)sx + 0.5f;
                    float py = (float)sy + 0.5f;
                    float b1 = ((fv2 - fv0) * (px - fu0) + (fu0 - fu2) * (py - fv0)) * inv_area;
                    float b2 = ((fv0 - fv1) * (px - fu0) + (fu1 - fu0) * (py - fv0)) * inv_area;
                    float b0 = 1.0f - b1 - b2;
                    if (b0 < 0 || b1 < 0 || b2 < 0) continue;
                    float depth = b0 * fd0 + b1 * fd1 + b2 * fd2;
                    shadow_cube_write(l, face, sx, sy, depth);
                }
            }
        }
    } else {
        // LIGHT_FOCUSED: perspective shadow map
        float fu0, fv0, fd0, fu1, fv1, fd1, fu2, fv2, fd2;
        light_focused_project(l, v0, &fu0, &fv0, &fd0);
        light_focused_project(l, v1, &fu1, &fv1, &fd1);
        light_focused_project(l, v2, &fu2, &fv2, &fd2);
        if (fu0 < -1 || fu1 < -1 || fu2 < -1) return;

        int min_su = (int)fminf(fminf(fu0, fu1), fu2);
        int max_su = (int)fmaxf(fmaxf(fu0, fu1), fu2);
        int min_sv = (int)fminf(fminf(fv0, fv1), fv2);
        int max_sv = (int)fmaxf(fmaxf(fv0, fv1), fv2);
        if (min_su < 0) min_su = 0;
        if (min_sv < 0) min_sv = 0;
        if (max_su >= l->shadow_w) max_su = l->shadow_w - 1;
        if (max_sv >= l->shadow_h) max_sv = l->shadow_h - 1;

        float area = (fu1 - fu0) * (fv2 - fv0) - (fu2 - fu0) * (fv1 - fv0);
        if (fabsf(area) < 0.001f) return;
        float inv_area = 1.0f / area;

        for (int sy = min_sv; sy <= max_sv; sy++) {
            for (int sx = min_su; sx <= max_su; sx++) {
                float px = (float)sx + 0.5f;
                float py = (float)sy + 0.5f;
                float b1 = ((fv2 - fv0) * (px - fu0) + (fu0 - fu2) * (py - fv0)) * inv_area;
                float b2 = ((fv0 - fv1) * (px - fu0) + (fu1 - fu0) * (py - fv0)) * inv_area;
                float b0 = 1.0f - b1 - b2;
                if (b0 < 0 || b1 < 0 || b2 < 0) continue;
                float depth = b0 * fd0 + b1 * fd1 + b2 * fd2;
                shadow_write(l, sx, sy, depth);
            }
        }
    }
}

// ─── Shadow Query ────────────────────────────────────────────────────────────

// Returns 1 if the world point is lit by this light, 0 if in shadow.
static int light_shadow_test(Light* l, Vec3f wp) {
    if (!l || !l->shadow_map) return 1;

    if (l->type == LIGHT_RADIAL) {
        float dx = wp.x - l->pos.x;
        float dy = wp.y - l->pos.y;
        float dz = wp.z - l->pos.z;
        int face = light_cube_face(dx, dy, dz);

        float su, sv, depth;
        if (!light_cube_project(face, dx, dy, dz, &su, &sv, &depth)) return 1;

        int ix = (int)(su + 0.5f);
        int iy = (int)(sv + 0.5f);
        if (ix < 0 || iy < 0 || ix >= g_shadow_cube_size || iy >= g_shadow_cube_size)
            return 1;

        int idx = face * g_shadow_cube_size * g_shadow_cube_size
                + iy * g_shadow_cube_size + ix;
        float stored = l->shadow_map[idx];
        float bias = 0.3f;
        return (depth <= stored + bias) ? 1 : 0;
    } else {
        float su, sv, depth;
        light_focused_project(l, wp, &su, &sv, &depth);
        if (su < 0) return 0;

        int ix = (int)(su + 0.5f);
        int iy = (int)(sv + 0.5f);
        if (ix < 0 || iy < 0 || ix >= l->shadow_w || iy >= l->shadow_h)
            return 0;

        float stored = l->shadow_map[iy * l->shadow_w + ix];
        float bias = 0.3f;
        return (depth <= stored + bias) ? 1 : 0;
    }
}

// ─── Lighting Computation ────────────────────────────────────────────────────

// Compute total lighting at a world-space point with a given face normal.
// Modulates (base_r, base_g, base_b) by all active lights (diffuse + shadow).
// Writes final lit color to out_r/g/b.
static void light_compute(Vec3f world_pos, Vec3f normal,
                           float base_r, float base_g, float base_b,
                           float* out_r, float* out_g, float* out_b) {
    light_pool_init();

    // Start with ambient
    float total_r = LIGHT_AMBIENT * base_r;
    float total_g = LIGHT_AMBIENT * base_g;
    float total_b = LIGHT_AMBIENT * base_b;

    for (int i = 0; i < LIGHT_MAX; i++) {
        Light* l = &g_lights[i];
        if (l->id == -1 || !l->enabled) continue;

        // Direction from surface point to light
        Vec3f to_light = vec3f_sub(l->pos, world_pos);
        float dist = vec3f_length(to_light);
        if (dist < 0.001f || dist > l->range) continue;

        Vec3f light_dir = vec3f_scale(to_light, 1.0f / dist);

        // Diffuse: N dot L (clamped to 0)
        float ndotl = vec3f_dot(normal, light_dir);
        if (ndotl <= 0.0f) continue;  // surface faces away from light

        // Distance attenuation: smooth falloff where range is the effective radius
        float half_range = l->range * 0.5f;
        float atten = 1.0f / (1.0f + (dist * dist) / (half_range * half_range));

        // Cone attenuation for focused lights
        float cone_atten = 1.0f;
        if (l->type == LIGHT_FOCUSED) {
            // Angle between light direction and light-to-point vector
            Vec3f neg_light_dir = vec3f_scale(light_dir, -1.0f);
            float cos_angle = vec3f_dot(l->dir, neg_light_dir);
            float cos_cone  = cosf(l->cone_angle);
            if (cos_angle < cos_cone) continue;  // outside cone
            // Smooth falloff toward cone edge
            float cos_inner = cosf(l->cone_angle * 0.7f);
            if (cos_angle < cos_inner)
                cone_atten = (cos_angle - cos_cone) / (cos_inner - cos_cone);
        }

        // Shadow test
        float shadow = light_shadow_test(l, world_pos) ? 1.0f : 0.0f;

        // Final contribution for this light
        float contrib = ndotl * atten * cone_atten * l->intensity * shadow;

        float lr = (float)l->r / 255.0f;
        float lg = (float)l->g / 255.0f;
        float lb = (float)l->b / 255.0f;

        total_r += base_r * contrib * lr;
        total_g += base_g * contrib * lg;
        total_b += base_b * contrib * lb;
    }

    // Clamp to [0, 255]
    if (total_r > 255.0f) total_r = 255.0f;
    if (total_g > 255.0f) total_g = 255.0f;
    if (total_b > 255.0f) total_b = 255.0f;
    *out_r = total_r;
    *out_g = total_g;
    *out_b = total_b;
}

// ─── Query: are any lights active? ───────────────────────────────────────────

static int light_any_active(void) {
    light_pool_init();
    for (int i = 0; i < LIGHT_MAX; i++) {
        if (g_lights[i].id != -1 && g_lights[i].enabled) return 1;
    }
    return 0;
}

// ─── Rasterize all entity faces into all shadow maps ─────────────────────────

// Bridge function called from entity_shadow_pass via function pointer.
// light_id = which light to rasterize into.
static void light_shadow_rasterize_bridge(int light_id, Vec3f v0, Vec3f v1, Vec3f v2) {
    Light* l = light_get(light_id);
    if (!l || !l->enabled) return;
    light_shadow_rasterize(l, v0, v1, v2);
}

// Call this once after including both Light.h and Entity.h to wire up the shadow system.
// Collects active light IDs and sets the entity shadow callback.
static void light_shadow_setup(void) {
    light_pool_init();

    // Tell entity system which function to call for shadow rasterization
    entity_set_shadow_fn(light_shadow_rasterize_bridge);

    // Collect active light IDs
    g_shadow_light_count = 0;
    for (int i = 0; i < LIGHT_MAX && g_shadow_light_count < 16; i++) {
        if (g_lights[i].id != -1 && g_lights[i].enabled)
            g_shadow_light_ids[g_shadow_light_count++] = i;
    }
}

// Full shadow build: clear all shadow maps → setup → entity shadow pass.
// Call this once per frame before entity_draw_all().
static void light_build_shadows(void) {
    light_shadow_clear_all();
    light_shadow_setup();
    entity_shadow_pass();
}

// ─── Cycle Shadow Resolution ─────────────────────────────────────────────────
// Call this to cycle to the next shadow resolution preset.
// Reallocates all shadow maps with new dimensions.

static void light_cycle_shadow_resolution(void) {
    light_pool_init();
    g_shadow_preset_idx = (g_shadow_preset_idx + 1) % g_shadow_preset_count;
    
    // Set new globals from preset
    g_shadow_cube_size = g_shadow_presets[g_shadow_preset_idx][0];
    g_shadow_map_w     = g_shadow_presets[g_shadow_preset_idx][1];
    g_shadow_map_h     = g_shadow_presets[g_shadow_preset_idx][2];
    
    // Reallocate shadow maps for all lights
    for (int i = 0; i < LIGHT_MAX; i++) {
        Light* l = &g_lights[i];
        if (l->id == -1) continue;
        
        free(l->shadow_map);
        l->shadow_map = NULL;
        
        l->shadow_w = g_shadow_map_w;
        l->shadow_h = g_shadow_map_h;
        
        int alloc_size = (l->type == LIGHT_RADIAL)
            ? (6 * g_shadow_cube_size * g_shadow_cube_size)
            : (g_shadow_map_w * g_shadow_map_h);
        
        l->shadow_map = (float*)malloc(alloc_size * sizeof(float));
        if (l->shadow_map) {
            for (int j = 0; j < alloc_size; j++)
                l->shadow_map[j] = 1e30f;
        }
    }
}

static const char* light_get_shadow_resolution_str(void) {
    if (g_shadow_preset_idx == 0) return "HIGH";
    if (g_shadow_preset_idx == 1) return "MID";
    return "LOW";
}

// Bridge: lighting callback for the rasterizer (matches RenderLightFn signature)
static void light_compute_callback(Vec3f world_pos, Vec3f normal,
                                    float base_r, float base_g, float base_b,
                                    float* out_r, float* out_g, float* out_b) {
    light_compute(world_pos, normal, base_r, base_g, base_b, out_r, out_g, out_b);
}

#endif // LIGHT_H
