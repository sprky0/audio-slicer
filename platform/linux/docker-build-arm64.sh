#!/usr/bin/env bash
set -euo pipefail

# Cross-build frog-appliance for linux/arm64 inside the ratfactory-linux-host
# build container (Debian bookworm, GCC 12, cmake, libasound2-dev, the
# DRM / GBM / EGL / GLES headers). Output:
# platform/linux/build-arm64/bin/frog-appliance (+ lib/libfrog-editor.so).
#
# Three checkouts take part, all bind-mounted into the container:
#   this repo                     -> /src
#   the Rat Factory iPlug2 fork   -> /src/iPlug2   (RFLH_IPLUG2_DIR; default: the submodule path)
#   ratfactory-linux-host         -> /rflh         (RFLH_ROOT; default: ../ratfactory-linux-host)
#
# Environment overrides:
#   RFLH_ROOT        path to a ratfactory-linux-host checkout
#   RFLH_IPLUG2_DIR  path to an iPlug2 checkout on the ratfactory-linux branch
#   RFLH_BUILD_DIR   build directory (default: platform/linux/build-arm64)
#   FROG_EDITOR      OFF builds the editor-less appliance (default ON)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RFLH_ROOT="${RFLH_ROOT:-$(cd "${REPO_ROOT}/.." && pwd)/ratfactory-linux-host}"
IPLUG2_DIR="${RFLH_IPLUG2_DIR:-${REPO_ROOT}/iPlug2}"
BUILD_DIR="${RFLH_BUILD_DIR:-${SCRIPT_DIR}/build-arm64}"
EDITOR_ON="${FROG_EDITOR:-ON}"
IMAGE_TAG="rflh-build"

[ -f "${RFLH_ROOT}/scripts/Dockerfile.build" ] || { echo "RFLH_ROOT=${RFLH_ROOT} is not a ratfactory-linux-host checkout" >&2; exit 1; }
[ -f "${IPLUG2_DIR}/IPlug/Headless/IPlugHeadless.h" ] || {
	echo "RFLH_IPLUG2_DIR=${IPLUG2_DIR} is not the Rat Factory iPlug2 fork (no IPlug/Headless/IPlugHeadless.h)." >&2
	echo "Run: git submodule update --init iPlug2" >&2
	exit 1
}

echo "==> building image ${IMAGE_TAG} (linux/arm64) from ${RFLH_ROOT}/scripts/Dockerfile.build"
docker build --platform linux/arm64 -t "${IMAGE_TAG}" -f "${RFLH_ROOT}/scripts/Dockerfile.build" "${RFLH_ROOT}/scripts"

mkdir -p "${BUILD_DIR}"
DOCKER_RUN=(docker run --rm --platform linux/arm64
	-v "${REPO_ROOT}:/src"
	-v "${IPLUG2_DIR}:/src/iPlug2"
	-v "${RFLH_ROOT}:/rflh"
	-v "${BUILD_DIR}:/build"
	-w /src "${IMAGE_TAG}")

echo "==> configuring cmake"
"${DOCKER_RUN[@]}" cmake -S /src/platform/linux -B /build -DCMAKE_BUILD_TYPE=Release \
	-DIPLUG2_DIR=/src/iPlug2 -DRFLH_ROOT=/rflh -DFROG_APPLIANCE_EDITOR="${EDITOR_ON}"

echo "==> building frog-appliance and the appliance tests"
TARGETS="frog-appliance shell_test"
[ "${EDITOR_ON}" = "ON" ] && TARGETS="${TARGETS} editor_shot_test"
"${DOCKER_RUN[@]}" cmake --build /build -j --target ${TARGETS}

# Frog as the appliance drives it (platform/linux/tests/): seconds, so it runs on every build
echo "==> running shell_test"
"${DOCKER_RUN[@]}" /build/bin/shell_test

# the editor, surfaceless on the container's Mesa (llvmpipe); its images land in the build dir
if [ "${EDITOR_ON}" = "ON" ]; then
	echo "==> running editor_shot_test"
	"${DOCKER_RUN[@]}" bash -c 'mkdir -p /build/shots && cd /build/bin && ./editor_shot_test /build/shots'
fi

# Proof of build without a board: the appliance renders a few seconds offline.
echo "==> render check"
"${DOCKER_RUN[@]}" /build/bin/frog-appliance --render-wav /build/render-check.wav --seconds 2

echo "==> build succeeded: ${BUILD_DIR}/bin/frog-appliance"
