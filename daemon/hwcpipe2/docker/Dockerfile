ARG UBUNTU_IMAGE_VERSION=20.04

ARG VERSION_TOOL_NINJA=1.11.1
ARG VERSION_TOOL_CMAKE=3.24.2
ARG VERSION_TOOL_ANDROID_NDK=25.1.8937393
ARG VERSION_TOOL_VULKAN_SDK=1.3.224.1
ARG VERSION_TOOL_LLVM=15
ARG VERSION_TOOL_GCC=10
ARG VERSION_TOOL_GCC_ARM=8

# Name of an image to use to create hwcpipe2-env image
ARG BASE_ENV_IMAGE

# hwcpipe2-builder is a minimal image to build hwcpipe2-<toolname> images.
FROM ubuntu:${UBUNTU_IMAGE_VERSION} as hwcpipe2-tool

ENV DEBIAN_FRONTEND="noninteractive" TZ="Europe/London"

RUN apt-get update && apt-get install --no-install-recommends -y \
    ca-certificates \
    curl \
    openjdk-11-jre \
    python3-apt \
    python3-distutils \
    python3.8 \
    unzip \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

FROM hwcpipe2-tool AS hwcpipe2-tool-ninja
ARG VERSION_TOOL_NINJA
COPY get/ninja.sh ninja.sh
RUN ./ninja.sh ${VERSION_TOOL_NINJA} /home/hwcpipe2-tool/export

FROM hwcpipe2-tool as hwcpipe2-tool-cmake
ARG VERSION_TOOL_CMAKE
COPY get/cmake.sh cmake.sh
RUN ./cmake.sh ${VERSION_TOOL_CMAKE} /home/hwcpipe2-tool/export

FROM hwcpipe2-tool as hwcpipe2-tool-android-ndk
ARG VERSION_TOOL_ANDROID_NDK
COPY get/android-ndk.sh android-ndk.sh
RUN ./android-ndk.sh ${VERSION_TOOL_ANDROID_NDK} /home/hwcpipe2-tool/export

FROM hwcpipe2-tool as hwcpipe2-tool-vulkan-sdk
ARG VERSION_TOOL_VULKAN_SDK
COPY get/vulkan-sdk.sh vulkan-sdk.sh
RUN ./vulkan-sdk.sh ${VERSION_TOOL_VULKAN_SDK} /home/hwcpipe2-tool/export

# Core hwcpipe2 image
FROM ubuntu:${UBUNTU_IMAGE_VERSION} as hwcpipe2-dev

ARG VERSION_TOOL_LLVM
ARG VERSION_TOOL_GCC
ARG VERSION_TOOL_GCC_ARM

