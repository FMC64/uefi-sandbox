#/bin/bash -e

DIR=$(realpath $(dirname $_))

PREVIOUS_DIR=$PWD

cd ~/edk2
export PACKAGES_PATH=~/edk2:~/edk2-platforms
source .venv/bin/activate

cd ~/edk2
source ./edksetup.sh

$DIR/overwrite_edk2_conf.sh

cd $PREVIOUS_DIR