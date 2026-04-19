# Sound Engine API Reference

## Core Types

| Type | Definition | Notes |
|---|---|---|
| `SoundID` | `typedef int SoundID` | `>= 0` = pinned slot; `AUDIO_AUTO` = engine picks |
| `AUDIO_AUTO` | `#define AUDIO_AUTO -1` | Pass as `id` to get an auto-assigned slot |
| `AUDIO_INVALID` | `#define AUDIO_INVALID -1` | Returned on failure |

---

## `PlayParams` Struct

Replaces 7 separate play functions. Use designated initializers to set only what you need.

```cpp
PlayParams p = { .amplitude = 0.5f, .loop = true, .start_delay = 1.0 };
```

| Field | Type | Default | Description |
|---|---|---|---|
| `amplitude` | `float` | `1.0f` | 0.0 – 1.0 volume |
| `pitch` | `float` | `1.0f` | 1.0 = normal, 2.0 = +1 octave, 0.5 = -1 octave |
| `loop` | `bool` | `false` | Repeat when done |
| `duration` | `double` | `0.0` | Seconds; 0 = play to end / infinite |
| `start_delay` | `double` | `0.0` | Seconds before playback begins |
| `fade_in_ms` | `int` | `50` | Fade-in duration in milliseconds |
| `fade_out_ms` | `int` | `50` | Fade-out duration in milliseconds |
| `angle` | `float` | `0.0f` | Stereo pan in degrees: 0=front, 90=right, 180=back, 270=left |
| `bus` | `int` | `BUS_SFX` | Which mix bus to route to |
| `priority` | `SoundPriority` | `PRIORITY_NORMAL` | Controls slot-stealing when all slots are full |

---

## Enums

```cpp
enum SoundPriority {
    PRIORITY_LOW,
    PRIORITY_NORMAL,
    PRIORITY_HIGH,
    PRIORITY_CRITICAL   // never stolen
};

enum FadeState {
    FADE_DELAY  = 0,    // waiting for start_delay
    FADE_IN     = 1,    // ramping up
    PLAYING     = 2,    // full volume
    TIMED       = 3,    // playing, counting down duration
    FADE_OUT    = 4,    // ramping down
    STOPPED     = 5     // inactive
};
```

---

## Bus IDs

| Constant | Value | Purpose |
|---|---|---|
| `BUS_MASTER` | `0` | Global output — affects everything |
| `BUS_MUSIC` | `1` | Background music |
| `BUS_SFX` | `2` | Sound effects (default) |
| `BUS_UI` | `3` | Interface sounds |
| `BUS_AMBIENT` | `4` | Environment / ambience |
| `NUM_BUSES` | `8` | Total available buses |

---

## 3D Audio Types

```cpp
struct Vec3 {
    float x, y, z;
};

struct AudioListener {
    Vec3  position;
    Vec3  forward;
    Vec3  up;
    float max_distance;  // beyond this, volume = 0
    float rolloff;       // attenuation curve steepness
};
```

---

## Engine Lifecycle

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `AudioEngine::Get()` | — | `AudioEngine&` | Singleton accessor |
| `Init()` | `int sample_rate=44100, int buf_ms=25` | `bool` | Start engine and audio thread |
| `Shutdown()` | — | `void` | Drain all sounds, free all memory |
| `IsInitialized()` | — | `bool` | Safe to call before Init |

---

## Bus & Master Control

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `SetMasterVolume()` | `float volume` | `void` | 0.0 – 1.0 |
| `GetMasterVolume()` | — | `float` | — |
| `SetBusVolume()` | `int bus, float volume` | `void` | Per-bus gain |
| `GetBusVolume()` | `int bus` | `float` | — |
| `MuteBus()` | `int bus, bool mute` | `void` | Silent but still advancing playback position |
| `PauseBus()` | `int bus, bool pause` | `void` | Freeze all sounds on this bus |
| `StopBus()` | `int bus` | `void` | Stop and deactivate all sounds on this bus |

---

## Asset Management

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `LoadWAV()` | `const char* filename` | `bool` | File I/O runs outside the lock; result is cached |
| `UnloadWAV()` | `const char* filename` | `void` | Nullifies any live WavSound pointers safely |
| `UnloadAllWAVs()` | — | `void` | — |
| `GetWAVInfo()` | `const char* filename` | `WAVInfo` | Returns sample_rate, total_samples |
| `IsWAVLoaded()` | `const char* filename` | `bool` | — |

---

## Tone Playback

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `PlayTone()` | `SoundID id, double freq, PlayParams p={}` | `SoundID` | Start a sine tone |
| `PlayToneSync()` | `SoundID id, double freq, PlayParams p={}` | `SoundID` | Start on next buffer boundary (click-free) |

