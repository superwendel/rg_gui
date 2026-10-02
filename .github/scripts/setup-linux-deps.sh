#!/usr/bin/env bash
# Build the same pinned shadercross/SPIRV-Cross revisions as Windows, using
# installed SDL3 and upstream's hash-verified prebuilt DXC (no LLVM build).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
destination=${1:-"$root/out/linux-deps"}
mkdir -p "$destination"
destination=$(cd "$destination" && pwd)
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
shadercross_revision=1ca46e0ef7a9e50c706e7be6ef73ce467bac3b2e
spirv_cross_revision=d1d4adbefd411fc4721a2fece15a7f4aaa3dcdfa

for tool in cmake curl unzip sha256sum pkg-config; do
    command -v "$tool" >/dev/null || { printf 'Required tool not found: %s\n' "$tool" >&2; exit 1; }
done
pkg-config --exists sdl3 || { printf '%s\n' 'Install SDL3 development files or set PKG_CONFIG_PATH to sdl3.pc.' >&2; exit 1; }
[[ $(uname -m) == x86_64 ]] || { printf '%s\n' 'The pinned prebuilt Linux DXC package requires x86_64.' >&2; exit 1; }

download() {
    local name=$1 url=$2 expected=$3
    if [[ ! -f $destination/$name ]]; then
        curl --fail --location --retry 3 "$url" -o "$destination/$name.part"
        mv "$destination/$name.part" "$destination/$name"
    fi
    printf '%s  %s\n' "$expected" "$destination/$name" | sha256sum --check --status || {
        printf 'SHA-256 mismatch: %s\n' "$destination/$name" >&2; exit 1;
    }
}

download "SDL_shadercross-$shadercross_revision.zip" \
    "https://github.com/libsdl-org/SDL_shadercross/archive/$shadercross_revision.zip" \
    6e44f63b3343a046adeebe7b35cdc635bc9efc7b0c0f0f68b5feac0ff67d7320
download "SPIRV-Cross-$spirv_cross_revision.zip" \
    "https://github.com/KhronosGroup/SPIRV-Cross/archive/$spirv_cross_revision.zip" \
    186c9f04b3a2b7283d57c322faa2ea28f3fc6f99a395548a93e9bd0016168514

mkdir -p "$destination/sources"
unzip -q -o "$destination/SDL_shadercross-$shadercross_revision.zip" -d "$destination/sources"
unzip -q -o "$destination/SPIRV-Cross-$spirv_cross_revision.zip" -d "$destination/sources"
shadercross_source="$destination/sources/SDL_shadercross-$shadercross_revision"
spirv_cross_source="$destination/sources/SPIRV-Cross-$spirv_cross_revision"
sdl_prefix=$(pkg-config --variable=prefix sdl3)

# This immutable upstream script verifies the selected official DXC download.
(cd "$destination" && cmake -DCMAKE_SYSTEM_NAME=Linux -P \
    "$shadercross_source/build-scripts/download-prebuilt-DirectXShaderCompiler.cmake")

cmake -S "$spirv_cross_source" -B "$destination/spirv-cross-build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$destination/spirv-cross" \
    -DCMAKE_INSTALL_LIBDIR=lib -DSPIRV_CROSS_SHARED=ON -DSPIRV_CROSS_STATIC=OFF \
    -DSPIRV_CROSS_CLI=OFF -DSPIRV_CROSS_ENABLE_TESTS=OFF
cmake --build "$destination/spirv-cross-build" --parallel "$jobs"
cmake --install "$destination/spirv-cross-build"

# INSTALL_RUNTIME would also build unpinned Vulkan/vkd3d projects on Linux.
# Use the existing SDL and explicit runtime paths to our pinned dependencies.
cmake -S "$shadercross_source" -B "$destination/shadercross-build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$destination/shadercross" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    "-DCMAKE_PREFIX_PATH=$sdl_prefix;$destination/spirv-cross" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    "-DCMAKE_INSTALL_RPATH=$destination/shadercross/lib;$shadercross_source/external/DirectXShaderCompiler-binaries/linux/lib" \
    -DSDLSHADERCROSS_SHARED=ON -DSDLSHADERCROSS_STATIC=OFF \
    -DSDLSHADERCROSS_SPIRVCROSS_SHARED=ON -DSDLSHADERCROSS_VENDORED=OFF \
    -DSDLSHADERCROSS_DXC=ON -DSDLSHADERCROSS_CLI=ON -DSDLSHADERCROSS_TESTS=OFF \
    -DSDLSHADERCROSS_INSTALL=ON -DSDLSHADERCROSS_INSTALL_RUNTIME=OFF \
    -DSDLSHADERCROSS_INSTALL_CPACK=OFF -DSDLSHADERCROSS_INSTALL_MAN=OFF
cmake --build "$destination/shadercross-build" --parallel "$jobs"
cmake --install "$destination/shadercross-build"

shadercross_exe="$destination/shadercross/bin/shadercross"
"$shadercross_exe" --help
printf 'export SHADERCROSS_EXE=%q\n' "$shadercross_exe" > "$destination/env.sh"
if [[ -n ${GITHUB_ENV:-} ]]; then printf 'SHADERCROSS_EXE=%s\n' "$shadercross_exe" >> "$GITHUB_ENV"; fi
printf 'SDL %s; SDL_shadercross %s installed.\n' "$(pkg-config --modversion sdl3)" "$shadercross_revision"
printf 'For this shell: source %q\n' "$destination/env.sh"
