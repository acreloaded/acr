#!/bin/sh

# The Git commit should have these steps already done:
# * Set AC_VERSION in source/src/cube.h
# * Set PROTOCOL_VERSION in source/src/protocol.h
# * Regenerated config/docs.cfg and
#   source/locale/AC.pot

# Usage:
# ./package_acr.sh [_version]
#
# Examples:
# ./package_acr.sh
# ./package_acr.sh _v1337

# Before running this script, complete this checklist:
# * Do a fresh Git checkout (or download the .zip from GitHub and extract)
# * Verify that REPOPATH is correct and OUTPUTPATH exists
# * Add compiled binaries:
#   * bin_win32/acr_client.exe
#   * bin_win32/acr_server.exe
#   * bin_unix/linux_client
#   * bin_unix/linux_server
#   * bin_unix/linux_64_client
#   * bin_unix/linux_64_server
# * DO NOT use a repo contaminated with gameplay files.

REPOPATH=..
OUTPUTPATH=../../ACR_packaged
OUTPUTNAME=acr$1

# Arguments: package_name 
make_package() {
  mkdir -p $OUTPUTPATH/${OUTPUTNAME}-$1
  if [ "$1" = "serv" ]; then
    # Server binaries
    mkdir $OUTPUTPATH/${OUTPUTNAME}-$1/bin_win32
    mkdir $OUTPUTPATH/${OUTPUTNAME}-$1/bin_unix
    cp $REPOPATH/bin_win32/acr_server.exe $OUTPUTPATH/${OUTPUTNAME}-$1/bin_win32/acr_server.exe
    cp $REPOPATH/bin_win32/zlib1.dll $OUTPUTPATH/${OUTPUTNAME}-$1/bin_win32/zlib1.dll
    cp $REPOPATH/bin_unix/* $OUTPUTPATH/${OUTPUTNAME}-$1/bin_unix
    # Add official maps
    mkdir -p $OUTPUTPATH/${OUTPUTNAME}-$1/acr/packages/maps/official
    cp $REPOPATH/acr/packages/maps/official/*.cfg $OUTPUTPATH/${OUTPUTNAME}-$1/acr/packages/maps/official
    cp $REPOPATH/acr/packages/maps/official/*.cgz $OUTPUTPATH/${OUTPUTNAME}-$1/acr/packages/maps/official
    # Default server config
    mkdir $OUTPUTPATH/${OUTPUTNAME}-$1/config
    cp $REPOPATH/config/default.* $OUTPUTPATH/${OUTPUTNAME}-$1/config
    # Maps from packages
    mkdir $OUTPUTPATH/${OUTPUTNAME}-$1/packages
    cp -R $REPOPATH/packages/maps $OUTPUTPATH/${OUTPUTNAME}-$1/packages/maps
    # Server scripts
    cp $REPOPATH/server.bat $OUTPUTPATH/${OUTPUTNAME}-$1/server.bat
    cp $REPOPATH/server_wizard.bat $OUTPUTPATH/${OUTPUTNAME}-$1/server_wizard.bat
    cp $REPOPATH/server.sh $OUTPUTPATH/${OUTPUTNAME}-$1/server.sh
    cp $REPOPATH/server_wizard.sh $OUTPUTPATH/${OUTPUTNAME}-$1/server_wizard.sh
  elif [ "$1" != "src" ]; then
    # Both client and server binaries
    if [ "$1" = "w" ]; then
      cp -R $REPOPATH/bin_win32 $OUTPUTPATH/${OUTPUTNAME}-$1/bin_win32
      cp $REPOPATH/client.bat $OUTPUTPATH/${OUTPUTNAME}-$1/client.bat
      cp $REPOPATH/server.bat $OUTPUTPATH/${OUTPUTNAME}-$1/server.bat
      cp $REPOPATH/server_wizard.bat $OUTPUTPATH/${OUTPUTNAME}-$1/server_wizard.bat
    elif [ "$1" = "l" ]; then
      cp -R $REPOPATH/bin_unix $OUTPUTPATH/${OUTPUTNAME}-$1/bin_unix
      cp $REPOPATH/client.sh $OUTPUTPATH/${OUTPUTNAME}-$1/client.sh
      cp $REPOPATH/server.sh $OUTPUTPATH/${OUTPUTNAME}-$1/server.sh
      cp $REPOPATH/server_wizard.sh $OUTPUTPATH/${OUTPUTNAME}-$1/server_wizard.sh
    fi
    # Gameplay stuff
    cp -R $REPOPATH/acr $OUTPUTPATH/${OUTPUTNAME}-$1/acr
    cp -R $REPOPATH/bot $OUTPUTPATH/${OUTPUTNAME}-$1/bot
    cp -R $REPOPATH/config $OUTPUTPATH/${OUTPUTNAME}-$1/config
    cp -R $REPOPATH/docs $OUTPUTPATH/${OUTPUTNAME}-$1/docs
    cp -R $REPOPATH/mods $OUTPUTPATH/${OUTPUTNAME}-$1/mods
    cp -R $REPOPATH/packages $OUTPUTPATH/${OUTPUTNAME}-$1/packages
    cp -R $REPOPATH/scripts $OUTPUTPATH/${OUTPUTNAME}-$1/scripts
    find $OUTPUTPATH/${OUTPUTNAME}-$1/acr $OUTPUTPATH/${OUTPUTNAME}-$1/packages -type f \( -name \*.psd -o -name \*.ms3d \) -exec rm {} \;
  fi
  # Source folder (all except Windows)
  if [ "$1" = "src" ]; then
    cp -R $REPOPATH/source/* $OUTPUTPATH/${OUTPUTNAME}-$1
  elif [ "$1" != "w" ]; then
    cp -R $REPOPATH/source $OUTPUTPATH/${OUTPUTNAME}-$1/source
  fi
}

# Package for various environments
make_package w
make_package l
make_package src
make_package serv
