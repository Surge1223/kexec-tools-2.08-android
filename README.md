TO MAKE KEXEC TOOLS



Download arm-2014.05 codesourcerty toolchain, unpack and add to path

if you dont know how to that google it, i found this on a quick search




Okay after thats done, look at how i exported the variables below and adapt for your system, open terminal in kexec-tools folder then type 

make clean
make distclean
autoreconf --install


export NDK=/home/surge/android-ndk
export TOOLCHAIN=/home/surge/build/bin/arm-none-linux-gnueabi-
export NDK_SYSROOT=/home/surge/build/arm-none-linux-gnueabi/libc
export ARCH=arm
export COMPILE=${TOOLCHAIN}
export PATH="/home/surge/build/bin/:${PATH}"
export SYSROOT=/home/surge/build/arm-none-linux-gnueabi/libc
export CROSS_COMPILE="arm-linux-androideabi-"
export CROSS="arm-none-linux-gnueabi-"
export STRIP="${CROSS}strip"
export CC="${CROSS}gcc"
export CXX="${CROSS}g++"
export LD="${CROSS}ld"
export RANLIB="${CROSS}ranlib"
export AR="${CROSS}ar"
export AS="${CROSS}as"
export NM="${CROSS}nm"
export READELF="${CROSS}readelf"
export CFLAGS=--sysroot=/home/surge/android-ndk/platforms/android-19/arch-arm/
export CPPFLAGS=-I/home/surge/build/arm-none-linux-gnueabi/libc
export PATH="/home/surge/build/bin:${PATH}"
export ARCH=arm
export "LDFLAGS = -Wl,-dynamic-linker,/system/bin/linker,-rpath-link=opt/android-ndk/platforms/android-L/arch-arm/usr/lib -Lopt/android-ndk/platforms/android-L/arch-arm/usr/lib -nostdlib -lc -lm -lstdc++"
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
export CROSS_COMPILE=/home/surge/build/bin/arm-none-linux-gnueabi-gcc
export LDFLAGS=-static


./configure --host=arm-none-linux-gnueabi CC=/home/surge/build/bin/arm-none-linux-gnueabi-gcc --prefix=/home/surge/build/arm-none-linux-gnueabi/libc  CFLAGS="-I/home/surge/build/arm-none-linux-gnueabi/libc" 

make


then files are in ../build/sbin

adb push to device
