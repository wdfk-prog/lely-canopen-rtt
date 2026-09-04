#!/usr/bin/env sh
set -eu

usage() {
    cat <<USAGE
Usage:
  $0 --ref <tag-or-commit> [--remote <git-url>]
  $0 --source <upstream-checkout> [--ref <ref>]

Modes:
  --ref       Fetch the requested upstream revision into a temporary sparse
              checkout. Only paths listed in metadata/VENDOR_ALLOWLIST.txt are materialized.
  --source    Import from an existing upstream checkout/directory. A Git
              worktree must be its repository root and completely clean; the
              exact HEAD commit is recorded automatically. A plain directory
              requires --ref so provenance remains explicit.
USAGE
}

script_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -P "$script_dir/.." && pwd)
allowlist="$root/metadata/VENDOR_ALLOWLIST.txt"
lock="$root/metadata/UPSTREAM.lock"
check_script="$root/tools/check_vendor.sh"

[ -f "$allowlist" ] || { printf 'Missing %s\n' "$allowlist" >&2; exit 1; }
[ -f "$lock" ] || { printf 'Missing %s\n' "$lock" >&2; exit 1; }
[ -x "$check_script" ] || { printf 'Missing executable %s\n' "$check_script" >&2; exit 1; }

primary_url=$(sed -n 's/^UPSTREAM_PRIMARY_URL=//p' "$lock" | sed -n '1p')
source_dir=
ref=
remote=$primary_url

while [ "$#" -gt 0 ]; do
    case "$1" in
        --source)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            source_dir=$2
            shift 2
            ;;
        --ref)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            ref=$2
            shift 2
            ;;
        --remote)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            remote=$2
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

command -v sha256sum >/dev/null 2>&1 || { printf 'sha256sum is required.\n' >&2; exit 1; }
command -v mktemp >/dev/null 2>&1 || { printf 'mktemp is required.\n' >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/lely-rtt-update.XXXXXX")
staging="$root/.lely-update-staging.$$"
backup="$root/.lely-update-backup.$$"
backup_active=0

restore_backup() {
    rm -rf "$root/upstream" "$root/LICENSE" "$root/NOTICE" \
        "$root/metadata/VENDOR_MANIFEST.sha256" "$root/metadata/UPSTREAM.lock"
    for path in upstream LICENSE NOTICE; do
        if [ -e "$backup/$path" ]; then
            cp -pR "$backup/$path" "$root/$path"
        fi
    done
    mkdir -p "$root/metadata"
    for path in VENDOR_MANIFEST.sha256 UPSTREAM.lock; do
        if [ -e "$backup/metadata/$path" ]; then
            cp -p "$backup/metadata/$path" "$root/metadata/$path"
        fi
    done
}

cleanup() {
    rc=$?
    trap - 0 HUP INT TERM
    if [ "$backup_active" -eq 1 ] && [ -d "$backup" ]; then
        printf '%s\n' 'Update interrupted or failed; restoring the previous vendor snapshot.' >&2
        restore_backup
    fi
    rm -rf "$work" "$staging" "$backup"
    exit "$rc"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

for_each_allowlist() {
    while IFS= read -r path; do
        case "$path" in
            ''|\#*) continue ;;
        esac
        printf '%s\n' "$path"
    done < "$allowlist"
}

resolved_ref=UNRESOLVED
ref_verification=UNRESOLVED
import_source=

if [ -n "$source_dir" ]; then
    [ -d "$source_dir" ] || { printf 'Source directory not found: %s\n' "$source_dir" >&2; exit 1; }
    source_dir=$(CDPATH= cd -P "$source_dir" && pwd)
    import_source=external-source

    if command -v git >/dev/null 2>&1 && [ -e "$source_dir/.git" ]; then
        git_top=$(git -C "$source_dir" rev-parse --show-toplevel 2>/dev/null || true)
        [ -n "$git_top" ] || { printf 'Invalid Git worktree: %s\n' "$source_dir" >&2; exit 1; }
        git_top=$(CDPATH= cd -P "$git_top" && pwd)
        [ "$git_top" = "$source_dir" ] || {
            printf 'Git --source must be the repository root: %s\n' "$source_dir" >&2
            exit 1
        }
        if [ -n "$(git -C "$source_dir" status --porcelain --untracked-files=all)" ]; then
            printf 'Git --source worktree must be clean before import: %s\n' "$source_dir" >&2
            exit 1
        fi
        resolved_ref=$(git -C "$source_dir" rev-parse HEAD)
        ref_verification=GIT_CLEAN_WORKTREE
        import_source=git-worktree
    elif [ -n "$ref" ]; then
        resolved_ref=$ref
        ref_verification=USER_ASSERTED
    else
        printf '%s\n' 'Plain --source directories require --ref so provenance is not silently lost.' >&2
        exit 1
    fi
else
    [ -n "$ref" ] || { printf '%s\n' '--ref is required when --source is not used.' >&2; exit 2; }
    command -v git >/dev/null 2>&1 || { printf 'git is required for network update mode.\n' >&2; exit 1; }
    [ -n "$remote" ] || { printf 'No upstream remote URL is configured.\n' >&2; exit 1; }

    checkout="$work/checkout"
    git init -q "$checkout"
    git -C "$checkout" remote add origin "$remote"
    git -C "$checkout" config core.sparseCheckout true
    mkdir -p "$checkout/.git/info"
    : > "$checkout/.git/info/sparse-checkout"
    for_each_allowlist | while IFS= read -r path; do
        case "$path" in
            */) printf '/%s**\n' "$path" ;;
            *) printf '/%s\n' "$path" ;;
        esac
    done > "$checkout/.git/info/sparse-checkout"

    git -C "$checkout" fetch -q --depth=1 --filter=blob:none origin "$ref"
    git -C "$checkout" checkout -q --detach FETCH_HEAD
    source_dir=$checkout
    resolved_ref=$(git -C "$checkout" rev-parse HEAD)
    ref_verification=GIT_FETCHED
    import_source="git:$remote"
