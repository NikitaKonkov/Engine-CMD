#ifndef SHADER_SPLAT_H
#define SHADER_SPLAT_H

// ═══════════════════════════════════════════════════════════════════════════════
// Shader_Splat.h — Gaussian splat builder + optional pulse animation
//
// Utility to generate a 3D Gaussian point cloud into an entity, with
// density-based ASCII characters (dense core '@', wispy edges '.').
// Also provides an optional breathing/pulse shader that gently oscillates
// dots inward and outward from the entity center.
//
// Usage:
//   int id = entity_create();
//   splat_build(id, 300, 3.5f, 3.5f, 3.5f, 96);      // build dots
//   entity_set_pos(id, 10, 5, 0);                       // place in world
//   shader_splat_pulse_attach(id, 0.5f, 2.0f);          // optional anim
// ═══════════════════════════════════════════════════════════════════════════════

#include "Entity.h"

// ─── Box-Muller Gaussian RNG ─────────────────────────────────────────────────

static float splat_gauss_rand(float mean, float stddev) {
    static int   spare_ready = 0;
    static float spare_val   = 0.0f;
    if (spare_ready) { spare_ready = 0; return mean + stddev * spare_val; }
    float u, v, s;
    do {
        u = (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
        v = (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
        s = u * u + v * v;
    } while (s >= 1.0f || s == 0.0f);
    float mul   = sqrtf(-2.0f * logf(s) / s);
    spare_val   = v * mul;
    spare_ready = 1;
    return mean + stddev * u * mul;
}

// ─── Builder ─────────────────────────────────────────────────────────────────

// Density characters: core → edge
static const char g_splat_density[] = { '@', 'O', 'o', '*', '.' };

// Build n_points Gaussian-distributed dots into the entity (local space).
// Dots are centered at (0,0,0); use entity_set_pos() to place in world.
static void splat_build(int entity_id, int n_points,
                         float sx, float sy, float sz, int color) {
    Entity* e = entity_get(entity_id);
    if (!e) return;
    entity_grow_dots(e, e->dot_count + n_points);

    for (int i = 0; i < n_points; i++) {
        float px = splat_gauss_rand(0.0f, sx);
        float py = splat_gauss_rand(0.0f, sy);
        float pz = splat_gauss_rand(0.0f, sz);

        // Normalised Mahalanobis distance — sigma units from center
        float nd = sqrtf((px * px) / (sx * sx) +
                         (py * py) / (sy * sy) +
                         (pz * pz) / (sz * sz));
        int ci = (int)(nd * 1.6f);
        if (ci > 4) ci = 4;

        e->dots[e->dot_count++] = rdot_make(
            vec3f_make(px, py, pz), g_splat_density[ci], color);
    }
}

// ─── Pulse Shader ────────────────────────────────────────────────────────────
// Gentle breathing animation: dots drift in/out from the center.

struct ShaderSplatPulseData {
    float* base_dist;    // original distance from center for each dot
    float* dir_x;        // normalised direction components
    float* dir_y;
    float* dir_z;
    int    count;
    float  amplitude;    // pulse magnitude (world units)
    float  speed;        // pulse speed (radians / second)
};

static void shader_splat_pulse_cleanup(Entity* e) {
    ShaderSplatPulseData* d = (ShaderSplatPulseData*)e->shader_data;
    if (d) {
        free(d->base_dist);
        free(d->dir_x);
        free(d->dir_y);
        free(d->dir_z);
        free(d);
        e->shader_data = NULL;
    }
}

static void shader_splat_pulse(Entity* e, float dt) {
    (void)dt;
    ShaderSplatPulseData* d = (ShaderSplatPulseData*)e->shader_data;
    if (!d) return;

    float t = e->shader_time;

    for (int i = 0; i < e->dot_count && i < d->count; i++) {
        // Phase-shift each dot by its base distance for organic feel
        float offset  = sinf(t * d->speed + d->base_dist[i] * 2.0f) * d->amplitude;
        float new_r   = d->base_dist[i] + offset;
        e->dots[i].pos.x = d->dir_x[i] * new_r;
        e->dots[i].pos.y = d->dir_y[i] * new_r;
        e->dots[i].pos.z = d->dir_z[i] * new_r;
    }
}

// Attach the pulse shader.  Call AFTER splat_build().
static void shader_splat_pulse_attach(int id, float amplitude, float speed) {
    Entity* e = entity_get(id);
    if (!e || e->dot_count == 0) return;

    ShaderSplatPulseData* d = (ShaderSplatPulseData*)malloc(sizeof(ShaderSplatPulseData));
    d->count     = e->dot_count;
    d->amplitude = amplitude;
    d->speed     = speed;
    d->base_dist = (float*)malloc(d->count * sizeof(float));
    d->dir_x     = (float*)malloc(d->count * sizeof(float));
    d->dir_y     = (float*)malloc(d->count * sizeof(float));
    d->dir_z     = (float*)malloc(d->count * sizeof(float));

    for (int i = 0; i < d->count; i++) {
        float x = e->dots[i].pos.x;
        float y = e->dots[i].pos.y;
        float z = e->dots[i].pos.z;
        float dist = sqrtf(x * x + y * y + z * z);
        d->base_dist[i] = dist;
        if (dist > 0.001f) {
            d->dir_x[i] = x / dist;
            d->dir_y[i] = y / dist;
            d->dir_z[i] = z / dist;
        } else {
            d->dir_x[i] = 0.0f;
            d->dir_y[i] = 1.0f;
            d->dir_z[i] = 0.0f;
        }
    }

    e->shader_data = d;
    e->shader      = shader_splat_pulse;
    e->cleanup     = shader_splat_pulse_cleanup;
}

#endif // SHADER_SPLAT_H
