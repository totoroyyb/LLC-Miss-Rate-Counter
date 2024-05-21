#!/bin/bash

DIR_PATH=$(dirname $0)

pushd $DIR_PATH/../
make submodule
make all
popd

pushd $DIR_PATH/../ksched
make default
popd
