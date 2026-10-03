#!/bin/sh
# Build and run every engine test. Run from the Frog/ directory:
#
#   ./engine/tests/run-tests.sh          # everything except the TSan race test
#   ./engine/tests/run-tests.sh race     # only the ThreadSanitizer race test
#   ./engine/tests/run-tests.sh all      # both
#
# No framework: a test passes when it prints "ALL CHECKS PASSED" and exits 0.
# Exits non-zero if any test fails, so it is usable as a pre-commit gate.
#
# Cross-running on another box (build in Docker, run on the Pi):
#   FROG_TESTS_OUT=dir            keep the binaries in dir instead of a temp dir
#   FROG_TESTS_BUILD_ONLY=1       compile into FROG_TESTS_OUT, run nothing
#   FROG_TESTS_PREBUILT=1         run the binaries already in FROG_TESTS_OUT

set -u
cd "$(dirname "$0")/../.." || exit 1

if [ -n "${FROG_TESTS_OUT:-}" ]; then
	OUT=$FROG_TESTS_OUT; mkdir -p "$OUT"
else
	OUT=$(mktemp -d)
	trap 'rm -rf "$OUT"' EXIT
fi
BUILD_ONLY=${FROG_TESTS_BUILD_ONLY:-}
PREBUILT=${FROG_TESTS_PREBUILT:-}
MODE=${1:-default}
FAIL=0

ENGINE_SRCS="$(ls engine/*.c) engine/third_party/cJSON.c"
CFLAGS="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iengine"
case "$(uname -s)" in Linux) CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=200809L -pthread" ;; esac
LIBS="-lm"

say() { printf '\n=== %s ===\n' "$1"; }
build() { # name, extra flags...
	name=$1; shift
	[ -n "$PREBUILT" ] && return 0
	cc $CFLAGS "$@" "engine/tests/$name.c" $ENGINE_SRCS $LIBS -o "$OUT/$name" || return 1
}
run() { # name
	[ -n "$BUILD_ONLY" ] && return 0
	( cd engine/tests && "$OUT/$1" ) 2>&1 | tee "$OUT/$1.log" | tail -3
	grep -q "ALL CHECKS PASSED" "$OUT/$1.log" && ! grep -q "ThreadSanitizer" "$OUT/$1.log"
}
test_one() { # name, extra flags...
	name=$1; shift
	say "$name"
	if build "$name" "$@" && run "$name"; then
		echo "PASS $name"
	else
		echo "FAIL $name"; FAIL=1
	fi
}

if [ "$MODE" = "default" ] || [ "$MODE" = "all" ]; then
	for t in engine/tests/*_test.c; do
		n=$(basename "$t" .c)
		case "$n" in *race_test) continue ;; esac
		test_one "$n"
	done
fi
if [ "$MODE" = "race" ] || [ "$MODE" = "all" ]; then
	for t in engine/tests/*race_test.c; do
		[ -f "$t" ] || continue
		test_one "$(basename "$t" .c)" -fsanitize=thread -g -O1
	done
fi

printf '\n'
if [ "$FAIL" = 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $FAIL
