#include "audio_engine.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct AudioEngine {
	/* Decoded PCM: interleaved float32, all channels */
	float         *raw_samples;
	guint32        raw_sample_count;   /* samples per channel */
	guint32        channels;
	guint32        sample_rate;

	/* Sliced segments */
	AudioSegment  *segments;
	int            segment_count;

	/* Playback state */
	gboolean       is_playing;
	int            active_segment;
	GstElement    *pipeline;           /* gst playback pipeline */
	GstElement    *src_element;        /* appsrc */
	GstElement    *audio_sink;
	GstElement    *volume_elem;
	GstElement    *panorama_elem;

	double         volume;
	double         pan;
	double         play_start_time;    /* monotonic seconds at play start */
	double         offset_sec;        /* seek offset within segment */

	/* Event callback */
	AudioEngineCallback callback;
	void               *callback_user_data;

	/* GLib main loop for decode */
	GMainLoop     *decode_loop;
	GMutex         mutex;
};

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */
static void     _engine_free_segments(AudioEngine *e);
static void     _engine_free_raw(AudioEngine *e);
static gboolean _engine_decode_file(AudioEngine *e, const char *path);
static void     _engine_build_playback_pipeline(AudioEngine *e);
static void     _engine_teardown_pipeline(AudioEngine *e);
static gboolean _engine_bus_watch(GstBus *bus, GstMessage *msg, gpointer data);

/* -------------------------------------------------------------------------
 * Constructor / Destructor
 * ---------------------------------------------------------------------- */
AudioEngine *audio_engine_new(void) {
	AudioEngine *e = g_new0(AudioEngine, 1);
	e->volume          = 1.0;
	e->pan             = 0.0;
	e->active_segment  = -1;
	g_mutex_init(&e->mutex);
	return e;
}

void audio_engine_free(AudioEngine *engine) {
	if (!engine) return;
	audio_engine_stop(engine);
	_engine_teardown_pipeline(engine);
	_engine_free_segments(engine);
	_engine_free_raw(engine);
	g_mutex_clear(&engine->mutex);
	g_free(engine);
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */
static void _engine_free_segments(AudioEngine *e) {
	if (!e->segments) return;
	for (int i = 0; i < e->segment_count; i++) {
		g_free(e->segments[i].data);
	}
	g_free(e->segments);
	e->segments      = NULL;
	e->segment_count = 0;
}

static void _engine_free_raw(AudioEngine *e) {
	g_free(e->raw_samples);
	e->raw_samples      = NULL;
	e->raw_sample_count = 0;
	e->channels         = 0;
	e->sample_rate      = 0;
}

static void _engine_teardown_pipeline(AudioEngine *e) {
	if (e->pipeline) {
		gst_element_set_state(e->pipeline, GST_STATE_NULL);
		gst_object_unref(e->pipeline);
		e->pipeline     = NULL;
		e->src_element  = NULL;
		e->audio_sink   = NULL;
		e->volume_elem  = NULL;
		e->panorama_elem= NULL;
	}
}

/* -------------------------------------------------------------------------
 * GStreamer decode: load audio file into raw float PCM
 * ---------------------------------------------------------------------- */

/* Decode pipeline: filesrc -> decodebin -> audioconvert -> audioresample
 *                           -> capsfilter(F32LE) -> appsink
 */
typedef struct {
	AudioEngine *engine;
	GError      *error;
	GMainLoop   *loop;
} DecodeCtx;

static void _on_pad_added(GstElement *src, GstPad *pad, gpointer data) {
	(void)src;
	GstElement *convert = (GstElement *)data;
	GstPad     *sinkpad = gst_element_get_static_pad(convert, "sink");
	if (!gst_pad_is_linked(sinkpad)) {
		gst_pad_link(pad, sinkpad);
	}
	gst_object_unref(sinkpad);
}

static gboolean _decode_bus_watch(GstBus *bus, GstMessage *msg, gpointer data) {
	DecodeCtx *ctx = (DecodeCtx *)data;
	(void)bus;

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		g_main_loop_quit(ctx->loop);
		break;
	case GST_MESSAGE_ERROR: {
		GError *err  = NULL;
		gchar  *dbg  = NULL;
		gst_message_parse_error(msg, &err, &dbg);
		g_printerr("Decode error: %s (%s)\n", err->message, dbg ? dbg : "");
		g_error_free(err);
		g_free(dbg);
		g_main_loop_quit(ctx->loop);
		break;
	}
	default:
		break;
	}
	return TRUE;
}

