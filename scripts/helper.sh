# Ensures passed in version values are supported.
function check-version-numbers() {
  CHECK_VERSION_MAJOR=$1
  CHECK_VERSION_MINOR=$2

  if [[ $CHECK_VERSION_MAJOR -lt $ARISEN_MIN_VERSION_MAJOR ]]; then
    exit 1
  fi
  if [[ $CHECK_VERSION_MAJOR -gt $ARISEN_MAX_VERSION_MAJOR ]]; then
    exit 1
  fi
  if [[ $CHECK_VERSION_MAJOR -eq $ARISEN_MIN_VERSION_MAJOR ]]; then
    if [[ $CHECK_VERSION_MINOR -lt $ARISEN_MIN_VERSION_MINOR ]]; then
      exit 1
    fi
  fi
  if [[ $CHECK_VERSION_MAJOR -eq $ARISEN_MAX_VERSION_MAJOR ]]; then
    if [[ $CHECK_VERSION_MINOR -gt $ARISEN_MAX_VERSION_MINOR ]]; then
      exit 1
    fi
  fi
  exit 0
}


# Handles choosing which ARISEN directory to select when the default location is used.
function default-arisen-directories() {
  REGEX='^[0-9]+([.][0-9]+)?$'
  ALL_ARISEN_SUBDIRS=()
  if [[ -d ${HOME}/arisen ]]; then
    ALL_ARISEN_SUBDIRS=($(ls ${HOME}/arisen | sort -V))
  fi
  for ITEM in "${ALL_ARISEN_SUBDIRS[@]}"; do
    if [[ "$ITEM" =~ $REGEX ]]; then
      DIR_MAJOR=$(echo $ITEM | cut -f1 -d '.')
      DIR_MINOR=$(echo $ITEM | cut -f2 -d '.')
      if $(check-version-numbers $DIR_MAJOR $DIR_MINOR); then
        PROMPT_ARISEN_DIRS+=($ITEM)
      fi
    fi
  done
  for ITEM in "${PROMPT_ARISEN_DIRS[@]}"; do
    if [[ "$ITEM" =~ $REGEX ]]; then
      ARISEN_VERSION=$ITEM
    fi
  done
}


# Prompts or sets default behavior for choosing ARISEN directory.
function arisen-directory-prompt() {
  if [[ -z $ARISEN_DIR_PROMPT ]]; then
    default-arisen-directories;
    echo 'No ARISEN location was specified.'
    while true; do
      if [[ $NONINTERACTIVE != true ]]; then
        if [[ -z $ARISEN_VERSION ]]; then
          echo "No default ARISEN installations detected..."
          PROCEED=n
        else
          printf "Is ARISEN installed in the default location: $HOME/arisen/$ARISEN_VERSION (y/n)" && read -p " " PROCEED
        fi
      fi
      echo ""
      case $PROCEED in
        "" )
          echo "Is ARISEN installed in the default location?";;
        0 | true | [Yy]* )
          break;;
        1 | false | [Nn]* )
          if [[ $PROMPT_ARISEN_DIRS ]]; then
            echo "Found these compatible ARISEN versions in the default location."
            printf "$HOME/arisen/%s\n" "${PROMPT_ARISEN_DIRS[@]}"
          fi
          printf "Enter the installation location of ARISEN:" && read -e -p " " ARISEN_DIR_PROMPT;
          ARISEN_DIR_PROMPT="${ARISEN_DIR_PROMPT/#\~/$HOME}"
          break;;
        * )
          echo "Please type 'y' for yes or 'n' for no.";;
      esac
    done
  fi
  export ARISEN_INSTALL_DIR="${ARISEN_DIR_PROMPT:-${HOME}/arisen/${ARISEN_VERSION}}"
}


# Prompts or default behavior for choosing ARISEN.CDT directory.
function cdt-directory-prompt() {
  if [[ -z $CDT_DIR_PROMPT ]]; then
    echo 'No ARISEN.CDT location was specified.'
    while true; do
      if [[ $NONINTERACTIVE != true ]]; then
        printf "Is ARISEN.CDT installed in the default location? /usr/local/arisen.cdt (y/n)" && read -p " " PROCEED
      fi
      echo ""
      case $PROCEED in
        "" )
          echo "Is ARISEN.CDT installed in the default location?";;
        0 | true | [Yy]* )
          break;;
        1 | false | [Nn]* )
          printf "Enter the installation location of ARISEN.CDT:" && read -e -p " " CDT_DIR_PROMPT;
          CDT_DIR_PROMPT="${CDT_DIR_PROMPT/#\~/$HOME}"
          break;;
        * )
          echo "Please type 'y' for yes or 'n' for no.";;
      esac
    done
  fi
  export CDT_INSTALL_DIR="${CDT_DIR_PROMPT:-/usr/local/arisen.cdt}"
}


# Ensures ARISEN is installed and compatible via version listed in tests/CMakeLists.txt.
function aos-version-check() {
  INSTALLED_VERSION=$(echo $($ARISEN_INSTALL_DIR/bin/aos --version))
  INSTALLED_VERSION_MAJOR=$(echo $INSTALLED_VERSION | cut -f1 -d '.' | sed 's/v//g')
  INSTALLED_VERSION_MINOR=$(echo $INSTALLED_VERSION | cut -f2 -d '.' | sed 's/v//g')

  if [[ -z $INSTALLED_VERSION_MAJOR || -z $INSTALLED_VERSION_MINOR ]]; then
    echo "Could not determine ARISEN version. Exiting..."
    exit 1;
  fi

  if $(check-version-numbers $INSTALLED_VERSION_MAJOR $INSTALLED_VERSION_MINOR); then
    if [[ $INSTALLED_VERSION_MAJOR -gt $ARISEN_SOFT_MAX_MAJOR ]]; then
      echo "Detected ARISEN version is greater than recommended soft max: $ARISEN_SOFT_MAX_MAJOR.$ARISEN_SOFT_MAX_MINOR. Proceed with caution."
    fi
    if [[ $INSTALLED_VERSION_MAJOR -eq $ARISEN_SOFT_MAX_MAJOR && $INSTALLED_VERSION_MINOR -gt $ARISEN_SOFT_MAX_MINOR ]]; then
      echo "Detected ARISEN version is greater than recommended soft max: $ARISEN_SOFT_MAX_MAJOR.$ARISEN_SOFT_MAX_MINOR. Proceed with caution."
    fi
  else
    echo "Supported versions are: $ARISEN_MIN_VERSION_MAJOR.$ARISEN_MIN_VERSION_MINOR - $ARISEN_MAX_VERSION_MAJOR.$ARISEN_MAX_VERSION_MINOR"
    echo "Invalid ARISEN installation. Exiting..."
    exit 1;
  fi
}