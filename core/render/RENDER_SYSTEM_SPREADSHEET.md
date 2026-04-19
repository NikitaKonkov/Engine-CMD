# Old Render System — Full Feature Spreadsheet
> Audit of all C source in `core/render/` before the crossplatform C++ rewrite.
> Verdict key: **KEEP** · **REWORK** · **DROP** · **ADAPT** · **REFERENCE**

---

## 1. SHADER.h / SHADER.c — Primitives & Shading

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `vertex {x,y,z}` | struct | Basic 3D world-space point | **KEEP** | Foundation type; rename to `Vec3f` or keep as `vertex` |
| `edge {start,end,ascii,color}` | struct | Line segment between 2 verts | **KEEP** | Wireframe mode; add per-vertex color in rework |
| `dot {position,ascii,color}` | struct | Single rendered point | **KEEP** | Useful for particles and debug visualisation |
| `face {vertices[4], vertex_count, texture*, color, ascii}` | struct | Tri or quad with optional flat texture | **REWORK** | Add per-vertex UV `{u,v}` and per-vertex normal; separate texture handle from raw pointer |
| `angle {a[2]}` | struct | Edge pair container for rotation shader | **DROP** | Pass two `vertex` values directly; struct is a pointless wrapper |
| `edge_distance_calc()` | func | Euclidean camera→edge-midpoint distance | **REWORK** | Missing `return` statement (UB); fold into rasterizer utilities |
| `dot_distance_calc()` | func | Euclidean camera→dot distance | **KEEP** | Correct; move to rasterizer math layer |
| `face_distance_calc()` | func | Euclidean camera→face-center distance | **KEEP** | Correct; has a typo (`mouse_cursour_x`) — fix on port |
| `edge_ascii_depth()` | func | Distance → ASCII char LUT (14 levels) | **KEEP concept** | Great for console depth cues; extract LUT to shared table |
| `dot_ascii_depth()` | func | Distance → ASCII char LUT (190+ levels) | **KEEP concept** | Very detailed; will be the per-pixel character resolver |
| `face_ascii_depth()` | func | Distance → ASCII char LUT (14 levels) | **KEEP concept** | Same LUT as edge; unify into one function with a density param |
| `edge_color_depth()` | func | Distance → ANSI 16-color LUT | **KEEP concept** | Unify all three `*_color_depth` into one `ansi_color_from_depth()` |
| `dot_color_depth()` | func | Distance → ANSI 16-color LUT | **KEEP concept** | See above |
| `face_color_depth()` | func | Distance → ANSI 16-color LUT | **KEEP concept** | See above |
| `edge_rotation_shader()` | func | View-angle → ASCII char (8 directions) | **KEEP** | Unique to ASCII rendering; computes atan2(cross, dot) correctly |
| `face_rotation_shader()` | func | Face-normal vs view-dir → ASCII char | **KEEP** | Good concept; rename `mouse_cursour_*` variables |
| `calculate_face_normal()` | func | Cross-product face normal + normalize | **KEEP** | Core math; move to a `math_utils` module |
| `dot_shader()` | func | Factory: vertex → shaded dot | **REWORK** | Shader should be a separate pass; don't bake into construction |
| `edge_shader()` | func | Factory: 2 verts → shaded edge | **REWORK** | Same issue; decouple construction from shading |
| `face_shader()` | func | Factory: face → shaded face | **REWORK** | Same issue |
| `face_rotation_shader_face()` | func | Factory: face → rotation-shaded face | **REWORK** | Duplicate of above with different shader; unify into shader-pass system |
| `create_edge_with_shader()` | func | Factory + auto-shade (rotation + rand color) | **REWORK** | `rand()` for color is a hack; pass explicit color or palette index |
| `create_face_with_shader()` | func | Factory + auto-shade for tris/quads | **REWORK** | Shader should be applied at draw time, not construction time |

---

