#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
TAREA_DIR=$(realpath ${FINDER_APP_DIR}/..)
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    # 1) "Deep clean" the kernel build tree - removing the .config file
    # with any existing configurations
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    # 2) Configure for our "virt" arm dev board we will simulate in 
    # QEMU
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    # 3) Build a kernel image for booting with QEMU
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # 4) Build any kernel modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    # 5) Build the devicetree
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
    # ENDTODO
fi

echo "Adding the Image in outdir"
# Añade esta línea:
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
ROOTFSDIR="${OUTDIR}/rootfs" 
mkdir ${ROOTFSDIR}
cd ${ROOTFSDIR}
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log 
# END TODO

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# TODO: Make and install busybox
make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} 
make CONFIG_PREFIX=${ROOTFSDIR} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"

# TODO: Add library dependencies to rootfs
${CROSS_COMPILE}readelf -a ${ROOTFSDIR}/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${ROOTFSDIR}/bin/busybox | grep "Shared library"
# Obtener la ruta del sysroot de la toolchain
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

# Copiar el cargador dinámico (Interprete)
# Usualmente está en /lib o /lib64 del sysroot
cp -a ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${ROOTFSDIR}/lib/

# Copiar las librerías compartidas (Shared Libraries)
cp -a ${SYSROOT}/lib64/libm.so.6 ${ROOTFSDIR}/lib64/
cp -a ${SYSROOT}/lib64/libc.so.6 ${ROOTFSDIR}/lib64/
cp -a ${SYSROOT}/lib64/libresolv.so.2 ${ROOTFSDIR}/lib64/

# TODO: Make device nodes
sudo mknod -m 666 ${ROOTFSDIR}/dev/null c 1 3
sudo mknod -m 666 ${ROOTFSDIR}/dev/console c 5 1

# TODO: Clean and build the writer utility

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copying finder-app scripts and binaries to rootfs/home..."

# 1. Copiar los scripts principales
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# mkdir ${ROOTFSDIR}/home/finder-app
# cp ${FINDER_APP_DIR}/finder.sh ${ROOTFSDIR}/home/finder-app/
# cp ${FINDER_APP_DIR}/finder-test.sh ${ROOTFSDIR}/home/finder-app/
# cp ${FINDER_APP_DIR}/autorun-qemu.sh ${ROOTFSDIR}/home/finder-app/

# 2. Copiar el binario 'writer' (asegúrate de haberlo compilado antes para ARM64)
# cp ${FINDER_APP_DIR}/writer ${ROOTFSDIR}/home/finder-app/

# 3. Copiar la carpeta de configuración 'conf' necesaria para los tests
# Usamos -r para copiar el directorio completo´
#mkdir ${ROOTFSDIR}/home/finder-app/conf
#cp -r ${FINDER_APP_DIR}/conf/ ${ROOTFSDIR}/home/finder-app/
#cp -r ${FINDER_APP_DIR}/conf/ ${ROOTFSDIR}/home/
cp -ar ${TAREA_DIR}/. ${ROOTFSDIR}/home/

# TODO: Chown the root directory
cd ${ROOTFSDIR}
sudo chown -R root:root ${ROOTFSDIR}/home
sudo chmod -R +x ${ROOTFSDIR}/home
sudo chmod -R +x ${ROOTFSDIR}/home/finder-app/*
ls -l ${ROOTFSDIR}/home/

# TODO: Create initramfs.cpio.gz
echo "Generating initramfs.cpio.gz..."

cd "${ROOTFSDIR}"

# Usando el pipe para mayor seguridad
find . | cpio -H newc -ov --owner root:root | gzip -f > "${OUTDIR}/initramfs.cpio.gz"

if [ -f "${OUTDIR}/initramfs.cpio.gz" ]; then
    echo "Done! Your initramfs is ready at ${OUTDIR}/initramfs.cpio.gz"
else
    echo "Error: Failed to generate initramfs.cpio.gz"
    exit 1
fi