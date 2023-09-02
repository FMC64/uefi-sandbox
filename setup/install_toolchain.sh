#!/bin/bash -e

DIR=$(realpath $(dirname $0))

cd ~
git clone https://github.com/tianocore/edk2.git
cd edk2

git submodule update --init --recursive

python3 -m venv .venv
source .venv/bin/activate

pip install -r pip-requirements.txt --upgrade
stuart_setup -c .pytool/CISettings.py