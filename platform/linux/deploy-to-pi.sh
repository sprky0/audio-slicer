#!/usr/bin/env bash
set -euo pipefail

# Install frog-appliance (+ the Rat Factory logo assets + the unit) on the dev
# Pi and, with --service, make it the boot unit. Forked from
# ratfactory-linux-host/scripts/deploy-to-pi.sh and kept to the same shape:
# root-owned /opt/ratfactory (the binary sits beside rflh-poc), the
# `ratfactory` service user, and on a read-only root (overlayroot, L7) every
# file is installed twice — into the live tree so the restarted unit runs it
# now, and into the underlying root at /media/root-ro, remounted writable for
# the copy and synced, so the next boot has it too. No reboot, no toggling
# the overlay off. The boot-unit choice itself is
# ratfactory-linux-host/scripts/pi-switch-unit.sh (one appliance unit
# enabled; the others installed and disabled).
#
#   platform/linux/deploy-to-pi.sh                # copy binary + assets + unit; boot unit unchanged
#   platform/linux/deploy-to-pi.sh --service      # ... then pi-switch-unit.sh frog-appliance.service (others disabled, kept)
#   platform/linux/deploy-to-pi.sh --disable      # pi-switch-unit.sh poc (frog-appliance disabled, kept)
#
# Switch back to the proof-of-concept unit at any time, without a copy:
#   ../ratfactory-linux-host/scripts/pi-switch-unit.sh poc
#
# Environment overrides:
#   RFLH_ROOT         ratfactory-linux-host checkout (default: ../ratfactory-linux-host;
#                     provides assets/ and scripts/pi-switch-unit.sh)
#   RFLH_PI_HOST      target hostname            (default: rflh-pi.local)
#   RFLH_PI_USER      ssh user                   (default: pi)
#   RFLH_PI_PORT      ssh port                   (default: 22)
#   RFLH_SSH_OPTS     extra ssh/scp options, word-split
#   RFLH_BUILD_DIR    build directory            (default: platform/linux/build-arm64)
#   MESA_BUNDLE       the vc4 Mesa runtime for the editor on the panel: a
#                     directory with lib/libEGL.so.1, libGLESv2.so.2, libgbm.so.1,
#                     libgallium-*.so and lib/gbm/dri_gbm.so (ratfactory-linux-host
#                     scripts/mesa-vc4-build.sh output, no LLVM, ~18 MB); installed
#                     in /opt/ratfactory/lib beside the editor module
#                     (libfrog-editor.so, whose RPATH is $ORIGIN). Optional since
#                     1.68.10: the binary links no GPU library, so without Mesa on
#                     the board it still plays, with the performance view only
#                     (the script says so); once installed, Mesa stays
#
# Paths match systemd/frog-appliance.service; edit both if you move things:
# /opt/ratfactory/bin/frog-appliance, /opt/ratfactory/assets/, data root
# /data/ratfactory (the unit's only ReadWritePaths=), Frog's state in
# /data/ratfactory/frog (RF_DATA_DIR: sessions/, samples/, appliance.conf), and the
# getty@tty1 drop-in from ratfactory-linux-host/systemd/ that keeps the
# panel in graphics mode under the splash.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RFLH_ROOT="${RFLH_ROOT:-$(cd "${REPO_ROOT}/.." && pwd)/ratfactory-linux-host}"

HOST="${RFLH_PI_HOST:-rflh-pi.local}"
USER="${RFLH_PI_USER:-pi}"
PORT="${RFLH_PI_PORT:-22}"
PREFIX="/opt/ratfactory"
DATA_ROOT="/data/ratfactory"
DATA_DIR="${DATA_ROOT}/frog"
SERVICE_USER="ratfactory"
UNIT="frog-appliance.service"

BINARY="${RFLH_BUILD_DIR:-${SCRIPT_DIR}/build-arm64}/bin/frog-appliance"
RESOURCES="$(dirname "${BINARY}")/resources"
MODULE="$(dirname "${BINARY}")/../lib/libfrog-editor.so"
MESA_BUNDLE="${MESA_BUNDLE:-}"
UNIT_SRC="${SCRIPT_DIR}/systemd/${UNIT}"
ASSETS_DIR="${RFLH_ROOT}/assets"
SWITCH="${RFLH_ROOT}/scripts/pi-switch-unit.sh"
GETTY_DROPIN_SRC="${RFLH_ROOT}/systemd/getty@tty1.service.d/90-rflh-keep-graphics.conf"
GETTY_DROPIN_DST="/etc/systemd/system/getty@tty1.service.d/90-rflh-keep-graphics.conf"
STAGE="/tmp/frog-deploy"