## 2. CAMERA.h / CAMERA.c — Camera System

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `camera3d {x,y,z,yaw,pitch}` | struct | FPS-style Euler camera | **REWORK** | Add `roll`, separate `fov_x`/`fov_y`, move speed fields in |
| `camera` (global) | var | Singleton camera instance | **KEEP** | Keep as single global for now; initial pos `(100,-2.5,100)` is test data — reset to origin |
| `aspect_ratio_width/height` | globals | Console char-stretch compensation (1.0/2.0) | **REWORK** | Move inside `camera3d` struct; don't expose as bare globals |
| `culling_distance` (0.5f) | const | Near clip plane | **REWORK** | Move into `camera3d`; rename to `near_plane` |
| `view_distance` (100000.f) | const | Far clip plane | **REWORK** | Move into `camera3d`; rename to `far_plane` |
| `diagonal_x/y/z` | globals | Forward movement vector | **REWORK** | Move inside `camera3d` as `forward`; remove bare globals |
| `horizontal_x/y/z` | globals | Right strafe vector | **REWORK** | Move inside `camera3d` as `right`; remove bare globals |
| `camera_speed` (0.1f) | global | Movement speed | **REWORK** | Move inside `camera3d` |
| `camera_turn_speed` (0.1f) | global | Turn speed | **REWORK** | Move inside `camera3d` |
| `camera_cache {cos_yaw, sin_yaw, cos_pitch, sin_pitch, …}` | struct | Cached trig values for frame | **KEEP** | Excellent optimization; keep as internal rasterizer state |
| `cached_transform` | global | Singleton cache instance | **KEEP** | Keep; integrate into render state struct later |
| `update_camera_cache()` | func | Recompute sin/cos when camera moves | **KEEP** | Correct and efficient |
| `is_camera_cache_valid()` | func | Dirty-flag check on camera fields | **KEEP** | Correct pattern |
| `camera_update()` | func | Compute `forward`/`right` vectors + pitch clamp | **KEEP** | Core movement logic; small bug: computes cos/sin twice (uses cache above separately) |
| `#include "windows.h"` in CAMERA.c | dep | Windows-only for RENDER.h pull-in | **DROP** | Remove; camera has no business including Windows headers |

---

## 3. RASTERIZER.h / RASTERIZER.c — Framebuffer & Rasterization

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `pixel {ascii,color,depth,valid}` | struct | Single framebuffer cell | **KEEP** | Core of ASCII rendering; `valid` flag is good |
| `screen_buffer[2560][2560]` | array | Main framebuffer — **~26 MB static** | **REWORK** | Dynamic allocation (`malloc`/`calloc`) sized to actual terminal at startup |
| `previous_screen_buffer[2560][2560]` | array | Diff buffer — **~26 MB static** | **REWORK** | Same; dynamic alloc; differential rendering idea is excellent |
| `screen_width / screen_height` | globals | Active render resolution | **KEEP** | Keep; update on resize |
| `cmd_buffer_width / cmd_buffer_height` | globals | Console buffer size from WinAPI | **REWORK** | Replace WinAPI query with `con_get_size()` from Console_Manager |
| `frame_buffer_pos` | global | Write cursor into ANSI output string | **KEEP** | Fine as render-loop local state |
| `renderable {type, union{edge,dot,face}, depth}` | struct | Painter's-algorithm sort node | **DROP** | Z-buffer already handles depth; painter's sort is redundant |
| `compare_renderables_by_depth()` | func | `qsort` comparator back-to-front | **DROP** | Removed with Painter's algorithm |
| `calculate_renderable_depth()` | func | Per-type camera-space Z | **DROP** | Z-buffer depth comes from `project_vertex()` directly |
| `set_pixel()` | func | Bounds check + Z-test + write | **KEEP** | Core operation; already correct |
| `set_aspect_ratio()` / `get_aspect_ratio()` | funcs | Aspect ratio accessors | **REWORK** | Fold into camera struct accessors |
| `calculate_dot_distance()` | func | Camera→dot Euclidean dist | **KEEP** | Used for far-clip culling |
| `calculate_edge_distance()` | func | Camera→edge-midpoint dist | **KEEP** | Used for far-clip culling |
| `calculate_face_distance()` | func | Camera→face-center dist | **KEEP** | Used for far-clip culling |
| `project_vertex()` | func | World→screen with perspective, uses cache | **KEEP** | Correct; restructure signature to take `const camera3d*` instead of 9 loose params |
| `calculate_edge_depth()` | func | Camera-space Z of edge midpoint | **REWORK** | Keep the math; remove once depth comes directly from `project_vertex().z` |
| `draw_dot()` | func | Project + Z-cull + `set_pixel` | **KEEP** | The heart of it; camera params should come from camera struct, not be recomputed per call |
| `draw_edge()` | func | Project both ends + Bresenham line | **KEEP** | Correct Bresenham; camera params same issue |
| `draw_face()` | func | Barycentric tri + perspective-correct 1/z UV | **KEEP** | Solid algorithm; quad→2-triangle split present; comment says "brain damage version" — clean up |
| Bresenham line in `draw_edge()` | algo | Pixel-perfect integer line | **KEEP** | Correct standard implementation |
| Barycentric rasterizer in `draw_face()` | algo | Bounding-box + barycentric test | **KEEP** | Correct and working |
| Perspective-correct UV (`w0=1/z`, `w_interp`) | algo | 1/z interpolation for texture | **KEEP** | Correct technique |
| Quad→2-triangle split | algo | Rasterize quads as 2 tris | **KEEP** | Standard approach; currently only first tri is drawn — verify second tri code |
| `HASH_MAP_SIZE 256*256` | macro | Unused define | **DROP** | Dead code; never referenced |
| `#define M_PI` | macro | Pi constant | **REWORK** | Use `M_PI` from `<math.h>` or define once in a math header, not per-file |

