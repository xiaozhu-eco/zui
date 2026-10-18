#!/usr/bin/env bash
# Linux/macOS counterpart of fetch_trace_processor.ps1. Same pinned release, same reasoning: a
# diagnostic tool downloaded into build/, never a build dependency.
#
# Usage: tools/trace/fetch_trace_processor.sh [-f]
set -euo pipefail

version='v58.2'
force=0
if [ "${1:-}" = "-f" ] || [ "${1:-}" = "--force" ]; then
  force=1
fi

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    sha256='58042408e6cc861fb1a731c26bb082dc222285561eaa4e12a48a8b2b90dca7b9'
    arch='linux-amd64'
    ;;
  Linux-aarch64 | Linux-arm64)
    sha256='0e6e0c5452c505c8d46fe472fd196a0d17d963460727e2ce2013b02aa1309555'
    arch='linux-arm64'
    ;;
  Darwin-arm64)
    sha256='d29864d1ba3b36855527bb1b0ca3aa7f703cdce338b9680bb922c5c151b358fa'
    arch='mac-arm64'
    ;;
  Darwin-x86_64)
    sha256='3927a2767eadd140db3ff4fe0dfbf1bde35c1f56501149cd367f5cee898bef27'
    arch='mac-amd64'
    ;;
  *)
    echo "no pinned trace_processor for $(uname -s)-$(uname -m)" >&2
    exit 1
    ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="$root/build/tools/trace_processor_shell"
# Both Google addresses serve the same artifact; some networks resolve one and not the other.
artifact="perfetto-luci-artifacts/$version/$arch/trace_processor_shell"
urls="https://storage.googleapis.com/$artifact
https://commondatastorage.googleapis.com/$artifact"

if [ -x "$target" ] && [ "$force" -eq 0 ]; then
  if [ "$(sha256sum "$target" | cut -d' ' -f1)" = "$sha256" ]; then
    echo "$target"
    exit 0
  fi
  echo "cached binary does not match the pin, refetching" >&2
fi

mkdir -p "$(dirname "$target")"
downloaded=0
while IFS= read -r url; do
  [ -n "$url" ] || continue
  echo "downloading Perfetto trace_processor $version ($arch) from $url" >&2
  if curl -fL --progress-bar --connect-timeout 20 --max-time 900 -o "$target.download" "$url"; then
    downloaded=1
    break
  fi
  echo "could not download from $url" >&2
done <<<"$urls"
if [ "$downloaded" -eq 0 ]; then
  echo "no source served $artifact" >&2
  exit 1
fi
actual="$(sha256sum "$target.download" | cut -d' ' -f1)"
if [ "$actual" != "$sha256" ]; then
  rm -f "$target.download"
  echo "checksum mismatch" >&2
  echo "  expected $sha256" >&2
  echo "  actual   $actual" >&2
  exit 1
fi
chmod +x "$target.download"
mv "$target.download" "$target"
echo "$target"
