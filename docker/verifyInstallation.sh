#!/bin/bash
set -e # exit on failure of any "simple" command (excludes &&, ||, or | chains)
# expected places to find ARISEN CMAKE in the docker container, in ascending order of preference
[[ -e /usr/lib/arisen/lib/cmake/arisen/arisen-config.cmake ]] && export CMAKE_FRAMEWORK_PATH="/usr/lib/arisen"
[[ -e /opt/arisen/lib/cmake/arisen/arisen-config.cmake ]] && export CMAKE_FRAMEWORK_PATH="/opt/arisen"
[[ ! -z "$ARISEN_ROOT" && -e $ARISEN_ROOT/lib/cmake/arisen/arisen-config.cmake ]] && export CMAKE_FRAMEWORK_PATH="$ARISEN_ROOT"
# fail if we didn't find it
[[ -z "$CMAKE_FRAMEWORK_PATH" ]] && exit 1
# export variables
echo "" >> /arisen.contracts/docker/environment.Dockerfile # necessary if there is no '\n' at end of file
echo "ENV CMAKE_FRAMEWORK_PATH=$CMAKE_FRAMEWORK_PATH" >> /arisen.contracts/docker/environment.Dockerfile
echo "ENV ARISEN_ROOT=$CMAKE_FRAMEWORK_PATH" >> /arisen.contracts/docker/environment.Dockerfile