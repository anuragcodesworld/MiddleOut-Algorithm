#!/bin/bash

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$ROOT/benchmarks/data"
RESULTS="$ROOT/benchmarks/results"

mkdir -p "$RESULTS"

echo
echo "============================================================"
echo "          Middle-Out V6 vs Baseline Benchmark"
echo "============================================================"
echo

printf "%-20s %-12s %-12s %-12s %-12s\n" \
    "Dataset" "Input" "Baseline" "V6" "V6/BL Improvement"

echo "--------------------------------------------------------------------------"

for file in "$DATA"/*; do

    name=$(basename "$file")

    baseline="$RESULTS/${name}.blz"
    moc="$RESULTS/${name}.compare.moc"

    input_size=$(stat -c%s "$file")

    echo
    echo "Testing: $name"

    # ----------------------------------------------------------
    # Baseline
    # ----------------------------------------------------------

    "$ROOT/moc" baseline "$file" "$baseline" > /tmp/moc_baseline.log

    baseline_size=$(stat -c%s "$baseline")

    if [ "$baseline_size" -gt 0 ]; then
        baseline_ratio=$(awk \
            "BEGIN {printf \"%.2fx\", $input_size / $baseline_size}")
    else
        baseline_ratio="N/A"
    fi

    # ----------------------------------------------------------
    # MOC V6
    # ----------------------------------------------------------

    "$ROOT/moc" moc-compress "$file" "$moc" > /tmp/moc_v6.log

    moc_size=$(stat -c%s "$moc")

    if [ "$moc_size" -gt 0 ]; then
        moc_ratio=$(awk \
            "BEGIN {printf \"%.2fx\", $input_size / $moc_size}")
    else
        moc_ratio="N/A"
    fi

    # ----------------------------------------------------------
    # Calculate improvement
    # ----------------------------------------------------------

    improvement=$(awk \
        "BEGIN {
            if ($baseline_size > 0 && $moc_size > 0)
                printf \"%.2fx\", $baseline_size / $moc_size;
            else
                print \"N/A\";
        }")

    printf "%-20s %-12s %-12s %-12s %-12s\n" \
        "$name" \
        "$input_size" \
        "$baseline_ratio" \
        "$moc_ratio" \
        "$improvement"

    # ----------------------------------------------------------
    # Verify V6 decompression
    # ----------------------------------------------------------

    restored="$RESULTS/${name}.compare.restored"

    "$ROOT/moc" moc-decompress "$moc" "$restored" > /dev/null

    if cmp -s "$file" "$restored"; then
        echo "  ✓ V6 round-trip verified"
    else
        echo "  ✗ V6 ROUND-TRIP FAILED"
        exit 1
    fi

    rm -f "$restored"
done

echo
echo "============================================================"
echo "Benchmark completed."
echo "V6 round-trips verified successfully."
echo "============================================================"
echo
