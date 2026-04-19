// ═══════════════════════════════════════════════════════════════════════════════
// Sound_Engine.cpp — Cross-platform audio engine using miniaudio
//
// miniaudio handles: device enumeration, format conversion, audio thread,
//                    WAV/MP3/FLAC decoding, all OS backends
//
// We handle:         asset cache, tone synthesis, mixer with FMA, sync groups
// ═══════════════════════════════════════════════════════════════════════════════

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "Sound_Engine.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Sine Table ───────────────────────────────────────────────────────────────
// Precomputed 1024-point sine for fast tone generation.
// Lookup + linear interpolation is faster than sin() and good enough for audio.

#define SINE_TABLE_SIZE 1024
static float  g_sine_table[SINE_TABLE_SIZE];
static int    g_sine_ready = 0;

static void init_sine_table(void) {
    if (g_sine_ready) return;
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        g_sine_table[i] = sinf((float)(2.0 * M_PI * i / SINE_TABLE_SIZE));
    }
    g_sine_ready = 1;
}

// Fast sine using table + linear interpolation
static inline float fast_sin(double phase) {
    // Normalize to [0, TABLE_SIZE)
    double idx = phase * (SINE_TABLE_SIZE / (2.0 * M_PI));
    int    i0  = (int)idx & (SINE_TABLE_SIZE - 1);
    int    i1  = (i0 + 1) & (SINE_TABLE_SIZE - 1);
    float  frac = (float)(idx - (int)idx);
    return g_sine_table[i0] + frac * (g_sine_table[i1] - g_sine_table[i0]);
}

// ─── Filename helper ──────────────────────────────────────────────────────────

static const char* get_filename(const char* path) {
    const char* s1 = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    const char* slash = s1 > s2 ? s1 : s2;
    return slash ? slash + 1 : path;
}

// ─── 3D spatial helper ────────────────────────────────────────────────────────
// Listener at (0,0,0). Source at (x,y,z).
//   pan        = x / distance   (clamped to [-1,1])
//   attenuation = 1 / (1 + distance)  (full at origin, halves per unit)

static void compute_3d(float px, float py, float pz, float* out_pan, float* out_atten) {
    float dist = sqrtf(px * px + py * py + pz * pz);
    if (dist < 0.0001f) {
        *out_pan   = 0.0f;
        *out_atten = 1.0f;
        return;
    }
    // Pan from X axis, clamped
    float p = px / dist;
    if (p < -1.0f) p = -1.0f;
    if (p >  1.0f) p =  1.0f;
    *out_pan   = p;
    *out_atten = 1.0f / (1.0f + dist);
}

