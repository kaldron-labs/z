#!/bin/sh
# Pinned, verified fixture dependency in a caller-selected directory.
set -eu
if test "$#" -ne 1; then
  echo "usage: build-quickfix.sh ABSOLUTE_BUILD_ROOT" >&2
  exit 2
fi
case "$1" in /*) ;; *) echo "build root must be absolute" >&2; exit 2 ;; esac
zfix_script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$zfix_script/quickfix.lock"
zfix_patch="$zfix_script/quickfix.patch"
printf '%s  %s\n' "$QUICKFIX_PATCH_SHA256" "$zfix_patch" | sha256sum -c -
zfix_dep=$1
mkdir -p "$zfix_dep"
zfix_archive="$zfix_dep/quickfix-$QUICKFIX_VERSION.tar.gz"
if ! test -f "$zfix_archive"; then
  curl -fL --retry 2 --max-time 120 \
    "https://codeload.github.com/quickfix/quickfix/tar.gz/$QUICKFIX_COMMIT" \
    -o "$zfix_archive.part"
  mv "$zfix_archive.part" "$zfix_archive"
fi
printf '%s  %s\n' "$QUICKFIX_ARCHIVE_SHA256" "$zfix_archive" | sha256sum -c -
zfix_source="$zfix_dep/quickfix-$QUICKFIX_COMMIT"
if ! test -d "$zfix_source"; then tar -xzf "$zfix_archive" -C "$zfix_dep"; fi
printf '%s  %s\n' "$QUICKFIX_FIX44_SHA256" "$zfix_source/spec/FIX44.xml" | sha256sum -c -
# Derive exact original/patched file states from the verified archive. Session::Reject
# drift and mixed application; a repeated run accepts only the complete result.
zfix_patch_work=$(mktemp -d "$zfix_dep/.quickfix-patch.XXXXXX")
trap 'rm -rf "$zfix_patch_work"' 0
sed -n 's|^--- a/||p' "$zfix_patch" > "$zfix_patch_work/files"
while IFS= read -r zfix_file; do
  tar -xzf "$zfix_archive" -C "$zfix_patch_work" \
    "quickfix-$QUICKFIX_COMMIT/$zfix_file"
done < "$zfix_patch_work/files"
zfix_expected="$zfix_patch_work/quickfix-$QUICKFIX_COMMIT"
patch --batch --fuzz=0 --backup --suffix=.orig -p1 -d "$zfix_expected" < "$zfix_patch"
zfix_original=yes
zfix_patched=yes
while IFS= read -r zfix_file; do
  cmp -s "$zfix_source/$zfix_file" "$zfix_expected/$zfix_file.orig" || zfix_original=no
  cmp -s "$zfix_source/$zfix_file" "$zfix_expected/$zfix_file" || zfix_patched=no
done < "$zfix_patch_work/files"
if test "$zfix_patched" = yes; then
  echo "QuickFIX fixture patch already applied; exact file contents verified"
elif test "$zfix_original" = yes; then
  patch --batch --fuzz=0 -p1 -d "$zfix_source" < "$zfix_patch"
else
  echo "QuickFIX source differs from the pinned original and fixture patch" >&2
  exit 1
fi
while IFS= read -r zfix_file; do
  cmp "$zfix_source/$zfix_file" "$zfix_expected/$zfix_file"
done < "$zfix_patch_work/files"
cmake -S "$zfix_source" -B "$zfix_dep/build" \
  -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=ON \
  -DQUICKFIX_EXAMPLES=OFF -DQUICKFIX_TESTS=OFF \
  -DQUICKFIX_LIB_OUTPUT_DIR="$zfix_dep/build/lib" \
  -DCMAKE_INSTALL_PREFIX="$zfix_dep/install"
cmake --build "$zfix_dep/build" -j8
cmake --install "$zfix_dep/build"
# Dictionaries stay in the verified source tree; fixtures name this exact file.
printf 'QuickFIX prefix: %s\nFIX44 dictionary: %s\n' \
  "$zfix_dep/install" "$zfix_source/spec/FIX44.xml"
