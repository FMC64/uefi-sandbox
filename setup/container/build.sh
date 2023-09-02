#/bin/bash -e

DIR=$(realpath $(dirname $0))

docker build -t tianocore_ubuntu22 -f $DIR/Dockerfile $DIR
docker run -it --name edk2 -v ".":"/home/edk2/mnt" -e EDK2_DOCKER_USER_HOME="/home/edk2" tianocore_ubuntu22 /bin/bash