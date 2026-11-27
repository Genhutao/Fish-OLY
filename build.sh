#!/usr/bin/env bash
# OLY Persona — build script (Linux / macOS / Windows via cross-compile)
# Usage:
#   ./build.sh              # build for the current platform
#   ./build.sh linux        # linux/amd64
#   ./build.sh windows      # windows/amd64
#
# Toolchain is looked up on PATH; the project-local caches used by the
# original sandbox (../.gopath, ../.gomodcache, ../.gocache) are picked up
# automatically when they exist.
set -euo pipefail

cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"

export GOPATH="${GOPATH:-$ROOT/.gopath}"
export GOMODCACHE="${GOMODCACHE:-$ROOT/.gomodcache}"
export GOCACHE="${GOCACHE:-$ROOT/.gocache}"

TARGET="${1:-$(go env GOOS)}"

echo "==> Building frontend…"
(cd frontend && pnpm build)

echo "==> Building $TARGET/amd64…"
LDFLAGS="-w -s"
if [ "$TARGET" = "windows" ]; then
  LDFLAGS="$LDFLAGS -H windowsgui"
fi

GOOS="$TARGET" GOARCH=amd64 CGO_ENABLED=0 \
  go build -tags desktop,production -trimpath -ldflags "$LDFLAGS" \
  -o "bin/oly-persona$( [ "$TARGET" = "windows" ] && echo '.exe' )" .

echo "==> Done"
ls -lh bin/
