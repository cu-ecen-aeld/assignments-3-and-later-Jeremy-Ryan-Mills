#!/bin/bash

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Error: Two arguments required."
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile="$1"
writestr="$2"

mkdir -p "$(dirname "$writefile")"

if ! echo "$writestr" > "$writefile"; then
    echo "Error: Could not create or write to file '$writefile'."
    exit 1
fi