// ─── data_callback ────────────────────────────────────────────────────────────
// Called by miniaudio on its audio thread whenever the device needs more data.
// We mix all active voices + tones into the output buffer using FMA.
//
// Output format: float32, stereo (2 channels), at engine->master_sample_rate.

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    SoundEngine* engine = (SoundEngine*)pDevice->pUserData;
    float* out = (float*)pOutput;
    int    sr  = engine->master_sample_rate;

    // Buffer is already zeroed by miniaudio

    // ── Mix PlayingSounds ─────────────────────────────────────────────────────
    for (int v = 0; v < MAX_PLAYING_SOUNDS; v++) {
        PlayingSound* voice = &engine->voices[v];
        if (!voice->active || voice->paused || !voice->asset) continue;

        AudioAsset* a = voice->asset;
        float vol = voice->volume;
        float pan;
        float atten;
        compute_3d(voice->pos_x, voice->pos_y, voice->pos_z, &pan, &atten);
        vol *= atten;

        // Pan law: constant-power approximation
        float left_vol  = vol * (1.0f - pan) * 0.5f;
        float right_vol = vol * (1.0f + pan) * 0.5f;

        for (ma_uint32 i = 0; i < frameCount; i++) {
            // Fade envelope
            float env = 1.0f;
            if (voice->fade_state == 1) { // fade in
                env = (float)voice->fade_counter / (float)voice->fade_duration;
                voice->fade_counter++;
                if (voice->fade_counter >= voice->fade_duration) voice->fade_state = 0;
            } else if (voice->fade_state == 2) { // fade out
                env = 1.0f - (float)voice->fade_counter / (float)voice->fade_duration;
                voice->fade_counter++;
                if (voice->fade_counter >= voice->fade_duration) {
                    voice->active = 0;
                    break;
                }
            }

            uint64_t pos = voice->position;
            if (pos >= a->frame_count) {
                if (voice->looping) {
                    voice->position = 0;
                    pos = 0;
                } else {
                    voice->active = 0;
                    break;
                }
            }

            float L, R;
            if (a->channels == 2) {
                L = a->samples[pos * 2];
                R = a->samples[pos * 2 + 1];
            } else {
                L = R = a->samples[pos];
            }

            // FMA: output += sample * volume * envelope
            out[i * 2]     = fmaf(L * env, left_vol, out[i * 2]);
            out[i * 2 + 1] = fmaf(R * env, right_vol, out[i * 2 + 1]);

            voice->position++;
        }
    }

    // ── Mix PlayingTones ──────────────────────────────────────────────────────
    double phase_inc_base = 2.0 * M_PI / (double)sr;

    for (int t = 0; t < MAX_PLAYING_TONES; t++) {
        PlayingTone* tone = &engine->tones[t];
        if (!tone->active || tone->paused || tone->pending) continue;

        float amp = tone->amplitude;
        float pan;
        float atten;
        compute_3d(tone->pos_x, tone->pos_y, tone->pos_z, &pan, &atten);
        amp *= atten;

        float left_vol  = amp * (1.0f - pan) * 0.5f;
        float right_vol = amp * (1.0f + pan) * 0.5f;
        double phase = tone->phase;
        double phase_inc = phase_inc_base * tone->frequency;

        for (ma_uint32 i = 0; i < frameCount; i++) {
            // Fade envelope
            float env = 1.0f;
            if (tone->fade_state == 1) {
                env = (float)tone->fade_counter / (float)tone->fade_duration;
                tone->fade_counter++;
                if (tone->fade_counter >= tone->fade_duration) tone->fade_state = 0;
            } else if (tone->fade_state == 2) {
                env = 1.0f - (float)tone->fade_counter / (float)tone->fade_duration;
                tone->fade_counter++;
                if (tone->fade_counter >= tone->fade_duration) {
                    tone->active = 0;
                    break;
                }
            }

            float sample = fast_sin(phase) * env;

            // FMA into stereo output
            out[i * 2]     = fmaf(sample, left_vol, out[i * 2]);
            out[i * 2 + 1] = fmaf(sample, right_vol, out[i * 2 + 1]);

            phase += phase_inc;
            // Keep phase in [0, 2π) to avoid floating point drift over time
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }

        tone->phase = phase;
    }

    (void)pInput; // unused (playback only)
}

// ─── sound_init ───────────────────────────────────────────────────────────────

