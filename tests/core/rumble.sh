#!/bin/sh
bin=$1
dir=$2
[ -w /dev/uinput ] || exit 77
"$dir/mkcapture" "$dir/long.pcapng" 150 || exit 1
rm -f "$dir/out.pcapng"
"$bin" replay --profile ninja-sword --record "$dir/out.pcapng" "$dir/long.pcapng" &
pid=$!
"$dir/ffplay" >/dev/null || { kill $pid; exit 1; }
wait $pid || exit 1
"$dir/outpackets" "$dir/out.pcapng"
