#pragma once

#include <gst/gst.h>
#include <glib.h>
#include <stdint.h>

/*
 * AudioSegment: Holds a slice of decoded PCM audio data.
 */
typedef struct {
	float    *data;         /* interleaved float PCM samples */
	guint32   num_samples;  /* samples per channel */
	guint32   channels;
	guint32   sample_rate;
	gboolean  enabled;
} AudioSegment;

/*
 * AudioEngineCallbackType: Event types emitted by AudioEngine.
 */
typedef enum {
	AUDIO_ENGINE_FILE_LOADED,
	AUDIO_ENGINE_SEGMENT_SLICED,
	AUDIO_ENGINE_SEGMENT_PLAY,
	AUDIO_ENGINE_SEGMENT_END,
	AUDIO_ENGINE_SEGMENT_ENABLE,
} AudioEngineCallbackType;

/*
 * AudioEngineCallback: Function pointer for engine event handlers.
 */
typedef void (*AudioEngineCallback)(AudioEngineCallbackType type, int index, void *user_data);

/*
 * AudioEngine: Manages audio decoding, buffering, slicing, and GStreamer playback.
 */
typedef struct AudioEngine AudioEngine;

AudioEngine   *audio_engine_new(void);
void           audio_engine_free(AudioEngine *engine);

/* Load a file path into the engine (blocks until decode completes). */
gboolean       audio_engine_load_file(AudioEngine *engine, const char *path);

/* Slice the loaded audio buffer from [start,end] into N subdivisions. */
void           audio_engine_slice(AudioEngine *engine,
                                  double       start_position,
                                  double       end_position,
                                  int          subdivisions);

/* Playback control */
void           audio_engine_play_segment(AudioEngine *engine, int index, double offset_sec);
void           audio_engine_stop(AudioEngine *engine);
void           audio_engine_set_volume(AudioEngine *engine, double volume);
void           audio_engine_set_pan(AudioEngine *engine, double pan);

/* Segment enable/disable */
void           audio_engine_enable_segment(AudioEngine *engine, int index, gboolean enabled);

/* Accessors */
int            audio_engine_get_segment_count(const AudioEngine *engine);
AudioSegment  *audio_engine_get_segment(const AudioEngine *engine, int index);
gboolean       audio_engine_is_playing(const AudioEngine *engine);
int            audio_engine_get_active_segment(const AudioEngine *engine);
double         audio_engine_get_playback_position(const AudioEngine *engine);

/* Raw PCM buffer accessors for waveform rendering (channel 0 only) */
const float   *audio_engine_get_raw_samples(const AudioEngine *engine, guint32 *out_count);
guint32        audio_engine_get_sample_rate(const AudioEngine *engine);
guint32        audio_engine_get_channels(const AudioEngine *engine);

/* Event callback registration */
void           audio_engine_set_callback(AudioEngine *engine, AudioEngineCallback cb, void *user_data);
