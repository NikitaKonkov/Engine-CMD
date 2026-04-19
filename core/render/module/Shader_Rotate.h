#ifndef SHADER_ROTATE_H
#define SHADER_ROTATE_H

// ═══════════════════════════════════════════════════════════════════════════════
// Shader_Rotate.h — Viewing-angle-based ASCII character selection
//
// Adapted from the original SHADER.c rotation shader.
// Changes the ASCII character of faces and edges based on the angle between
// the surface normal (or edge direction) and the camera's view direction.
// Head-on faces get dense characters; glancing faces get sparse ones.
//
// Usage:
//   entity_set_shader(id, shader_rotate);
// ═══════════════════════════════════════════════════════════════════════════════

#include "Entity.h"

// Face characters ordered by angle (head-on → glancing)
static const char g_rotate_face_chars[] = {
    '#', '#', '@', '@', '&', '&', '%', '%',
    'O', 'O', '*', '*', '+', '+', '=', '=',
    '-', '-', ':', ':', '.', '.', ' ', ' '
};
static const int g_rotate_face_count = sizeof(g_rotate_face_chars) / sizeof(g_rotate_face_chars[0]);

// Edge characters based on direction relative to view
static const char g_rotate_edge_chars[] = {
    '|', '/', '-', '\\', '|', '/', '-', '\\'
};

static void shader_rotate(Entity* e, float dt) {
    (void)dt;
    Camera* active = cam_get_active();
    if (!active) return;

    // ── Shade faces ──────────────────────────────────────────────────────
    for (int i = 0; i < e->face_count; i++) {
        // Transform verts to world space for angle calculation
        Vec3f w0 = entity_local_to_world(e, e->faces[i].verts[0]);
        Vec3f w1 = entity_local_to_world(e, e->faces[i].verts[1]);
        Vec3f w2 = entity_local_to_world(e, e->faces[i].verts[2]);

        // Face normal
        Vec3f normal = vec3f_normalize(vec3f_cross(
            vec3f_sub(w1, w0), vec3f_sub(w2, w0)));

        // Face center
        Vec3f center = vec3f_scale(vec3f_add(vec3f_add(w0, w1), w2), 1.0f / 3.0f);

        // View direction: camera → face center
        Vec3f view = vec3f_normalize(vec3f_sub(center, active->pos));

        // Angle between normal and view (0° = head-on, 90° = glancing)
        float dp = vec3f_dot(normal, view);
        if (dp < -1.0f) dp = -1.0f;
        if (dp >  1.0f) dp =  1.0f;
        float angle_deg = acosf(fabsf(dp)) * 180.0f / (float)M_PI;

        int idx = (int)(angle_deg / (90.0f / (float)g_rotate_face_count));
        if (idx < 0) idx = 0;
        if (idx >= g_rotate_face_count) idx = g_rotate_face_count - 1;
        e->faces[i].ascii = g_rotate_face_chars[idx];
    }

    // ── Shade edges ──────────────────────────────────────────────────────
    for (int i = 0; i < e->edge_count; i++) {
        Vec3f ws = entity_local_to_world(e, e->edges[i].start);
        Vec3f we = entity_local_to_world(e, e->edges[i].end);
        Vec3f edge_dir = vec3f_normalize(vec3f_sub(we, ws));
        Vec3f mid  = vec3f_scale(vec3f_add(ws, we), 0.5f);
        Vec3f view = vec3f_normalize(vec3f_sub(mid, active->pos));

        float dp = vec3f_dot(edge_dir, view);
        Vec3f cross = vec3f_cross(edge_dir, view);
        float cross_mag = vec3f_length(cross);
        float angle = atan2f(cross_mag, fabsf(dp)) * 180.0f / (float)M_PI;

        int idx = ((int)(angle * 4.0f) / 45) % 8;
        if (idx < 0) idx = 0;
        if (idx > 7) idx = 7;
        e->edges[i].ascii = g_rotate_edge_chars[idx];
    }
}

#endif // SHADER_ROTATE_H
