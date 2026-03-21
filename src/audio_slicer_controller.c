#include "audio_slicer_controller.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct AudioSlicerController {
	AudioEngine  *engine;
	WaveformView *view;

	/* Pause state */
	gboolean      is_paused;
	int           paused_segment;
	double        paused_offset;

	/* Playhead animation timer */
	guint         playhead_timer_id;

	/* Current slice parameters */
	double        slice_start;
	double        slice_end;
	int           slice_subdivisions;
};

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */
static void _ctrl_engine_callback(AudioEngineCallbackType type, int index, void *data);
static void _ctrl_play_segment_at(AudioSlicerController *ctrl, int index, double offset_sec);
static int  _ctrl_find_next_enabled(AudioSlicerController *ctrl, int from);
static void _ctrl_stop_playhead_timer(AudioSlicerController *ctrl);
static gboolean _ctrl_playhead_tick(gpointer data);

/* Waveform interaction callbacks */
static void _on_segment_click (int index,             void *data);
static void _on_segment_toggle(int index, gboolean en, void *data);
static void _on_waveform_jump (double rel,            void *data);

/* -------------------------------------------------------------------------
 * Constructor / Destructor
 * ---------------------------------------------------------------------- */
AudioSlicerController *audio_slicer_controller_new(GtkWidget *parent_box) {
	AudioSlicerController *ctrl = g_new0(AudioSlicerController, 1);
	ctrl->engine         = audio_engine_new();
	ctrl->view           = waveform_view_new(parent_box);
	ctrl->paused_segment = -1;
	ctrl->slice_start    = 0.0;
	ctrl->slice_end      = 1.0;
	ctrl->slice_subdivisions = 2;

	/* Wire engine callback */
	audio_engine_set_callback(ctrl->engine, _ctrl_engine_callback, ctrl);

	/* Wire view interaction callbacks */
	waveform_view_set_segment_click_cb (ctrl->view, _on_segment_click,  ctrl);
	waveform_view_set_segment_toggle_cb(ctrl->view, _on_segment_toggle, ctrl);
	waveform_view_set_jump_cb          (ctrl->view, _on_waveform_jump,  ctrl);

	return ctrl;
}

void audio_slicer_controller_free(AudioSlicerController *ctrl) {
	if (!ctrl) return;
	_ctrl_stop_playhead_timer(ctrl);
	audio_engine_free(ctrl->engine);
	waveform_view_free(ctrl->view);
	g_free(ctrl);
}

/* -------------------------------------------------------------------------
 * Load file
 * ---------------------------------------------------------------------- */
gboolean audio_slicer_controller_load_file(AudioSlicerController *ctrl,
                                            const char            *path) {
	if (!audio_engine_load_file(ctrl->engine, path)) return FALSE;

	/* Precompute waveform peaks */
	guint32      n_samples;
	const float *raw = audio_engine_get_raw_samples(ctrl->engine, &n_samples);
	guint32      ch  = audio_engine_get_channels(ctrl->engine);
	waveform_view_precompute_peaks(ctrl->view, raw, n_samples, ch, 1500);

	/* Default slice: full range, 2 subdivisions */
	audio_slicer_controller_slice(ctrl, 0.0, 1.0, 2);
	return TRUE;
}

/* -------------------------------------------------------------------------
 * Slice
 * ---------------------------------------------------------------------- */
void audio_slicer_controller_slice(AudioSlicerController *ctrl,
                                    double                 start,
                                    double                 end,
                                    int                    subdivisions) {
	ctrl->slice_start        = start;
	ctrl->slice_end          = end;
	ctrl->slice_subdivisions = subdivisions;

	audio_engine_slice(ctrl->engine, start, end, subdivisions);
	waveform_view_set_selection(ctrl->view, start, end, subdivisions);
}

/* -------------------------------------------------------------------------
 * Playback
 * ---------------------------------------------------------------------- */
static void _ctrl_stop_playhead_timer(AudioSlicerController *ctrl) {
	if (ctrl->playhead_timer_id) {
		g_source_remove(ctrl->playhead_timer_id);
		ctrl->playhead_timer_id = 0;
	}
}

