#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "audio_slicer_controller.h"

/* -------------------------------------------------------------------------
 * Slicer instance state (one per slicer panel)
 * ---------------------------------------------------------------------- */
typedef struct {
	AudioSlicerController *ctrl;

	GtkWidget *panel;
	GtkWidget *file_btn;
	GtkWidget *play_pause_btn;
	GtkWidget *stop_btn;
	GtkWidget *reset_btn;
	GtkWidget *remove_btn;

	GtkWidget *start_spin;
	GtkWidget *end_spin;
	GtkWidget *subdiv_combo;

	GtkWidget *volume_scale;
	GtkWidget *pan_scale;

	gboolean   playing;
} SlicerInstance;

/* -------------------------------------------------------------------------
 * File chooser response
 * ---------------------------------------------------------------------- */
static void _on_file_set(GtkFileChooserButton *btn, gpointer data) {
	SlicerInstance *inst = (SlicerInstance *)data;
	char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(btn));
	if (!filename) return;

	if (!audio_slicer_controller_load_file(inst->ctrl, filename)) {
		GtkWidget *dlg = gtk_message_dialog_new(NULL,
			GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
			"Failed to load audio file:\n%s", filename);
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
	}
	g_free(filename);
}

/* -------------------------------------------------------------------------
 * Play / Pause button
 * ---------------------------------------------------------------------- */
static void _on_play_pause(GtkButton *btn, gpointer data) {
	SlicerInstance *inst = (SlicerInstance *)data;

	if (inst->playing) {
		audio_slicer_controller_pause(inst->ctrl);
		gtk_button_set_label(btn, "Play");
		inst->playing = FALSE;
	} else {
		if (audio_slicer_controller_is_paused(inst->ctrl)) {
			audio_slicer_controller_resume(inst->ctrl);
		} else {
			audio_slicer_controller_play(inst->ctrl, 0);
		}
		gtk_button_set_label(btn, "Pause");
		inst->playing = TRUE;
	}
}

/* -------------------------------------------------------------------------
 * Stop button
 * ---------------------------------------------------------------------- */
static void _on_stop(GtkButton *btn, gpointer data) {
	(void)btn;
	SlicerInstance *inst = (SlicerInstance *)data;
	audio_slicer_controller_stop(inst->ctrl);
	gtk_button_set_label(GTK_BUTTON(inst->play_pause_btn), "Play");
	inst->playing = FALSE;
}

/* -------------------------------------------------------------------------
 * Reset button
 * ---------------------------------------------------------------------- */
static void _on_reset(GtkButton *btn, gpointer data) {
	(void)btn;
	SlicerInstance *inst = (SlicerInstance *)data;

	audio_slicer_controller_stop(inst->ctrl);
	gtk_button_set_label(GTK_BUTTON(inst->play_pause_btn), "Play");
	inst->playing = FALSE;

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(inst->start_spin), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(inst->end_spin),   1.0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(inst->subdiv_combo), 0);

	gtk_range_set_value(GTK_RANGE(inst->volume_scale), 1.0);
	gtk_range_set_value(GTK_RANGE(inst->pan_scale),    0.0);

	audio_slicer_controller_set_volume(inst->ctrl, 1.0);
	audio_slicer_controller_set_pan(inst->ctrl, 0.0);
	audio_slicer_controller_slice(inst->ctrl, 0.0, 1.0, 2);
}

/* -------------------------------------------------------------------------
 * Slice button / helpers
 * ---------------------------------------------------------------------- */
static void _do_slice(SlicerInstance *inst) {
	double start = gtk_spin_button_get_value(GTK_SPIN_BUTTON(inst->start_spin));
	double end   = gtk_spin_button_get_value(GTK_SPIN_BUTTON(inst->end_spin));

	const char *subdiv_text = gtk_combo_box_text_get_active_text(
		GTK_COMBO_BOX_TEXT(inst->subdiv_combo));
	char *endptr = NULL;
	int subdiv = subdiv_text ? (int)strtol(subdiv_text, &endptr, 10) : 2;
	if (!subdiv_text || endptr == subdiv_text || subdiv <= 0) subdiv = 2;
	g_free((gpointer)subdiv_text);

	if (end > start && subdiv > 0)
		audio_slicer_controller_slice(inst->ctrl, start, end, subdiv);
}

static void _on_slice(GtkButton *btn, gpointer data) {
	(void)btn;
	_do_slice((SlicerInstance *)data);
}

static void _on_subdiv_changed(GtkComboBox *combo, gpointer data) {
	(void)combo;
	_do_slice((SlicerInstance *)data);
}

