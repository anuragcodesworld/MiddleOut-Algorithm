#!/bin/bash

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$ROOT/benchmarks/data"
RESULTS="$ROOT/benchmarks/results"

mkdir -p "$RESULTS"

echo "======================================"
echo "       Middle-Out Compressor V6"
echo "          Benchmark Suite"
echo "======================================"
echo

printf "%-22s %-12s %-12s %-10s\n" \
    "Dataset" "Input" "Compressed" "Ratio"
echo "--------------------------------------------------------"

for file in "$DATA"/*; do

    name=$(basename "$file")
    output="$RESULTS/${name}.moc"

    echo
    echo "Testing: $name"

    "$ROOT/moc" moc-compress "$file" "$output"

    input_size=$(stat -c%s "$file")
    output_size=$(stat -c%s "$output")

    if [ "$output_size" -gt 0 ]; then
        ratio=$(awk "BEGIN {printf \"%.2fx\", $input_size / $output_size}")
    else
        ratio="N/A"
    fi

    printf "%-22s %-12s %-12s %-10s\n" \
        "$name" "$input_size" "$output_size" "$ratio"

    restored="$RESULTS/${name}.restored"

    "$ROOT/moc" moc-decompress "$output" "$restored" > /dev/null

    if cmp -s "$file" "$restored"; then
        echo "  ✓ Round-trip verified"
    else
        echo "  ✗ ROUND-TRIP FAILED"
        exit 1
    fi

    rm -f "$restored"
done

echo
echo "======================================"
echo "Benchmark completed successfully."
echo "All round-trips verified."
echo "======================================"
