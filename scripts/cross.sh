#!/usr/bin/env bash
set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
src_dir=$(cd "${PADBRIDGE_SRC:-$root}" && pwd)
app=app-$(basename "$src_dir")-$(printf %s "$src_dir" | cksum | cut -d" " -f1)
libs=(bitspan@v3.1.0 hid-parser@v3.4.0 wirefile@v2.2.0 padff@v3.6.0 cmdspawn@v3.5.0 vpad@v3.3.0 report-guess@v4.0.0)
targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=(x86_64-windows-gnu x86-linux-musl x86_64-freebsd)
failed=0

system_of() {
    case $1 in
    *windows*) echo Windows ;;
    *freebsd*) echo FreeBSD ;;
    *macos*) echo Darwin ;;
    *) echo Linux ;;
    esac
}

toolchain() {
    local target=$1 dir=$2
    mkdir -p "$dir"
    printf '#!/bin/sh\nexec zig cc -target %s "$@"\n' "$target" >"$dir/cc"
    printf '#!/bin/sh\nexec zig ar "$@"\n' >"$dir/ar"
    printf '#!/bin/sh\nexec zig ranlib "$@"\n' >"$dir/ranlib"
    printf '#!/bin/sh\nexec zig rc "$@"\n' >"$dir/rc"
    chmod +x "$dir/cc" "$dir/ar" "$dir/ranlib" "$dir/rc"
    cat >"$dir/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME $(system_of "$target"))
set(CMAKE_SYSTEM_PROCESSOR ${target%%-*})
set(CMAKE_C_COMPILER $dir/cc)
set(CMAKE_AR $dir/ar)
set(CMAKE_RANLIB $dir/ranlib)
set(CMAKE_RC_COMPILER $dir/rc)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
}

build_lib() {
    local name=${1%@*} tag=${1#*@} work=$2 prefix=$3 tc=$4 log=$5
    local upper src
    upper=$(echo "$name" | tr a-z- A-Z_)
    src=$work/src/$name-$tag
    [ -d "$src" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "https://github.com/augustoflorentino/$name.git" "$src"
    cmake -S "$src" -B "$work/$name" -DCMAKE_TOOLCHAIN_FILE="$tc" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -D${upper}_BUILD_TESTS=OFF -D${upper}_BUILD_EXAMPLES=OFF \
        -D${upper}_BUILD_FUZZ=OFF -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_FIND_ROOT_PATH="$prefix" \
        -DCMAKE_PREFIX_PATH="$prefix" >>"$log" 2>&1 &&
        cmake --build "$work/$name" -j >>"$log" 2>&1 &&
        cmake --install "$work/$name" >>"$log" 2>&1
}

build_app() {
    local work=$1 prefix=$2 tc=$3 log=$4
    cmake -S "$src_dir" -B "$work/$app" -DCMAKE_TOOLCHAIN_FILE="$tc" -DCMAKE_BUILD_TYPE=Release \
        -DPADBRIDGE_BUILD_TESTS=ON -DCMAKE_FIND_ROOT_PATH="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
        -DCMAKE_CROSSCOMPILING_EMULATOR="$5" >>"$log" 2>&1 &&
        cmake --build "$work/$app" -j >>"$log" 2>&1
}

run_wine() {
    local work=$1 log=$2
    local exe=$work/$app/padbridge.exe
    export WINEDEBUG=-all WINEPREFIX=$root/deps/cross/wine
    wine "$exe" --help >>"$log" 2>&1 && wine "$exe" backends >>"$log" 2>&1 &&
        (cd "$work/$app" && ctest --output-on-failure >>"$log" 2>&1)
}

for target in "${targets[@]}"; do
    work=$root/deps/cross/$target
    prefix=$work/prefix
    log=$work/build.log
    mkdir -p "$work"
    : >"$log"
    export PKG_CONFIG_LIBDIR=$prefix/lib/pkgconfig PKG_CONFIG_PATH=""
    toolchain "$target" "$work/tc"
    status=ok
    for lib in "${libs[@]}"; do
        if ! build_lib "$lib" "$work" "$prefix" "$work/tc/toolchain.cmake" "$log"; then
            status="lib ${lib%@*}"
            break
        fi
    done
    emulator=""
    [[ $target == *windows* ]] && emulator=wine
    if [ "$status" = ok ] && ! build_app "$work" "$prefix" "$work/tc/toolchain.cmake" "$log" "$emulator"; then
        status=padbridge
    fi
    if [ "$status" = ok ] && [[ $target == *windows* ]] && ! run_wine "$work" "$log"; then
        status=wine
    fi
    [ "$status" = ok ] || failed=1
    echo "$target: $status (build $work/$app, log $log)"
done
exit $failed
