#!/usr/bin/env python3
# Copyright (c) 2018-2019 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Framework for Xaya ZMQ tests."""

from .test_framework import (
  BitcoinTestFramework,
)
from .util import (
  assert_equal,
)

import codecs
import json
import struct


class ZmqSubscriber:
  """
  Helper class that implements subscription to one of the game ZMQ
  notifiers of Xaya Core.
  """

  def __init__ (self, ctx, addr, game):
    self.sequence = {}
    self.game = game
    self.prefixes = []

    import zmq
    self.socket = ctx.socket (zmq.SUB)
    self.socket.set (zmq.RCVTIMEO, 60000)
    self.socket.connect (addr)

  def subscribe (self, prefix):
    import zmq
    self.prefixes.append (prefix)
    self.socket.setsockopt_string (zmq.SUBSCRIBE,
                                   "%s json %s" % (prefix, self.game))

  def receive (self):
    topic, body, seq = self.socket.recv_multipart ()

    topic = codecs.decode (topic, "ascii")
    parts = topic.split (" ")
    assert_equal (len (parts), 3)
    assert_equal (parts[1], "json")
    assert_equal (parts[2], self.game)
    assert parts[0] in self.prefixes

    # Sequence should be incremental for the full topic string.
    seqNum = struct.unpack ("<I", seq)[-1]
    if not topic in self.sequence:
      self.sequence[topic] = 0
    assert_equal (seqNum, self.sequence[topic])
    self.sequence[topic] += 1

    return topic, json.loads (codecs.decode (body, "ascii"))

  def assertNoMessage (self):
    import zmq
    try:
      _ = self.socket.recv (zmq.NOBLOCK)
      raise AssertionError ("expected no more messages, but got one")
    except zmq.error.Again:
      pass


class XayaZmqTest (BitcoinTestFramework):

  def skip_test_if_missing_module (self):
    self.skip_if_no_py3_zmq ()
    self.skip_if_no_bitcoind_zmq ()

  def run_test (self):
    try:
      import zmq
      ctx = zmq.Context ()
      self.log.info ("Created ZMQ context");
      self.run_test_with_zmq (ctx)
    finally:
      self.log.debug ("Destroying ZMQ context")
      ctx.destroy (linger=None)