int sound_init(SoundEngine* engine) {
    memset(engine, 0, sizeof(SoundEngine));
    init_sine_table();

    // Init all tone sync groups to -1 (no group)
    for (int i = 0; i < MAX_PLAYING_TONES; i++) {
        engine->tones[i].sync_group = -1;
    }

    engine->master_sample_rate = 44100;

    // Allocate miniaudio device on heap (keeps it out of the header)
    ma_device* device = (ma_device*)malloc(sizeof(ma_device));
    if (!device) {
        printf("[SoundEngine] Failed to allocate device\n");
        return 0;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = engine->master_sample_rate;
    config.dataCallback      = data_callback;
    config.pUserData         = engine;

    if (ma_device_init(NULL, &config, device) != MA_SUCCESS) {
        printf("[SoundEngine] Failed to init audio device\n");
        free(device);
        return 0;
    }

    engine->device_ptr = device;

    if (ma_device_start(device) != MA_SUCCESS) {
        printf("[SoundEngine] Failed to start audio device\n");
        ma_device_uninit(device);
        free(device);
        engine->device_ptr = NULL;
        return 0;
    }

    engine->initialized = 1;
    printf("[SoundEngine] Initialized: %d Hz, stereo, float32\n", engine->master_sample_rate);
    return 1;
}

// ─── sound_shutdown ───────────────────────────────────────────────────────────

void sound_shutdown(SoundEngine* engine) {
    if (!engine->initialized) return;

    if (engine->device_ptr) {
        ma_device_uninit((ma_device*)engine->device_ptr);
        free(engine->device_ptr);
        engine->device_ptr = NULL;
    }

    // Free all loaded asset sample data
    for (int i = 0; i < engine->asset_count; i++) {
        if (engine->assets[i].samples) {
            free(engine->assets[i].samples);
            engine->assets[i].samples = NULL;
        }
    }

    engine->initialized = 0;
    printf("[SoundEngine] Shutdown complete\n");
}

// ─── sound_load ───────────────────────────────────────────────────────────────
// Uses miniaudio's decoder to load any supported format (WAV, MP3, FLAC).
// Decodes entirely to float32 interleaved at the file's native sample rate.

AudioAsset* sound_load(SoundEngine* engine, const char* filepath) {
    if (engine->asset_count >= MAX_LOADED_SOUNDS) {
        printf("[SoundEngine] Asset cache full (%d)\n", MAX_LOADED_SOUNDS);
        return NULL;
    }

    const char* filename = get_filename(filepath);

    // Already loaded?
    AudioAsset* existing = sound_find(engine, filename);
    if (existing) return existing;

    // Decode to float32 stereo at engine sample rate (resamples if needed)
    ma_decoder_config dec_config = ma_decoder_config_init(ma_format_f32, 2, engine->master_sample_rate);
    ma_decoder decoder;

    if (ma_decoder_init_file(filepath, &dec_config, &decoder) != MA_SUCCESS) {
        printf("[SoundEngine] Failed to open: %s\n", filepath);
        return NULL;
    }

    // Get total frame count
    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);

    if (total_frames == 0) {
        // Some formats don't report length. Decode in chunks.
        // First pass: count frames
        float tmp[4096];
        ma_uint64 chunk_read;
        total_frames = 0;
        while (ma_decoder_read_pcm_frames(&decoder, tmp, 4096 / decoder.outputChannels, &chunk_read) == MA_SUCCESS && chunk_read > 0) {
            total_frames += chunk_read;
        }
        ma_decoder_seek_to_pcm_frame(&decoder, 0);
    }

    int channels = decoder.outputChannels;
    int sample_rate = decoder.outputSampleRate;
    ma_uint64 total_floats = total_frames * channels;

    float* samples = (float*)malloc(total_floats * sizeof(float));
    if (!samples) {
        printf("[SoundEngine] Out of memory for: %s\n", filepath);
        ma_decoder_uninit(&decoder);
        return NULL;
    }

    // Read all frames
    ma_uint64 frames_read;
    ma_decoder_read_pcm_frames(&decoder, samples, total_frames, &frames_read);
    ma_decoder_uninit(&decoder);

    // Store in asset slot
    AudioAsset* asset = &engine->assets[engine->asset_count];
    asset->samples     = samples;
    asset->frame_count = frames_read;
    asset->channels    = channels;
    asset->sample_rate = sample_rate;
    strncpy(asset->name, filename, 259);
    asset->name[259] = '\0';

    engine->asset_count++;

    float duration = frames_read / (float)sample_rate;
    float mb = (frames_read * channels * sizeof(float)) / (1024.0f * 1024.0f);
    printf("[SoundEngine] Loaded: %-40s | %d Hz | %dch | %.1fs | %.2f MB\n",
        asset->name, sample_rate, channels, duration, mb);

    return asset;
}

// ─── sound_find ───────────────────────────────────────────────────────────────

AudioAsset* sound_find(SoundEngine* engine, const char* name) {
    for (int i = 0; i < engine->asset_count; i++) {
        if (strcmp(engine->assets[i].name, name) == 0)
            return &engine->assets[i];
    }
    return NULL;
}

// ─── sound_load_folder ────────────────────────────────────────────────────────
// Cross-platform: uses C++17 filesystem if available, otherwise a simple
// approach. For now we provide a platform-abstracted implementation.

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <dirent.h>
#endif

