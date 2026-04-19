# Clock System – Multi-Speed Timing and Frame Rate Management

## Overview

The clock system provides utilities for managing multiple independent timing systems, including:
- ID-based clock creation and management
- Multi-speed frame timing (60 FPS, 120 FPS, 240 FPS, 360 FPS etc.)
- Real-time and average FPS monitoring
- Frame counting and uptime tracking

---

## Core API

### Clock Creation and Management

#### Clock Lifecycle

```c
// Create new clock with specified FPS and name, returns clock ID
int clock_create(int fps, const char* name);

// Destroy clock by ID
void clock_destroy(int clock_id);

// Check if enough time passed for clock to tick (returns 1 if should run, 0 if skip)
int clock_sync(int clock_id);

// Set new FPS for existing clock
void clock_set_fps(int clock_id, int fps);
```

#### Utility Functions

```c
// Print all active clocks with their statistics
void clock_list_all();

// Get number of currently active clocks
int clock_count_active();

// Destroy all active clocks (cleanup)
void clock_destroy_all();
```

### Clock Query Functions

#### Performance Monitoring

```c
// Get current real-time FPS (updated every 250ms)
double clock_get_current_fps(int clock_id);

// Get average FPS since clock creation
double clock_get_average_fps(int clock_id);

// Get total frames processed by this clock
unsigned long clock_get_total_frames(int clock_id);

// Get clock uptime in seconds
double clock_get_uptime(int clock_id);
```

#### Clock Information

```c
// Get target FPS setting
int clock_get_target_fps(int clock_id);

// Get clock name
const char* clock_get_name(int clock_id);

// Get time elapsed since last frame (for delta time calculations)
double clock_get_delta_time(int clock_id);

// Reset frame counters and statistics
void clock_reset_counters(int clock_id);
```

### Main Engine Clock (Convenience Functions)

#### Traditional Engine Clock API

```c
// Set main engine clock FPS (uses internal clock ID)
void engine_clock(int fps);

// Check main engine clock sync
int engine_sync();

// Get main engine clock statistics
double get_current_fps();
double get_average_fps();
unsigned long get_total_frames();
double get_uptime();
double get_delta_time();
int get_target_fps();
void reset_frame_counters();
```

---

## Usage Examples

### Basic Multi-Clock Setup

```c
#include "system/clock/clock.h"

int main() {
    // Create specialized clocks for different subsystems
    int render_clock = clock_create(240, "rendering");     // High-speed rendering
    int physics_clock = clock_create(120, "physics");      // Physics simulation
    int input_clock = clock_create(60, "input");           // Input processing
    int ui_clock = clock_create(30, "ui");                 // UI updates
    
    // Main engine clock (traditional API)
    engine_clock(60);
    
    while (1) {
        // Main engine timing
        if (!engine_sync()) continue;
        
        // High-speed rendering
        if (clock_sync(render_clock)) {
            // Render code here - runs at 240 FPS
        }
        
        // Physics updates
        if (clock_sync(physics_clock)) {
            // Physics code here - runs at 120 FPS
        }
        
        // Input processing
        if (clock_sync(input_clock)) {
            // Input handling - runs at 60 FPS
        }
        
        // UI updates (less frequent to save CPU)
        if (clock_sync(ui_clock)) {
            // UI refresh - runs at 30 FPS
            printf("Render: %.0f FPS, Physics: %.0f FPS\n",
                   clock_get_current_fps(render_clock),
                   clock_get_current_fps(physics_clock));
        }
    }
    
    // Cleanup
    clock_destroy_all();
    return 0;
}
```

### Clock Monitoring and Debugging

```c
// Print detailed statistics for specific clock
int my_clock = clock_create(120, "custom_system");

printf("Clock %d (%s): %.1f/%.0f FPS, %lu frames, %.2fs uptime\n",
       my_clock, clock_get_name(my_clock),
       clock_get_current_fps(my_clock),
       (double)clock_get_target_fps(my_clock),
       clock_get_total_frames(my_clock),
       clock_get_uptime(my_clock));

// List all active clocks
clock_list_all();

// Get system overview
printf("Total active clocks: %d\n", clock_count_active());
```

### Dynamic Clock Management

```c
// Create clock
int dynamic_clock = clock_create(60, "dynamic");

// Change speed at runtime
clock_set_fps(dynamic_clock, 120);  // Increase to 120 FPS

// Reset statistics
clock_reset_counters(dynamic_clock);

// Destroy when no longer needed
clock_destroy(dynamic_clock);
```

---

## Architecture

- `clock.h`: API definitions for all clock functions
- `engine_clock.c`: Implementation of ID-based clock system
- **Maximum Clocks**: 16 concurrent clocks (configurable via `MAX_CLOCKS`)
- **Timing Method**: Uses `clock()` function with microsecond precision
- **Frame Drop Handling**: Automatically skips ahead if >3 frame periods behind

---

## Key Features

### Performance Optimizations

- **Differential Timing**: Only processes frames when enough time has elapsed
- **Frame Drop Recovery**: Prevents halting when frame rate drops
- **Efficient FPS Calculation**: Updates real-time FPS every 250ms
- **Memory Efficient**: Fixed pool of clock structures

### Multi-Speed Capabilities

- **Independent Timing**: Each clock runs at its own speed
- **No Interference**: Fast clocks don't affect slow clocks
- **Flexible Assignment**: Any subsystem can use any clock speed
- **Real-time Adjustment**: Change FPS at runtime

### Debugging and Monitoring

- **Named Clocks**: Each clock has a descriptive name
- **Comprehensive Statistics**: Current FPS, average FPS, frame count, uptime
- **Easy Listing**: Print all active clocks at once
- **Performance Tracking**: Monitor each subsystem independently

---

## Common Use Cases

| System | Recommended FPS | Purpose |
|--------|----------------|---------|
| **Main Engine** | 60 FPS | Overall game loop control |
| **Rendering** | 120-240 FPS | Smooth graphics and animations |
| **Physics** | 120-240 FPS | Accurate collision detection |
| **Input Processing** | 60-120 FPS | Responsive controls |
| **UI Updates** | 30-60 FPS | Efficient interface refresh |
| **Sound Effects** | 240+ FPS | Ultra-smooth audio positioning |
| **Network** | 20-60 FPS | Multiplayer synchronization |
| **Debug Info** | 1-10 FPS | Performance monitoring |

---

This clock system enables precise, multi-speed timing control for complex applications requiring different subsystems to run at optimal frequencies.