static void _on_spin_changed(GtkSpinButton *spin, gpointer data) {
	(void)spin;
	_do_slice((SlicerInstance *)data);
}

/* -------------------------------------------------------------------------
 * Volume / Pan controls
 * ---------------------------------------------------------------------- */
static void _on_volume_changed(GtkRange *range, gpointer data) {
	SlicerInstance *inst = (SlicerInstance *)data;
	audio_slicer_controller_set_volume(inst->ctrl, gtk_range_get_value(range));
}

static void _on_pan_changed(GtkRange *range, gpointer data) {
	SlicerInstance *inst = (SlicerInstance *)data;
	audio_slicer_controller_set_pan(inst->ctrl, gtk_range_get_value(range));
}

/* -------------------------------------------------------------------------
 * Remove slicer
 * ---------------------------------------------------------------------- */
static void _on_remove(GtkButton *btn, gpointer data) {
	(void)btn;
	SlicerInstance *inst = (SlicerInstance *)data;
	audio_slicer_controller_free(inst->ctrl);
	gtk_widget_destroy(inst->panel);
	g_free(inst);
}

/* -------------------------------------------------------------------------
 * Create a slicer panel
 * ---------------------------------------------------------------------- */
static SlicerInstance *_create_slicer(GtkWidget *slicers_box) {
	SlicerInstance *inst = g_new0(SlicerInstance, 1);

	/* Outer panel (vertical box inside a frame) */
	GtkWidget *frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(slicers_box), frame, FALSE, FALSE, 4);
	inst->panel = frame;

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	/* Create controller (waveform drawing area is packed into vbox) */
	inst->ctrl = audio_slicer_controller_new(vbox);

	/* --- Controls row 1: file, start, end, subdivisions, slice --- */
	GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(vbox), row1, FALSE, FALSE, 0);

	GtkWidget *file_label = gtk_label_new("File:");
	gtk_box_pack_start(GTK_BOX(row1), file_label, FALSE, FALSE, 0);

	inst->file_btn = gtk_file_chooser_button_new("Open Audio File",
	                                              GTK_FILE_CHOOSER_ACTION_OPEN);
	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Audio files");
	gtk_file_filter_add_mime_type(filter, "audio/wav");
	gtk_file_filter_add_mime_type(filter, "audio/mpeg");
	gtk_file_filter_add_mime_type(filter, "audio/ogg");
	gtk_file_filter_add_mime_type(filter, "audio/flac");
	gtk_file_filter_add_pattern(filter, "*.wav");
	gtk_file_filter_add_pattern(filter, "*.mp3");
	gtk_file_filter_add_pattern(filter, "*.ogg");
	gtk_file_filter_add_pattern(filter, "*.flac");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(inst->file_btn), filter);
	g_signal_connect(inst->file_btn, "file-set", G_CALLBACK(_on_file_set), inst);
	gtk_box_pack_start(GTK_BOX(row1), inst->file_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(row1), gtk_label_new("Start:"), FALSE, FALSE, 0);
	inst->start_spin = gtk_spin_button_new_with_range(0.0, 1.0, 0.01);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(inst->start_spin), 0.0);
	gtk_widget_set_size_request(inst->start_spin, 70, -1);
	g_signal_connect(inst->start_spin, "value-changed", G_CALLBACK(_on_spin_changed), inst);
	gtk_box_pack_start(GTK_BOX(row1), inst->start_spin, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(row1), gtk_label_new("End:"), FALSE, FALSE, 0);
	inst->end_spin = gtk_spin_button_new_with_range(0.0, 1.0, 0.01);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(inst->end_spin), 1.0);
	gtk_widget_set_size_request(inst->end_spin, 70, -1);
	g_signal_connect(inst->end_spin, "value-changed", G_CALLBACK(_on_spin_changed), inst);
	gtk_box_pack_start(GTK_BOX(row1), inst->end_spin, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(row1), gtk_label_new("Subdivisions:"), FALSE, FALSE, 0);
	inst->subdiv_combo = gtk_combo_box_text_new();
	const char *subdiv_opts[] = { "2", "4", "8", "16", NULL };
	for (int i = 0; subdiv_opts[i]; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(inst->subdiv_combo), subdiv_opts[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(inst->subdiv_combo), 0);
	g_signal_connect(inst->subdiv_combo, "changed", G_CALLBACK(_on_subdiv_changed), inst);
	gtk_box_pack_start(GTK_BOX(row1), inst->subdiv_combo, FALSE, FALSE, 0);

	GtkWidget *slice_btn = gtk_button_new_with_label("Slice");
	g_signal_connect(slice_btn, "clicked", G_CALLBACK(_on_slice), inst);
	gtk_box_pack_start(GTK_BOX(row1), slice_btn, FALSE, FALSE, 0);

	/* --- Controls row 2: play/pause, stop, reset, remove, volume, pan --- */
	GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(vbox), row2, FALSE, FALSE, 0);

	inst->play_pause_btn = gtk_button_new_with_label("Play");
	g_signal_connect(inst->play_pause_btn, "clicked", G_CALLBACK(_on_play_pause), inst);
	gtk_box_pack_start(GTK_BOX(row2), inst->play_pause_btn, FALSE, FALSE, 0);

	inst->stop_btn = gtk_button_new_with_label("Stop");
	g_signal_connect(inst->stop_btn, "clicked", G_CALLBACK(_on_stop), inst);
	gtk_box_pack_start(GTK_BOX(row2), inst->stop_btn, FALSE, FALSE, 0);

	inst->reset_btn = gtk_button_new_with_label("Reset");
	g_signal_connect(inst->reset_btn, "clicked", G_CALLBACK(_on_reset), inst);
	gtk_box_pack_start(GTK_BOX(row2), inst->reset_btn, FALSE, FALSE, 0);

	inst->remove_btn = gtk_button_new_with_label("Remove");
	g_signal_connect(inst->remove_btn, "clicked", G_CALLBACK(_on_remove), inst);
	gtk_box_pack_start(GTK_BOX(row2), inst->remove_btn, FALSE, FALSE, 0);

	/* Volume */
	gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Volume:"), FALSE, FALSE, 6);
	inst->volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
	gtk_range_set_value(GTK_RANGE(inst->volume_scale), 1.0);
	gtk_scale_set_draw_value(GTK_SCALE(inst->volume_scale), TRUE);
	gtk_widget_set_size_request(inst->volume_scale, 100, -1);
	g_signal_connect(inst->volume_scale, "value-changed", G_CALLBACK(_on_volume_changed), inst);
	gtk_box_pack_start(GTK_BOX(row2), inst->volume_scale, FALSE, FALSE, 0);

	/* Pan */
	gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Pan:"), FALSE, FALSE, 6);
	inst->pan_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -1.0, 1.0, 0.01);
	gtk_range_set_value(GTK_RANGE(inst->pan_scale), 0.0);
	gtk_scale_set_draw_value(GTK_SCALE(inst->pan_scale), TRUE);
	gtk_widget_set_size_request(inst->pan_scale, 100, -1);
	g_signal_connect(inst->pan_scale, "value-changed", G_CALLBACK(_on_pan_changed), inst);
	gtk_box_pack_start(GTK_BOX(row2), inst->pan_scale, FALSE, FALSE, 0);

	gtk_widget_show_all(frame);
	return inst;
}

