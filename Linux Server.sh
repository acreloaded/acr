#!/bin/sh
# CUBE_DIR should refer to the directory in which Cube is placed.
#CUBE_DIR=~/cube
#CUBE_DIR=/usr/local/cube
CUBE_DIR=.

# CUBE_OPTIONS contains any command line options you would like to start Cube with.
CUBE_OPTIONS=

# comment this to disable reading command line options from config/servercmdline.txt
CUBE_OPTIONFILE=-Cconfig/servercmdline.txt

exec ${CUBE_DIR}/bin_linux/linux_server ${CUBE_OPTIONS} ${CUBE_OPTIONFILE}
