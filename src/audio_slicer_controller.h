#pragma once

#include <gtk/gtk.h>
#include "audio_engine.h"
#include "waveform_view.h"

/*
 * AudioSlicerController: Connects AudioEngine and WaveformView.
 * Handles event wiring, playhead animation, and multi-slicer support.
 */
typedef struct AudioSlicerController AudioSlicerController;

AudioSlicerController *audio_slicer_controller_new(GtkWidget *parent_box);
void                   audio_slicer_controller_free(AudioSlicerController *ctrl);

/* Load an audio file by path */
gboolean               audio_slicer_controller_load_file(AudioSlicerController *ctrl,
                                                          const char            *path);

/* Slice the loaded audio */
void                   audio_slicer_controller_slice(AudioSlicerController *ctrl,
                                                      double                 start,
                                                      double                 end,
                                                      int                    subdivisions);

/* Playback control */
void                   audio_slicer_controller_play(AudioSlicerController *ctrl, int index);
void                   audio_slicer_controller_stop(AudioSlicerController *ctrl);
void                   audio_slicer_controller_pause(AudioSlicerController *ctrl);
void                   audio_slicer_controller_resume(AudioSlicerController *ctrl);

/* Volume / pan */
void                   audio_slicer_controller_set_volume(AudioSlicerController *ctrl,
                                                           double                 vol);
void                   audio_slicer_controller_set_pan(AudioSlicerController *ctrl,
                                                        double                 pan);

/* State query */
gboolean               audio_slicer_controller_is_playing(const AudioSlicerController *ctrl);
gboolean               audio_slicer_controller_is_paused(const AudioSlicerController *ctrl);
