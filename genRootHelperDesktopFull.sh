#!/bin/bash

set -e

cd CMAKE

mkdir -p ../bin
rm -rf build

cmake -H. -Bbuild
cmake --build build -- -j4

cp -f ../cert/* ../bin/