int sound_load_folder(SoundEngine* engine, const char* folder_path) {
    int loaded = 0;

#if defined(_WIN32)
    char search[512];
    snprintf(search, sizeof(search), "%s\\*", folder_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[SoundEngine] Cannot open folder: %s\n", folder_path);
        return 0;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s\\%s", folder_path, fd.cFileName);
        if (sound_load(engine, full)) loaded++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(folder_path);
    if (!dir) {
        printf("[SoundEngine] Cannot open folder: %s\n", folder_path);
        return 0;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", folder_path, entry->d_name);
        if (sound_load(engine, full)) loaded++;
    }
    closedir(dir);
#endif

    printf("[SoundEngine] Folder: %s → %d files loaded\n", folder_path, loaded);
    return loaded;
}

// ─── Sound Playback ───────────────────────────────────────────────────────────

int sound_play(SoundEngine* engine, const char* name, float volume, int looping) {
    AudioAsset* asset = sound_find(engine, name);
    if (!asset) {
        printf("[SoundEngine] Asset not found: %s\n", name);
        return -1;
    }

    // Find free voice slot
    for (int i = 0; i < MAX_PLAYING_SOUNDS; i++) {
        if (!engine->voices[i].active) {
            PlayingSound* v = &engine->voices[i];
            memset(v, 0, sizeof(PlayingSound));
            v->asset         = asset;
            v->volume        = volume;
            v->pitch         = 1.0f;
            v->active        = 1;
            v->looping       = looping;
            v->fade_state    = 1; // fade in
            v->fade_counter  = 0;
            v->fade_duration = FADE_SAMPLES_DEFAULT;
            v->id            = i;
            return i;
        }
    }
    printf("[SoundEngine] No free voice slots\n");
    return -1;
}

void sound_stop(SoundEngine* engine, int voice_id) {
    if (voice_id < 0 || voice_id >= MAX_PLAYING_SOUNDS) return;
    PlayingSound* v = &engine->voices[voice_id];
    if (v->active && v->fade_state != 2) {
        v->fade_state   = 2;
        v->fade_counter = 0;
        v->fade_duration = FADE_SAMPLES_DEFAULT;
    }
}

void sound_stop_all(SoundEngine* engine) {
    for (int i = 0; i < MAX_PLAYING_SOUNDS; i++) sound_stop(engine, i);
}

void sound_pause(SoundEngine* engine, int voice_id) {
    if (voice_id < 0 || voice_id >= MAX_PLAYING_SOUNDS) return;
    engine->voices[voice_id].paused = 1;
}

void sound_resume(SoundEngine* engine, int voice_id) {
    if (voice_id < 0 || voice_id >= MAX_PLAYING_SOUNDS) return;
    engine->voices[voice_id].paused = 0;
}

void sound_set_volume(SoundEngine* engine, int voice_id, float volume) {
    if (voice_id < 0 || voice_id >= MAX_PLAYING_SOUNDS) return;
    engine->voices[voice_id].volume = volume;
}

void sound_set_pitch(SoundEngine* engine, int voice_id, float pitch) {
    if (voice_id < 0 || voice_id >= MAX_PLAYING_SOUNDS) return;
    engine->voices[voice_id].pitch = pitch;
}

void sound_set_position(SoundEngine* engine, int voice_id, float x, float y, float z) {
    if (voice_id < 0 || voice_id >= MAX_PLAYING_SOUNDS) return;
    engine->voices[voice_id].pos_x = x;
    engine->voices[voice_id].pos_y = y;
    engine->voices[voice_id].pos_z = z;
}

// ─── Tone Playback ────────────────────────────────────────────────────────────

static int find_free_tone(SoundEngine* engine) {
    for (int i = 0; i < MAX_PLAYING_TONES; i++) {
        if (!engine->tones[i].active) return i;
    }
    return -1;
}

int tone_play(SoundEngine* engine, double frequency, float amplitude) {
    int slot = find_free_tone(engine);
    if (slot < 0) {
        printf("[SoundEngine] No free tone slots\n");
        return -1;
    }
    PlayingTone* t = &engine->tones[slot];
    memset(t, 0, sizeof(PlayingTone));
    t->frequency     = frequency;
    t->amplitude     = amplitude;
    t->phase         = 0.0;
    t->active        = 1;
    t->fade_state    = 1;
    t->fade_counter  = 0;
    t->fade_duration = FADE_SAMPLES_DEFAULT;
    t->sync_group    = -1;
    t->id            = slot;
    return slot;
}

int tone_prepare(SoundEngine* engine, double frequency, float amplitude, int sync_group) {
    int slot = find_free_tone(engine);
    if (slot < 0) {
        printf("[SoundEngine] No free tone slots\n");
        return -1;
    }
    PlayingTone* t = &engine->tones[slot];
    memset(t, 0, sizeof(PlayingTone));
    t->frequency     = frequency;
    t->amplitude     = amplitude;
    t->phase         = 0.0;
    t->phase_offset  = 0.0;
    t->active        = 1;
    t->pending       = 1;      // won't play until sync_and_start
    t->fade_state    = 1;
    t->fade_counter  = 0;
    t->fade_duration = FADE_SAMPLES_DEFAULT;
    t->sync_group    = sync_group;
    t->id            = slot;
    return slot;
}

void tone_sync_and_start(SoundEngine* engine, int sync_group) {
    // All tones in this group: reset phase to their offset, unpend
    for (int i = 0; i < MAX_PLAYING_TONES; i++) {
        PlayingTone* t = &engine->tones[i];
        if (t->active && t->pending && t->sync_group == sync_group) {
            t->phase   = t->phase_offset;
            t->pending = 0; // now the mixer will pick it up
        }
    }
}

void tone_stop(SoundEngine* engine, int tone_id) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    PlayingTone* t = &engine->tones[tone_id];
    if (t->active && t->fade_state != 2) {
        t->fade_state   = 2;
        t->fade_counter = 0;
        t->fade_duration = FADE_SAMPLES_DEFAULT;
    }
}

