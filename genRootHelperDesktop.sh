#!/bin/bash

set -e

cd CMAKE
cmake --build build -- -j4

cp -f ../cert/* ../bin/
