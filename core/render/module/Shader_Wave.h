#ifndef SHADER_WAVE_H
#define SHADER_WAVE_H

// ═══════════════════════════════════════════════════════════════════════════════
// Shader_Wave.h — Wave animation for dot-based entities
//
// Adapted from the original DOT_ANIMATION.h wave functions.
// Oscillates dot Y positions with multiple overlapping sine waves
// plus a circular wave from the entity's local origin.
//
// Requires shader_wave_attach() to store base Y positions.
//
// Usage:
//   entity_add_dots(id, grid_dots, count);
//   shader_wave_attach(id, 2.0f, 0.3f, 2.0f);  // amplitude, freq, speed
// ═══════════════════════════════════════════════════════════════════════════════

#include "Entity.h"

// ─── Data ────────────────────────────────────────────────────────────────────

struct ShaderWaveData {
    float* base_y;       // original Y of each dot (snapshot at attach time)
    int    count;        // number of base positions stored
    float  amplitude;    // wave height
    float  frequency;    // spatial frequency
    float  speed;        // temporal speed
};

// ─── Cleanup ─────────────────────────────────────────────────────────────────

static void shader_wave_cleanup(Entity* e) {
    ShaderWaveData* d = (ShaderWaveData*)e->shader_data;
    if (d) {
        free(d->base_y);
        free(d);
        e->shader_data = NULL;
    }
}

// ─── Per-frame callback ──────────────────────────────────────────────────────

static void shader_wave(Entity* e, float dt) {
    (void)dt;
    ShaderWaveData* d = (ShaderWaveData*)e->shader_data;
    if (!d) return;

    float t = e->shader_time;

    for (int i = 0; i < e->dot_count && i < d->count; i++) {
        float x = e->dots[i].pos.x;
        float z = e->dots[i].pos.z;

        // Multi-layered sine waves (from dot_wave_grid)
        float w1 = sinf(x * d->frequency + t * d->speed) * d->amplitude;
        float w2 = sinf(z * d->frequency * 0.8f + t * d->speed * 1.1f)
                    * d->amplitude * 0.6f;
        float w3 = sinf((x + z) * d->frequency * 0.4f + t * d->speed * 0.7f)
                    * d->amplitude * 0.3f;

        // Circular wave from entity local origin
        float dist = sqrtf(x * x + z * z);
        float wc = sinf(dist * d->frequency * 1.5f - t * d->speed * 1.5f)
                   * d->amplitude * 0.4f;

        e->dots[i].pos.y = d->base_y[i] + w1 + w2 + w3 + wc;
    }
}

// ─── Attach ──────────────────────────────────────────────────────────────────
// Call AFTER adding dots to the entity.

static void shader_wave_attach(int id, float amplitude, float frequency, float speed) {
    Entity* e = entity_get(id);
    if (!e || e->dot_count == 0) return;

    ShaderWaveData* d = (ShaderWaveData*)malloc(sizeof(ShaderWaveData));
    d->count     = e->dot_count;
    d->base_y    = (float*)malloc(d->count * sizeof(float));
    d->amplitude = amplitude;
    d->frequency = frequency;
    d->speed     = speed;

    for (int i = 0; i < d->count; i++)
        d->base_y[i] = e->dots[i].pos.y;

    e->shader_data = d;
    e->shader      = shader_wave;
    e->cleanup     = shader_wave_cleanup;
}

#endif // SHADER_WAVE_H
