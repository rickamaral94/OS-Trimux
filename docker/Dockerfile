# TriMux cross toolchain for the TrimUI Brick Pro (TG4040).
#
# Why Debian bullseye: the stock firmware (Tina Linux, v1.1.1) ships glibc 2.33
# and libstdc++ GLIBCXX_3.4.28 (GCC 10). Bullseye provides glibc 2.31 and GCC 10,
# so every binary built here only requires symbol versions the device already has.
# Libraries such as SDL2, EGL/GLES and ALSA are linked against bullseye's arm64
# packages (same sonames) and resolve to the firmware's own copies at runtime.
FROM debian:bullseye@sha256:6f519a81440354a85eb592c5f32109ab80605f6b892455983a6f618bf87fabe9

ENV DEBIAN_FRONTEND=noninteractive
# Pin the package set to a Debian snapshot so the toolchain is reproducible
# (bullseye left the regular security mirror; snapshot.debian.org keeps it).
# Same snapshot the pinned base image was built from.
ARG DEBIAN_SNAPSHOT=20260824T000000Z
RUN rm -f /etc/apt/sources.list.d/* \
 && printf '%s\n' \
    "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye main" \
    "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye-updates main" \
    "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT} bullseye-security main" \
    > /etc/apt/sources.list \
 && echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80retries \
 && dpkg --add-architecture arm64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl git make cmake ninja-build pkg-config file \
      xz-utils p7zip-full python3 python3-pytest dosfstools mtools fdisk \
      gcc g++ \
      gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
      libc6-dev:arm64 libasound2-dev:arm64 zlib1g-dev:arm64 \
      libegl-dev:arm64 libgles-dev:arm64 \
      fonts-dejavu-core qemu-user-static \
 && rm -rf /var/lib/apt/lists/*

ENV CROSS_COMPILE=aarch64-linux-gnu- \
    CC=aarch64-linux-gnu-gcc \
    CXX=aarch64-linux-gnu-g++ \
    AR=aarch64-linux-gnu-ar \
    STRIP=aarch64-linux-gnu-strip \
    PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig

# SDL2 is built from source at exactly the firmware's version (2.30.8, see
# /usr/trimui/lib/libSDL2-2.0.so.0.3000.8 in firmware v1.1.1). It is used only
# to compile and link; on the device the firmware's own libSDL2 is loaded.
ARG SDL2_VERSION=2.30.8
ARG SDL2_SHA256=380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058
# The archive is fetched on the host by scripts/fetch.sh (see Makefile).
COPY cmake-aarch64.cmake /opt/cmake-aarch64.cmake
COPY downloads/SDL2-${SDL2_VERSION}.tar.gz /tmp/sdl2.tar.gz
RUN echo "${SDL2_SHA256}  /tmp/sdl2.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/sdl2.tar.gz -C /tmp \
 && cmake -S /tmp/SDL2-${SDL2_VERSION} -B /tmp/sdl2-build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=/opt/cmake-aarch64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/aarch64 -DSDL_STATIC=OFF -DSDL_TEST=OFF \
      -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF \
      -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
      -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
      -DSDL_ALSA=ON -DSDL_OPENGLES=ON -DSDL_OPENGL=OFF \
 && cmake --build /tmp/sdl2-build && cmake --install /tmp/sdl2-build \
 && rm -rf /tmp/sdl2*

ENV PKG_CONFIG_PATH=/opt/aarch64/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig

WORKDIR /src
