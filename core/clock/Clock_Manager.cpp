// ═══════════════════════════════════════════════════════════════════════════════
// Clock_Manager.cpp — Cross-platform high-precision clock system
//
// Backend: C++ <chrono> steady_clock — monotonic, high-resolution, portable.
// Nanosecond timestamps stored as int64_t in EngineClock to keep <chrono>
// out of the header.
// ═══════════════════════════════════════════════════════════════════════════════

#include "Clock_Manager.hpp"

#include <chrono>
#include <stdio.h>
#include <string.h>

// ─── Internal time helpers ────────────────────────────────────────────────────

using SteadyClock = std::chrono::steady_clock;

static int64_t now_ns(void) {
    auto t = SteadyClock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t).count();
}

static double ns_to_sec(int64_t ns) {
    return (double)ns / 1000000000.0;
}

// ─── Global clock array ──────────────────────────────────────────────────────

static EngineClock g_clocks[MAX_CLOCKS] = {};

static int valid(int id) {
    return (id >= 0 && id < MAX_CLOCKS && g_clocks[id].active);
}

// ─── clock_create ─────────────────────────────────────────────────────────────

int clock_create(double fps, const char* name) {
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (!g_clocks[i].active) {
            EngineClock* c = &g_clocks[i];
            memset(c, 0, sizeof(EngineClock));

            if (fps <= 0.0) fps = 60.0;
            c->target_fps      = fps;
            c->target_duration = 1.0 / fps;
            c->start_time      = now_ns();
            c->last_fps_update = c->start_time;
            c->last_frame_time = 0;  // signals "first frame"
            c->active          = 1;

            if (name) {
                strncpy(c->name, name, sizeof(c->name) - 1);
                c->name[sizeof(c->name) - 1] = '\0';
            } else {
                strncpy(c->name, "unnamed", sizeof(c->name) - 1);
            }

            return i;
        }
    }
    printf("[Clock] No free slots (max %d)\n", MAX_CLOCKS);
    return -1;
}

// ─── clock_destroy ────────────────────────────────────────────────────────────

void clock_destroy(int clock_id) {
    if (clock_id < 0 || clock_id >= MAX_CLOCKS) return;
    memset(&g_clocks[clock_id], 0, sizeof(EngineClock));
}

void clock_destroy_all(void) {
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (g_clocks[i].active) clock_destroy(i);
    }
}

// ─── clock_sync ───────────────────────────────────────────────────────────────
// The heart of the system. Returns 1 when enough time has passed for the
// next frame. Computes delta, updates FPS counters.

int clock_sync(int clock_id) {
    if (!valid(clock_id)) return 0;

    EngineClock* c = &g_clocks[clock_id];
    int64_t current = now_ns();

    // First frame — always trigger
    if (c->last_frame_time == 0) {
        c->last_frame_time = current;
        c->delta_time      = 0.0;
        c->total_frames    = 1;
        c->recent_frame_count = 1;
        return 1;
    }

    double elapsed = ns_to_sec(current - c->last_frame_time);

    // Not time yet
    if (elapsed < c->target_duration) {
        return 0;
    }

    // Compute delta time
    c->delta_time = elapsed;

    // Clamp large gaps (e.g. debugger pause) to avoid physics explosions
    if (c->delta_time > c->target_duration * 3.0) {
        c->delta_time = c->target_duration;
    }

    c->last_frame_time = current;
    c->total_frames++;
    c->recent_frame_count++;

    // ── Update rolling FPS (every ~0.25s) ─────────────────────────────────
    double fps_elapsed = ns_to_sec(current - c->last_fps_update);
    if (fps_elapsed >= 0.25) {
        c->current_fps        = (double)c->recent_frame_count / fps_elapsed;
        c->recent_frame_count = 0;
        c->last_fps_update    = current;
    }

    // ── Update lifetime average FPS ───────────────────────────────────────
    double total_elapsed = ns_to_sec(current - c->start_time);
    if (total_elapsed > 0.0) {
        c->average_fps = (double)c->total_frames / total_elapsed;
    }

    return 1;
}

