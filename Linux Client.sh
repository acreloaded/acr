#!/bin/sh
# CUBE_DIR should refer to the directory in which Cube is placed.
#CUBE_DIR=~/cube
#CUBE_DIR=/usr/local/cube
CUBE_DIR=.
#CUBE_DIR=$(dirname $(readlink -f "${0}"))

# CUBE_OPTIONS contains any command line options you would like to start Cube with.
#CUBE_OPTIONS="-f"
CUBE_OPTIONS="--home=home --init"

exec ${CUBE_DIR}/bin_linux/native_client ${CUBE_OPTIONS}
