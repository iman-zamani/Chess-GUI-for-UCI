#!/bin/sh
# Ad hoc signature for the macOS app bundle (CMakeLists.txt): nested executables
# (the reference engines) first, as nested code must be, then the bundle.
set -e
bundle="$1"
for f in "$bundle"/Contents/Resources/reference/*; do
    if [ -f "$f" ] && [ -x "$f" ]; then codesign --force --sign - "$f"; fi
done
codesign --force --sign - "$bundle"
