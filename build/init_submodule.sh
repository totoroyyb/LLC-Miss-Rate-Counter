#!/bin/bash

set -e

DIR_PATH = "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORES = $(nproc)

# Initialize submodules
git submodule init
git submodule update --init -f --recursive

clean() {
  for mod in deps/pcm; do
    cd $DIR_PATH/$mod
    git checkout .
    git clean -df .
    rm -rf build/
    cd ..
  done
}

# echo building PCM
# pushd $DIR_PATH/deps/pcm
# rm -f src/pcm-caladan.cpp
# patch -p1 -N < ../../build/pcm.patch
# mkdir -p build
# pushd build
# cmake ..
# make PCM_STATIC -j $CORES
# popd
# popd