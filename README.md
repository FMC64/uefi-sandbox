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