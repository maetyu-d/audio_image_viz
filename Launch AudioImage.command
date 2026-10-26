#!/bin/zsh
set -eu
cd "${0:A:h}"
if [[ ! -x "AudioImage.app/Contents/MacOS/AudioImage" ]]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j 4
  ditto "build/AudioImage.app" "AudioImage.app"
fi
open "AudioImage.app"
