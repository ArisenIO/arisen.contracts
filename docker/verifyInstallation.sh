#!/bin/bash
set -e # exit on failure of any "simple" command (excludes &&, ||, or | chains)
# expected places to find ARISEN CMAKE in the docker container, in ascending order of preference
[[ -e /root/arisen/1.0/lib/cmake/arisen/arisen-config.cmake ]] && export CMAKE_FRAMEWORK_PATH="/root/arisen/1.0"
[[ -e /root/arisen/1.0/lib/cmake/arisen/arisen-config.cmake ]] && export CMAKE_FRAMEWORK_PATH="/root/arisen/1.0"
[[ ! -z "$ARISEN_ROOT" && -e $ARISEN_ROOT/lib/cmake/arisen/arisen-config.cmake ]] && export CMAKE_FRAMEWORK_PATH="$ARISEN_ROOT"
# fail if we didn't find it
[[ -z "$CMAKE_FRAMEWORK_PATH" ]] && exit 1
# export variables
echo "" >> /arisen.contracts/docker/environment.Dockerfile # necessary if there is no '\n' at end of file
echo "ENV CMAKE_FRAMEWORK_PATH=$CMAKE_FRAMEWORK_PATH" >> /arisen.contracts/docker/environment.Dockerfile
echo "ENV ARISEN_ROOT=$CMAKE_FRAMEWORK_PATH" >> /arisen.contracts/docker/environment.Dockerfile