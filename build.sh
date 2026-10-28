#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/build"
PURE_SOURCES=(pool.cpp addon_lua.cpp grace.cpp report.cpp sandbox_compare.cpp command.cpp turns.cpp shot_folders.cpp
    figures.cpp address_space.cpp)
NAME=LinuxXI
DLL="$NAME.dll"
PLUGIN_SOURCES=("${PURE_SOURCES[@]}" d3d_grace.cpp imports.cpp memory_query.cpp plugin.cpp)

# compile DIR COMPILER [FLAGS...] -- SOURCES...: each source to an object of its own in DIR, as many at a time as there
# are cores, the objects in OBJECTS. Each source's messages print whole once all are done, and it fails if any did.
compile() {
    local dir="$1" cores source object failed=0
    local command=()
    shift
    while [[ "$1" != -- ]]; do
        command+=("$1")
        shift
    done
    shift
    cores="$(nproc)"
    rm -rf "$dir"
    mkdir -p "$dir"
    OBJECTS=()
    for source in "$@"; do
        object="$dir/$(basename "$source" .cpp)"
        OBJECTS+=("$object.o")
        while (( $(jobs -rp | wc -l) >= cores )); do wait -n; done
        { "${command[@]}" -c -o "$object.o" "$source" > "$object.log" 2>&1 || touch "$object.failed"; } &
    done
    wait
    for object in "${OBJECTS[@]%.o}"; do
        cat "$object.log" >&2
        if [[ -e "$object.failed" ]]; then failed=1; fi
    done
    return "$failed"
}

unit_tests() {
    mkdir -p "$OUT"
    compile "$OUT/unit" g++ -std=c++20 -O1 -Wall -Wextra -Werror -I"$ROOT/src" -I"$ROOT/tests" -- \
        "${PURE_SOURCES[@]/#/$ROOT/src/}" "$ROOT"/tests/*.cpp
    g++ -o "$OUT/unit_tests" "${OBJECTS[@]}"
    "$OUT/unit_tests"
}

# Rejects calls to Ashita's overloaded or variadic virtuals and to ones that return a struct by value, which MSVC
# returns through a hidden pointer.
abi_check() { python3 "$ROOT/tools/abi_check.py"; }

plugin() {
    mkdir -p "$OUT"
    # -fpermissive for the SDK's Registry.h (an int* for an LPDWORD). The game calls in with the stack 4-byte aligned,
    # and 32-bit Windows' malloc aligns to 8 where GCC takes new to give 16.
    compile "$OUT/plugin" i686-w64-mingw32-g++ -std=c++20 -O2 -msse2 -mfpmath=sse -mincoming-stack-boundary=2 -faligned-new=8 \
        -isystem "$ROOT/shim" -isystem "$ROOT/third_party/ashita-sdk" -I"$ROOT/src" \
        -Wall -Wextra -Werror -fpermissive -- "${PLUGIN_SOURCES[@]/#/$ROOT/src/}"
    i686-w64-mingw32-g++ -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at \
        -o "$OUT/$DLL" "${OBJECTS[@]}"
    # The DLL ships without its debug sections; $NAME.debug beside it keeps them and the symbols, for reading crashes.
    i686-w64-mingw32-objcopy --only-keep-debug "$OUT/$DLL" "$OUT/$NAME.debug"
    i686-w64-mingw32-strip --strip-all "$OUT/$DLL"
    i686-w64-mingw32-objcopy --add-gnu-debuglink="$OUT/$NAME.debug" "$OUT/$DLL"
    echo "built $OUT/$DLL"
}

install_plugin() {
    local PLUGINS_DIR="${LINUXXI_PLUGINS_DIR:?set LINUXXI_PLUGINS_DIR to the plugins folder of Ashita}"
    # Write a new file and rename it over the old one: overwriting in place would corrupt a copy Wine has mapped.
    cp "$OUT/$DLL" "$PLUGINS_DIR/$DLL.tmp"
    mv -f "$PLUGINS_DIR/$DLL.tmp" "$PLUGINS_DIR/$DLL"
    echo "installed $PLUGINS_DIR/$DLL"
}

case "${1:-install}" in
    test) unit_tests ;;
    plugin) unit_tests; abi_check; plugin ;;
    install) unit_tests; abi_check; plugin; install_plugin ;;
    *) echo "usage: $0 [test|plugin|install]" >&2; exit 2 ;;
esac
