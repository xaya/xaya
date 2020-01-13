#!/bin/sh -e

case $1 in
  xaya-tx)
    exec "$@"
    ;;

  xaya-cli)
    shift
    exec xaya-cli \
      --datadir="/var/lib/xaya" \
      --rpcconnect="${HOST}" \
      --rpcpassword="${RPC_PASSWORD}" \
      --rpcport="${RPC_PORT}" \
      "$@"
    ;;

  xayad)
    bin=$1
    shift
    ;;

  *)
    bin=xayad
    ;;
esac

if [ -z "${RPC_PASSWORD}" ]
then
  echo "RPC_PASSWORD must be set"
  exit 1
fi

exec $bin \
  --datadir="/var/lib/xaya" \
  --rpcpassword="${RPC_PASSWORD}" \
  --rpcbind="${HOST}" \
  --rpcallowip="${RPC_ALLOW_IP}" \
  --rpcport="${RPC_PORT}" \
  --port="${P2P_PORT}" \
  --zmqpubgameblocks="tcp://${HOST}:${ZMQ_PORT}" \
  --zmqpubgamepending="tcp://${HOST}:${ZMQ_PORT}" \
  "$@"
