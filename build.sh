#!/usr/bin/env bash
set -eo pipefail

function usage() {
   printf "Usage: $0 OPTION...
  -e DIR      Directory where ARISEN is installed. (Default: $HOME/arisen/X.Y)
  -c DIR      Directory where ARISEN.CDT is installed. (Default: /usr/local/arisen.cdt)
  -y          Noninteractive mode (Uses defaults for each prompt.)
  -h          Print this help menu.
   \\n" "$0" 1>&2
   exit 1
}

if [ $# -ne 0 ]; then
  while getopts "e:c:yh" opt; do
    case "${opt}" in
      e )
        ARISEN_DIR_PROMPT=$OPTARG
      ;;
      c )
        CDT_DIR_PROMPT=$OPTARG
      ;;
      y )
        NONINTERACTIVE=true
        PROCEED=true
      ;;
      h )
        usage
      ;;
      ? )
        echo "Invalid Option!" 1>&2
        usage
      ;;
      : )
        echo "Invalid Option: -${OPTARG} requires an argument." 1>&2
        usage
      ;;
      * )
        usage
      ;;
    esac
  done
fi

# Source helper functions and variables.
. ./scripts/.environment
. ./scripts/helper.sh

# Prompt user for location of arisen.
arisen-directory-prompt

# Prompt user for location of arisen.cdt.
cdt-directory-prompt

# Ensure arisen version is appropriate.
aos-version-check

printf "\t=========== Building arisen.contracts ===========\n\n"
RED='\033[0;31m'
NC='\033[0m'
CPU_CORES=$(getconf _NPROCESSORS_ONLN)
mkdir -p build
pushd build &> /dev/null
cmake ../
make -j $CPU_CORES
popd &> /dev/null
