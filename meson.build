project('audio-slicer', 'c',
  version : '1.0.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
  ],
)

# Dependencies
gtk3_dep     = dependency('gtk+-3.0',               version : '>= 3.22')
gst_dep      = dependency('gstreamer-1.0',           version : '>= 1.14')
gst_app_dep  = dependency('gstreamer-app-1.0',       version : '>= 1.14')
gst_audio_dep= dependency('gstreamer-audio-1.0',     version : '>= 1.14', required : false)
fftw3_dep    = dependency('fftw3',                   required : false)

deps = [gtk3_dep, gst_dep, gst_app_dep]
if gst_audio_dep.found()
  deps += gst_audio_dep
endif
if fftw3_dep.found()
  deps += fftw3_dep
endif

# Source files
sources = files(
  'src/audio_engine.c',
  'src/waveform_view.c',
  'src/audio_slicer_controller.c',
  'src/main.c',
)

# Compiler flags
cc = meson.get_compiler('c')
extra_args = []
if cc.has_argument('-Wno-deprecated-declarations')
  extra_args += '-Wno-deprecated-declarations'
endif

executable('audio-slicer',
  sources,
  dependencies   : deps,
  c_args         : extra_args,
  install        : true,
)