static gboolean _engine_decode_file(AudioEngine *e, const char *path) {
	_engine_free_raw(e);

	GMainLoop  *loop       = g_main_loop_new(NULL, FALSE);
	DecodeCtx   ctx        = { e, NULL, loop };

	/* Build pipeline */
	GstElement *pipeline   = gst_pipeline_new("decode");
	GstElement *filesrc    = gst_element_factory_make("filesrc",      "filesrc");
	GstElement *decodebin  = gst_element_factory_make("decodebin",    "decodebin");
	GstElement *convert    = gst_element_factory_make("audioconvert", "convert");
	GstElement *resample   = gst_element_factory_make("audioresample","resample");
	GstElement *capsfilter = gst_element_factory_make("capsfilter",   "capsfilter");
	GstElement *appsink    = gst_element_factory_make("appsink",      "appsink");

	if (!pipeline || !filesrc || !decodebin || !convert ||
	    !resample || !capsfilter || !appsink) {
		g_printerr("Failed to create GStreamer decode elements\n");
		goto cleanup_fail;
	}

	/* Set audio caps: signed 32-bit float, native endian, interleaved */
	GstCaps *caps = gst_caps_new_simple("audio/x-raw",
		"format",   G_TYPE_STRING,  "F32LE",
		"layout",   G_TYPE_STRING,  "interleaved",
		NULL);
	g_object_set(capsfilter, "caps", caps, NULL);
	gst_caps_unref(caps);

	g_object_set(filesrc, "location", path, NULL);
	g_object_set(appsink,
		"emit-signals", FALSE,
		"sync",         FALSE,
		"max-buffers",  0,
		"drop",         FALSE,
		NULL);

	gst_bin_add_many(GST_BIN(pipeline),
		filesrc, decodebin, convert, resample, capsfilter, appsink, NULL);

	/* filesrc -> decodebin: static */
	if (!gst_element_link(filesrc, decodebin)) {
		g_printerr("Cannot link filesrc -> decodebin\n");
		goto cleanup_fail_pipeline;
	}
	/* decodebin -> convert: dynamic (pad-added) */
	g_signal_connect(decodebin, "pad-added", G_CALLBACK(_on_pad_added), convert);

	/* convert -> resample -> capsfilter -> appsink: static */
	if (!gst_element_link_many(convert, resample, capsfilter, appsink, NULL)) {
		g_printerr("Cannot link convert -> resample -> capsfilter -> appsink\n");
		goto cleanup_fail_pipeline;
	}

	/* Bus watch */
	GstBus *bus = gst_element_get_bus(pipeline);
	guint   watch_id = gst_bus_add_watch(bus, _decode_bus_watch, &ctx);
	gst_object_unref(bus);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	g_main_loop_run(loop);

	/* Pull samples from appsink */
	GArray *pcm_array = g_array_new(FALSE, FALSE, sizeof(float));

	/* Retrieve caps from first sample for channel/rate info */
	gboolean got_caps = FALSE;
	guint32  channels = 2, sample_rate = 44100;

	GstSample *sample;
	while ((sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 0)) != NULL) {
		if (!got_caps) {
			GstCaps        *scaps    = gst_sample_get_caps(sample);
			GstStructure   *s        = gst_caps_get_structure(scaps, 0);
			gst_structure_get_int(s, "channels",   (gint *)&channels);
			gst_structure_get_int(s, "rate",        (gint *)&sample_rate);
			got_caps = TRUE;
		}
		GstBuffer   *buf  = gst_sample_get_buffer(sample);
		GstMapInfo   info;
		if (gst_buffer_map(buf, &info, GST_MAP_READ)) {
			guint n = info.size / sizeof(float);
			g_array_append_vals(pcm_array, (float *)info.data, n);
			gst_buffer_unmap(buf, &info);
		}
		gst_sample_unref(sample);
	}

	gst_element_set_state(pipeline, GST_STATE_NULL);
	g_source_remove(watch_id);

	if (pcm_array->len == 0) {
		g_printerr("No PCM data decoded from %s\n", path);
		g_array_free(pcm_array, TRUE);
		goto cleanup_fail_pipeline;
	}

	e->channels         = channels;
	e->sample_rate      = sample_rate;
	e->raw_sample_count = pcm_array->len / channels;
	e->raw_samples      = (float *)g_array_free(pcm_array, FALSE);

	gst_object_unref(pipeline);
	g_main_loop_unref(loop);
	return TRUE;

