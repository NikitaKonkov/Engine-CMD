#if !defined(CLOCK_HPP)
#define CLOCK_HPP

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAX_CLOCKS 16

// Clock structure for individual clocks
struct EngineClock {
    LARGE_INTEGER lastFrameTime;
    double targetFrameDuration;
    double targetFps;
    
    unsigned long totalFrames;
    LARGE_INTEGER startTime;
    LARGE_INTEGER lastFpsUpdate;
    int recentFrameCount;
    double currentFps;
    double averageFps;
    
    char name[32];
    bool active;
    
    EngineClock() : lastFrameTime({0}), targetFrameDuration(1.0/60.0), targetFps(60.0),
                   totalFrames(0), startTime({0}), lastFpsUpdate({0}), recentFrameCount(0),
                   currentFps(0.0), averageFps(0.0), active(false) {
        strcpy(name, "unnamed");
    }
};

// Clock Manager Class
class ClockManager {
private:
    static LARGE_INTEGER performanceFrequency;
    EngineClock clocks[MAX_CLOCKS];
    
    // Helper methods
    void InitializeClock(int clockId, double fps, const char* name);
    void UpdateFpsCounters(int clockId, LARGE_INTEGER currentTime);
    LARGE_INTEGER GetCurrentTime();
    double GetTimeSeconds(LARGE_INTEGER time);
    
public:
    // Constructor and Destructor
    ClockManager();
    ~ClockManager();
    
    // Clock Management Methods
    int CreateClock(double fps, const char* name = "unnamed");
    void DestroyClock(int clockId);
    bool SyncClock(int clockId);
    void SetClockFps(int clockId, double fps);
    
    // Clock Information Methods
    double GetCurrentFps(int clockId);
    double GetAverageFps(int clockId);
    unsigned long GetTotalFrames(int clockId);
    double GetUptime(int clockId);
    double GetDeltaTime(int clockId);
    double GetTargetFps(int clockId);
    const char* GetClockName(int clockId);
    bool IsClockActive(int clockId);
    
    // Clock Control Methods
    void ResetCounters(int clockId);
    void ListAllClocks();
    int CountActiveClocks();
    void DestroyAllClocks();
    
    // Utility Methods
    void PrintClockInfo(int clockId);
    int FindClockByName(const char* name);
};

#endif // CLOCK_HPP