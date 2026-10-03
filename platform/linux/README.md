# platform/linux — Frog as a Linux appliance

The plugin-side half of ROADMAP.md F3. Everything platform (ALSA, MIDI,
framebuffer, systemd, image) lives in the sister repo `ratfactory-linux-host`
and is consumed as a CMake library; this folder holds only what is Frog's.
It is a copy-and-edit of tink-vst's `platform/linux/`, the family's worked
example (`ratfactory-linux-host/docs/PORTING_A_PLUGIN.md`).

```
platform/linux/
  CMakeLists.txt              project root for the Linux build (not the mac one)
  frog-plugin/                frog_plugin (editor-less, HEADLESS_API), frog_plugin_ui + libfrog-editor.so (the panel)
  frog-appliance/             frog-appliance: main.cpp — construct Frog, wrap, rflh::runAppliance()
  systemd/frog-appliance.service
  docker-build-arm64.sh       cross-build in the host repo's Docker image, then a 2 s --render-wav check
  deploy-to-pi.sh             install binary + module + resources + unit on the dev Pi; --service makes it the boot unit
```

## Build

Three checkouts side by side: this repo with the `iPlug2` submodule (the
Rat Factory fork, branch `ratfactory-linux`; its patch series over upstream
is documented in tink-vst `platform/linux/iplug2-patches/README.md`) and
`ratfactory-linux-host`.

```
git submodule update --init iPlug2
platform/linux/docker-build-arm64.sh        # -> platform/linux/build-arm64/bin/frog-appliance
FROG_EDITOR=OFF platform/linux/docker-build-arm64.sh   # editor-less binary, no GL anywhere
```

`frog-appliance` links `rflh_host` and `frog_plugin_ui`. The editor's GPU side
(IGraphicsKMS on DRM/KMS + EGL + GBM, NanoVG on GLES2) is `lib/libfrog-editor.so`,
loaded after the first audio callback; the binary links no GPU library and
plays with the performance view only when the module or Mesa is missing.
Fonts and SVGs go to `<bin>/resources/`.

## Run

```
platform/linux/deploy-to-pi.sh              # copy; boot unit unchanged
platform/linux/deploy-to-pi.sh --service    # ... and make frog-appliance the boot unit
```

Offline check without a board or an ear:
`frog-appliance --render-wav out.wav --seconds 10 [--midi-file f.mid]`.

Data on the device: `/data/ratfactory/frog/{sessions,samples}/` and
`appliance.conf`.
