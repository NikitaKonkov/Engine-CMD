#include "clock.hpp"

LARGE_INTEGER ClockManager::performanceFrequency = {0};

////////////////////// Constructor - Initialize performance frequency
ClockManager::ClockManager() {
    if (performanceFrequency.QuadPart == 0) {
        QueryPerformanceFrequency(&performanceFrequency);
    }
    // Initialize all clocks as inactive
    for (int i = 0; i < MAX_CLOCKS; i++) {
        clocks[i] = EngineClock();
    }
}

////////////////////// Destructor - Clean up all active clocks
ClockManager::~ClockManager() {
    DestroyAllClocks();
}

////////////////////// Get current high-precision time
LARGE_INTEGER ClockManager::GetCurrentTime() {
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    return currentTime;
}

////////////////////// Convert LARGE_INTEGER to seconds
double ClockManager::GetTimeSeconds(LARGE_INTEGER time) {
    return (double)time.QuadPart / (double)performanceFrequency.QuadPart;
}

////////////////////// Create a new clock and return its ID
int ClockManager::CreateClock(double fps, const char* name) {
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (!clocks[i].active) {
            InitializeClock(i, fps, name);
            clocks[i].active = true;
            return i;
        }
    }
    return -1; // No available slots
}

////////////////////// Destroy a specific clock
void ClockManager::DestroyClock(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS) return;
    
    clocks[clockId] = EngineClock();
    clocks[clockId].active = false;
}

////////////////////// Synchronize clock and return true if frame should update
bool ClockManager::SyncClock(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) {
        return false;
    }

    EngineClock* clk = &clocks[clockId];
    LARGE_INTEGER currentTime = GetCurrentTime();

    // First frame
    if (clk->lastFrameTime.QuadPart == 0) {
        clk->lastFrameTime = currentTime;
        return true;
    }

    double elapsed = GetTimeSeconds(currentTime) - GetTimeSeconds(clk->lastFrameTime);

    // Not enough time has passed
    if (elapsed < clk->targetFrameDuration) {
        return false;
    }

    // Handle large time gaps (e.g., debugging pause)
    if (elapsed > (clk->targetFrameDuration * 3.0)) {
        clk->lastFrameTime = currentTime;
        clk->totalFrames++;
        clk->recentFrameCount++;
        UpdateFpsCounters(clockId, currentTime);
        return true;
    }

    // Normal frame update
    clk->lastFrameTime = currentTime;
    clk->totalFrames++;
    clk->recentFrameCount++;
    UpdateFpsCounters(clockId, currentTime);

    return true;
}

////////////////////// Set target FPS for a clock
void ClockManager::SetClockFps(int clockId, double fps) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return;
    
    if (fps <= 0.0) fps = 60.0;
    clocks[clockId].targetFps = fps;
    clocks[clockId].targetFrameDuration = 1.0 / fps;
}

////////////////////// Get current FPS
double ClockManager::GetCurrentFps(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return 0.0;
    return clocks[clockId].currentFps;
}

////////////////////// Get average FPS
double ClockManager::GetAverageFps(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return 0.0;
    return clocks[clockId].averageFps;
}

////////////////////// Get total frames processed
unsigned long ClockManager::GetTotalFrames(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return 0;
    return clocks[clockId].totalFrames;
}

////////////////////// Get uptime in seconds
double ClockManager::GetUptime(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return 0.0;
    
    if (clocks[clockId].startTime.QuadPart == 0) return 0.0;
    LARGE_INTEGER currentTime = GetCurrentTime();
    return GetTimeSeconds(currentTime) - GetTimeSeconds(clocks[clockId].startTime);
}

////////////////////// Get delta time since last frame
double ClockManager::GetDeltaTime(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return 0.0;
    
    LARGE_INTEGER currentTime = GetCurrentTime();
    if (clocks[clockId].lastFrameTime.QuadPart == 0) return 0.0;
    
    return GetTimeSeconds(currentTime) - GetTimeSeconds(clocks[clockId].lastFrameTime);
}

////////////////////// Get target FPS
double ClockManager::GetTargetFps(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return 0.0;
    return clocks[clockId].targetFps;
}

////////////////////// Get clock name
const char* ClockManager::GetClockName(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return "invalid";
    return clocks[clockId].name;
}

