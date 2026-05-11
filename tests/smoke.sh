#!/usr/bin/env sh
set -eu

BIN="${BIN:-./build/jcl-sim}"

out="$($BIN --demo)"
printf '%s\n' "$out"

printf '%s' "$out" | grep 'JuanCarlosLegals SIM Alpha emulator' >/dev/null && exit 1 || true
printf '%s' "$out" | grep 'SW=6982 (security status not satisfied)' >/dev/null
printf '%s' "$out" | grep 'SW=9000 (OK)' >/dev/null
printf '%s' "$out" | grep 'ASCII="001010123456789"' >/dev/null
printf '%s' "$out" | grep 'ASCII="Hola!"' >/dev/null
