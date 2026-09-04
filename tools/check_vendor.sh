#!/usr/bin/env sh
set -eu

usage() {
    printf '%s\n' "Usage: $0 [--root DIR]"
}

root=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --root)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            root=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -z "$root" ]; then
    script_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
    root=$(CDPATH= cd -P "$script_dir/.." && pwd)
else
    root=$(CDPATH= cd -P "$root" && pwd)
fi

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

for path in UPSTREAM.lock VENDOR_ALLOWLIST.txt VENDOR_MANIFEST.sha256; do
    [ -f "$root/metadata/$path" ] || fail "required metadata file is missing: metadata/$path"
done

while IFS= read -r path; do
    case "$path" in
        ''|\#*) continue ;;
    esac
    source_path=${path%/}
    case "$source_path" in
        LICENSE|NOTICE)
            vendor_path=$source_path
            ;;
        *)
            vendor_path=upstream/$source_path
            ;;
    esac
    [ -e "$root/$vendor_path" ] || fail "allowlisted path is missing: $vendor_path"
done < "$root/metadata/VENDOR_ALLOWLIST.txt"

# The B0 package intentionally retains ev/io2 because the RT-Thread port will
# use Lely's executor and asynchronous I/O mechanisms. Keep unrelated
# application/legacy I/O/tap modules out of this target vendor boundary.
for path in \
    upstream/include/lely/coapp \
    upstream/include/lely/io \
    upstream/include/lely/tap \
    upstream/src/coapp \
    upstream/src/io \
    upstream/src/tap; do
    [ ! -e "$root/$path" ] || fail "excluded module is present: $path"
done

# Reject unexpected first-level modules. A new upstream module must be reviewed
# and explicitly added to the allowlist instead of entering the snapshot by accident.
for entry in "$root"/upstream/include/lely/*; do
    [ -e "$entry" ] || continue
    name=$(basename "$entry")
    case "$name" in
        features.h|libc|util|can|co|ev|io2) ;;
        *) fail "unexpected include module: upstream/include/lely/$name" ;;
    esac
done

for entry in "$root"/upstream/src/*; do
    [ -e "$entry" ] || continue
    name=$(basename "$entry")
    case "$name" in
        libc|util|can|co|ev|io2) ;;
        *) fail "unexpected source module: upstream/src/$name" ;;
    esac
done

if find "$root/upstream" -type l -print | grep . >/dev/null 2>&1; then
    fail "symbolic links are not allowed in the frozen vendor snapshot"
fi

# Verify that every <lely/...> include referenced by the retained snapshot is
# also present in the retained public-header tree. This is a static closure check,
# not a compile/link test.
missing=0
includes_tmp=${TMPDIR:-/tmp}/lely-vendor-includes.$$
trap 'rm -f "$includes_tmp"' EXIT HUP INT TERM
(
    grep -RhoE '#[[:space:]]*include[[:space:]]*<lely/[^>]+>' "$root/upstream" 2>/dev/null || true
) | sed -E 's/.*<([^>]+)>.*/\1/' | LC_ALL=C sort -u > "$includes_tmp"

while IFS= read -r inc; do
    [ -n "$inc" ] || continue
    if [ ! -f "$root/upstream/include/$inc" ]; then
        printf 'ERROR: retained source references missing header: %s\n' "$inc" >&2
        missing=1
    fi
done < "$includes_tmp"
[ "$missing" -eq 0 ] || exit 1

(
    cd "$root"
    sha256sum -c metadata/VENDOR_MANIFEST.sha256 >/dev/null
) || fail "metadata/VENDOR_MANIFEST.sha256 verification failed"

file_count=$(find "$root/upstream" -type f | wc -l | tr -d ' ')
printf 'Vendor baseline check OK: %s retained upstream files.\n' "$file_count"