cleanup_fail_pipeline:
	gst_object_unref(pipeline);
cleanup_fail:
	g_main_loop_unref(loop);
	return FALSE;
}

/* -------------------------------------------------------------------------
 * Public API: load file
 * ---------------------------------------------------------------------- */
gboolean audio_engine_load_file(AudioEngine *engine, const char *path) {
	if (!_engine_decode_file(engine, path)) return FALSE;
	if (engine->callback)
		engine->callback(AUDIO_ENGINE_FILE_LOADED, -1, engine->callback_user_data);
	return TRUE;
}

/* -------------------------------------------------------------------------
 * Public API: slice
 * ---------------------------------------------------------------------- */
void audio_engine_slice(AudioEngine *engine,
                        double       start_position,
                        double       end_position,
                        int          subdivisions) {
	if (!engine->raw_samples || subdivisions <= 0) return;

	guint32 total      = engine->raw_sample_count;
	guint32 start_s    = (guint32)(start_position * total);
	guint32 end_s      = (guint32)(end_position   * total);
	if (end_s > total)  end_s = total;
	if (start_s >= end_s) return;

	guint32 range = end_s - start_s;

	_engine_free_segments(engine);
	engine->segments      = g_new0(AudioSegment, subdivisions);
	engine->segment_count = subdivisions;

	for (int i = 0; i < subdivisions; i++) {
		guint32 seg_start = start_s + (guint32)((double)range / subdivisions * i);
		guint32 seg_end   = (i == subdivisions - 1)
			? end_s
			: start_s + (guint32)((double)range / subdivisions * (i + 1));
		guint32 len       = seg_end - seg_start;

		AudioSegment *seg = &engine->segments[i];
		seg->num_samples  = len;
		seg->channels     = engine->channels;
		seg->sample_rate  = engine->sample_rate;
		seg->enabled      = TRUE;

		/* Copy interleaved PCM */
		seg->data = g_new(float, len * engine->channels);
		memcpy(seg->data,
		       engine->raw_samples + seg_start * engine->channels,
		       len * engine->channels * sizeof(float));
	}

	if (engine->callback)
		engine->callback(AUDIO_ENGINE_SEGMENT_SLICED, -1, engine->callback_user_data);
}

/* -------------------------------------------------------------------------
 * GStreamer playback pipeline: appsrc -> audioconvert -> audioresample
 *                              -> volume -> audiopanorama -> autoaudiosink
 * ---------------------------------------------------------------------- */
