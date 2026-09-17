#!/bin/sh
set -eu

version=$1
output=$2
temporary="${output}.tmp"
escaped=$(printf '%s' "$version" | sed 's/[\\"]/\\&/g')
printf '#ifndef ITELEX_VERSION_H\n#define ITELEX_VERSION_H\n#define SVNVERSION "%s"\n#endif\n' "$escaped" > "$temporary"
mv "$temporary" "$output"
