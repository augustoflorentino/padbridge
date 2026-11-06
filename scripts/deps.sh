#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
prefix=$root/deps/prefix
work=$root/deps/build
libs=(bitspan@v3.1.0 hid-parser@v3.4.0 wirefile@v2.2.0 padff@v3.6.0 cmdspawn@v3.5.0 vpad@v3.3.0 report-guess@v4.0.0)

mkdir -p "$prefix" "$work"
for lib in "${libs[@]}"; do
    name=${lib%@*} tag=${lib#*@}
    src=$work/$name-$tag
    [ -d "$src" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "https://github.com/augustoflorentino/$name.git" "$src"
    cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
    cmake --build "$src/build" >/dev/null
    cmake --install "$src/build" >/dev/null
    echo "$name $tag"
done
