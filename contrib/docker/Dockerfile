# Builds a Docker image that has Xaya Core (and all its dependencies)
# installed.  It allows running xayad, xaya-cli and xaya-tx when executed.
#
# The Docker build with this file should be run from the source repository
# root file.  It will copy over the current folder as source, and thus build
# whatever version / tag is there.

# Start by setting up a base image with Debian packages that we need
# both for the build but also in the final image.
FROM debian:buster AS basepackages
RUN apt-get update && apt-get install -y \
  build-essential \
  libboost-chrono-dev \
  libboost-filesystem-dev \
  libboost-system-dev \
  libboost-thread-dev \
  libevent-dev \
  libzmq3-dev

# Create the image that we use to build everything, and install additional
# packages that are needed only for the build itself.
FROM basepackages AS build
RUN apt-get update && apt-get install -y \
  autoconf \
  automake \
  libtool \
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

# For the final image, just copy over all built / installed stuff.
FROM basepackages
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
