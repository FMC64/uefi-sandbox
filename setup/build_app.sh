#!/bin/bash -e

DIR=$(realpath $(dirname $0))
ROOT=$(realpath $DIR/..)

if [[ -z $1 ]]; then
	echo "Expected the first argument to be the app name"
	exit 1
fi

mkdir -p ~/mirror
cp -r $ROOT/$1 ~/mirror/$1


echo "Now, make sure that ~/edk2/EmulatorPkg/EmulatorPkg.dsc contains '/home/edk2/mirror/$1/app.inf' in [Components] and run 'build'"
echo "To test, run '~/edk2/EmulatorPkg/build.sh run'"