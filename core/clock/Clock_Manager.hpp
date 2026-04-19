#if !defined(CLOCK_MANAGER_HPP)
#define CLOCK_MANAGER_HPP

// ═══════════════════════════════════════════════════════════════════════════════
// Clock_Manager.hpp — Cross-platform high-precision clock system
//
// Uses C++ <chrono> steady_clock internally — no platform-specific types
// leak into this header. All timing is in seconds (double).
//
// Each clock tracks:
//   - Target FPS and frame duration
//   - Current (rolling) and average FPS
//   - Total frame count, uptime, delta time
//   - Named slots for identification
//
// SyncClock() is the core: call it in your loop, it returns 1 when enough
// time has passed for the next frame at the target FPS. Spin-waits internally
// for sub-millisecond accuracy.
// ═══════════════════════════════════════════════════════════════════════════════

#include <stdint.h>

#define MAX_CLOCKS 16

// ─── Clock slot ───────────────────────────────────────────────────────────────
// Opaque time values stored as int64_t nanoseconds from steady_clock epoch.
// Avoids exposing <chrono> in the header.

struct EngineClock {
    int64_t     start_time;          // ns — when this clock was created
    int64_t     last_frame_time;     // ns — timestamp of last SyncClock success
    int64_t     last_fps_update;     // ns — when we last computed current_fps
    double      target_fps;          // desired frames per second
    double      target_duration;     // 1.0 / target_fps (seconds)
    double      current_fps;         // rolling FPS (updated every ~0.25s)
    double      average_fps;         // lifetime average
    double      delta_time;          // seconds between last two frames
    uint64_t    total_frames;        // lifetime frame count
    int         recent_frame_count;  // frames since last fps update
    char        name[32];            // human-readable label
    int         active;              // 1 = in use, 0 = free slot
};

// ─── API: Lifecycle ───────────────────────────────────────────────────────────

// Create a clock targeting `fps` frames per second. Returns clock ID (0-15) or -1.
int  clock_create(double fps, const char* name);

// Destroy a single clock by ID.
void clock_destroy(int clock_id);

// Destroy all active clocks.
void clock_destroy_all(void);

// ─── API: Frame Sync ─────────────────────────────────────────────────────────

// Call this in your loop. Returns 1 when it's time for the next frame.
// Handles FPS counting, delta time, and skip-ahead for large gaps.
int  clock_sync(int clock_id);

// ─── API: Control ─────────────────────────────────────────────────────────────

// Change target FPS at runtime.
void clock_set_fps(int clock_id, double fps);

// Reset all counters (frames, uptime, averages) without destroying.
void clock_reset(int clock_id);

// ─── API: Queries ─────────────────────────────────────────────────────────────

double      clock_get_fps(int clock_id);         // current (rolling) FPS
double      clock_get_avg_fps(int clock_id);     // lifetime average FPS
double      clock_get_target_fps(int clock_id);  // target FPS setting
double      clock_get_delta(int clock_id);       // seconds since last frame
double      clock_get_uptime(int clock_id);      // seconds since creation
uint64_t    clock_get_frames(int clock_id);      // total frame count
const char* clock_get_name(int clock_id);        // clock name
int         clock_is_active(int clock_id);       // 1 if slot is in use

// ─── API: Utility ─────────────────────────────────────────────────────────────

// Get current wall time in seconds (monotonic, high-res).
double clock_now(void);

// Find a clock by name. Returns ID or -1.
int  clock_find(const char* name);

// Count how many clocks are active.
int  clock_count(void);

// ─── API: Debug ──────────────────────────────────────────────────────────────

// Print info for one clock.
void clock_print(int clock_id);

// Print all active clocks.
void clock_print_all(void);

#endif // CLOCK_MANAGER_HPP
