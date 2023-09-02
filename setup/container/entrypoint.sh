#!/bin/bash -e

cd /home/edk2
chown edk2:edk2 .
cd /home/edk2/mnt

exec runuser -u "edk2" -- "$@"