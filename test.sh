#!/bin/bash

if [ $# -lt 1 ]; then
    echo "Usage: $0 <cpp_file>"
    exit 1
fi

CPP_FILE="$1"
EXECUTABLE="a.out"
N=10

g++ -fopenmp -o "$EXECUTABLE" "$CPP_FILE"
if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

if [ ! -f "$EXECUTABLE" ]; then
    echo "Executable not found."
    exit 1
fi

total_time=0

for (( i=1; i<=N; i++ )); do
    echo "Round $i"
    output=$(./"$EXECUTABLE")
    echo "$output"
done
