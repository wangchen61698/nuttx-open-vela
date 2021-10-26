#!/usr/bin/env bash
# tools/build.sh
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

function cleanup()
{
  # keep the mapping but change to the link since:
  # 1.root can't access mount point created by normal user
  # 2.debugger could find the source code without manual setting
  fusermount -u ${MOUNTDIR}
  rmdir ${MOUNTDIR}
  ln -s ${ROOTDIR} ${MOUNTDIR}
}

function mount_unionfs()
{
  echo -e "Mount command line:"
  echo -e "  unionfs-fuse -o cow ${OUTDIR}=RW:${ROOTDIR}=RO ${MOUNTDIR}"

  rm -f ${MOUNTDIR}
  mkdir -p ${MOUNTDIR}
  unionfs-fuse -o cow ${OUTDIR}=RW:${ROOTDIR}=RO ${MOUNTDIR}
}

function build_board()
{
  echo -e "Build command line:"
  echo -e "  ${TOOLSDIR}/configure.sh -e $*"
  echo -e "  make -C ${NUTTXDIR} EXTRAFLAGS=[-Wno-cpp] ${@:2}"
  echo -e "  make -C ${NUTTXDIR} savedefconfig"

  if [ ! -f "${ROOTDIR}/prebuilts/kconfig-frontends/bin/kconfig-conf" ]; then
    pushd ${ROOTDIR}/prebuilts/kconfig-frontends
    ./configure --prefix=${ROOTDIR}/prebuilts/kconfig-frontends 1>/dev/null
    touch aclocal.m4 Makefile.in
    make install 1>/dev/null
    popd
  fi
  export PATH=${ROOTDIR}/prebuilts/kconfig-frontends/bin:$PATH

  if ! ${TOOLSDIR}/configure.sh -e $*; then
    echo "Error: ############# config ${1} fail ##############"
    exit 1
  fi

  ARCH=`sed -n 's/CONFIG_ARCH="\(.*\)"/\1/p' ${NUTTXDIR}/.config`
  TOOLCHAIN="gcc"

  EXTRAFLAGS=-Wno-cpp
  if [ "$ARCH" == "xtensa" ]; then
    export XTENSAD_LICENSE_FILE=28000@0.0.0.0
    EXTRAFLAGS=""
  fi

  if grep -nR "TOOLCHAIN.*CLANG.*y" ${NUTTXDIR}/.config; then
    TOOLCHAIN="clang"
  fi

  export PATH=${ROOTDIR}/prebuilts/$TOOLCHAIN/linux/$ARCH/bin:$PATH
  export WASI_SDK_ROOT=${ROOTDIR}/prebuilts/clang/linux/wasm

  if ! make -C ${NUTTXDIR} EXTRAFLAGS=$EXTRAFLAGS ${@:2}; then
    echo "Error: ############# build ${1} fail ##############"
    exit 2
  fi

  if [ "${2}" == "distclean" ]; then
    return;
  fi

   if ! make -C ${NUTTXDIR} savedefconfig; then
    echo "Error: ############# save ${1} fail ##############"
    exit 3
  fi
  if [ ! -d $1 ]; then
    cp ${NUTTXDIR}/defconfig ${ROOTDIR}/nuttx/boards/*/*/${1/[:|\/]//configs/}
  else
    cp ${NUTTXDIR}/defconfig $1
  fi
}


if [ $# == 0 ]; then
  echo "Usage: $0 [-m] <board-name>:<config-name> [make options]"
  echo ""
  echo "Where:"
  echo "  -m: out of tree build. Or default in tree build without it."
  exit 1
fi

ROOTDIR=$(dirname $(readlink -f ${0}))
ROOTDIR=$(realpath ${ROOTDIR}/../..)

if [ $1 == "-m" ]; then
  # out of tree build
  configdir=`echo ${2} | cut -s -d':' -f2`
  if [ -z "${configdir}" ]; then
    boarddir=`echo ${2} | rev | cut -d'/' -f3 | rev`
    configdir=`echo ${2} | rev | cut -d'/' -f1 | rev`
    if [ -z "${configdir}" ]; then
      boarddir=`echo ${2} | rev | cut -d'/' -f4 | rev`
      configdir=`echo ${2} | rev | cut -d'/' -f2 | rev`
    fi
  else
    boarddir=`echo ${2} | cut -d':' -f1`
  fi

  OUTDIR=${ROOTDIR}/out/${boarddir}/${configdir}
  MOUNTDIR=${OUTDIR}/.unionfs
  NUTTXDIR=${MOUNTDIR}/nuttx

  trap cleanup EXIT
  mount_unionfs
  shift
else
  # in tree build
  OUTDIR=${ROOTDIR}
  NUTTXDIR=${ROOTDIR}/nuttx
fi

TOOLSDIR=${NUTTXDIR}/tools

if [ -d ${ROOTDIR}/${1} ]; then
  build_board ${ROOTDIR}/${1} ${@:2}
else
  build_board $*
fi

