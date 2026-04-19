#ifndef SHADER_DEPTH_H
#define SHADER_DEPTH_H

// ═══════════════════════════════════════════════════════════════════════════════
// Shader_Depth.h — Distance-based ASCII and color shading
//
// Adapted from the original SHADER.c depth shading system.
// Closer primitives get denser characters and brighter colors;
// farther primitives fade to sparse characters and dim colors.
//
// Usage:
//   entity_set_shader(id, shader_depth);
// ═══════════════════════════════════════════════════════════════════════════════

#include "Entity.h"

// ASCII characters ordered by visual density (dense → sparse)
static const char g_depth_chars[] = {
    '#', '#', '@', '@', '&', '%', 'M', 'W', 'B', 'H',
    'N', 'O', 'P', 'S', 'E', 'F', 'X', 'Y', 'Z', 'V',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'o', '*',
    '+', '=', '-', ':', ';', ',', '.', ' '
};
static const int g_depth_char_count = sizeof(g_depth_chars) / sizeof(g_depth_chars[0]);

// Color codes ordered by brightness (bright → dim)
static const int g_depth_colors[] = {
    97, 96, 93, 92, 91, 95, 94, 37, 36, 33, 32, 31, 35, 34, 90
};
static const int g_depth_color_count = sizeof(g_depth_colors) / sizeof(g_depth_colors[0]);

// Distance divisor — controls how quickly chars/colors change with distance.
// Lower = faster transition.  8.0 works well for a scene spanning ~100 units.
#define DEPTH_DIVISOR 8.0f

static void shader_depth(Entity* e, float dt) {
    (void)dt;
    Camera* active = cam_get_active();
    if (!active) return;

    // Shade dots
    for (int i = 0; i < e->dot_count; i++) {
        Vec3f world = entity_local_to_world(e, e->dots[i].pos);
        float dist  = vec3f_length(vec3f_sub(world, active->pos));
        int idx = (int)(dist / DEPTH_DIVISOR);
        if (idx < 0) idx = 0;
        e->dots[i].ascii = (idx < g_depth_char_count)  ? g_depth_chars[idx]  : '.';
        e->dots[i].color = (idx < g_depth_color_count) ? g_depth_colors[idx] : 90;
    }

    // Shade non-textured faces (textured faces keep their texture colors)
    for (int i = 0; i < e->face_count; i++) {
        if (e->faces[i].texture) continue;
        Vec3f center = vec3f_scale(
            vec3f_add(vec3f_add(
                entity_local_to_world(e, e->faces[i].verts[0]),
                entity_local_to_world(e, e->faces[i].verts[1])),
                entity_local_to_world(e, e->faces[i].verts[2])),
            1.0f / 3.0f);
        float dist = vec3f_length(vec3f_sub(center, active->pos));
        int idx = (int)(dist / DEPTH_DIVISOR);
        if (idx < 0) idx = 0;
        e->faces[i].ascii = (idx < g_depth_char_count)  ? g_depth_chars[idx]  : '.';
        e->faces[i].color = (idx < g_depth_color_count) ? g_depth_colors[idx] : 90;
    }

    // Shade edges
    for (int i = 0; i < e->edge_count; i++) {
        Vec3f mid = vec3f_scale(vec3f_add(
            entity_local_to_world(e, e->edges[i].start),
            entity_local_to_world(e, e->edges[i].end)), 0.5f);
        float dist = vec3f_length(vec3f_sub(mid, active->pos));
        int idx = (int)(dist / DEPTH_DIVISOR);
        if (idx < 0) idx = 0;
        e->edges[i].ascii = (idx < g_depth_char_count)  ? g_depth_chars[idx]  : '.';
        e->edges[i].color = (idx < g_depth_color_count) ? g_depth_colors[idx] : 90;
    }
}

#endif // SHADER_DEPTH_H
