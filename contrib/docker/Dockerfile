# Builds a Docker image that has Xaya Core (and all its dependencies)
# installed.  It allows running xayad, xaya-cli and xaya-tx when executed.
#
# The Docker build with this file should be run from the source repository
# root file.  It will copy over the current folder as source, and thus build
# whatever version / tag is there.

# Create the image that we use to build everything, and install additional
# packages that are needed only for the build itself.
FROM alpine AS build
RUN apk add --no-cache \
  autoconf \
  automake \
  boost-dev \
  build-base \
  czmq-dev \
  libevent-dev \
  libtool

# Build and install Xaya Core itself.  Make sure to clean up any build artefacts
# that may have been copied over from the host machine.
WORKDIR /usr/src/xaya
COPY . .
RUN make distclean || true
RUN ./autogen.sh
RUN ./configure --disable-tests --disable-bench --disable-wallet --without-gui
RUN make && make install-strip

# For the final image, just copy over the build binaries and install
# the necessary runtime libraries (without build environment).
FROM alpine
RUN apk add --no-cache \
  boost-chrono \
  boost-filesystem \
  boost-system \
  boost-thread \
  libevent \
  libzmq
COPY --from=build /usr/local/bin/ /usr/local/bin/
LABEL description="Minimal image with Xaya Core and utilities"

# Set up the runtime environment.
RUN addgroup xaya && adduser -S -G xaya xaya \
  && mkdir -p /var/lib/xaya \
  && chown xaya:xaya -R /var/lib/xaya
ENV PATH "/usr/local/bin"
USER xaya
COPY contrib/docker/xaya.conf /var/lib/xaya/
COPY contrib/docker/entrypoint.sh /usr/local/bin/
VOLUME ["/var/lib/xaya"]
ENV HOST "127.0.0.1"
ENV ZMQ_PORT "28555"
ENV RPC_PASSWORD ""
ENV RPC_ALLOW_IP "127.0.0.1"
EXPOSE ${ZMQ_PORT}
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD []
