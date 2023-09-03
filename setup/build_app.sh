#!/bin/bash -e

DIR=$(realpath $(dirname $0))
ROOT=$(realpath $DIR/..)

if [[ -z $1 ]]; then
	echo "Expected the first argument to be the app name"
	exit 1
fi

mkdir -p ~/mirror
rm -rf ~/mirror/$1
cp -r $ROOT/$1 ~/mirror/$1


#echo "Now, make sure that ~/edk2/EmulatorPkg/EmulatorPkg.dsc contains '/home/edk2/mirror/$1/app.inf' in [Components] and run 'build'"
#echo "To test, run '~/edk2/EmulatorPkg/build.sh run'"

#echo "Now, make sure that ~/edk2-platforms/Platform/AMD/AmdMinBoardPkg/AmdMinBoardPkg.dsc contains '/home/edk2/mirror/$1/app.inf' in [Components] and run 'build -p Platform/AMD/AmdMinBoardPkg/AmdMinBoardPkg.dsc'"

build -p Platform/Intel/MinPlatformPkg/MinPlatformPkg.dsc -b RELEASE

OUTPUT=~/mirror/$1/app/OUTPUT/$1.efi

if [ ! -f $OUTPUT ]; then
	echo "Output not found, make sure that ~/edk2-platforms/Platform/Intel/MinPlatformPkg/MinPlatformPkg.dsc contains '/home/edk2/mirror/$1/app.inf' in [Components]"
	exit 1
fi

cp $OUTPUT $ROOT