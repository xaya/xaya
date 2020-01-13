# Builds a Docker image that has Xaya Core (and all its dependencies)
# installed.  It allows running xayad, xaya-cli and xaya-tx when executed.
#
# The Docker build with this file should be run from the source repository
# root file.  It will copy over the current folder as source, and thus build
# whatever version / tag is there.

# Create the image that we use to build everything, and install additional
# packages that are needed only for the build itself.
FROM debian:buster AS build
RUN apt-get update && apt-get install -y \
  autoconf \
  automake \
  build-essential \
  libboost-chrono1.67-dev \
  libboost-filesystem1.67-dev \
  libboost-system1.67-dev \
  libboost-thread1.67-dev \
  libevent-dev \
  libtool \
  libzmq3-dev \
  pkg-config

# Build and install Xaya Core itself.  Make sure to clean up any build artefacts
# that may have been copied over from the host machine.
WORKDIR /usr/src/xaya
COPY . .
RUN (make distclean || true) \
  && ./autogen.sh \
  && ./configure --disable-tests --disable-bench --disable-wallet \
                 --without-gui \
  && make && make install
ARG binaries="xayad xaya-cli xaya-tx"
RUN mkdir /usr/local/bin/stripped && \
    for b in $binaries; do \
      echo $b; \
      strip "/usr/local/bin/${b}" -o "/usr/local/bin/stripped/${b}"; \
    done

# For the final image, just copy over all built / installed stuff and only
# install the runtime libraries (without build environment).
FROM debian:buster
RUN apt-get update && apt-get install -y \
  libboost-chrono1.67.0 \
  libboost-filesystem1.67.0 \
  libboost-system1.67.0 \
  libboost-thread1.67.0 \
  libevent-2.1-6 \
  libevent-pthreads-2.1-6 \
  libzmq5
COPY --from=build /usr/local/bin/stripped /usr/local/bin/
LABEL description="Debian-based image with Xaya Core and utilities."

# Set up the runtime environment.
RUN  useradd xaya \
  && mkdir -p /var/lib/xaya \
  && chown xaya:xaya -R /var/lib/xaya
COPY contrib/docker/xaya.conf /var/lib/xaya/
COPY contrib/docker/entrypoint.sh /usr/local/bin/
USER xaya
VOLUME ["/var/lib/xaya"]
ENV HOST 127.0.0.1
ENV P2P_PORT=8394 RPC_PORT=8396 ZMQ_PORT=28555
ENV RPC_PASSWORD=
ENV RPC_ALLOW_IP 127.0.0.1
EXPOSE ${P2P_PORT}
EXPOSE ${RPC_PORT}
EXPOSE ${ZMQ_PORT}
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD []
