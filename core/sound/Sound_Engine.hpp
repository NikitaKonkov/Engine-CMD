#if !defined(SOUND_ENGINE_HPP)
#define SOUND_ENGINE_HPP

#include <stdint.h>

// ─── Limits ───────────────────────────────────────────────────────────────────

#define MAX_LOADED_SOUNDS  64   // max preloaded AudioAssets
#define MAX_PLAYING_SOUNDS 64   // max simultaneously playing sounds
#define MAX_PLAYING_TONES  64   // max simultaneously playing tones
#define FADE_SAMPLES_DEFAULT 4410  // ~100ms at 44100 Hz

// ─── AudioAsset ───────────────────────────────────────────────────────────────
// A fully decoded sound living on the heap.
// float32 interleaved at native sample rate. You get a raw pointer.
//
// Frame = one moment in time across all channels
//   Mono:   frame = [sample]
//   Stereo: frame = [L, R]
// Total floats in memory = frame_count * channels

struct AudioAsset {
    float*      samples;       // heap pointer to float32 interleaved PCM
    uint64_t    frame_count;   // total frames (duration = frame_count / sample_rate)
    int         channels;      // 1 = mono, 2 = stereo
    int         sample_rate;   // native rate, not resampled
    char        name[260];     // filename only, e.g. "kick.wav"
};

// ─── PlayingSound ─────────────────────────────────────────────────────────────
// Represents one active voice referencing an AudioAsset.
// The data_callback reads from asset->samples + position each frame.

struct PlayingSound {
    AudioAsset* asset;           // which loaded sound we're playing
    uint64_t    position;        // current frame position in the asset
    float       volume;          // 0.0 to 1.0+
    float       pitch;           // 1.0 = normal, 2.0 = double speed/octave up
    float       pos_x, pos_y, pos_z;  // 3D position of source (listener at origin)
    int         active;          // 1 = playing, 0 = slot is free
    int         looping;         // 1 = loop back to start when done
    int         paused;          // 1 = skip in mixer but keep position
    int         fade_state;      // 0=none, 1=fading in, 2=fading out (then stop)
    int         fade_counter;    // samples remaining in current fade
    int         fade_duration;   // total fade length in samples
    int         id;              // user-facing ID for stop/pause/etc
};

// ─── PlayingTone ──────────────────────────────────────────────────────────────
// A live synthesized waveform (sine). Each tone has its own frequency,
// amplitude, phase, and pan. Phase advances per sample:
//
//   phase += (2π × frequency) / sample_rate
//
// Multiple tones can be phase-aligned using sync groups:
//   1. Create tones with sound_tone_prepare() — they don't play yet
//   2. Call sound_tone_sync_and_start() — aligns phases, then activates all
//
// This guarantees constructive/destructive interference is intentional,
// not accidental from different start times.

struct PlayingTone {
    double      frequency;       // Hz (e.g. 440.0 for A4)
    double      phase;           // current phase in radians [0, 2π)
    double      phase_offset;    // user-set initial phase (for alignment)
    float       amplitude;       // 0.0 to 1.0
    float       pos_x, pos_y, pos_z;  // 3D position of source (listener at origin)
    int         active;          // 1 = generating audio, 0 = slot is free
    int         paused;          // 1 = skip in mixer but keep phase
    int         fade_state;      // 0=none, 1=fading in, 2=fading out (then stop)
    int         fade_counter;
    int         fade_duration;
    int         sync_group;      // -1 = none. Same group ID = phase-aligned start
    int         pending;         // 1 = prepared but not yet started (waiting for sync)
    int         id;              // user-facing ID
};

// ─── SoundEngine ──────────────────────────────────────────────────────────────
// The whole system. One global instance.
//
//   data_callback (called by miniaudio on audio thread):
//   ┌──────────────────────────────────────────────────────────┐
//   │  zero the output buffer                                  │
//   │                                                          │
//   │  for each active PlayingSound:                           │
//   │    read samples from asset, apply volume + pan           │
//   │    FMA into output buffer                                │
//   │                                                          │
//   │  for each active PlayingTone:                            │
//   │    compute sin(phase) × amplitude                        │
//   │    FMA into output buffer                                │
//   │    phase += 2π × frequency / sample_rate                 │
//   │                                                          │
//   │  → miniaudio sends buffer to speakers                    │
//   └──────────────────────────────────────────────────────────┘
//
// FMA = Fused Multiply-Add: output = fma(sample, volume, output)
// One instruction, one rounding. Matters when stacking 20+ voices.

struct SoundEngine {
    AudioAsset    assets[MAX_LOADED_SOUNDS];
    int           asset_count;

    PlayingSound  voices[MAX_PLAYING_SOUNDS];
    PlayingTone   tones[MAX_PLAYING_TONES];

    int           master_sample_rate;  // output device sample rate (e.g. 44100)
    int           initialized;

    // miniaudio device lives in the .cpp (opaque, we forward-declare size there)
    // This avoids pulling miniaudio.h into every file that includes this header
    void*         device_ptr;
};

// ─── API: Lifecycle ───────────────────────────────────────────────────────────

int  sound_init(SoundEngine* engine);
void sound_shutdown(SoundEngine* engine);

// ─── API: Asset Loading ───────────────────────────────────────────────────────

AudioAsset* sound_load(SoundEngine* engine, const char* filepath);
int         sound_load_folder(SoundEngine* engine, const char* folder_path);
AudioAsset* sound_find(SoundEngine* engine, const char* name);

// ─── API: Sound Playback (WAV/MP3 assets) ─────────────────────────────────────

int  sound_play(SoundEngine* engine, const char* name, float volume, int looping);
void sound_stop(SoundEngine* engine, int voice_id);
void sound_stop_all(SoundEngine* engine);
void sound_pause(SoundEngine* engine, int voice_id);
void sound_resume(SoundEngine* engine, int voice_id);
void sound_set_volume(SoundEngine* engine, int voice_id, float volume);
void sound_set_pitch(SoundEngine* engine, int voice_id, float pitch);
void sound_set_position(SoundEngine* engine, int voice_id, float x, float y, float z);

// ─── API: Tone Playback (synthesized waveforms) ──────────────────────────────

// Play a tone immediately. Returns tone ID (or -1).
int  tone_play(SoundEngine* engine, double frequency, float amplitude);

// Prepare a tone without starting it (pending=1). For phase alignment.
int  tone_prepare(SoundEngine* engine, double frequency, float amplitude, int sync_group);

// Start all pending tones in the given sync group at once, phases aligned.
// All tones in the group reset to their phase_offset simultaneously.
void tone_sync_and_start(SoundEngine* engine, int sync_group);

void tone_stop(SoundEngine* engine, int tone_id);
void tone_stop_all(SoundEngine* engine);
void tone_pause(SoundEngine* engine, int tone_id);
void tone_resume(SoundEngine* engine, int tone_id);

// Per-tone control — takes effect immediately (next buffer)
void tone_set_frequency(SoundEngine* engine, int tone_id, double frequency);
void tone_set_amplitude(SoundEngine* engine, int tone_id, float amplitude);
void tone_set_phase(SoundEngine* engine, int tone_id, double phase_radians);
void tone_set_position(SoundEngine* engine, int tone_id, float x, float y, float z);

// ─── API: Debug ───────────────────────────────────────────────────────────────

void sound_print_assets(SoundEngine* engine);

#endif // SOUND_ENGINE_HPP
