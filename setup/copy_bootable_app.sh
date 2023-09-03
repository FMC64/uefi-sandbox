#/bin/bash -e

DIR=$(realpath $(dirname $0))
ROOT=$(realpath $DIR/..)

if [[ -z $1 ]]; then
	echo "Expected the first argument to be the app name"
	exit 1
fi

if [[ -z $2 ]]; then
	echo "Expected the first argument to be the USB drive root directory"
	exit 1
fi

mkdir -p $2/EFI/boot
cp $ROOT/$1.efi $2/EFI/boot/bootx64.efi