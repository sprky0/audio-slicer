#include "waveform_view.h"

#include <cairo.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
#define WAVEFORM_HEIGHT       200
#define INDICATOR_HEIGHT       28
#define PRECOMPUTED_WIDTH    1500
#define TOGGLE_RADIUS           8
#define TOGGLE_MARGIN_RIGHT    18

/* -------------------------------------------------------------------------
 * Internal structure
 * ---------------------------------------------------------------------- */
struct WaveformView {
	GtkWidget *drawing_area;

	/* Precomputed peaks */
	WaveformPeak *peaks;
	int           peak_count;

	/* Segment state */
	gboolean     *enabled_segments;
	int           segment_count;
	int           active_segment;

	/* Selection */
	double        start_pos;
	double        end_pos;
	int           subdivisions;

	/* Playback */
	double        playhead_pos;
	gboolean      is_playing;
	gboolean      is_paused;

	/* Callbacks */
	WaveformSegmentClickCb  on_segment_click;
	void                   *on_segment_click_data;
	WaveformSegmentToggleCb on_segment_toggle;
	void                   *on_segment_toggle_data;
	WaveformJumpCb          on_waveform_jump;
	void                   *on_waveform_jump_data;
};

/* -------------------------------------------------------------------------
 * Drawing
 * ---------------------------------------------------------------------- */
static gboolean _on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	WaveformView *view = (WaveformView *)data;
	(void)widget;

	int width  = gtk_widget_get_allocated_width(widget);
	int height = WAVEFORM_HEIGHT;
	int total_h= height + INDICATOR_HEIGHT;

	/* Background */
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
	cairo_rectangle(cr, 0, 0, width, total_h);
	cairo_fill(cr);

	/* Draw waveform peaks */
	if (view->peak_count > 0) {
		double amp   = height / 2.0;
		double mid   = amp;
		double scale = (double)view->peak_count / (double)width;

		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		cairo_set_line_width(cr, 1.0);

		for (int x = 0; x < width; x++) {
			int idx = (int)(x * scale);
			if (idx >= view->peak_count) idx = view->peak_count - 1;
			float min_v = view->peaks[idx].min;
			float max_v = view->peaks[idx].max;
			double y1   = mid + min_v * amp;
			double y2   = mid + max_v * amp;
			/* Draw only in selected range */
			cairo_move_to(cr, x + 0.5, y1);
			cairo_line_to(cr, x + 0.5, y2);
		}
		cairo_stroke(cr);
	}

	if (view->segment_count <= 0) {
		/* Show "Drop or open audio file" hint */
		if (view->peak_count == 0) {
			cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.8);
			cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cr, 14.0);
			const char *hint = "Open an audio file to begin";
			cairo_text_extents_t ext;
			cairo_text_extents(cr, hint, &ext);
			cairo_move_to(cr, (width - ext.width) / 2.0, height / 2.0);
			cairo_show_text(cr, hint);
		}
	}

	/* Selection highlight */
	double startX = view->start_pos * width;
	double endX   = view->end_pos   * width;
	cairo_set_source_rgba(cr, 0.2, 0.6, 0.85, 0.15);
	cairo_rectangle(cr, startX, 0, endX - startX, height);
	cairo_fill(cr);

	/* Subdivision lines and active segment highlight */
	if (view->subdivisions > 1 && view->segment_count > 0) {
		double seg_w = (endX - startX) / view->subdivisions;

		if (view->is_playing && view->active_segment >= 0) {
			double sx = startX + seg_w * view->active_segment;
			cairo_set_source_rgba(cr, 0.2, 0.6, 0.85, 0.25);
			cairo_rectangle(cr, sx, 0, seg_w, height);
			cairo_fill(cr);
		}

		cairo_set_source_rgba(cr, 0.2, 0.6, 0.85, 0.5);
		cairo_set_line_width(cr, 1.0);
		for (int i = 1; i < view->subdivisions; i++) {
			double x = startX + seg_w * i;
			cairo_move_to(cr, x, 0);
			cairo_line_to(cr, x, height);
			cairo_stroke(cr);
		}
	}

	/* Playhead */
	if ((view->is_playing || view->is_paused) && view->active_segment >= 0 &&
	    view->segment_count > 0) {
		double seg_w  = (endX - startX) / view->segment_count;
		double seg_x  = startX + seg_w * view->active_segment;
		double head_x = seg_x + seg_w * view->playhead_pos;
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
		cairo_set_line_width(cr, 2.0);
		cairo_move_to(cr, head_x, 0);
		cairo_line_to(cr, head_x, height);
		cairo_stroke(cr);
	}

	/* Segment indicators */
	if (view->segment_count > 0) {
		double seg_w = (endX - startX) / view->segment_count;
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 12.0);

		for (int i = 0; i < view->segment_count; i++) {
			double sx      = startX + seg_w * i;
			gboolean enab  = (view->enabled_segments && i < view->segment_count)
			                 ? view->enabled_segments[i] : TRUE;
			gboolean active= (i == view->active_segment);

			cairo_save(cr);

			/* Indicator bar */
			if (active) {
				cairo_set_source_rgba(cr, 0.2, 0.6, 0.85, enab ? 1.0 : 0.3);
			} else {
				cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, enab ? 1.0 : 0.3);
			}
			cairo_rectangle(cr, sx + 1, height + 4, seg_w - 2, INDICATOR_HEIGHT - 8);
			cairo_fill(cr);

			/* Segment number */
			char label[16];
			snprintf(label, sizeof(label), "%d", i + 1);
			cairo_text_extents_t ext;
			cairo_text_extents(cr, label, &ext);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, enab ? 1.0 : 0.5);
			cairo_move_to(cr,
				sx + (seg_w - ext.width) / 2.0 - TOGGLE_MARGIN_RIGHT / 2,
				height + 4 + (INDICATOR_HEIGHT - 8) / 2.0 + ext.height / 2.0);
			cairo_show_text(cr, label);

			/* Enable/disable circle */
			double cx = sx + seg_w - TOGGLE_MARGIN_RIGHT;
			double cy = height + 4 + (INDICATOR_HEIGHT - 8) / 2.0;
			cairo_arc(cr, cx, cy, TOGGLE_RADIUS, 0, 2 * G_PI);
			if (enab)
				cairo_set_source_rgb(cr, 0.18, 0.8, 0.25);
			else
				cairo_set_source_rgb(cr, 0.9, 0.3, 0.2);
			cairo_fill(cr);

			cairo_restore(cr);
		}
	}

	return FALSE;
}

