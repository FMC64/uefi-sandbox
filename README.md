# UEFI sandbox

Just a cute little repository with UEFI programs, where I experiment with stuff.

## Build

`docker` is required.

### One time

- In the host, run `setup/container/build.sh` to create a container named `edk2`
	- This container will have the current directory `.` mounted to `/home/edk2/mnt`
	- This container is ready to have the toolchain installed and will build the UEFI images
	- The first build run will leave you with a shell into `edk2`, to re-enter use `setup/container/run.sh`
- In the `edk2` container, run `setup/install_toolchain.sh` to install the build tools required

### To start each build session

- In the host, `setup/container/run.sh`
- In the `edk2` container, run:
	- `source setup/activate.sh` to have the proper build environment

### Building an app

- Run `setup/build_app.sh $APP_NAME`, with `$APP_NAME` simply the directory name of the application at the root of this repo
	- Follow on-screen instructions
- `$APP_NAME.efi` should appear at the root of this repository in the host

### Running an app

- Grab a USB drive, FAT32 formatted
- Run `setup/copy_bootable_app.sh $APP_NAME $DRIVE_ROOT`
	- `$APP_NAME` is the same value passed to `setup/build_app.sh` to build said application
	- `$DRIVE_ROOT` points to the root of the USB drive
- Now insert the USB drive onto the target, start the AMD64 computer, enter UEFI setup and boot from the USB drive to launch the application