#!/bin/bash

DIR_PATH=$(dirname $0)

pushd $DIR_PATH/../
make submodule-clean
make clean
popd

pushd $DIR_PATH/../ksched
make clean
popd
