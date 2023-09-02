FROM ghcr.io/tianocore/containers/ubuntu-22-test

RUN apt update
RUN apt install build-essential uuid-dev libx11-dev libxext-dev gdb nano -y

RUN groupadd edk2 -f -o -g 1000
RUN useradd edk2 -u 1000 -g 1000 -G sudo -d /home/edk2 -M -s /bin/bash

RUN echo edk2:tianocore | chpasswd

COPY ./entrypoint.sh /usr/libexec/entrypoint
ENTRYPOINT ["/usr/libexec/entrypoint"]