MODE="copy"
while [ $# -gt 0 ]; do
    case "$1" in
        --no-service) MODE="copy"; shift ;;
        --service)    MODE="service"; shift ;;
        --disable)    MODE="disable"; shift ;;
        --host)       HOST="$2"; shift 2 ;;
        --user)       USER="$2"; shift 2 ;;
        --port)       PORT="$2"; shift 2 ;;
        -h|--help)    sed -n '4,35p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -x "${SWITCH}" ] || { echo "missing ${SWITCH}; set RFLH_ROOT to a ratfactory-linux-host checkout" >&2; exit 1; }
SWITCH_ARGS=(--host "${HOST}" --user "${USER}" --port "${PORT}")

if [ "${MODE}" = "disable" ]; then
    exec "${SWITCH}" poc "${SWITCH_ARGS[@]}"
fi

[ -f "${BINARY}" ] || { echo "missing ${BINARY}; run platform/linux/docker-build-arm64.sh first" >&2; exit 1; }
[ -f "${ASSETS_DIR}/rat-factory-720.png" ] || { echo "missing ${ASSETS_DIR}/rat-factory-*.png" >&2; exit 1; }
[ -f "${GETTY_DROPIN_SRC}" ] || { echo "missing ${GETTY_DROPIN_SRC}" >&2; exit 1; }
# The editor build (1.68.10): the binary links no GPU library; the editor's
# GPU side is lib/libfrog-editor.so, loaded after the first audio callback,
# with Mesa beside it. A binary from before the split (it links libEGL
# itself) is refused: without Mesa it would not start at all.
if LC_ALL=C grep -aq 'libEGL.so.1' "${BINARY}"; then
    echo "${BINARY} links libEGL itself (a build from before 1.68.10): rebuild; the appliance binary must start without Mesa" >&2
    exit 1
fi
EDITOR=0
MESA=0
if [ -f "${MODULE}" ]; then
    EDITOR=1
    [ -f "${RESOURCES}/fonts/Roboto-Regular.ttf" ] || { echo "missing ${RESOURCES} (the editor's fonts and SVGs)" >&2; exit 1; }
    if [ -n "${MESA_BUNDLE}" ]; then
        [ -f "${MESA_BUNDLE}/lib/gbm/dri_gbm.so" ] && [ -e "${MESA_BUNDLE}/lib/libEGL.so.1" ] || {
            echo "MESA_BUNDLE=${MESA_BUNDLE} is not the vc4 Mesa runtime (lib/, lib/gbm/; ratfactory-linux-host scripts/mesa-vc4-build.sh)" >&2
            exit 1
        }
        MESA=1
    fi
fi

# shellcheck disable=SC2206
EXTRA_SSH=(${RFLH_SSH_OPTS:-})
TARGET="${USER}@${HOST}"
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=5 -p "${PORT}" ${EXTRA_SSH[@]+"${EXTRA_SSH[@]}"} "${TARGET}")
SCP=(scp -q -P "${PORT}" ${EXTRA_SSH[@]+"${EXTRA_SSH[@]}"})

echo "==> reaching ${TARGET}"
"${SSH[@]}" true

echo "==> staging files in ${STAGE} on ${HOST}"
"${SSH[@]}" "rm -rf ${STAGE} && mkdir -p ${STAGE}/assets"
"${SCP[@]}" "${BINARY}" "${TARGET}:${STAGE}/frog-appliance"
"${SCP[@]}" "${ASSETS_DIR}/rat-factory-720.png" "${ASSETS_DIR}/rat-factory-1080.png" "${TARGET}:${STAGE}/assets/"
"${SCP[@]}" "${UNIT_SRC}" "${TARGET}:${STAGE}/${UNIT}"
"${SCP[@]}" "${GETTY_DROPIN_SRC}" "${TARGET}:${STAGE}/getty-dropin.conf"
if [ "${EDITOR}" = 1 ]; then
    "${SSH[@]}" "mkdir -p ${STAGE}/lib/gbm ${STAGE}/module ${STAGE}/resources"
    "${SCP[@]}" "${MODULE}" "${TARGET}:${STAGE}/module/libfrog-editor.so"
    if [ "${MESA}" = 1 ]; then
        # the libraries the Pi lacks, symlinks kept (scp -r would copy them as files)
        # (COPYFILE_DISABLE: no AppleDouble ._ files from a Mac's tar)
        (cd "${MESA_BUNDLE}/lib" && COPYFILE_DISABLE=1 tar cf - libEGL.so* libGLESv2.so* libgbm.so* libgallium-*.so gbm/dri_gbm.so) | \
            "${SSH[@]}" "tar xf - -C ${STAGE}/lib 2>/dev/null"
    fi
    "${SCP[@]}" -r "${RESOURCES}/fonts" "${RESOURCES}/img" "${TARGET}:${STAGE}/resources/"
