#!/bin/sh
set -eu

EWDB_URL=https://github.com/BrandonKowalski/ewdb.git
EWDB_REV=9ae15fa774ca584dafd6bdb57a2dec906c157442

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=${EWDB_SOURCE_DIR:-"$repo_root/build/ewdb-src"}
output=${EWDB_OUTPUT:-"$repo_root/build/ewdbd-linux-armv7"}

if ! command -v go >/dev/null 2>&1; then
	printf '%s\n' 'error: Go 1.26 or newer is required' >&2
	exit 1
fi

case $(go env GOVERSION) in
	go1.2[6-9]*|go1.[3-9][0-9]*) ;;
	*)
		printf 'error: Go 1.26 or newer is required (found %s)\n' "$(go env GOVERSION)" >&2
		exit 1
		;;
esac

mkdir -p "$(dirname -- "$source_dir")" "$(dirname -- "$output")"
if test ! -d "$source_dir/.git"; then
	git clone "$EWDB_URL" "$source_dir"
fi

git -C "$source_dir" fetch --depth 1 origin "$EWDB_REV"
git -C "$source_dir" checkout --detach "$EWDB_REV"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$EWDB_REV"

(
	cd "$source_dir"
	GOTOOLCHAIN=local \
	GOFLAGS=-mod=vendor \
	CGO_ENABLED=0 \
	GOOS=linux \
	GOARCH=arm \
	GOARM=7 \
	go build -trimpath -ldflags='-s -w' -o "$output" ./cmd/ewdbd
)

sha256sum "$output"
