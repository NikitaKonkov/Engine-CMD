#ifndef ENTITY_H
#define ENTITY_H

// ═══════════════════════════════════════════════════════════════════════════════
// Entity.h — Modular entity system for the ASCII 3D engine
//
// Each entity has:
//   • A unique ID (pool slot index)
//   • A world transform: position, rotation (yaw/pitch/roll), scale
//   • Local-space primitives: dots, edges, faces
//   • An optional per-frame shader callback
//
// Usage:
//   int id = entity_create();
//   entity_add_faces(id, faces, 12);
//   entity_set_pos(id, 10, 0, 5);
//   entity_set_shader(id, shader_depth);
//   ...
//   entity_draw_all(dt);       // draws all enabled entities
//   entity_destroy_all();      // cleanup
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

#define ENTITY_MAX_POOL  64

// ─── Types ───────────────────────────────────────────────────────────────────

struct Entity;

// Per-frame shader callback.  Called once before drawing.
// `dt` = frame delta time (seconds).
typedef void (*EntityShaderFn)(struct Entity* e, float dt);

// Cleanup callback.  Called when entity_destroy() frees the entity.
// Use it to free shader_data allocations.
typedef void (*EntityCleanupFn)(struct Entity* e);

// ─── Entity ──────────────────────────────────────────────────────────────────

struct Entity {
    int   id;            // pool slot, -1 = unused
    int   enabled;       // 1 = visible, 0 = hidden

    // ── World Transform ──────────────────────────────────────────────────
    Vec3f pos;
    float yaw, pitch, roll;   // radians
    Vec3f scl;                 // per-axis scale (1,1,1 = identity)

    // ── Trig Cache (refreshed each frame in entity_draw) ─────────────────
    float cos_yaw, sin_yaw;
    float cos_pitch, sin_pitch;
    float cos_roll, sin_roll;

    // ── Local-space Primitives ───────────────────────────────────────────
    RDot*  dots;    int dot_count;   int dot_cap;
    REdge* edges;   int edge_count;  int edge_cap;
    RFace* faces;   int face_count;  int face_cap;

    // ── Shader / Effect ──────────────────────────────────────────────────
    EntityShaderFn  shader;
    EntityCleanupFn cleanup;
    float           shader_time;   // accumulated time (seconds)
    void*           shader_data;   // per-entity custom data
};

// ─── Pool ────────────────────────────────────────────────────────────────────

static Entity g_entities[ENTITY_MAX_POOL];
static int    g_entity_init = 0;