static gboolean _ctrl_playhead_tick(gpointer data) {
	AudioSlicerController *ctrl = (AudioSlicerController *)data;
	if (!audio_engine_is_playing(ctrl->engine)) {
		waveform_view_set_is_playing(ctrl->view, FALSE);
		waveform_view_set_playhead_position(ctrl->view, 0.0);
		ctrl->playhead_timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	double pos = audio_engine_get_playback_position(ctrl->engine);
	waveform_view_set_playhead_position(ctrl->view, pos);
	return G_SOURCE_CONTINUE;
}

static void _ctrl_play_segment_at(AudioSlicerController *ctrl,
                                   int                    index,
                                   double                 offset_sec) {
	ctrl->is_paused      = FALSE;
	ctrl->paused_segment = -1;
	ctrl->paused_offset  = 0.0;

	waveform_view_set_is_paused(ctrl->view, FALSE);
	audio_engine_play_segment(ctrl->engine, index, offset_sec);
	waveform_view_set_is_playing(ctrl->view, TRUE);
	waveform_view_set_active_segment(ctrl->view, index);

	/* Start playhead animation at ~60 fps */
	_ctrl_stop_playhead_timer(ctrl);
	ctrl->playhead_timer_id = g_timeout_add(16, _ctrl_playhead_tick, ctrl);
}

static int _ctrl_find_next_enabled(AudioSlicerController *ctrl, int from) {
	int n = audio_engine_get_segment_count(ctrl->engine);
	if (n == 0) return -1;
	int start = (from < 0) ? -1 : from;
	for (int i = 1; i <= n; i++) {
		int idx = (start + i) % n;
		AudioSegment *seg = audio_engine_get_segment(ctrl->engine, idx);
		if (seg && seg->enabled) return idx;
	}
	return -1;
}

void audio_slicer_controller_play(AudioSlicerController *ctrl, int index) {
	/* Validate and find first enabled if needed */
	int seg_count = audio_engine_get_segment_count(ctrl->engine);
	if (seg_count == 0) return;
	if (index < 0 || index >= seg_count) {
		index = _ctrl_find_next_enabled(ctrl, -1);
		if (index < 0) return;
	}
	AudioSegment *seg = audio_engine_get_segment(ctrl->engine, index);
	if (!seg || !seg->enabled) {
		index = _ctrl_find_next_enabled(ctrl, index);
		if (index < 0) return;
	}
	_ctrl_play_segment_at(ctrl, index, 0.0);
}

void audio_slicer_controller_stop(AudioSlicerController *ctrl) {
	_ctrl_stop_playhead_timer(ctrl);
	audio_engine_stop(ctrl->engine);
	ctrl->is_paused      = FALSE;
	ctrl->paused_segment = -1;
	ctrl->paused_offset  = 0.0;
	waveform_view_set_is_playing(ctrl->view, FALSE);
	waveform_view_set_is_paused(ctrl->view, FALSE);
	waveform_view_set_playhead_position(ctrl->view, 0.0);
	waveform_view_set_active_segment(ctrl->view, -1);
}

void audio_slicer_controller_pause(AudioSlicerController *ctrl) {
	if (!audio_engine_is_playing(ctrl->engine)) return;

	int    seg_idx = audio_engine_get_active_segment(ctrl->engine);
	double pos     = audio_engine_get_playback_position(ctrl->engine);
	AudioSegment *seg = audio_engine_get_segment(ctrl->engine, seg_idx);
	double duration   = seg ? (double)seg->num_samples / seg->sample_rate : 0.0;

	ctrl->paused_segment = seg_idx;
	ctrl->paused_offset  = pos * duration;
	ctrl->is_paused      = TRUE;

	_ctrl_stop_playhead_timer(ctrl);
	audio_engine_stop(ctrl->engine);
	waveform_view_set_is_playing(ctrl->view, FALSE);
	waveform_view_set_is_paused(ctrl->view, TRUE);
	waveform_view_set_playhead_position(ctrl->view, pos);
}

void audio_slicer_controller_resume(AudioSlicerController *ctrl) {
	if (!ctrl->is_paused || ctrl->paused_segment < 0) return;
	waveform_view_set_is_paused(ctrl->view, FALSE);
	_ctrl_play_segment_at(ctrl, ctrl->paused_segment, ctrl->paused_offset);
}

void audio_slicer_controller_set_volume(AudioSlicerController *ctrl, double vol) {
	audio_engine_set_volume(ctrl->engine, vol);
}

void audio_slicer_controller_set_pan(AudioSlicerController *ctrl, double pan) {
	audio_engine_set_pan(ctrl->engine, pan);
}

gboolean audio_slicer_controller_is_playing(const AudioSlicerController *ctrl) {
	return audio_engine_is_playing(ctrl->engine);
}

gboolean audio_slicer_controller_is_paused(const AudioSlicerController *ctrl) {
	return ctrl->is_paused;
}

/* -------------------------------------------------------------------------
 * Engine event callback
 * ---------------------------------------------------------------------- */
static void _ctrl_engine_callback(AudioEngineCallbackType type, int index, void *data) {
	AudioSlicerController *ctrl = (AudioSlicerController *)data;

	switch (type) {
	case AUDIO_ENGINE_FILE_LOADED:
		break;

	case AUDIO_ENGINE_SEGMENT_SLICED: {
		int cnt = audio_engine_get_segment_count(ctrl->engine);
		waveform_view_set_segments(ctrl->view,
			audio_engine_get_segment(ctrl->engine, 0),
			cnt);
		break;
	}

	case AUDIO_ENGINE_SEGMENT_PLAY:
		waveform_view_set_active_segment(ctrl->view, index);
		break;

	case AUDIO_ENGINE_SEGMENT_END: {
		int next = _ctrl_find_next_enabled(ctrl, index);
		if (next >= 0 && next != index) {
			_ctrl_play_segment_at(ctrl, next, 0.0);
		} else {
			_ctrl_stop_playhead_timer(ctrl);
			waveform_view_set_active_segment(ctrl->view, -1);
			waveform_view_set_is_playing(ctrl->view, FALSE);
			waveform_view_set_playhead_position(ctrl->view, 0.0);
		}
		break;
	}

	case AUDIO_ENGINE_SEGMENT_ENABLE: {
		int cnt = audio_engine_get_segment_count(ctrl->engine);
		gboolean *enabled = g_new(gboolean, cnt);
		for (int i = 0; i < cnt; i++) {
			AudioSegment *s = audio_engine_get_segment(ctrl->engine, i);
			enabled[i] = s ? s->enabled : TRUE;
		}
		waveform_view_set_enabled_segments(ctrl->view, enabled, cnt);
		g_free(enabled);

		/* If the active/paused segment was just disabled, advance */
		AudioSegment *toggled = audio_engine_get_segment(ctrl->engine, index);
		gboolean now_disabled = toggled && !toggled->enabled;
		if (now_disabled) {
			if (audio_engine_is_playing(ctrl->engine) &&
			    audio_engine_get_active_segment(ctrl->engine) == index) {
				int next = _ctrl_find_next_enabled(ctrl, index);
				if (next >= 0)
					_ctrl_play_segment_at(ctrl, next, 0.0);
				else
					audio_slicer_controller_stop(ctrl);
			} else if (ctrl->is_paused && ctrl->paused_segment == index) {
				int next = _ctrl_find_next_enabled(ctrl, index);
				if (next >= 0) {
					ctrl->paused_segment = next;
					ctrl->paused_offset  = 0.0;
					waveform_view_set_active_segment(ctrl->view, next);
					waveform_view_set_playhead_position(ctrl->view, 0.0);
				} else {
					audio_slicer_controller_stop(ctrl);
				}
			}
		}
		break;
	}
	}
}

/* -------------------------------------------------------------------------
 * View interaction callbacks
 * ---------------------------------------------------------------------- */
static void _on_segment_click(int index, void *data) {
	AudioSlicerController *ctrl = (AudioSlicerController *)data;
	AudioSegment *seg = audio_engine_get_segment(ctrl->engine, index);
	if (!seg || !seg->enabled) return;

	if (ctrl->is_paused) {
		ctrl->paused_segment = index;
		ctrl->paused_offset  = 0.0;
		waveform_view_set_active_segment(ctrl->view, index);
		waveform_view_set_playhead_position(ctrl->view, 0.0);
	} else {
		audio_slicer_controller_play(ctrl, index);
	}
}

static void _on_segment_toggle(int index, gboolean en, void *data) {
	AudioSlicerController *ctrl = (AudioSlicerController *)data;
	audio_engine_enable_segment(ctrl->engine, index, en);
}

static void _on_waveform_jump(double rel, void *data) {
	AudioSlicerController *ctrl = (AudioSlicerController *)data;
	int n = audio_engine_get_segment_count(ctrl->engine);
	if (n == 0) return;

	int    seg_idx    = (int)(rel * n);
	if (seg_idx >= n) seg_idx = n - 1;
	AudioSegment *seg = audio_engine_get_segment(ctrl->engine, seg_idx);
	if (!seg) return;

	double seg_rel    = (rel * n) - seg_idx;
	double duration   = (double)seg->num_samples / seg->sample_rate;
	double offset_sec = seg_rel * duration;

	if (seg->enabled) {
		if (ctrl->is_paused) {
			ctrl->paused_segment = seg_idx;
			ctrl->paused_offset  = offset_sec;
			waveform_view_set_active_segment(ctrl->view, seg_idx);
			waveform_view_set_playhead_position(ctrl->view, seg_rel);
		} else {
			_ctrl_play_segment_at(ctrl, seg_idx, offset_sec);
		}
	} else {
		int next = _ctrl_find_next_enabled(ctrl, seg_idx);
		if (next >= 0) {
			if (ctrl->is_paused) {
				ctrl->paused_segment = next;
				ctrl->paused_offset  = 0.0;
				waveform_view_set_active_segment(ctrl->view, next);
				waveform_view_set_playhead_position(ctrl->view, 0.0);
			} else {
				_ctrl_play_segment_at(ctrl, next, 0.0);
			}
		}
	}
}
