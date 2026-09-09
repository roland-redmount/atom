#!/bin/sh
#
# Copy the built web front end into a site that serves it.
#
#   web/publish.sh ../www-roland/atom
#
# The site keeps a copy rather than building atom itself, so that building the
# site needs no C toolchain and no network. What is copied is recorded in a
# VERSION file, so that the site says which atom build it is carrying.
#
# Build the front end first:
#
#   emcmake cmake -B build/wasm -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/wasm
#
set -e

atomDirectory=$(cd "$(dirname "$0")/.." && pwd)
buildDirectory=${ATOM_WEB_BUILD:-$atomDirectory/build/wasm/web}
destination=$1

if [ -z "$destination" ]; then
	echo "usage: $0 <destination directory>" >&2
	exit 2
fi

# The demonstration page and the replay script are for trying the front end out,
# not for serving, so what is copied is named rather than taken wholesale.
files="atomweb.wasm atomweb.mjs atom-snippet.js atom-session.js atom-worker.js"

for file in $files; do
	if [ ! -f "$buildDirectory/$file" ]; then
		echo "$0: $buildDirectory/$file is missing; build the front end first" >&2
		exit 1
	fi
done

mkdir -p "$destination"
for file in $files; do
	cp "$buildDirectory/$file" "$destination/$file"
done

commit=$(cd "$atomDirectory" && git rev-parse --short HEAD 2>/dev/null || echo unknown)
if ! (cd "$atomDirectory" && git diff --quiet HEAD 2>/dev/null); then
	commit="$commit (with uncommitted changes)"
fi
cat > "$destination/VERSION" <<VERSION
atom $commit
copied $(date -u +%Y-%m-%dT%H:%MZ)
VERSION

echo "Copied the atom web front end to $destination"
for file in $files; do
	echo "  $file	$(du -h "$destination/$file" | cut -f1)"
done
