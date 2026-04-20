#if !defined(UI_SYSTEM_HPP)
#define UI_SYSTEM_HPP

// ═══════════════════════════════════════════════════════════════════════════════
// UI_system.hpp — HUD overlay: FPS, resolution, camera info, 3D axis gizmo
//
// Draws directly into the camera's front buffer at depth 0 (always on top),
// so the overlay participates in differential ANSI output — no tearing.
//
// The axis gizmo shows world X (red), Y (green), Z (blue) as they appear
// from the camera's current viewpoint.  Rotates as you look around.
//
// Usage:
//   ui_draw(cam_id, clk_id);   // call after scene, before render_present
// ═══════════════════════════════════════════════════════════════════════════════

// Draw the full HUD overlay into the camera's front buffer.
//   cam_id  — camera whose buffer receives the overlay
//   clk_id  — clock to read FPS / delta from
void ui_draw(int cam_id, int clk_id);

#endif // UI_SYSTEM_HPP