/* -------------------------------------------------------------------------
 * Click event
 * ---------------------------------------------------------------------- */
static gboolean _on_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	WaveformView *view = (WaveformView *)data;
	if (event->type != GDK_BUTTON_PRESS || event->button != 1) return FALSE;

	double x      = event->x;
	double y      = event->y;
	int    width  = gtk_widget_get_allocated_width(widget);
	int    height = WAVEFORM_HEIGHT;

	double startX = view->start_pos * width;
	double endX   = view->end_pos   * width;

	/* Waveform area click → seek */
	if (y >= 0 && y <= height) {
		if (x >= startX && x <= endX && view->on_waveform_jump) {
			double rel = (x - startX) / (endX - startX);
			view->on_waveform_jump(rel, view->on_waveform_jump_data);
		}
		return TRUE;
	}

	/* Segment indicator click */
	if (y > height && view->segment_count > 0) {
		double seg_w = (endX - startX) / view->segment_count;
		for (int i = 0; i < view->segment_count; i++) {
			double sx = startX + seg_w * i;
			if (x >= sx && x < sx + seg_w) {
				/* Toggle circle hit test */
				double cx = sx + seg_w - TOGGLE_MARGIN_RIGHT;
				if (x > cx - TOGGLE_RADIUS * 2 && view->on_segment_toggle) {
					gboolean cur = view->enabled_segments ? view->enabled_segments[i] : TRUE;
					view->on_segment_toggle(i, !cur, view->on_segment_toggle_data);
				} else if (view->on_segment_click) {
					view->on_segment_click(i, view->on_segment_click_data);
				}
				break;
			}
		}
	}
	return TRUE;
}

/* -------------------------------------------------------------------------
 * Constructor
 * ---------------------------------------------------------------------- */
WaveformView *waveform_view_new(GtkWidget *parent_box) {
	WaveformView *view = g_new0(WaveformView, 1);
	view->active_segment = -1;
	view->start_pos      = 0.0;
	view->end_pos        = 1.0;
	view->subdivisions   = 2;

	view->drawing_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(view->drawing_area, 800, WAVEFORM_HEIGHT + INDICATOR_HEIGHT);
	gtk_widget_add_events(view->drawing_area, GDK_BUTTON_PRESS_MASK);

	g_signal_connect(view->drawing_area, "draw",            G_CALLBACK(_on_draw),  view);
	g_signal_connect(view->drawing_area, "button-press-event", G_CALLBACK(_on_click), view);

	gtk_box_pack_start(GTK_BOX(parent_box), view->drawing_area, TRUE, TRUE, 0);

	return view;
}