fi

for_each_allowlist | while IFS= read -r path; do
    source_path=${path%/}
    [ -e "$source_dir/$source_path" ] || {
        printf 'Required upstream path missing: %s\n' "$source_path" >&2
        exit 1
    }
done

rm -rf "$staging" "$backup"
mkdir -p "$staging/upstream" "$staging/metadata"
cp "$root/metadata/VENDOR_ALLOWLIST.txt" "$staging/metadata/VENDOR_ALLOWLIST.txt"
cp "$root/metadata/UPSTREAM.lock" "$staging/metadata/UPSTREAM.lock"

for_each_allowlist | while IFS= read -r path; do
    source_path=${path%/}
    case "$source_path" in
        LICENSE|NOTICE)
            destination="$staging/$source_path"
            ;;
        *)
            destination="$staging/upstream/$source_path"
            ;;
    esac
    mkdir -p "$(dirname "$destination")"
    if [ -d "$source_dir/$source_path" ]; then
        cp -pR "$source_dir/$source_path" "$destination"
    else
        cp -p "$source_dir/$source_path" "$destination"
    fi
done

(
    cd "$staging"
    find upstream LICENSE NOTICE -type f -print | LC_ALL=C sort | while IFS= read -r file; do
        sha256sum "$file"
    done > metadata/VENDOR_MANIFEST.sha256
)

now=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
awk -v ref="$resolved_ref" -v verification="$ref_verification" \
        -v source="$import_source" -v now="$now" '
    BEGIN { seen_ref=0; seen_verification=0; seen_source=0; seen_time=0 }
    /^CURRENT_UPSTREAM_REF=/ { print "CURRENT_UPSTREAM_REF=" ref; seen_ref=1; next }
    /^CURRENT_REF_VERIFICATION=/ {
        print "CURRENT_REF_VERIFICATION=" verification; seen_verification=1; next
    }
    /^CURRENT_IMPORT_SOURCE=/ { print "CURRENT_IMPORT_SOURCE=" source; seen_source=1; next }
    /^CURRENT_IMPORT_TIME_UTC=/ { print "CURRENT_IMPORT_TIME_UTC=" now; seen_time=1; next }
    { print }
    END {
        if (!seen_ref) print "CURRENT_UPSTREAM_REF=" ref
        if (!seen_verification) print "CURRENT_REF_VERIFICATION=" verification
        if (!seen_source) print "CURRENT_IMPORT_SOURCE=" source
        if (!seen_time) print "CURRENT_IMPORT_TIME_UTC=" now
    }
' "$staging/metadata/UPSTREAM.lock" > "$staging/metadata/UPSTREAM.lock.new"
mv "$staging/metadata/UPSTREAM.lock.new" "$staging/metadata/UPSTREAM.lock"

"$check_script" --root "$staging"

mkdir "$backup"
for path in upstream LICENSE NOTICE; do
    if [ -e "$root/$path" ]; then
        cp -pR "$root/$path" "$backup/$path"
    fi
done
mkdir -p "$backup/metadata"
for path in VENDOR_MANIFEST.sha256 UPSTREAM.lock; do
    if [ -e "$root/metadata/$path" ]; then
        cp -p "$root/metadata/$path" "$backup/metadata/$path"
    fi
done
backup_active=1

rm -rf "$root/upstream" "$root/LICENSE" "$root/NOTICE" \
    "$root/metadata/VENDOR_MANIFEST.sha256" "$root/metadata/UPSTREAM.lock"
mv "$staging/upstream" "$root/upstream"
mv "$staging/LICENSE" "$root/LICENSE"
mv "$staging/NOTICE" "$root/NOTICE"
mv "$staging/metadata/VENDOR_MANIFEST.sha256" "$root/metadata/VENDOR_MANIFEST.sha256"
mv "$staging/metadata/UPSTREAM.lock" "$root/metadata/UPSTREAM.lock"

"$check_script"
backup_active=0
rm -rf "$backup"
printf 'Updated Lely vendor snapshot to %s (%s).\n' "$resolved_ref" "$import_source"
