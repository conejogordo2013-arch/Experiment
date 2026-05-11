#!/usr/bin/env sh
set -eu

BIN="${BIN:-./build/jcl-sim}"

out="$($BIN --demo)"
printf '%s\n' "$out"

printf '%s' "$out" | grep 'SW=6982 (security status not satisfied)' >/dev/null
printf '%s' "$out" | grep 'SW=9000 (OK)' >/dev/null
printf '%s' "$out" | grep 'ASCII="READY"' >/dev/null
printf '%s' "$out" | grep 'ASCII="AUTH_OK"' >/dev/null
printf '%s' "$out" | grep 'ASCII="JNS_OK"' >/dev/null
printf '%s' "$out" | grep 'ASCII="ROM=512 EEPROM=1024 RAM=256 ATR=16"' >/dev/null
printf '%s' "$out" | grep 'ASCII="JCL' >/dev/null
printf '%s' "$out" | grep 'ASCII="TEST"' >/dev/null
printf '%s' "$out" | grep 'ASCII="001010123456789"' >/dev/null
printf '%s' "$out" | grep 'ASCII="Hola!"' >/dev/null

tamper="$($BIN 'JCI:STATE' 'JCI:NETTAP:JCCS' 'JCI:STATE' '80 E2 02 20 04 54 45 53 54')"
printf '%s\n' "$tamper"
printf '%s' "$tamper" | grep 'ASCII="READY"' >/dev/null
printf '%s' "$tamper" | grep 'ASCII="TAMPER_JCCS_BRICK"' >/dev/null
printf '%s' "$tamper" | grep 'ASCII="JC_BRICKED"' >/dev/null
printf '%s' "$tamper" | grep 'SW=6982 (security status not satisfied)' >/dev/null