fi

echo "==> installing (user ${SERVICE_USER}, ${PREFIX}, ${DATA_DIR})"
"${SSH[@]}" "sudo env STAGE=${STAGE} PREFIX=${PREFIX} DATA_ROOT=${DATA_ROOT} DATA_DIR=${DATA_DIR} \
             SERVICE_USER=${SERVICE_USER} UNIT=${UNIT} GETTY_DROPIN_DST=${GETTY_DROPIN_DST} EDITOR=${EDITOR} MESA=${MESA} bash -s" <<'REMOTE'
set -euo pipefail

# the binary's own libraries resolve from the system alone (a binary that
# cannot load means no audio at boot): refused before anything is installed
if ldd "${STAGE}/frog-appliance" | grep -q 'not found'; then
    ldd "${STAGE}/frog-appliance" | grep 'not found' >&2
    echo "    refusing to install: the binary's libraries do not resolve" >&2
    exit 1
fi
# the editor module's resolve from the staged Mesa, or the Mesa already in
# ${PREFIX}/lib; if they do not, it is installed anyway and the appliance
# runs with the performance view only (it logs why), audio unaffected
if [ "${EDITOR}" = 1 ]; then
    MISSING="$(LD_LIBRARY_PATH="${STAGE}/lib:${PREFIX}/lib" ldd "${STAGE}/module/libfrog-editor.so" | grep 'not found' || true)"
    if [ -n "${MISSING}" ]; then
        echo "    NOTE: the editor module's libraries do not resolve (set MESA_BUNDLE); the panel will show the performance view only:" >&2
        echo "${MISSING}" >&2
    fi
    echo "    editor build: libfrog-editor.so$([ "${MESA}" = 1 ] && echo " and Mesa $(du -sh "${STAGE}/lib" | cut -f1)") into ${PREFIX}/lib, resources into ${PREFIX}/bin/resources"
fi

if ! getent passwd "${SERVICE_USER}" >/dev/null; then
    useradd --system --user-group --no-create-home --home-dir "${DATA_ROOT}" \
            --shell /usr/sbin/nologin --comment "Rat Factory appliance host" "${SERVICE_USER}"
    echo "    created user ${SERVICE_USER}"
fi
for g in audio video input; do
    getent group "$g" >/dev/null && usermod -aG "$g" "${SERVICE_USER}"
done

ROOTS="/"
LOWER=""
if findmnt -n -o FSTYPE / | grep -q '^overlay$' && findmnt -n /media/root-ro >/dev/null 2>&1; then
    LOWER="/media/root-ro"
    ROOTS="/ ${LOWER}"
    echo "    read-only root active: installing into the live overlay and ${LOWER}"
fi