void tone_stop_all(SoundEngine* engine) {
    for (int i = 0; i < MAX_PLAYING_TONES; i++) tone_stop(engine, i);
}

void tone_pause(SoundEngine* engine, int tone_id) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    engine->tones[tone_id].paused = 1;
}

void tone_resume(SoundEngine* engine, int tone_id) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    engine->tones[tone_id].paused = 0;
}

void tone_set_frequency(SoundEngine* engine, int tone_id, double frequency) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    engine->tones[tone_id].frequency = frequency;
}

void tone_set_amplitude(SoundEngine* engine, int tone_id, float amplitude) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    engine->tones[tone_id].amplitude = amplitude;
}

void tone_set_phase(SoundEngine* engine, int tone_id, double phase_radians) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    engine->tones[tone_id].phase_offset = phase_radians;
    // If still pending, this will take effect at sync_and_start.
    // If already playing, set live phase too.
    if (!engine->tones[tone_id].pending) {
        engine->tones[tone_id].phase = phase_radians;
    }
}

void tone_set_position(SoundEngine* engine, int tone_id, float x, float y, float z) {
    if (tone_id < 0 || tone_id >= MAX_PLAYING_TONES) return;
    engine->tones[tone_id].pos_x = x;
    engine->tones[tone_id].pos_y = y;
    engine->tones[tone_id].pos_z = z;
}

// ─── Debug ────────────────────────────────────────────────────────────────────

void sound_print_assets(SoundEngine* engine) {
    printf("\n=== SoundEngine: %d assets loaded ===\n", engine->asset_count);
    float total_mb = 0;
    for (int i = 0; i < engine->asset_count; i++) {
        AudioAsset* a = &engine->assets[i];
        float mb = (a->frame_count * a->channels * sizeof(float)) / (1024.0f * 1024.0f);
        total_mb += mb;
        float dur = a->frame_count / (float)a->sample_rate;
        printf("  [%2d] %-40s | %5d Hz | %dch | %6.1fs | %6.2f MB | @ %p\n",
            i, a->name, a->sample_rate, a->channels, dur, mb, (void*)a->samples);
    }
    printf("  Total: %.2f MB\n", total_mb);
    printf("==========================================\n\n");
}
