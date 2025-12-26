#!/usr/bin/env bash
set -euo pipefail

CLI=${CLI:-./build/bin/lithos_cli}
DB_DIR=${1:-/tmp/lithos_demo}

log() { echo "[$(date +%H:%M:%S)] $*"; }

if [[ ! -x "$CLI" ]]; then
  echo "CLI not found or not executable: $CLI" >&2
  exit 1
fi

rm -rf "$DB_DIR"
mkdir -p "$DB_DIR"

log "Functional check"
$CLI "$DB_DIR" put name Aditya
name_val=$($CLI "$DB_DIR" get name)
if [[ "$name_val" != "Aditya" ]]; then
  echo "Expected 'Aditya', got '$name_val'" >&2
  exit 1
fi
$CLI "$DB_DIR" del name
if [[ -n "$($CLI "$DB_DIR" scan)" ]]; then
  echo "Expected empty DB after delete" >&2
  exit 1
fi

log "Persistence check"
$CLI "$DB_DIR" put city Paris
city_val=$($CLI "$DB_DIR" get city)
if [[ "$city_val" != "Paris" ]]; then
  echo "Persistence get mismatch: '$city_val'" >&2
  exit 1
fi

log "Stress test: fill 100000 x 100B"
$CLI "$DB_DIR" fill 100000 100

log "Sample verification from scan"
mapfile -t SAMPLE_KEYS < <($CLI "$DB_DIR" scan | head -n 3 | cut -f1)
if [[ ${#SAMPLE_KEYS[@]} -eq 0 ]]; then
  echo "Scan did not return any keys" >&2
  exit 1
fi
for k in "${SAMPLE_KEYS[@]}"; do
  val=$($CLI "$DB_DIR" get "$k")
  if [[ -z "$val" ]]; then
    echo "Missing value for key: $k" >&2
    exit 1
  fi
done

log "Size check (<10MB expected)"
size_k=$(du -sk "$DB_DIR" | awk '{print $1}')
if (( size_k >= 10000 )); then
  echo "DB directory too large: ${size_k}K (expected < 10000K)" >&2
  exit 1
fi

log "Demo completed successfully"
