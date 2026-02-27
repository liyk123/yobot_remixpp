# Multi-stage Dockerfile using CMakePresets (Release) and vcpkg (target: yobot_remixpp)
# - Use CMake configure preset "Release" (preset sets binaryDir=${sourceDir}/build)
# - Build the target yobot_remixpp and collect build-time shared libraries from /src/build
# - Create non-root user 'appuser' in runtime image, chown runtime files to that user
# - Expose TCP port 9444

FROM python:3.13-trixie AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG CONFIGURE_PRESET=Release
ARG TARGET_NAME=yobot_remixpp

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl git zip unzip tar \
        pkg-config cmake ninja-build autoconf autoconf-archive automake \
        libtool libltdl-dev findutils wget &&\
    rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy project source
COPY . /src

RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git /vcpkg && \
    /vcpkg/bootstrap-vcpkg.sh

# Expose vcpkg environment for CMakePresets (the preset uses $env{VCPKG_ROOT})
ENV VCPKG_ROOT=/vcpkg
ENV VCPKG_BUILD_TYPE=release

# Configure using CMakePresets (Release preset sets binaryDir to ${sourceDir}/build)
RUN cmake --preset ${CONFIGURE_PRESET} -S /src || (echo "cmake configure preset failed" && false)

# Build the specific target
RUN cmake --build /src/build --target ${TARGET_NAME} -- -j$(nproc)

# Copy the produced executable to /dist
RUN mkdir -p /dist && \
    cp "$(find /src/build -type f -executable -name ${TARGET_NAME} -print -quit)" /dist/ || (echo "built executable not found" && false)


# ---------- Runtime ----------
FROM debian:trixie-slim AS runtime
ARG CMAKE_INSTALL_PREFIX=/usr/local
ARG TARGET_NAME=yobot_remixpp

RUN apt-get update && apt-get upgrade -y && apt-get autoremove -y && \
    apt-get install -y --no-install-recommends ca-certificates libstdc++6 adduser locales && \
    apt-get clean && \
    cp /usr/share/zoneinfo/Asia/Shanghai /etc/localtime && \
    echo 'Asia/Shanghai' > /etc/timezone && \
    sed -i 's/^# *zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' /etc/locale.gen && \
    locale-gen zh_CN.UTF-8

   

# Copy executable
COPY --from=builder /dist/${TARGET_NAME} ${CMAKE_INSTALL_PREFIX}/bin/${TARGET_NAME}

# Create non-root user and ensure ownership/permissions
RUN adduser --disabled-password --gecos "" appuser || true && \
    mkdir -p /home/appuser /app && \
    chown -R appuser:appuser /app ${CMAKE_INSTALL_PREFIX}/bin/${TARGET_NAME} && \
    chmod +x ${CMAKE_INSTALL_PREFIX}/bin/${TARGET_NAME}

ENV PATH=${CMAKE_INSTALL_PREFIX}/bin:$PATH
 
WORKDIR /app

# Expose the requested port
EXPOSE 9444 

# Run as non-root
USER appuser

# Default command
CMD ["yobot_remixpp"]
