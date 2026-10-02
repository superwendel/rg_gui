#!/usr/bin/env bash
# Linux build entry point. Run from any directory; runtime assets use the repo root.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$root"
target=${1:-test}
if (($#)); then shift; fi
export RG_CORE_DIR=${RG_CORE_DIR:-"$root/../rg_core"}
export RG_TEXT_DIR=${RG_TEXT_DIR:-"$root/../rg_text"}
build_dir=${RG_GUI_BUILD_DIR:-"$root/out/linux"}
cc=${CC:-gcc}
common=()
sdl_cflags=()
sdl_libs=()
host_ready=0
sdl_ready=0

fail() { printf '%s\n' "$*" >&2; exit 1; }

setup_host() {
    if ((host_ready)); then return; fi
    command -v "$cc" >/dev/null || fail "C compiler not found: $cc (set CC)."
    [[ -f "$RG_CORE_DIR/src/rg_defs.h" ]] || fail 'Set RG_CORE_DIR to the rg_core repository root.'
    [[ -f "$RG_TEXT_DIR/src/rg_text.h" ]] || fail 'Set RG_TEXT_DIR to the rg_text repository root.'
    mkdir -p "$build_dir"
    common=(-std=gnu11 -O2 -g -Wall -Wextra -Werror
        -isystem "$RG_CORE_DIR/src" -isystem "$RG_TEXT_DIR/src")
    local extra=()
    read -r -a extra <<< "${CFLAGS:-}"
    common+=("${extra[@]}")
    if [[ ${RG_GUI_SANITIZE:-0} == 1 ]]; then
        common+=(-O1 -fno-omit-frame-pointer -fno-sanitize-recover=all -fsanitize=address,undefined)
    fi
    host_ready=1
}

setup_sdl() {
    setup_host
    if ((sdl_ready)); then return; fi
    command -v pkg-config >/dev/null || fail 'pkg-config is required.'
    pkg-config --exists sdl3 || fail 'SDL3 development files not found; set PKG_CONFIG_PATH to the directory containing sdl3.pc.'
    local flags=() flag
    read -r -a flags <<< "$(pkg-config --cflags sdl3)"
    for flag in "${flags[@]}"; do
        # Keep dependency-only diagnostics out of this project's strict build.
        if [[ $flag == -I* ]]; then sdl_cflags+=(-isystem "${flag#-I}")
        else sdl_cflags+=("$flag"); fi
    done
    read -r -a sdl_libs <<< "$(pkg-config --libs sdl3) ${LDFLAGS:-}"
    sdl_ready=1
}

build_test() {
    setup_sdl
    local name=$1 source=$2
    shift 2
    printf 'Building %s with %s...\n' "$name" "$cc"
    local command=("$cc" "${common[@]}" "${sdl_cflags[@]}" -UNDEBUG "$@" "$source"
        "${sdl_libs[@]}" -lm -o "$build_dir/$name")
    "${command[@]}"
    if [[ $name == test_demo_tearout || $name == test_gui_gpu_device ]]; then
        python3 .github/scripts/write-linux-build-metadata.py "$build_dir/$name" "${command[@]}"
    fi
}

run_test() {
    local name=$1 source=${2:-"tests/$1.c"}
    shift
    if (($#)); then shift; fi
    build_test "$name" "$source" "$@"
    "$build_dir/$name"
}

test_assets() {
    setup_host
    "$cc" "${common[@]}" -UNDEBUG tests/test_assets.c -lm -o "$build_dir/test_assets"
    "$build_dir/test_assets"
    python3 tests/test_asset_hashes.py
}

test_no_string_ids() {
    run_test test_gui_no_string_ids
    if "$cc" "${common[@]}" "${sdl_cflags[@]}" -UNDEBUG \
        -DRG_GUI_TEST_EXPECT_STRING_ID_FAILURE -c tests/test_gui_no_string_ids.c \
        -o "$build_dir/test_gui_no_string_ids_negative.o" \
        >"$build_dir/test_gui_no_string_ids_negative.log" 2>&1; then
        fail 'RG_GUI_NO_STRING_IDS unexpectedly allowed a disabled string-ID API.'
    fi
    printf '%s\n' 'RG_GUI_NO_STRING_IDS rejected disabled string-ID APIs as expected.'
}

test_demo_helpers() {
    run_test test_demo_profile
    run_test test_demo_format
}

test_gpu_prepare() {
    run_test test_gui_gpu
    case $(uname -m) in
        x86_64|amd64|i?86) run_test test_gui_gpu_sse2 tests/test_gui_gpu.c -DRG_GUI_GPU_USE_SSE2=1 -msse2 ;;
        *) printf '%s\n' 'SSE2 packing checks require x86; portable preparation checks passed.' ;;
    esac
}

test_host() {
    test_assets
    test_no_string_ids
    test_demo_helpers
    run_test test_gui
    run_test test_renderer
    test_gpu_prepare
    python3 -m unittest discover -s benchmarks -p 'test_*.py'
    printf '%s\n' 'Host and GPU-preparation tests passed; no GPU device was executed.'
}

shaders() {
    local shadercross=${SHADERCROSS_EXE:-} spec name stage
    if [[ -z $shadercross ]]; then
        shadercross=$(command -v shadercross || true)
        if [[ -z $shadercross && -x "$root/out/linux-deps/shadercross/bin/shadercross" ]]; then
            shadercross="$root/out/linux-deps/shadercross/bin/shadercross"
        fi
    fi
    [[ -n $shadercross && -x $shadercross ]] || fail 'SDL_shadercross not found. Run .github/scripts/setup-linux-deps.sh or set SHADERCROSS_EXE.'
    mkdir -p shaders/Compiled/SPIRV
    for spec in rg_gui_text.vert:vertex rg_gui_text.frag:fragment rg_gui_expand.comp:compute \
        rg_gui_geometry.vert:vertex rg_gui_solid.frag:fragment rg_gui_image.frag:fragment \
        rg_gui_indexed.vert:vertex rg_gui_indexed.frag:fragment; do
        name=${spec%:*}
        stage=${spec#*:}
        "$shadercross" "shaders/$name.hlsl" -s HLSL -d SPIRV -t "$stage" -e main \
            -o "shaders/Compiled/SPIRV/$name.spv"
        [[ -s shaders/Compiled/SPIRV/$name.spv ]] || fail "Empty shader output: $name"
    done
    printf '%s\n' 'Eight Vulkan SPIR-V shaders compiled successfully.'
}

build_demo() {
    setup_sdl
    local name=rg_gui_demo_$1
    printf 'Building %s with %s...\n' "$name" "$cc"
    local command=("$cc" "${common[@]}" "${sdl_cflags[@]}" -O2 -DNDEBUG "examples/$name.c"
        "${sdl_libs[@]}" -lm -o "$build_dir/$name")
    "${command[@]}"
    python3 .github/scripts/write-linux-build-metadata.py "$build_dir/$name" "${command[@]}"
}

demo() {
    shaders
    build_demo minimal
    build_demo full
    build_demo tearout
}

test_gpu_device_build() { build_test test_gui_gpu_device tests/test_gui_gpu_device.c; }
test_demo_lifecycle_build() {
    setup_sdl
    pkg-config --exists x11 || fail 'Native close lifecycle checks need X11 development files (for example libx11-dev).'
    local x11_cflags=() x11_libs=()
    read -r -a x11_cflags <<< "$(pkg-config --cflags x11)"
    read -r -a x11_libs <<< "$(pkg-config --libs x11)"
    # Libraries must follow the translation unit for --as-needed linkers.
    sdl_libs+=("${x11_libs[@]}")
    build_test test_demo_tearout tests/test_demo_tearout.c -DRG_GUI_TEST_X11_NATIVE_CLOSE=1 "${x11_cflags[@]}"
}

run_demo_smoke_built() {
    local name
    for name in minimal full tearout; do "$build_dir/rg_gui_demo_$name" --smoke-test; done
}

test_ci() {
    test_host
    demo
    test_gpu_device_build
    test_demo_lifecycle_build
    printf '%s\n' 'Linux CI checks passed. Use test_release for local GPU/display execution.'
}

case $target in
    test) test_host ;;
    test_sanitize)
        export RG_GUI_SANITIZE=1
        export RG_GUI_BUILD_DIR="$build_dir/sanitize"
        export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:strict_string_checks=1}
        export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}
        export SDL_VIDEODRIVER=dummy
        exec "$root/build.sh" test ;;
    test_assets) test_assets ;;
    test_no_string_ids) test_no_string_ids ;;
    test_demo_helpers) test_demo_helpers ;;
    test_gui) run_test test_gui ;;
    test_renderer) run_test test_renderer ;;
    test_gpu|test_gpu_prepare) test_gpu_prepare ;;
    shaders) shaders ;;
    demo) demo ;;
    demo_minimal|demo_full|demo_tearout) shaders; build_demo "${target#demo_}" ;;
    test_gpu_device_build) test_gpu_device_build ;;
    test_gpu_device) shaders; test_gpu_device_build; "$build_dir/test_gui_gpu_device" ;;
    test_demo_lifecycle_build) test_demo_lifecycle_build ;;
    test_demo_lifecycle) shaders; test_demo_lifecycle_build; "$build_dir/test_demo_tearout" ;;
    demo_smoke) demo; run_demo_smoke_built ;;
    test_ci) test_ci ;;
    test_release)
        test_ci
        "$build_dir/test_gui_gpu_device"
        run_demo_smoke_built
        "$build_dir/test_demo_tearout"
        printf '%s\n' 'All Linux release checks passed.' ;;
    run_demo|run_demo_full|run_demo_minimal|run_demo_tearout)
        name=${target#run_demo_}
        if [[ $target == run_demo ]]; then name=full; fi
        shaders
        build_demo "$name"
        exec "$build_dir/rg_gui_demo_$name" "$@" ;;
    *) fail "Unknown target: $target" ;;
esac
