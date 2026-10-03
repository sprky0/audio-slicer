#define PLUG_NAME "Frog"
#define PLUG_MFR "Rat Factory"
#define PLUG_VERSION_HEX 0x00001400
#define PLUG_VERSION_STR "0.20.0"
// AU subtype is the product, manufacturer is the org: auval -v aumu Frog RatF.
// Keep these lines free of trailing comments — prepare_resources-mac.py parses
// config.h by hand and folds anything after the value into the plist string.
#define PLUG_UNIQUE_ID 'Frog'
#define PLUG_MFR_ID 'RatF'
#define PLUG_URL_STR "https://github.com/Rat-Factory"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Rat Factory"
#define PLUG_CLASS_NAME Frog

#define BUNDLE_NAME "Frog"
#define BUNDLE_MFR "ratfactory"
// bundle ids: com.ratfactory.vst3.Frog, com.ratfactory.audiounit.Frog, com.ratfactory.app.Frog
#define BUNDLE_DOMAIN "com"

// 0-2: instrument (the appliance). 2-2: inputs available for record (F18).
#define PLUG_CHANNEL_IO "0-2 2-2"
#define SHARED_RESOURCES_SUBPATH "Frog"

#define PLUG_LATENCY 0
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
// Designed for the 7" appliance panel; the band layout flexes from here (PORT_PLAN §4).
#define PLUG_WIDTH 1024
#define PLUG_HEIGHT 600
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1
// Free re-layout at any aspect, never smaller than the panel it was designed for.
#define PLUG_MIN_WIDTH 1024
#define PLUG_MIN_HEIGHT 600
#define PLUG_MAX_WIDTH 8192
#define PLUG_MAX_HEIGHT 8192

#define AUV2_ENTRY Frog_Entry
#define AUV2_ENTRY_STR "Frog_Entry"
#define AUV2_FACTORY Frog_Factory
#define AUV2_VIEW_CLASS Frog_View
#define AUV2_VIEW_CLASS_STR "Frog_View"

#define AAX_TYPE_IDS 'FRG1', 'FRG2'
#define AAX_PLUG_MFR_STR "Rat Factory"
#define AAX_PLUG_NAME_STR "Frog\nFROG"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Synth"

#define VST3_SUBCATEGORY "Instrument|Sampler"
#define CLAP_MANUAL_URL "https://github.com/Rat-Factory"
#define CLAP_SUPPORT_URL "https://github.com/Rat-Factory"
#define CLAP_DESCRIPTION "Sample slicer and loop performer"
#define CLAP_FEATURES "instrument", "sampler"

// Samples are double end to end (the family decision D15).
#define SAMPLE_TYPE_DOUBLE

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
