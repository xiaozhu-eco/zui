#!/usr/bin/env bash
# Unix counterpart of run_app.ps1. Same defaults: build/trace/<app>.pftrace, categories app,engine.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
config="${ZUI_TRACE_CONFIG:-windows-x64-release}"
categories="${ZUI_TRACE_CATEGORIES-app,engine}"
perf=0
out=""
list=0
all_cats=0
app=""
args=()

known=(zui_studio zui_hello zui_reader zui_motion zui_text zui_checks zui_flow zui_style zui_table zui_dump)

usage() {
  cat <<EOF
Usage: tools/trace/run_app.sh <app> [args...]
       tools/trace/run_app.sh --list

  app     studio | hello | reader | ... | dump   (zui_ prefix optional)
  --config DIR          build/<DIR> (default windows-x64-release)
  --categories LIST     default app,engine; empty = all
  --all-categories
  --perf                set ZUI_PERF=1
  --out PATH            override build/trace/<app>.pftrace
  --list

Output: build/trace/<app>.pftrace when the process exits.
EOF
}

resolve() {
  local n="$1"
  n="${n%.exe}"
  local k
  for k in "${known[@]}"; do
    if [[ "$n" == "$k" ]]; then
      echo "$n"
      return
    fi
  done
  if [[ "$n" == zui_* ]]; then
    echo "$n"
    return
  fi
  echo "zui_$n"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --list|-l) list=1; shift ;;
    --config) config="$2"; shift 2 ;;
    --categories) categories="$2"; shift 2 ;;
    --all-categories) all_cats=1; shift ;;
    --perf) perf=1; shift ;;
    --out) out="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    --) shift; args+=("$@"); break ;;
    -*)
      if [[ -z "$app" ]]; then
        echo "unknown flag: $1" >&2
        usage
        exit 1
      fi
      args+=("$1"); shift
      ;;
    *)
      if [[ -z "$app" ]]; then
        app="$1"
      else
        args+=("$1")
      fi
      shift
      ;;
  esac
done

bin_dir="$root/build/$config"
trace_dir="$root/build/trace"

if [[ "$list" -eq 1 ]]; then
  echo "Traceable apps:"
  for k in "${known[@]}"; do
    if [[ -x "$bin_dir/$k" || -f "$bin_dir/$k.exe" ]]; then
      echo "  $k  [ok]"
    else
      echo "  $k  [missing]"
    fi
  done
  exit 0
fi

if [[ -z "$app" ]]; then
  usage
  exit 1
fi

resolved="$(resolve "$app")"
exe="$bin_dir/$resolved"
if [[ ! -x "$exe" && -f "$exe.exe" ]]; then
  exe="$exe.exe"
fi
if [[ ! -f "$exe" ]]; then
  echo "executable not found: $exe" >&2
  exit 1
fi

mkdir -p "$trace_dir"
if [[ -z "$out" ]]; then
  out="$trace_dir/$resolved.pftrace"
fi

export ZUI_TRACE="$out"
if [[ "$all_cats" -eq 1 ]]; then
  unset ZUI_TRACE_CATEGORIES || true
elif [[ -z "$categories" ]]; then
  unset ZUI_TRACE_CATEGORIES || true
else
  export ZUI_TRACE_CATEGORIES="$categories"
fi
if [[ "$perf" -eq 1 ]]; then
  export ZUI_PERF=1
fi

echo "exe:  $exe"
echo "out:  $out"
echo "Close the window (or let dump finish) to flush the trace."
(cd "$root" && exec "$exe" "${args[@]+"${args[@]}"}")
code=$?
if [[ ! -f "$out" ]]; then
  echo "warning: exit $code but $out was not written" >&2
  exit "$code"
fi
echo "wrote $out ($(wc -c <"$out") bytes), exit $code"
exit "$code"
