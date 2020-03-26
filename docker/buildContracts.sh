#!/bin/bash
set -e # exit on failure of any "simple" command (excludes &&, ||, or | chains)
cd /arisen.contracts
./build.sh -c /usr/opt/arisen.cdt -e /opt/arisen -y
cd build
tar -pczf /artifacts/contracts.tar.gz *
