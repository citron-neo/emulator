#! /bin/bash
set -e

# Make sure script is called from inside our container
test -e /tmp/torzu-src-ro || (echo "Script MUST NOT be called directly!" ; exit 1)

# Set up environment
export LANG=C.UTF-8
export LC_ALL=C.UTF-8
unset LC_ADDRESS LC_NAME LC_MONETARY LC_PAPER LC_TELEPHONE LC_MEASUREMENT LC_TIME

# Raise max open files count
ulimit -n 50000

# Install dependencies
apt -y install cmake ninja-build build-essential autoconf pkg-config locales wget git file mold libtool lsb-release wget software-properties-common gnupg \
               qtbase5-dev qtmultimedia5-dev qtbase5-private-dev glslang-tools libssl-dev libavcodec-dev libavfilter-dev libavutil-dev libswscale-dev libpulse-dev libasound2-dev
if [ ! "$BUILD_USE_CPM" = 1 ]; then
    apt -y install libfmt-dev libenet-dev liblz4-dev nlohmann-json3-dev zlib1g-dev libopus-dev libsimpleini-dev libstb-dev libzstd-dev libusb-1.0-0-dev libcubeb-dev libcpp-jwt-dev libvulkan-dev gamemode-dev libasound2-dev libglu1-mesa-dev libxext-dev mesa-common-dev libva-dev

    if [ ! -f /usr/local/lib/cmake/Boost-1.88.0/BoostConfigVersion.cmake ]; then
        # Install Boost
        wget https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.bz2
        echo "Extracting Boost sources..."
        tar xf boost_1_88_0.tar.bz2
        cd boost_1_88_0
        ./bootstrap.sh
        ./b2 install --with-{headers,context,system,fiber,atomic,filesystem} link=static
        cd ..
        rm -rf boost_1_88_0 boost_1_88_0.tar.bz2
    fi
fi

# Install Clang
if ([ "$BUILD_USE_CLANG" = 1 ] && ! clang-19 --version); then
    cd /tmp
    wget https://apt.llvm.org/llvm.sh
    chmod +x llvm.sh
    ./llvm.sh 19
    rm llvm.sh
fi

# Mount Torzu sources with temporary overlay
cd /tmp
mkdir torzu-src-upper torzu-src-work torzu-src
mount -t overlay overlay -olowerdir=torzu-src-ro,upperdir=torzu-src-upper,workdir=torzu-src-work torzu-src

# Get extra configuration/compilation options
EXTRA_COMPILE_FLAGS=""
EXTRA_CMAKE_FLAGS=""
if [ "$BUILD_USE_CLANG" = 1 ]; then
    EXTRA_CMAKE_FLAGS="-DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19"
    FATLTO_FLAG="-flto=full"
else
    FATLTO_FLAG="-flto"
fi
if [ "$BUILD_USE_THIN_LTO" = 1 ]; then
    EXTRA_COMPILE_FLAGS="-flto=thin"
fi
if [ "$BUILD_USE_FAT_LTO" = 1 ]; then
    EXTRA_COMPILE_FLAGS="$FATLTO_FLAG"
fi
if [ "$BUILD_USE_CPM" = 1 ]; then
    EXTRA_CMAKE_FLAGS="$EXTRA_CMAKE_FLAGS -DYUZU_USE_CPM=ON"
fi

# Build Torzu
cd /tmp
mkdir torzu-build
cd torzu-build
cmake /tmp/torzu-src -GNinja -DCMAKE_BUILD_TYPE=Release -DYUZU_TESTS=OFF -DENABLE_QT_TRANSLATION=OFF -DSPIRV_WERROR=OFF -DCMAKE_FIND_LIBRARY_SUFFIXES=".a;.so" -DSPIRV-Headers_SOURCE_DIR=/tmp/torzu-src/externals/SPIRV-Headers -DCMAKE_{C,CXX}_FLAGS="$EXTRA_COMPILE_FLAGS -fdata-sections -ffunction-sections" -DCMAKE_{EXE,SHARED}_LINKER_FLAGS="-Wl,--gc-sections" $EXTRA_CMAKE_FLAGS
ninja || (
    echo "Compilation has failed. Dropping you into a shell so you can inspect the situation. Run 'ninja' to retry and exit shell once compilation has finished successfully."
    echo "Note that any changes made here will not be reflected to the host environment, but changes made from the host environment will be reflected here."
    bash
)

# Generate AppImage
cp -rv /tmp/torzu-src/AppImageBuilder /tmp/AppImageBuilder
cd /tmp/AppImageBuilder
./build.sh /tmp/torzu-build /tmp/torzu.AppImage || echo "This error is known. Using workaround..."
cp /lib/$(uname -m)-linux-gnu/libICE.so.6 build/
mv build /tmp/hosttmp/torzu-debian-appimage-rootfs