install_tree() {
    local root="$1"
    install -d -m 0755 -o root -g root "${root}${PREFIX}" "${root}${PREFIX}/bin" "${root}${PREFIX}/assets"
    # beside + rename: writing over a running executable fails with ETXTBSY,
    # and rename is atomic so a restart never sees a torn file
    install -m 0755 -o root -g root "${STAGE}/frog-appliance" "${root}${PREFIX}/bin/frog-appliance.new"
    mv -f "${root}${PREFIX}/bin/frog-appliance.new" "${root}${PREFIX}/bin/frog-appliance"
    install -m 0644 -o root -g root "${STAGE}"/assets/*.png "${root}${PREFIX}/assets/"
    if [ "${EDITOR}" = 1 ]; then
        # the editor: its module and Mesa (vc4, no LLVM) in lib/ (the
        # module's RPATH is $ORIGIN), its fonts / SVGs in bin/resources/
        install -d -m 0755 -o root -g root "${root}${PREFIX}/lib" "${root}${PREFIX}/lib/gbm" \
            "${root}${PREFIX}/bin/resources" "${root}${PREFIX}/bin/resources/fonts" "${root}${PREFIX}/bin/resources/img"
        # beside + rename, as the binary: a running appliance has it mapped
        install -m 0755 -o root -g root "${STAGE}/module/libfrog-editor.so" "${root}${PREFIX}/lib/libfrog-editor.so.new"
        mv -f "${root}${PREFIX}/lib/libfrog-editor.so.new" "${root}${PREFIX}/lib/libfrog-editor.so"
        if [ "${MESA}" = 1 ]; then
            cp -a "${STAGE}"/lib/. "${root}${PREFIX}/lib/"
            chown -R root:root "${root}${PREFIX}/lib"
            chmod -R u=rwX,go=rX "${root}${PREFIX}/lib"
        fi
        install -m 0644 -o root -g root "${STAGE}"/resources/fonts/* "${root}${PREFIX}/bin/resources/fonts/"
        install -m 0644 -o root -g root "${STAGE}"/resources/img/* "${root}${PREFIX}/bin/resources/img/"
    fi
    install -m 0644 -o root -g root "${STAGE}/${UNIT}" "${root}/etc/systemd/system/${UNIT}"
    # getty@tty1 keeps the VT in KD_GRAPHICS once the appliance set it
    # (TTYReset=no); harmless while rflh-poc is the boot unit
    install -d -m 0755 -o root -g root "${root}$(dirname "${GETTY_DROPIN_DST}")"
    install -m 0644 -o root -g root "${STAGE}/getty-dropin.conf" "${root}${GETTY_DROPIN_DST}"
}

for root in ${ROOTS}; do
    if [ "${root}" = "${LOWER}" ]; then
        mount -o remount,rw "${LOWER}"
        install_tree "${LOWER}"
        sync
        mount -o remount,ro "${LOWER}" 2>/dev/null || \
            echo "    note: ${LOWER} stays writable until the next boot (busy; overlay maps it) — nothing writes to it"
    else
        install_tree ""
    fi
done
systemctl daemon-reload

# /data is its own partition on an L7 card (outside the overlay): one pass.
install -d -m 0755 -o "${SERVICE_USER}" -g "${SERVICE_USER}" "${DATA_ROOT}" "${DATA_DIR}" "${DATA_DIR}/sessions" "${DATA_DIR}/samples"

# L8 round trip: same path as the unit being replaced = the sequence carries on
rt_of() { sed -n 's/.*--roundtrip[[:space:]]\{1,\}\([^[:space:]\\]*\).*/\1/p' "/etc/systemd/system/$1" 2>/dev/null | head -1; }
NEW_RT="$(rt_of "${UNIT}")"
OLD_RT="$(rt_of ratfactory-linux-host.service)"
if [ -n "${NEW_RT}" ]; then
    if [ -f "${NEW_RT}" ]; then
        echo "    round trip: ${NEW_RT} present, $(grep -a -m1 '^seq=' "${NEW_RT}" || echo 'seq unreadable'); the sequence carries on"
    else
        echo "    round trip: NOTE ${NEW_RT} does not exist yet (previous unit used ${OLD_RT:-none}); the first boot logs MISSING and starts at seq=1"
    fi
fi

rm -rf "${STAGE}"
sync
REMOTE

if [ "${MODE}" = "copy" ]; then
    # A running unit keeps the binary it started with; restart it so the
    # deploy takes effect now rather than at the next boot.
    if "${SSH[@]}" "systemctl is-active --quiet ${UNIT}"; then
        echo "==> ${UNIT} is running; restarting it on the new binary"
        "${SSH[@]}" "sudo systemctl restart ${UNIT}"
    fi
    echo "==> installed; boot unit unchanged"
    exec "${SWITCH}" status "${SWITCH_ARGS[@]}"
fi

echo "==> making ${UNIT} the boot unit"
"${SWITCH}" frog-appliance.service "${SWITCH_ARGS[@]}"
sleep 15
"${SSH[@]}" "journalctl -b -u ${UNIT} --no-pager -o cat | grep -E 'audio: selected|midi: opened|SCHED_FIFO|roundtrip:|boot_to_audio|fb [0-9]' | tail -8"