RUN apt-get update && apt-get install --no-install-recommends -y \
    software-properties-common \
    curl \
    gpg \
    && add-apt-repository ppa:ubuntu-toolchain-r/test \
    && curl -L "https://apt.llvm.org/llvm-snapshot.gpg.key" | gpg --dearmor > /etc/apt/trusted.gpg.d/llvm-snapshot.gpg \
    && echo "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-15 main" >> /etc/apt/sources.list \
    && apt-get update && apt-get install --no-install-recommends -y \
    clang-${VERSION_TOOL_LLVM} \
    clang-format-${VERSION_TOOL_LLVM} \
    clang-tidy-${VERSION_TOOL_LLVM} \
    clang-tools-${VERSION_TOOL_LLVM} \
    clang++-${VERSION_TOOL_LLVM} \
    doxygen \
    g++-${VERSION_TOOL_GCC_ARM}-aarch64-linux-gnu \
    g++-${VERSION_TOOL_GCC_ARM}-arm-linux-gnueabihf \
    g++-${VERSION_TOOL_GCC} \
    g++-${VERSION_TOOL_GCC}-multilib \
    gcc-${VERSION_TOOL_GCC_ARM}-aarch64-linux-gnu \
    gcc-${VERSION_TOOL_GCC_ARM}-arm-linux-gnueabihf \
    gcc-${VERSION_TOOL_GCC} \
    gcc-${VERSION_TOOL_GCC}-multilib \
    git \
    lld-${VERSION_TOOL_LLVM} \
    llvm-${VERSION_TOOL_LLVM} \
    python3-distutils \
    && rm -rf /var/lib/apt/lists/* \
    # Workaround to fix these type of errors for x86 builds:
    #
    #     error: asm/errno.h: No such file or directory
    #
    # Normally, such link is added by gcc-multilib package,
    # however we cannot install it since it conflicts with a similar
    # versioned package (gcc-${VERSION_TOOL_GCC}-multilib) which
    # does NOT create this link.
    && ln -s /usr/include/asm-generic /usr/include/asm

COPY --from=hwcpipe2-tool-ninja /home/hwcpipe2-tool/export/ /
COPY --from=hwcpipe2-tool-cmake /home/hwcpipe2-tool/export/ /
COPY --from=hwcpipe2-tool-android-ndk /home/hwcpipe2-tool/export/ /
COPY --from=hwcpipe2-tool-vulkan-sdk /home/hwcpipe2-tool/export/ /

ENV ANDROID_NDK_HOME /opt/ndk

# Set default compiler versions
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${VERSION_TOOL_LLVM} 10 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-${VERSION_TOOL_LLVM} \
    --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-${VERSION_TOOL_LLVM} \
    --slave /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-${VERSION_TOOL_LLVM} \
    --slave /usr/bin/run-clang-tidy run-clang-tidy /usr/bin/run-clang-tidy-${VERSION_TOOL_LLVM} \
    --slave /usr/bin/clang-apply-replacements clang-apply-replacements /usr/bin/clang-apply-replacements-${VERSION_TOOL_LLVM} \
    --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-${VERSION_TOOL_LLVM} \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${VERSION_TOOL_GCC} 10 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-${VERSION_TOOL_GCC} \
    && update-alternatives --install /usr/bin/arm-linux-gnueabihf-gcc arm-linux-gnueabihf-gcc /usr/bin/arm-linux-gnueabihf-gcc-${VERSION_TOOL_GCC_ARM} 10 \
    --slave /usr/bin/arm-linux-gnueabihf-g++ arm-linux-gnueabihf-g++ /usr/bin/arm-linux-gnueabihf-g++-${VERSION_TOOL_GCC_ARM} \
    && update-alternatives --install /usr/bin/aarch64-linux-gnu-gcc aarch64-linux-gnu-gcc /usr/bin/aarch64-linux-gnu-gcc-${VERSION_TOOL_GCC_ARM} 10 \
    --slave /usr/bin/aarch64-linux-gnu-g++ aarch64-linux-gnu-g++ /usr/bin/aarch64-linux-gnu-g++-${VERSION_TOOL_GCC_ARM} \
    && update-alternatives --set clang /usr/bin/clang-${VERSION_TOOL_LLVM} \
    && update-alternatives --set gcc /usr/bin/gcc-${VERSION_TOOL_GCC} \
    && update-alternatives --set arm-linux-gnueabihf-gcc /usr/bin/arm-linux-gnueabihf-gcc-${VERSION_TOOL_GCC_ARM} \
    && update-alternatives --set aarch64-linux-gnu-gcc /usr/bin/aarch64-linux-gnu-gcc-${VERSION_TOOL_GCC_ARM}

# Check tools integrity
RUN set -x \
    && aarch64-linux-gnu-g++ --version \
    && aarch64-linux-gnu-gcc --version \
    && arm-linux-gnueabihf-g++ --version \
    && arm-linux-gnueabihf-gcc --version \
    && clang --version \
    && clang-format --version \
    && clang-tidy --version \
    && clang++ --version \
    && cmake --version \
    && doxygen --version \
    && g++ --version \
    && gcc --version \
    && git --version \
    && glslc --version \
    && llvm-symbolizer --version

# pre-commit image used to install pre-commit hooks environments
FROM hwcpipe2-tool as hwcpipe2-pre-commit

COPY --chown=10001:10001 .pre-commit-config.yaml /tmp/.pre-commit-config.yaml

RUN cd /tmp \
    && curl -L -O https://bootstrap.pypa.io/get-pip.py \
    && python3.8 get-pip.py \
    && python3.8 -m pip install \
    pre-commit \
    pystache==0.6.0 \
    PyYAML==6.0 \
    types-PyYAML==6.0.7 \
    parameterized==0.8.1 \
    && rm -rf /tmp/* \
    && apt-get update && apt-get install --no-install-recommends -y \
    git \
    && rm -rf /var/lib/apt/lists/* \
    # Warm up pre-commit cache using our .pre-commit-config.yaml file
    && mkdir /tmp/repo \
    && cd /tmp/repo \
    && git init \
    && PRE_COMMIT_HOME=/home/.cache/pre-commit pre-commit install-hooks --config /tmp/.pre-commit-config.yaml \
    # pre-commit won't work when premissions are restricted for other users
    && find /home/.cache/pre-commit -maxdepth 1 -exec chmod o+rwx '{}' \; \
    && rm -rf /tmp/repo /tmp/.pre-commit-config.yaml

# Image that sets up proper user environment for a given base image
FROM ${BASE_ENV_IMAGE} as hwcpipe2-final

# Configure docker users and groups privileges
COPY ./uid_entrypoint /

# Allow sudo without password
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

# Allow users to execute uid_entrypoint and to update /etc/passwd and /etc/group
RUN chmod a+rx /uid_entrypoint && \
    chmod a+rw /etc/passwd /etc/group /home

# Avoid running as root (when -u hasn't been specified)
USER 10001
ENV USER=default \
    HOME=/home/${USER}

ENTRYPOINT ["/uid_entrypoint"]

# By default, start a bash shell
CMD ["/bin/bash"]