---

## 4. RENDER.h / RENDER.c — Render Loop & Output

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `frame_buffer[2560*2560]` (static in .h) | array | ANSI output string — **~6.5 MB in header** | **REWORK** | Static array in a header is wrong; move to .c as dynamic alloc |
| `depth_buffer[2560*2560]` (static in .h) | array | Declared but unused — **~10 MB in header** | **DROP** | Depth is in `pixel.depth`; this is dead duplicate |
| `save_console_width / save_console_height` | globals | Previous frame console size for resize detect | **KEEP** | Good resize guard; move into render state struct |
| `init_rendering_system()` | func | One-time: `cls` + zero both buffers (O(n²)) | **REWORK** | Use `memset`; replace `system("cls")` with ANSI escape `\x1b[2J\x1b[H` |
| `init_frame_buffer()` | func | Per-frame: swap buffers + clear current | **KEEP** | Double-buffer concept is correct |
| `render_frame_buffer()` | func | Diff-scan → ANSI cursor-position output | **KEEP** | Excellent optimization; only updates changed pixels |
| `draw_unified()` | func | Submit face/edge/dot arrays + render | **REWORK** | Add a proper draw list / command buffer; remove hard ordering assumption |
| `cmd_init()` | func | WinAPI `GetConsoleScreenBufferInfo` | **REPLACE** | Use `con_get_size()` from Console_Manager for portability |
| `clock_tick()` | func | `clock()`-based 62.5 fps limiter | **REPLACE** | Use Clock_Manager |
| `output_buffer()` | func | Main loop callback: tick → init → debug → cam → mouse | **REWORK** | Good structure; decouple from Windows input calls |
| `debug_output()` | func | Corner markers + cam position to console | **KEEP** | Useful debug overlay; make conditional on a debug flag |
| `geometry_draw()` | func | Hardcoded test scene (empty body in read) | **DROP** | Demo scaffolding; replace with scene/mesh system |
| `#include "windows.h"` in RENDER.c | dep | Windows-only | **REPLACE** | All Windows calls go through Console_Manager / Input_Manager |

---

## 5. FACE_DRAWER.h / EDGE_DRAWER.h — Demo Scene Helpers

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `test_face_drawer()` | func | Hardcoded triangle + quad with heart texture | **DROP** | Test scaffolding |
| `draw_hearth_cube()` | func | Data-driven 6-face cube builder | **ADAPT** | Good pattern for a mesh builder API; extract the face-offset table approach |
| `draw_pyramid()` | func | 5-face pyramid builder | **ADAPT** | Same; useful pattern |
| `rgb_edge_cube()` | func | 12-edge wireframe cube | **ADAPT** | Keep as a `mesh_wireframe_box()` utility |
| Defined in `.h` files | issue | Function bodies in headers → ODR violations | **FIX** | Move to `.c` / `.cpp` files |

---

## 6. DOT_ANIMATION.h / EDGE_ANIMATION.h — Animations

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `dot_wave_grid()` | func | 256×256 animated wave particle grid | **ADAPT** | Great stress-test scene; becomes a demo/example not engine code |
| `dot_wave_cube()` | func | 6-face dot wave on a cube surface | **ADAPT** | Same; move to a `demos/` folder |
| `EDGE_ANIMATION.h` | file | Empty file | **DROP** | Delete |
| Static `time` variable inside functions | issue | Hidden animation state | **REWORK** | Pass elapsed time as parameter |

---

## 7. FACE_TEXTURE.h — Inline Textures

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `heart_texture[256]` | array | 16×16 ANSI-color int array hardcoded in header | **ADAPT** | Move to a texture loader; format becomes `uint8_t` palette indices |
| `checkerboard_texture[256]` | array | 16×16 checkerboard in header | **ADAPT** | Same; keep as a procedural generator instead |
| Defined in `.h` | issue | Global data in header → duplicate symbols | **FIX** | Move to `.c` with `extern` declarations |

---

## 8. TINYRENDERER Reference (`tinyrenderer-master/`)

