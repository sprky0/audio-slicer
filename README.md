# Audio Slicer

A multi-instance audio slicing desktop application for Linux / GNOME.

## Implementations

### C / GTK3 (primary)

Located in `src/` — a full C port using:

- **GTK 3** for the desktop GUI (drawing area, file chooser, controls)
- **GStreamer** for audio decoding (supports WAV, MP3, OGG, FLAC, and any format GStreamer can decode) and GStreamer-pipeline–based playback with volume and stereo pan
- **libfftw3** / bucket-peak precomputation for waveform visualization

#### Features

- Load audio files via a GTK file-chooser button
- Visualize the decoded waveform in a GTK drawing area with white-on-dark peaks
- Select a sub-range of the file (Start / End in 0–1 normalised range)
- Slice the selection into 2, 4, 8, or 16 equal subdivisions
- Play, pause, resume, and stop each slicer independently
- Animated playhead that tracks live playback position
- Per-segment enable/disable toggle (click the coloured circle)
- Click anywhere on the waveform to seek to that position
- Volume and pan sliders per slicer instance
- Support for multiple independent slicer panels ("Add Audio Slicer")

#### Build

Requirements: `libgtk-3-dev`, `libgstreamer1.0-dev`, `libgstreamer-plugins-base1.0-dev`, `libfftw3-dev`, `meson`, `ninja`.

```sh
# Install build dependencies (Ubuntu/Debian)
sudo apt-get install libgtk-3-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev libfftw3-dev meson ninja-build

# Configure and build
meson setup build
ninja -C build

# Run
./build/audio-slicer
```

### JavaScript / Web (original)

Located in `public/` — a browser-based implementation using the Web Audio API and Canvas API.
Open `public/index.html` in a modern browser (no server required for local files with a dev server, or use a simple HTTP server).
