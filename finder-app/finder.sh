#!/bin/bash

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Error: Two arguments required."
    echo "Usage: $0 <filesdir> <searchstr>"
    exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a valid directory on the filesystem."
    exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)

match_count=$(grep -r --include="*" -l "" "$filesdir" | xargs grep -s "$searchstr" 2>/dev/null | wc -l)

echo "The number of files are ${file_count} and the number of matching lines are ${match_count}"
