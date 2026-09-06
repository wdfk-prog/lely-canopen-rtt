#!/usr/bin/env sh
set -eu

if [ "$#" -ne 0 ]; then
    printf 'usage: %s\n' "$0" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
project_root=$(CDPATH= cd -P "$script_dir/.." && pwd)
input="$project_root/examples/node1/node1.dcf"
output="$project_root/examples/node1/node1_sdev.c"
tool=${DCF2C:-dcf2c}
tmp="$output.tmp.$$"

case "$tool" in
    */*)
        if [ ! -x "$tool" ]; then
            printf '%s: DCF2C is not executable: %s\n' "$0" "$tool" >&2
            exit 127
        fi
        ;;
    *)
        if ! command -v "$tool" >/dev/null 2>&1; then
            printf '%s: dcf2c not found; set DCF2C=/path/to/dcf2c\n' "$0" >&2
            exit 127
        fi
        ;;
esac

cleanup()
{
    rm -f "$tmp"
}
trap cleanup 0
trap 'exit 1' 1 2 3 15

"$tool" "$input" node1_sdev -o "$tmp"
mv -f "$tmp" "$output"
trap - 0

printf 'generated %s from %s\n' "$output" "$input"