Pass `AUDIO_AUTO` as `id` for fire-and-forget tones. Returns `AUDIO_INVALID` on failure.

---

## WAV Playback

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `PlayWAV()` | `SoundID id, const char* file, PlayParams p={}` | `SoundID` | Loads if not cached; all timing via PlayParams |
| `PlayWAVRange()` | `SoundID id, const char* file, int start_sample, int end_sample, PlayParams p={}` | `SoundID` | Play or loop a specific sample window |

### How `PlayParams` replaces the old 7 functions

| Old function | New equivalent |
|---|---|
| `play_WAV_by_duration(id, f, amp, dur)` | `PlayWAV(id, f, {.amplitude=amp, .duration=dur})` |
| `play_repeating_WAV(id, f, amp)` | `PlayWAV(id, f, {.amplitude=amp, .loop=true})` |
| `play_delayed_WAV_by_duration(id, f, amp, dur, del)` | `PlayWAV(id, f, {.amplitude=amp, .duration=dur, .start_delay=del})` |
| `play_delayed_repeating_WAV(id, f, amp, del)` | `PlayWAV(id, f, {.amplitude=amp, .loop=true, .start_delay=del})` |
| `play_repeating_delayed_WAV_by_duration(id, f, amp, dur, del)` | `PlayWAV(id, f, {.amplitude=amp, .loop=true, .duration=dur, .start_delay=del})` |
| `play_specific_part_WAV(id, f, amp, s, e)` | `PlayWAVRange(id, f, s, e, {.amplitude=amp})` |
| `play_repeating_specific_part_WAV(id, f, amp, s, e)` | `PlayWAVRange(id, f, s, e, {.amplitude=amp, .loop=true})` |

---

## Universal Sound Control

These work on **any** `SoundID` — tone or WAV — with no offset convention.

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `Stop()` | `SoundID id` | `void` | Triggers fade-out then deactivates |
| `StopImmediate()` | `SoundID id` | `void` | No fade, instant silence |
| `StopAll()` | — | `void` | All active sounds |
| `Pause()` | `SoundID id` | `void` | Freeze playback position |
| `Resume()` | `SoundID id` | `void` | Continue from paused position |
| `IsActive()` | `SoundID id` | `bool` | True until fully deactivated (includes fade-out tail) |
| `SetVolume()` | `SoundID id, float vol` | `void` | 0.0 – 1.0 |
| `SetPitch()` | `SoundID id, float pitch` | `void` | 1.0 = normal; uses linear interpolation |
| `SetAngle()` | `SoundID id, float degrees` | `void` | 2D stereo pan |
| `SetPosition()` | `SoundID id, Vec3 pos` | `void` | 3D position; uses listener for distance attenuation |
| `SetFadeIn()` | `SoundID id, int ms` | `void` | Override fade-in duration on active sound |
| `SetFadeOut()` | `SoundID id, int ms` | `void` | Override fade-out duration on active sound |
| `SetBus()` | `SoundID id, int bus` | `void` | Reroute to a different bus |
| `SetLoop()` | `SoundID id, bool loop` | `void` | Toggle looping on active sound |

---

## Effects

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `SetReverb()` | `SoundID id, float amount, float decay` | `void` | amount: 0.0–1.0, decay: 0.1–0.9 |
| `ClearReverb()` | `SoundID id` | `void` | Zeros reverb buffer and disables effect |

---

## 3D Audio

| Function | Parameters | Returns | Description |
|---|---|---|---|
| `SetListener()` | `const AudioListener& l` | `void` | Call once per frame from game thread |
| `GetListener()` | — | `AudioListener` | — |

---

## Usage Examples

```cpp
AudioEngine& audio = AudioEngine::Get();
audio.Init();

// Fire-and-forget WAV (no id needed)
audio.PlayWAV(AUDIO_AUTO, "sfx/explosion.wav", { .amplitude = 0.8f });

// Pinned music loop on music bus
audio.PlayWAV(0, "music/theme.wav", {
    .amplitude  = 0.6f,
    .loop       = true,
    .fade_in_ms = 2000,
    .bus        = BUS_MUSIC
});

// Stop music with fade
audio.Stop(0);

// Sine tone, 1 second, panned right, fire-and-forget
audio.PlayTone(AUDIO_AUTO, 440.0, {
    .amplitude = 0.5f,
    .duration  = 1.0,
    .angle     = 90.0f
});

// Bus volume control
audio.SetBusVolume(BUS_MUSIC, 0.4f);
audio.SetMasterVolume(0.9f);

audio.Shutdown();
```