static void _engine_build_playback_pipeline(AudioEngine *e) {
	_engine_teardown_pipeline(e);

	GstElement *pipeline   = gst_pipeline_new("playback");
	GstElement *appsrc     = gst_element_factory_make("appsrc",         "appsrc");
	GstElement *convert    = gst_element_factory_make("audioconvert",   "convert");
	GstElement *resample   = gst_element_factory_make("audioresample",  "resample");
	GstElement *volume     = gst_element_factory_make("volume",         "volume");
	GstElement *panorama   = gst_element_factory_make("audiopanorama",  "panorama");
	GstElement *sink       = gst_element_factory_make("autoaudiosink",  "sink");

	if (!pipeline || !appsrc || !convert || !resample || !volume || !sink) {
		g_printerr("Failed to create playback elements\n");
		if (pipeline) gst_object_unref(pipeline);
		return;
	}

	/* If panorama is unavailable, skip it */
	if (!panorama) {
		gst_bin_add_many(GST_BIN(pipeline), appsrc, convert, resample, volume, sink, NULL);
		if (!gst_element_link_many(appsrc, convert, resample, volume, sink, NULL)) {
			g_printerr("Failed to link playback pipeline (no panorama)\n");
			gst_object_unref(pipeline);
			return;
		}
	} else {
		gst_bin_add_many(GST_BIN(pipeline), appsrc, convert, resample, volume, panorama, sink, NULL);
		if (!gst_element_link_many(appsrc, convert, resample, volume, panorama, sink, NULL)) {
			g_printerr("Failed to link playback pipeline\n");
			gst_object_unref(pipeline);
			return;
		}
		g_object_set(panorama, "panorama", (gfloat)e->pan, NULL);
	}

	g_object_set(volume, "volume", e->volume, NULL);

	e->pipeline      = pipeline;
	e->src_element   = appsrc;
	e->volume_elem   = volume;
	e->panorama_elem = panorama;
	e->audio_sink    = sink;
}

/* Bus watch for playback EOS / errors */
static gboolean _engine_bus_watch(GstBus *bus, GstMessage *msg, gpointer data) {
	AudioEngine *e = (AudioEngine *)data;
	(void)bus;

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS: {
		int finished = e->active_segment;
		e->is_playing     = FALSE;
		e->active_segment = -1;
		if (e->callback)
			e->callback(AUDIO_ENGINE_SEGMENT_END, finished, e->callback_user_data);
		break;
	}
	case GST_MESSAGE_ERROR: {
		GError *err = NULL;
		gchar  *dbg = NULL;
		gst_message_parse_error(msg, &err, &dbg);
		g_printerr("Playback error: %s\n", err->message);
		g_error_free(err);
		g_free(dbg);
		e->is_playing     = FALSE;
		e->active_segment = -1;
		break;
	}
	default:
		break;
	}
	return TRUE;
}

/* -------------------------------------------------------------------------
 * Public API: play segment
 * ---------------------------------------------------------------------- */
void audio_engine_play_segment(AudioEngine *engine, int index, double offset_sec) {
	if (index < 0 || index >= engine->segment_count) return;
	AudioSegment *seg = &engine->segments[index];
	if (!seg->enabled) return;

	audio_engine_stop(engine);
	_engine_build_playback_pipeline(engine);
	if (!engine->pipeline) return;

	/* Configure appsrc caps */
	GstCaps *caps = gst_caps_new_simple("audio/x-raw",
		"format",   G_TYPE_STRING, "F32LE",
		"layout",   G_TYPE_STRING, "interleaved",
		"channels", G_TYPE_INT,    (int)seg->channels,
		"rate",     G_TYPE_INT,    (int)seg->sample_rate,
		NULL);
	g_object_set(engine->src_element,
		"caps",     caps,
		"format",   GST_FORMAT_BYTES,
		"stream-type", 0,   /* GST_APP_STREAM_TYPE_STREAM */
		NULL);
	gst_caps_unref(caps);

	/* Determine byte offset for seek within segment */
	guint32 offset_samples = (guint32)(offset_sec * seg->sample_rate);
	if (offset_samples >= seg->num_samples) offset_samples = 0;
	gsize   byte_offset = (gsize)offset_samples * seg->channels * sizeof(float);
	gsize   byte_total  = (gsize)seg->num_samples * seg->channels * sizeof(float);
	gsize   play_bytes  = byte_total - byte_offset;

	/* Push PCM data via appsrc buffer */
	GstBuffer *buf = gst_buffer_new_allocate(NULL, play_bytes, NULL);
	GstMapInfo info;
	if (gst_buffer_map(buf, &info, GST_MAP_WRITE)) {
		memcpy(info.data, (guint8 *)seg->data + byte_offset, play_bytes);
		gst_buffer_unmap(buf, &info);
	}

	/* Attach bus watch */
	GstBus *bus = gst_element_get_bus(engine->pipeline);
	gst_bus_add_watch(bus, _engine_bus_watch, engine);
	gst_object_unref(bus);

	/* Start pipeline then push data */
	gst_element_set_state(engine->pipeline, GST_STATE_PLAYING);
	GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(engine->src_element), buf);
	if (ret != GST_FLOW_OK)
		g_printerr("appsrc push failed: %d\n", ret);
	gst_app_src_end_of_stream(GST_APP_SRC(engine->src_element));

	engine->is_playing     = TRUE;
	engine->active_segment = index;
	engine->offset_sec     = offset_sec;
	engine->play_start_time= (double)g_get_monotonic_time() / 1e6;

	if (engine->callback)
		engine->callback(AUDIO_ENGINE_SEGMENT_PLAY, index, engine->callback_user_data);
}