| Symbol | Kind | What It Does | Verdict | Notes |
|---|---|---|---|---|
| `Model` class | C++ | OBJ loader: v/vt/vn/f, loads TGA textures | **ADAPT** | Rewrite as C-style `mesh_load_obj()` returning a flat vertex buffer |
| `TGAImage` class | C++ | Read/write TGA files | **ADAPT** | Rewrite as `tga_load()` / `tga_save()` with plain `uint8_t*` buffer |
| `lookat()` | func | View matrix from eye/center/up | **REFERENCE** | Adopt math; rewrite as `mat4_lookat()` |
| `init_perspective()` | func | Projection matrix | **REFERENCE** | Adopt math; rewrite as `mat4_perspective()` |
| `init_viewport()` | func | Viewport transform matrix | **REFERENCE** | Adopt math; rewrite as `mat4_viewport()` |
| `rasterize()` with `IShader` | func/interface | Clip-space triangle rasterizer | **REFERENCE** | Your own rasterizer already works; use this as a cross-check |
| `geometry.h` `vec`/`mat` templates | C++ templates | Linear algebra | **REFERENCE** | Replace with plain `float[4]` / `float[16]` arrays + free functions |

---

## 9. Known Bugs & Technical Debt (found during audit)

| Location | Issue | Severity |
|---|---|---|
| `SHADER.c: edge_distance_calc()` | Missing `return` statement — undefined behaviour | **Critical** |
| `RASTERIZER.c: draw_face()` quad path | Second triangle of quad split not verified to be drawn | **High** |
| `RENDER.h` | `frame_buffer` and `depth_buffer` declared `static` inside a header — every `.c` that includes this gets its own copy | **High** |
| `FACE_TEXTURE.h` / `FACE_DRAWER.h` / `EDGE_DRAWER.h` | Function and data definitions in headers — ODR violations on multi-TU builds | **High** |
| `RASTERIZER.c: screen_buffer[2560][2560]` × 2 | ~52 MB of static globals — crashes or corrupts on platforms with small BSS limits | **High** |
| `CAMERA.c` | `#include "windows.h"` pulled in transitively via `RENDER.h` — breaks non-Windows targets | **Medium** |
| `SHADER.c: face_distance_calc()` | Variable named `mouse_cursour_x` / `mouse_cursour_y` for face center X/Y | **Low (naming)** |
| `RASTERIZER.h` | `#define M_PI` collides with `<math.h>` on MSVC `/Za` or GCC `-std=c99` if `<math.h>` included first | **Low** |
| `DOT_ANIMATION.h` | Hidden animation state via `static float time` inside function body | **Low** |
| `RENDER.c: clock_tick()` | Uses `clock()` — counts CPU time not wall time; breaks at high CPU load | **Medium** |

---

## 10. Proposed New Architecture Map

```
Render_Engine.hpp          ← Public C++ API (no platform headers, no globals)
Render_Engine.cpp          ← Pimpl implementation

Internal modules (no public headers needed):
  math/     vec3.h mat4.h          ← float[3]/float[16] + free functions
  camera/   camera.h camera.cpp    ← camera3d struct + update; no globals
  raster/   raster.h raster.cpp    ← dynamic pixel buffer + set_pixel + project_vertex
  shader/   shader.h shader.cpp    ← LUT functions; no factory side-effects
  draw/     draw.h  draw.cpp       ← draw_dot / draw_edge / draw_face
  output/   output.h output.cpp    ← diff-render to ANSI; uses Console_Manager
  texture/  texture.h texture.cpp  ← tga_load + palette; no inline data
  mesh/     mesh.h  mesh.cpp       ← obj_load; flat vertex/index buffers
```

### What stays from the old system
- All depth/distance LUT logic  
- `camera_cache` optimization  
- `set_pixel()` + Z-buffer  
- `project_vertex()` math  
- Bresenham line  
- Barycentric tri rasterizer with perspective-correct 1/z UV  
- Differential ANSI output (`render_frame_buffer` concept)  

### What gets replaced
| Old | New |
|---|---|
| `screen_buffer[2560][2560]` static | `pixel* screen_buffer` dynamic, sized at init |
| `cmd_init()` WinAPI | `con_get_size()` from Console_Manager |
| `clock_tick()` / `clock()` | Clock_Manager |
| `system("cls")` | `\x1b[2J\x1b[H` ANSI escape |
| Windows headers in camera/render | Isolated behind Console_Manager / Input_Manager |
| Painter's algorithm + `renderable` union | Z-buffer only (already present) |
| Shader baked at construction | Shader applied as a per-pixel pass at draw time |
| Function bodies in `.h` files | All definitions in `.cpp` / `.c` files |
