#pragma once

#include <gtk/gtk.h>
#include "audio_engine.h"

/*
 * WaveformPeak: min/max sample values for one display column.
 */
typedef struct {
	float min;
	float max;
} WaveformPeak;

/*
 * WaveformView: GTK DrawingArea-based waveform renderer.
 * Draws peaks, subdivision markers, playhead, and segment indicators.
 * Emits GObject signals for user interactions.
 */
typedef struct WaveformView WaveformView;

/* Create a new WaveformView embedded in parent_box. */
WaveformView  *waveform_view_new(GtkWidget *parent_box);

/* Destroy / clean up resources */
void           waveform_view_free(WaveformView *view);

/* Return the underlying GtkWidget (GtkDrawingArea) */
GtkWidget     *waveform_view_get_widget(WaveformView *view);

/* Set precomputed waveform peaks (copied internally). */
void           waveform_view_set_peaks(WaveformView *view,
                                       const WaveformPeak *peaks,
                                       int                 count);

/* Precompute peaks from raw float32 PCM samples (channel 0). */
void           waveform_view_precompute_peaks(WaveformView *view,
                                              const float  *samples,
                                              guint32       num_samples_per_channel,
                                              guint32       channels,
                                              int           target_width);

/* Update slice selection parameters and trigger redraw. */
void           waveform_view_set_selection(WaveformView *view,
                                           double        start,
                                           double        end,
                                           int           subdivisions);

/* Update segment enable/disable state (array of length segment_count). */
void           waveform_view_set_segments(WaveformView  *view,
                                          AudioSegment  *segments,
                                          int            count);
void           waveform_view_set_enabled_segments(WaveformView  *view,
                                                  const gboolean *enabled,
                                                  int             count);

/* Playback state */
void           waveform_view_set_active_segment(WaveformView *view, int index);
void           waveform_view_set_playhead_position(WaveformView *view, double pos);
void           waveform_view_set_is_playing(WaveformView *view, gboolean playing);
void           waveform_view_set_is_paused(WaveformView *view, gboolean paused);

/* Trigger a manual redraw */
void           waveform_view_redraw(WaveformView *view);

/*
 * Interaction callbacks:
 *   on_segment_click(index, user_data)   - user clicked a segment indicator
 *   on_segment_toggle(index, enabled, user_data) - user toggled a segment enable/disable
 *   on_waveform_jump(rel, user_data)     - user clicked waveform to seek (rel in [0,1])
 */
typedef void (*WaveformSegmentClickCb)(int index, void *user_data);
typedef void (*WaveformSegmentToggleCb)(int index, gboolean enabled, void *user_data);
typedef void (*WaveformJumpCb)(double rel, void *user_data);

void waveform_view_set_segment_click_cb(WaveformView           *view,
                                         WaveformSegmentClickCb  cb,
                                         void                   *user_data);
void waveform_view_set_segment_toggle_cb(WaveformView            *view,
                                          WaveformSegmentToggleCb  cb,
                                          void                    *user_data);
void waveform_view_set_jump_cb(WaveformView    *view,
                                WaveformJumpCb   cb,
                                void            *user_data);