// ─── clock_set_fps ────────────────────────────────────────────────────────────

void clock_set_fps(int clock_id, double fps) {
    if (!valid(clock_id)) return;
    if (fps <= 0.0) fps = 60.0;
    g_clocks[clock_id].target_fps      = fps;
    g_clocks[clock_id].target_duration = 1.0 / fps;
}

// ─── clock_reset ──────────────────────────────────────────────────────────────

void clock_reset(int clock_id) {
    if (!valid(clock_id)) return;
    EngineClock* c = &g_clocks[clock_id];
    int64_t now = now_ns();
    c->start_time         = now;
    c->last_fps_update    = now;
    c->last_frame_time    = 0;
    c->total_frames       = 0;
    c->recent_frame_count = 0;
    c->current_fps        = 0.0;
    c->average_fps        = 0.0;
    c->delta_time         = 0.0;
}

// ─── Queries ──────────────────────────────────────────────────────────────────

double clock_get_fps(int clock_id) {
    if (!valid(clock_id)) return 0.0;
    return g_clocks[clock_id].current_fps;
}

double clock_get_avg_fps(int clock_id) {
    if (!valid(clock_id)) return 0.0;
    return g_clocks[clock_id].average_fps;
}

double clock_get_target_fps(int clock_id) {
    if (!valid(clock_id)) return 0.0;
    return g_clocks[clock_id].target_fps;
}

double clock_get_delta(int clock_id) {
    if (!valid(clock_id)) return 0.0;
    return g_clocks[clock_id].delta_time;
}

double clock_get_uptime(int clock_id) {
    if (!valid(clock_id)) return 0.0;
    if (g_clocks[clock_id].start_time == 0) return 0.0;
    return ns_to_sec(now_ns() - g_clocks[clock_id].start_time);
}

uint64_t clock_get_frames(int clock_id) {
    if (!valid(clock_id)) return 0;
    return g_clocks[clock_id].total_frames;
}

const char* clock_get_name(int clock_id) {
    if (!valid(clock_id)) return "invalid";
    return g_clocks[clock_id].name;
}

int clock_is_active(int clock_id) {
    if (clock_id < 0 || clock_id >= MAX_CLOCKS) return 0;
    return g_clocks[clock_id].active;
}

// ─── Utility ──────────────────────────────────────────────────────────────────

double clock_now(void) {
    return ns_to_sec(now_ns());
}

int clock_find(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (g_clocks[i].active && strcmp(g_clocks[i].name, name) == 0)
            return i;
    }
    return -1;
}

int clock_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (g_clocks[i].active) n++;
    }
    return n;
}

// ─── Debug ────────────────────────────────────────────────────────────────────

void clock_print(int clock_id) {
    if (!valid(clock_id)) {
        printf("[Clock %d] invalid or inactive\n", clock_id);
        return;
    }
    EngineClock* c = &g_clocks[clock_id];
    printf("[Clock %d] \"%s\"\n", clock_id, c->name);
    printf("  Target:  %.4g FPS (%.4f ms/frame)\n", c->target_fps, c->target_duration * 1000.0);
    printf("  Current: %.1f FPS\n", c->current_fps);
    printf("  Average: %.1f FPS\n", c->average_fps);
    printf("  Delta:   %.4f ms\n", c->delta_time * 1000.0);
    printf("  Frames:  %llu\n", (unsigned long long)c->total_frames);
    printf("  Uptime:  %.2f s\n", clock_get_uptime(clock_id));
}

void clock_print_all(void) {
    printf("\n=== Active Clocks (%d / %d) ===\n", clock_count(), MAX_CLOCKS);
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (g_clocks[i].active) {
            EngineClock* c = &g_clocks[i];
            printf("  [%2d] %-12s | target %6.1f | current %6.1f | avg %6.1f | %8llu frames | %.1fs\n",
                i, c->name, c->target_fps, c->current_fps, c->average_fps,
                (unsigned long long)c->total_frames, clock_get_uptime(i));
        }
    }
    printf("==============================\n\n");
}
