#!/bin/bash
set -euo pipefail

PARALLEL_JOBS=${PARALLEL_JOBS:-4}
CHUNK_SIZE=${CHUNK_SIZE:-1000}

echo "Fetching all cache IDs..."
CACHE_IDS=$(gh cache list --limit 1000 --json id -q '.[].id')

if [[ -z "$CACHE_IDS" ]]; then
  echo "No caches found."
  exit 0
fi

# Convert to array
mapfile -t ID_ARRAY <<< "$CACHE_IDS"
TOTAL="${#ID_ARRAY[@]}"
echo "Found  Deleting in chunks of $CHUNK_SIZE with $PARALLEL_JOBS parallel jobs..."

# Split into chunks and delete in parallel
delete_chunk() {
  local chunk=("$@")
  for id in "${chunk[@]}"; do
    echo "Deleting cache $id..."
    gh cache delete "$id" && echo "✓ Deleted $id" || echo "✗ Failed $id"
  done
}

export -f delete_chunk

# Build chunks and run in parallel
CHUNKS=()
for ((i = 0; i < TOTAL; i += CHUNK_SIZE)); do
  CHUNK=("${ID_ARRAY[@]:i:CHUNK_SIZE}")
  CHUNKS+=("$(IFS=' '; echo "${CHUNK[*]}")")
done

echo "Split into ${#CHUNKS[@]} chunks."

# Run chunks in parallel using xargs
printf '%s\n' "${CHUNKS[@]}" | xargs -P "$PARALLEL_JOBS" -I{} bash -c '
  delete_chunk() {
    for id in "$@"; do
      echo "Deleting cache $id..."
      gh cache delete "$id" && echo "✓ Deleted $id" || echo "✗ Failed $id"
    done
  }
  delete_chunk {}'

echo "Done."
