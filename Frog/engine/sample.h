/* sample.h — the decoded source audio a track plays from. float32 channels
 * at the engine's sample rate (converted once at load); all voice and mix
 * math stays double. Main thread: load / free. Audio thread: read only. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_SAMPLE_MAX_CH 2

typedef struct {
	float* ch[FG_SAMPLE_MAX_CH];
	int nCh;
	int64_t frames;
	double sampleRate;
	char name[256];
} fg_sample;

/* Allocate `frames` silent frames. */
fg_sample* fg_sample_create(int nCh, int64_t frames, double sampleRate);
void fg_sample_free(fg_sample* s);

/* Decode a WAV (any PCM / float format dr_wav reads), down-mix to ≤ 2
 * channels, resample to `targetRate` (0 keeps the file's rate), cap at
 * `maxSeconds` (0 = no cap). NULL on failure. */
fg_sample* fg_sample_load_wav(const char* path, double targetRate, double maxSeconds);

/* Write 16-bit PCM WAV. */
bool fg_sample_write_wav16(const char* path, const double* const* ch, int nCh, int64_t frames, double sampleRate);

/* Windowed-sinc resample of one channel (offline quality). Returns frames written. */
int64_t fg_resample(const float* in, int64_t inFrames, double inRate, float* out, int64_t outCap, double outRate);

#ifdef __cplusplus
}
#endif