////////////////////// Check if clock is active
bool ClockManager::IsClockActive(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS) return false;
    return clocks[clockId].active;
}

////////////////////// Reset clock counters
void ClockManager::ResetCounters(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) return;
    
    EngineClock* clk = &clocks[clockId];
    clk->totalFrames = 0;
    clk->recentFrameCount = 0;
    clk->currentFps = 0.0;
    clk->averageFps = 0.0;
    clk->startTime = GetCurrentTime();
    clk->lastFpsUpdate = clk->startTime;
    clk->lastFrameTime.QuadPart = 0;
}

////////////////////// List all active clocks
void ClockManager::ListAllClocks() {
    printf("\n=== ACTIVE CLOCKS ===\n");
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (clocks[i].active) {
            printf("Clock %d (%s): Target %.4g FPS, Current %.1f FPS, Avg %.1f FPS, %lu frames, %.1fs uptime\n",
                   i, clocks[i].name, clocks[i].targetFps,
                   clocks[i].currentFps, clocks[i].averageFps,
                   clocks[i].totalFrames, GetUptime(i));
        }
    }
}

////////////////////// Count active clocks
int ClockManager::CountActiveClocks() {
    int count = 0;
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (clocks[i].active) count++;
    }
    return count;
}

////////////////////// Destroy all clocks
void ClockManager::DestroyAllClocks() {
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (clocks[i].active) {
            DestroyClock(i);
        }
    }
}

////////////////////// Print detailed clock information
void ClockManager::PrintClockInfo(int clockId) {
    if (clockId < 0 || clockId >= MAX_CLOCKS || !clocks[clockId].active) {
        printf("Clock %d: Invalid or inactive\n", clockId);
        return;
    }
    
    EngineClock* clk = &clocks[clockId];
    printf("Clock %d (%s):\n", clockId, clk->name);
    printf("  Target FPS: %.4g\n", clk->targetFps);
    printf("  Current FPS: %.2f\n", clk->currentFps);
    printf("  Average FPS: %.2f\n", clk->averageFps);
    printf("  Total Frames: %lu\n", clk->totalFrames);
    printf("  Uptime: %.2f seconds\n", GetUptime(clockId));
    printf("  Delta Time: %.4f seconds\n", GetDeltaTime(clockId));
}

////////////////////// Find clock by name
int ClockManager::FindClockByName(const char* name) {
    if (!name) return -1;
    
    for (int i = 0; i < MAX_CLOCKS; i++) {
        if (clocks[i].active && strcmp(clocks[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

////////////////////// Initialize a clock (private helper)
void ClockManager::InitializeClock(int clockId, double fps, const char* name) {
    EngineClock* clk = &clocks[clockId];
    
    clk->targetFps = (fps <= 0.0) ? 60.0 : fps;
    clk->targetFrameDuration = 1.0 / clk->targetFps;
    clk->startTime = GetCurrentTime();
    clk->lastFpsUpdate = clk->startTime;
    clk->totalFrames = 0;
    clk->recentFrameCount = 0;
    clk->currentFps = 0.0;
    clk->averageFps = 0.0;
    clk->lastFrameTime.QuadPart = 0;
    clk->active = true;
    
    if (name) {
        strncpy(clk->name, name, sizeof(clk->name) - 1);
        clk->name[sizeof(clk->name) - 1] = '\0';
    } else {
        strcpy(clk->name, "unnamed");
    }
}

////////////////////// Update FPS counters (private helper)
void ClockManager::UpdateFpsCounters(int clockId, LARGE_INTEGER currentTime) {
    EngineClock* clk = &clocks[clockId];
    
    double fpsElapsed = GetTimeSeconds(currentTime) - GetTimeSeconds(clk->lastFpsUpdate);
    
    // Update current FPS every 0.25 seconds
    if (fpsElapsed >= 0.25) {
        clk->currentFps = (double)clk->recentFrameCount / fpsElapsed;
        clk->recentFrameCount = 0;
        clk->lastFpsUpdate = currentTime;
    }
    
    // Update average FPS
    double totalElapsed = GetTimeSeconds(currentTime) - GetTimeSeconds(clk->startTime);
    if (totalElapsed > 0.0) {
        clk->averageFps = (double)clk->totalFrames / totalElapsed;
    }
}