/* -------------------------------------------------------------------------
 * Public API: stop
 * ---------------------------------------------------------------------- */
void audio_engine_stop(AudioEngine *engine) {
	if (engine->pipeline) {
		gst_element_set_state(engine->pipeline, GST_STATE_NULL);
	}
	engine->is_playing     = FALSE;
	engine->active_segment = -1;
	_engine_teardown_pipeline(engine);
}

/* -------------------------------------------------------------------------
 * Public API: volume / pan
 * ---------------------------------------------------------------------- */
void audio_engine_set_volume(AudioEngine *engine, double volume) {
	engine->volume = volume;
	if (engine->volume_elem)
		g_object_set(engine->volume_elem, "volume", volume, NULL);
}

void audio_engine_set_pan(AudioEngine *engine, double pan) {
	engine->pan = pan;
	if (engine->panorama_elem)
		g_object_set(engine->panorama_elem, "panorama", (gfloat)pan, NULL);
}

/* -------------------------------------------------------------------------
 * Public API: enable/disable segment
 * ---------------------------------------------------------------------- */
void audio_engine_enable_segment(AudioEngine *engine, int index, gboolean enabled) {
	if (index < 0 || index >= engine->segment_count) return;
	engine->segments[index].enabled = enabled;
	if (engine->callback)
		engine->callback(AUDIO_ENGINE_SEGMENT_ENABLE, index, engine->callback_user_data);
}

/* -------------------------------------------------------------------------
 * Public API: accessors
 * ---------------------------------------------------------------------- */
int audio_engine_get_segment_count(const AudioEngine *engine) {
	return engine->segment_count;
}

AudioSegment *audio_engine_get_segment(const AudioEngine *engine, int index) {
	if (!engine->segments) return NULL;
	if (index < 0 || index >= engine->segment_count) return NULL;
	return &engine->segments[index];
}

gboolean audio_engine_is_playing(const AudioEngine *engine) {
	return engine->is_playing;
}

int audio_engine_get_active_segment(const AudioEngine *engine) {
	return engine->active_segment;
}

double audio_engine_get_playback_position(const AudioEngine *engine) {
	if (!engine->is_playing || engine->active_segment < 0) return 0.0;
	AudioSegment *seg = &engine->segments[engine->active_segment];
	if (!seg || seg->num_samples == 0) return 0.0;
	double duration = (double)seg->num_samples / seg->sample_rate;
	double elapsed  = (double)g_get_monotonic_time() / 1e6 - engine->play_start_time;
	elapsed        += engine->offset_sec;
	if (duration <= 0.0) return 0.0;
	double pos = elapsed / duration;
	if (pos < 0.0) pos = 0.0;
	if (pos > 1.0) pos = 1.0;
	return pos;
}

const float *audio_engine_get_raw_samples(const AudioEngine *engine, guint32 *out_count) {
	if (out_count) *out_count = engine->raw_sample_count;
	return engine->raw_samples;
}

guint32 audio_engine_get_sample_rate(const AudioEngine *engine) {
	return engine->sample_rate;
}

guint32 audio_engine_get_channels(const AudioEngine *engine) {
	return engine->channels;
}

void audio_engine_set_callback(AudioEngine *engine, AudioEngineCallback cb, void *user_data) {
	engine->callback           = cb;
	engine->callback_user_data = user_data;
}
