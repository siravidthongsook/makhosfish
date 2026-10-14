#!/usr/bin/env bash
# Build `makhosfish` if stale, then serve the local web board.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
bin="$root/build/makhosfish"

# macOS: the build needs the Xcode SDK sysroot or linking fails on this machine.
if [[ "$(uname)" == "Darwin" && -z "${CMAKE_OSX_SYSROOT:-}" ]]; then
  sdk="$(xcrun --show-sdk-path 2>/dev/null || true)"
  [[ -n "$sdk" ]] && export CMAKE_OSX_SYSROOT="$sdk"
fi

need_build=0
[[ -x "$bin" ]] || need_build=1
if [[ -n "$(find "$root/src" -newer "$bin" -print -quit 2>/dev/null)" ]]; then
  need_build=1
fi

if [[ "$need_build" == 1 ]]; then
  echo "building makhosfish…"
  cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release \
    ${CMAKE_OSX_SYSROOT:+-DCMAKE_OSX_SYSROOT="$CMAKE_OSX_SYSROOT"} >/dev/null
  cmake --build "$root/build" -j
fi

cd "$here/server"
if [[ ! -d node_modules ]]; then
  echo "installing bridge dependencies…"
  npm install --no-audit --no-fund
fi

exec node index.js
