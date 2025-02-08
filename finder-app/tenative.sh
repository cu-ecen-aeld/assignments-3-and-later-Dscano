#!/bin/bash
# Script outline to install and build kernel.

set -e
set -u

export PATH=$PATH:/home/diaccio/arm-cross-compiler/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin

#Default output directory
OUTDIR=/tmp/aesd-autograder
KERNEL_REPO="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git"
KERNEL_VERSION="v5.15.163"
BUSYBOX_VERSION="1_33_1"
FINDER_APP_DIR=$(realpath $(dirname $0))  # Absolute path to the script's directory
CONF_DIR=$(realpath ${FINDER_APP_DIR}/../conf)  # Points to the conf directory relative to FINDER_APP_DIR
ARCH="arm64"
CROSS_COMPILE="aarch64-none-linux-gnu-"

# Check if an output directory was provided, else use default
if [ $# -lt 1 ]
then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$(realpath $1)
    echo "Using passed directory ${OUTDIR} for output"
fi

# Create the output directory if it doesn¡¯t exist, fail if it cannot be created
mkdir -p ${OUTDIR} || { echo "Failed to create output directory ${OUTDIR}"; exit 1; }

# Kernel build steps
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    echo "Cloning Linux kernel ${KERNEL_VERSION} in ${OUTDIR}"
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION} ${OUTDIR}/linux-stable
fi

# Build the kernel if not already built
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd ${OUTDIR}/linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Kernel build steps
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc)
fi

# Copy the kernel image to the output directory
echo "Copying the Image to ${OUTDIR}"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/

# Prepare the root filesystem
echo "Creating root filesystem in ${OUTDIR}/rootfs"
if [ -d "${OUTDIR}/rootfs" ]; then
    echo "Deleting existing rootfs directory"
    sudo rm -rf ${OUTDIR}/rootfs
fi
mkdir -p ${OUTDIR}/rootfs/{bin,sbin,etc,proc,sys,usr/{bin,sbin},dev,home,lib,lib64}

# Clone, configure, and build BusyBox
cd "${OUTDIR}"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
else
    cd busybox
    make distclean
    make defconfig
fi

# Build and install BusyBox
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc)
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE CONFIG_PREFIX=${OUTDIR}/rootfs install

# Verify BusyBox dependencies
echo "Verifying BusyBox library dependencies"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# Copy necessary libraries to the root filesystem
# Ensure the dynamic linker is copied to /lib
if [ -d "$SYSROOT/lib" ]; then
    cp -a ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib/
else
    echo "Error: Cross-compiler sysroot does not contain the necessary libraries in lib"
    exit 1
fi

# Copy shared libraries to /lib64
if [ -d "$SYSROOT/lib64" ]; then
    cp -a ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64/
    cp -a ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64/
    cp -a ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64/
else
    echo "Error: Cross-compiler sysroot does not contain the necessary libraries in lib64"
    exit 1
fi

# Create device nodes in /dev
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1

# Cross-compile and copy writer utility from Assignment 2
cd ${FINDER_APP_DIR}
${CROSS_COMPILE}gcc -o writer writer.c
cp writer ${OUTDIR}/rootfs/home/

# Copy finder scripts and other necessary files from Assignment 2
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
cp ${CONF_DIR}/username.txt ${OUTDIR}/rootfs/home/
cp ${CONF_DIR}/assignment.txt ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/

# Copy the autorun-qemu.sh script into the root filesystem /home directory
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/

# Modify finder-test.sh to reference assignment.txt in /home directory
sed -i 's|\.\./conf/assignment.txt|assignment.txt|' ${OUTDIR}/rootfs/home/finder-test.sh

# Ensure start-qemu-app.sh is copied into the rootfs /home directory
cp ${FINDER_APP_DIR}/start-qemu-app.sh ${OUTDIR}/rootfs/home/

# Set ownership of root filesystem files to root
sudo chown -R root:root ${OUTDIR}/rootfs

# Create the initramfs.cpio.gz file
cd ${OUTDIR}/rootfs
find . | cpio -o -H newc | gzip > ${OUTDIR}/initramfs.cpio.gz

echo "Script completed successfully. Kernel image and initramfs.cpio.gz created in ${OUTDIR}."