/* -------------------------------------------------------------------------
 * Application activation
 * ---------------------------------------------------------------------- */
typedef struct {
	GtkWidget *window;
	GtkWidget *slicers_box;
} AppState;

static void _on_add_slicer(GtkButton *btn, gpointer data) {
	(void)btn;
	AppState *app = (AppState *)data;
	_create_slicer(app->slicers_box);
}

static void _on_activate(GtkApplication *app, gpointer data) {
	(void)data;

	AppState *state = g_new0(AppState, 1);

	state->window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(state->window), "Audio Slicer");
	gtk_window_set_default_size(GTK_WINDOW(state->window), 900, 600);

	/* Top-level vbox */
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
	gtk_container_add(GTK_CONTAINER(state->window), vbox);

	/* Header */
	GtkWidget *title_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(title_label),
		"<span size='x-large' weight='bold'>Audio Slicer</span>");
	gtk_widget_set_halign(title_label, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);

	/* Scrollable area for slicers */
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

	state->slicers_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_add(GTK_CONTAINER(scroll), state->slicers_box);

	/* Add Slicer button */
	GtkWidget *add_btn = gtk_button_new_with_label("+ Add Audio Slicer");
	gtk_widget_set_halign(add_btn, GTK_ALIGN_START);
	g_signal_connect(add_btn, "clicked", G_CALLBACK(_on_add_slicer), state);
	gtk_box_pack_start(GTK_BOX(vbox), add_btn, FALSE, FALSE, 0);

	gtk_widget_show_all(state->window);

	/* Create one slicer by default */
	_create_slicer(state->slicers_box);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(int argc, char **argv) {
	gst_init(&argc, &argv);

	GtkApplication *app = gtk_application_new("org.gnome.audio-slicer",
	                                           G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(_on_activate), NULL);

	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
