#!/bin/bash -e

DIR=$(realpath $(dirname $0))

cd ~
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init --recursive

python3 -m venv .venv
source .venv/bin/activate

pip install -r pip-requirements.txt --upgrade
python BaseTools/Edk2ToolsBuild.py -t GCC5

cd ~
git clone https://github.com/tianocore/edk2-platforms
cd edk2-platforms
git submodule update --init --recursive

cd ~/edk2

stuart_setup -c .pytool/CISettings.py
stuart_update -c .pytool/CISettings.py
$DIR/overwrite_edk2_conf.sh
stuart_ci_build -c .pytool/CISettings.py TOOL_CHAIN_TAG=GCC5 -a X64 -p EmulatorPkg