static void entity_pool_ensure(void) {
    if (g_entity_init) return;
    for (int i = 0; i < ENTITY_MAX_POOL; i++) {
        memset(&g_entities[i], 0, sizeof(Entity));
        g_entities[i].id = -1;
    }
    g_entity_init = 1;
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

static int entity_create(void) {
    entity_pool_ensure();
    for (int i = 0; i < ENTITY_MAX_POOL; i++) {
        if (g_entities[i].id != -1) continue;
        Entity* e = &g_entities[i];
        memset(e, 0, sizeof(Entity));
        e->id      = i;
        e->enabled = 1;
        e->scl     = vec3f_make(1, 1, 1);
        e->cos_yaw = 1; e->cos_pitch = 1; e->cos_roll = 1;
        return i;
    }
    return -1;
}

static void entity_destroy(int id) {
    if (id < 0 || id >= ENTITY_MAX_POOL) return;
    Entity* e = &g_entities[id];
    if (e->id == -1) return;
    if (e->cleanup) e->cleanup(e);
    free(e->dots);
    free(e->edges);
    free(e->faces);
    memset(e, 0, sizeof(Entity));
    e->id = -1;
}

static void entity_destroy_all(void) {
    for (int i = 0; i < ENTITY_MAX_POOL; i++) entity_destroy(i);
    g_entity_init = 0;
}

// ─── Access ──────────────────────────────────────────────────────────────────

static Entity* entity_get(int id) {
    if (id < 0 || id >= ENTITY_MAX_POOL) return NULL;
    if (g_entities[id].id == -1) return NULL;
    return &g_entities[id];
}

// ─── Transform ───────────────────────────────────────────────────────────────

static void entity_set_pos(int id, float x, float y, float z) {
    Entity* e = entity_get(id);
    if (e) e->pos = vec3f_make(x, y, z);
}

static void entity_move(int id, float dx, float dy, float dz) {
    Entity* e = entity_get(id);
    if (e) e->pos = vec3f_add(e->pos, vec3f_make(dx, dy, dz));
}

static void entity_set_rotation(int id, float yaw, float pitch, float roll) {
    Entity* e = entity_get(id);
    if (!e) return;
    e->yaw = yaw;  e->pitch = pitch;  e->roll = roll;
}

static void entity_rotate(int id, float dyaw, float dpitch, float droll) {
    Entity* e = entity_get(id);
    if (!e) return;
    e->yaw += dyaw;  e->pitch += dpitch;  e->roll += droll;
}

static void entity_set_scale(int id, float sx, float sy, float sz) {
    Entity* e = entity_get(id);
    if (e) e->scl = vec3f_make(sx, sy, sz);
}

static void entity_set_enabled(int id, int en) {
    Entity* e = entity_get(id);
    if (e) e->enabled = en;
}

// ─── Shader ──────────────────────────────────────────────────────────────────

static void entity_set_shader(int id, EntityShaderFn fn) {
    Entity* e = entity_get(id);
    if (e) e->shader = fn;
}

static void entity_set_cleanup(int id, EntityCleanupFn fn) {
    Entity* e = entity_get(id);
    if (e) e->cleanup = fn;
}

static void entity_set_shader_data(int id, void* data) {
    Entity* e = entity_get(id);
    if (e) e->shader_data = data;
}

// ─── Primitive Storage ───────────────────────────────────────────────────────

static void entity_grow_dots(Entity* e, int need) {
    if (e->dot_cap >= need) return;
    int cap = e->dot_cap ? e->dot_cap : 64;
    while (cap < need) cap *= 2;
    e->dots = (RDot*)realloc(e->dots, cap * sizeof(RDot));
    e->dot_cap = cap;
}

static void entity_grow_edges(Entity* e, int need) {
    if (e->edge_cap >= need) return;
    int cap = e->edge_cap ? e->edge_cap : 16;
    while (cap < need) cap *= 2;
    e->edges = (REdge*)realloc(e->edges, cap * sizeof(REdge));
    e->edge_cap = cap;
}

static void entity_grow_faces(Entity* e, int need) {
    if (e->face_cap >= need) return;
    int cap = e->face_cap ? e->face_cap : 64;
    while (cap < need) cap *= 2;
    e->faces = (RFace*)realloc(e->faces, cap * sizeof(RFace));
    e->face_cap = cap;
}

static void entity_add_dot(int id, RDot d) {
    Entity* e = entity_get(id);
    if (!e) return;
    entity_grow_dots(e, e->dot_count + 1);
    e->dots[e->dot_count++] = d;
}

static void entity_add_dots(int id, const RDot* d, int n) {
    Entity* e = entity_get(id);
    if (!e || n <= 0) return;
    entity_grow_dots(e, e->dot_count + n);
    memcpy(e->dots + e->dot_count, d, n * sizeof(RDot));
    e->dot_count += n;
}

static void entity_add_edge(int id, REdge ed) {
    Entity* e = entity_get(id);
    if (!e) return;
    entity_grow_edges(e, e->edge_count + 1);
    e->edges[e->edge_count++] = ed;
}

static void entity_add_edges(int id, const REdge* ed, int n) {
    Entity* e = entity_get(id);
    if (!e || n <= 0) return;
    entity_grow_edges(e, e->edge_count + n);
    memcpy(e->edges + e->edge_count, ed, n * sizeof(REdge));
    e->edge_count += n;
}

static void entity_add_face(int id, RFace f) {
    Entity* e = entity_get(id);
    if (!e) return;
    entity_grow_faces(e, e->face_count + 1);
    e->faces[e->face_count++] = f;
}

static void entity_add_faces(int id, const RFace* f, int n) {
    Entity* e = entity_get(id);
    if (!e || n <= 0) return;
    entity_grow_faces(e, e->face_count + n);
    memcpy(e->faces + e->face_count, f, n * sizeof(RFace));
    e->face_count += n;
}

static void entity_clear_primitives(int id) {
    Entity* e = entity_get(id);
    if (!e) return;
    e->dot_count = e->edge_count = e->face_count = 0;
}

// ─── Transform: Local → World ────────────────────────────────────────────────
// NOTE: Uses the trig cache (cos_yaw, sin_yaw, etc.).  The cache is refreshed
// automatically by entity_draw() each frame before shaders or drawing run.

static Vec3f entity_local_to_world(const Entity* e, Vec3f local) {
    // Scale
    float x = local.x * e->scl.x;
    float y = local.y * e->scl.y;
    float z = local.z * e->scl.z;

    // Yaw (Y-axis)
    float tx =  x * e->cos_yaw + z * e->sin_yaw;
    float tz = -x * e->sin_yaw + z * e->cos_yaw;
    x = tx;  z = tz;

    // Pitch (X-axis)
    float ty = y * e->cos_pitch - z * e->sin_pitch;
    tz       = y * e->sin_pitch + z * e->cos_pitch;
    y = ty;  z = tz;

    // Roll (Z-axis)
    tx = x * e->cos_roll - y * e->sin_roll;
    ty = x * e->sin_roll + y * e->cos_roll;
    x = tx;  y = ty;

    // Translate
    return vec3f_make(x + e->pos.x, y + e->pos.y, z + e->pos.z);
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

static void entity_draw(int id, float dt) {
    Entity* e = entity_get(id);
    if (!e || !e->enabled) return;

    // Refresh trig cache
    e->cos_yaw   = cosf(e->yaw);    e->sin_yaw   = sinf(e->yaw);
    e->cos_pitch = cosf(e->pitch);  e->sin_pitch = sinf(e->pitch);
    e->cos_roll  = cosf(e->roll);   e->sin_roll  = sinf(e->roll);

    // Advance shader timer and run shader
    e->shader_time += dt;
    if (e->shader) e->shader(e, dt);

    // Draw dots
    for (int i = 0; i < e->dot_count; i++) {
        RDot d = e->dots[i];
        d.pos = entity_local_to_world(e, d.pos);
        draw_dot(d);
    }

    // Draw edges
    for (int i = 0; i < e->edge_count; i++) {
        REdge ed = e->edges[i];
        ed.start = entity_local_to_world(e, ed.start);
        ed.end   = entity_local_to_world(e, ed.end);
        draw_edge(ed);
    }

    // Draw faces
    for (int i = 0; i < e->face_count; i++) {
        RFace f = e->faces[i];
        f.verts[0] = entity_local_to_world(e, f.verts[0]);
        f.verts[1] = entity_local_to_world(e, f.verts[1]);
        f.verts[2] = entity_local_to_world(e, f.verts[2]);
        draw_face(f);
    }
}

// Draw every enabled entity in the pool.
static void entity_draw_all(float dt) {
    entity_pool_ensure();
    for (int i = 0; i < ENTITY_MAX_POOL; i++) {
        if (g_entities[i].id != -1 && g_entities[i].enabled)
            entity_draw(i, dt);
    }
}

// ─── Shadow Pass ─────────────────────────────────────────────────────────────
// Rasterize all entity faces (world-space) into every active light's shadow map.
// Must be called AFTER shaders have run (entity_draw refreshes trig + runs shaders).
// To solve: we run shaders in entity_draw, but shadow pass needs world-space faces
// BEFORE the main camera rasterization.
//
// Solution: a separate pass that only refreshes transforms + rasterizes into
// shadow maps, without drawing to the camera.

// Forward declaration — implemented in Light.h (included before Entity.h usage)
// We use a function pointer to avoid circular header dependency.
typedef void (*ShadowRasterizeFn)(int light_id, Vec3f v0, Vec3f v1, Vec3f v2);

static ShadowRasterizeFn g_shadow_rasterize_fn = NULL;
static int  g_shadow_light_count = 0;
static int  g_shadow_light_ids[16];

static void entity_set_shadow_fn(ShadowRasterizeFn fn) {
    g_shadow_rasterize_fn = fn;
}

static void entity_shadow_pass(void) {
    if (!g_shadow_rasterize_fn || g_shadow_light_count <= 0) return;
    entity_pool_ensure();

    for (int ei = 0; ei < ENTITY_MAX_POOL; ei++) {
        Entity* e = &g_entities[ei];
        if (e->id == -1 || !e->enabled) continue;
        if (e->face_count <= 0) continue;

        // Refresh trig cache for transforms (don't run shader — already ran or will run)
        e->cos_yaw   = cosf(e->yaw);    e->sin_yaw   = sinf(e->yaw);
        e->cos_pitch = cosf(e->pitch);  e->sin_pitch = sinf(e->pitch);
        e->cos_roll  = cosf(e->roll);   e->sin_roll  = sinf(e->roll);

        for (int fi = 0; fi < e->face_count; fi++) {
            RFace f = e->faces[fi];
            Vec3f w0 = entity_local_to_world(e, f.verts[0]);
            Vec3f w1 = entity_local_to_world(e, f.verts[1]);
            Vec3f w2 = entity_local_to_world(e, f.verts[2]);

            // Rasterize into each active light's shadow map
            for (int li = 0; li < g_shadow_light_count; li++) {
                g_shadow_rasterize_fn(g_shadow_light_ids[li], w0, w1, w2);
            }
        }
    }
}

#endif // ENTITY_H