void waveform_view_free(WaveformView *view) {
	if (!view) return;
	g_free(view->peaks);
	g_free(view->enabled_segments);
	g_free(view);
}

GtkWidget *waveform_view_get_widget(WaveformView *view) {
	return view->drawing_area;
}

/* -------------------------------------------------------------------------
 * Peak precomputation: bucket-based min/max extraction (channel 0)
 * libfftw3 is linked as a dependency for potential FFT-based analysis;
 * the current implementation uses direct min/max per display bucket.
 * ---------------------------------------------------------------------- */
void waveform_view_precompute_peaks(WaveformView *view,
                                    const float  *samples,
                                    guint32       num_samples_per_channel,
                                    guint32       channels,
                                    int           target_width) {
	if (!samples || num_samples_per_channel == 0 || target_width <= 0) return;

	g_free(view->peaks);
	view->peaks      = g_new(WaveformPeak, target_width);
	view->peak_count = target_width;

	/* Use channel 0 data (every `channels` samples) */
	double samples_per_bucket = (double)num_samples_per_channel / target_width;

	/* For each bucket, compute min/max directly (no FFT needed for peak envelope) */
	for (int i = 0; i < target_width; i++) {
		guint32 start = (guint32)(i     * samples_per_bucket);
		guint32 end   = (guint32)((i+1) * samples_per_bucket);
		if (end > num_samples_per_channel) end = num_samples_per_channel;

		float mn = 1.0f, mx = -1.0f;
		for (guint32 j = start; j < end; j++) {
			/* channel 0 interleaved sample */
			float v = samples[j * channels];
			if (v < mn) mn = v;
			if (v > mx) mx = v;
		}
		view->peaks[i].min = mn;
		view->peaks[i].max = mx;
	}
}

void waveform_view_set_peaks(WaveformView *view,
                              const WaveformPeak *peaks,
                              int                 count) {
	g_free(view->peaks);
	view->peaks      = g_new(WaveformPeak, count);
	view->peak_count = count;
	memcpy(view->peaks, peaks, count * sizeof(WaveformPeak));
	waveform_view_redraw(view);
}

void waveform_view_set_selection(WaveformView *view,
                                  double        start,
                                  double        end,
                                  int           subdivisions) {
	view->start_pos    = start;
	view->end_pos      = end;
	view->subdivisions = subdivisions;
	waveform_view_redraw(view);
}

void waveform_view_set_segments(WaveformView *view,
                                 AudioSegment *segments,
                                 int           count) {
	g_free(view->enabled_segments);
	view->segment_count    = count;
	view->enabled_segments = g_new(gboolean, count);
	for (int i = 0; i < count; i++) {
		view->enabled_segments[i] = segments ? segments[i].enabled : TRUE;
	}
	waveform_view_redraw(view);
}

void waveform_view_set_enabled_segments(WaveformView   *view,
                                         const gboolean *enabled,
                                         int             count) {
	if (count != view->segment_count) return;
	if (view->enabled_segments)
		memcpy(view->enabled_segments, enabled, count * sizeof(gboolean));
	waveform_view_redraw(view);
}

void waveform_view_set_active_segment(WaveformView *view, int index) {
	view->active_segment = index;
	waveform_view_redraw(view);
}

void waveform_view_set_playhead_position(WaveformView *view, double pos) {
	view->playhead_pos = pos;
	waveform_view_redraw(view);
}

void waveform_view_set_is_playing(WaveformView *view, gboolean playing) {
	view->is_playing = playing;
	waveform_view_redraw(view);
}

void waveform_view_set_is_paused(WaveformView *view, gboolean paused) {
	view->is_paused = paused;
	waveform_view_redraw(view);
}

void waveform_view_redraw(WaveformView *view) {
	if (view->drawing_area)
		gtk_widget_queue_draw(view->drawing_area);
}

/* Callbacks */
void waveform_view_set_segment_click_cb(WaveformView          *view,
                                         WaveformSegmentClickCb cb,
                                         void                  *user_data) {
	view->on_segment_click      = cb;
	view->on_segment_click_data = user_data;
}

void waveform_view_set_segment_toggle_cb(WaveformView           *view,
                                          WaveformSegmentToggleCb cb,
                                          void                   *user_data) {
	view->on_segment_toggle      = cb;
	view->on_segment_toggle_data = user_data;
}

void waveform_view_set_jump_cb(WaveformView  *view,
                                WaveformJumpCb cb,
                                void          *user_data) {
	view->on_waveform_jump      = cb;
	view->on_waveform_jump_data = user_data;
}
