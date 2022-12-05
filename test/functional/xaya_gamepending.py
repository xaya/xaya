#!/usr/bin/env python3
# Copyright (c) 2019-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Tests the "game-pending" ZMQ notifications."""

from test_framework.util import (
  assert_equal,
  assert_greater_than,
  zmq_port,
)
from test_framework.xaya_zmq import (
  XayaZmqTest,
  ZmqSubscriber,
)

import json


def assertMove (obj, txid, name, move):
  """
  Utility method to assert the value of a move JSON without verifying
  the "out" field (which is unpredictable due to the change output).
  """

  assert_equal (obj["txid"], txid)
  assert_equal (obj["name"], name)
  assert_equal (obj["move"], move)

  # These fields should be reported, but we do not care about the
  # exact form for this test (this is verified in xaya_gameblocks.py in
  # more detail).
  for key in ["inputs", "out", "btxid"]:
    assert key in obj


class GamePendingTest (XayaZmqTest):

  def set_test_params (self):
    self.num_nodes = 1

  def skip_test_if_missing_module (self):
    self.skip_if_no_wallet ()

  def add_options (self, parser):
    self.add_wallet_options (parser)

  def setup_nodes (self):
    self.address = "tcp://127.0.0.1:%d" % zmq_port (1)

    args = []
    args.append ("-zmqpubgamepending=%s" % self.address)
    args.extend (["-trackgame=%s" % g for g in ["a", "b", "other"]])
    self.add_nodes (self.num_nodes, extra_args=[args])
    self.start_nodes ()
    self.import_deterministic_coinbase_privkeys ()

    self.node = self.nodes[0]

  def run_test (self):
    # Make the checks for BitcoinTestFramework subclasses happy.
    super ().run_test ()

  def run_test_with_zmq (self, ctx):
    self.games = {}
    for g in ["a", "b", "ignored"]:
      self.games[g] = ZmqSubscriber (ctx, self.address, g)
      self.games[g].subscribe ("game-pending-move")

    self._test_currencyIgnored ()
    self._test_register ()
    self._test_notWhenMined ()
    self._test_update ()
    self._test_sendAndBurn ()
    self._test_multipleGames ()
    self._test_duplicateKeys ()
    self._test_blockDetach (ctx)

    # After all the real tests, verify no more notifications are there.
    # This especially verifies that the "ignored" game we are subscribed to
    # has no notifications (because it is not tracked by the daemon).
    self.log.info ("Verifying that there are no unexpected messages...")
    for _, sub in self.games.items ():
      sub.assertNoMessage ()

  def _test_currencyIgnored (self):
    self.log.info ("Testing pure currency transaction...")

    addr = self.node.getnewaddress ()
    self.node.sendtoaddress (addr, 1.5)

    for _, sub in self.games.items ():
      sub.assertNoMessage ()

    self.generate (self.node, 1)

  def _test_register (self):
    self.log.info ("Registering names...")

    txid = self.node.name_register ("p/x", json.dumps ({"g":{"a":42}}))
    self.node.name_register ("p/y", json.dumps ({"g":{"other":False}}))

    _, data = self.games["a"].receive ()
    assertMove (data, txid, "x", 42)

    self.generate (self.node, 1)

  def _test_notWhenMined (self):
    self.log.info ("Verifying no notification when transactions are mined...")

    txid = self.node.name_register ("p/z", json.dumps ({"g":{"b":100}}))

    _, data = self.games["b"].receive ()
    assertMove (data, txid, "z", 100)

    self.generate (self.node, 1)
    self.games["b"].assertNoMessage ()

  def _test_update (self):
    self.log.info ("Updating names...")

    txid = self.node.name_update ("p/x", json.dumps ({"g":{"b":"foo"}}))
    self.node.name_update ("p/y", json.dumps ({"g":{"ignored":42}}))

    _, data = self.games["b"].receive ()
    assertMove (data, txid, "x", "foo")

    self.generate (self.node, 1)

  def _test_sendAndBurn (self):
    self.log.info ("Sending and burning CHI...")

    addr = self.node.getnewaddress ()
    opt = {
      "sendCoins": {addr: 1},
      "burn": {"g/a": 2, "g/b": 3, "ignored": 0.5},
    }
    txid = self.node.name_update ("p/x", json.dumps ({"g": {"a": "foo"}}), opt)

    _, data = self.games["a"].receive ()
    assertMove (data, txid, "x", "foo")

    assert_equal (len (data["out"]), 2)
    assert_equal (data["out"][addr], 1)
    assert_equal (data["burnt"], 2)

    self.generate (self.node, 1)

  def _test_multipleGames (self):
    self.log.info ("Testing multiple games in one move...")

    txid = self.node.name_update ("p/y", json.dumps ({
      "g":
        {
          "a": 1,
          "b": 2,
          "ignored": 3,
        }
    }))

    _, data = self.games["a"].receive ()
    assertMove (data, txid, "y", 1)

    _, data = self.games["b"].receive ()
    assertMove (data, txid, "y", 2)

    self.generate (self.node, 1)

  def _test_duplicateKeys (self):
    self.log.info ("Testing duplicate JSON keys...")

    mv = """
      {
        "g":
          {
            "a": "a1"
          },
        "g":
          {
            "a": "a2",
            "b": "b1"
          },
        "g":
          {
            "b": "b2"
          }
      }
    """
    txid = self.node.name_update ("p/x", mv, {"valueEncoding": "utf8"})

    _, data = self.games["a"].receive ()
    assertMove (data, txid, "x", "a2")

    _, data = self.games["b"].receive ()
    assertMove (data, txid, "x", "b2")

    self.generate (self.node, 1)

  def _test_blockDetach (self, ctx):
    self.log.info ("Testing block detach...")

    # Enable also block notifications, so that we can test the relationship
    # between pending tx and the block detach (the detach should come first).
    # We use a new game ID here so that we do not mess up other tests.
    args = []
    args.append ("-zmqpubgameblocks=%s" % self.address)
    args.append ("-zmqpubgamepending=%s" % self.address)
    args.append ("-trackgame=detach")
    self.restart_node (0, extra_args=args)

    notifier = ZmqSubscriber (ctx, self.address, "detach")
    notifier.subscribe ("game-pending-move")
    notifier.subscribe ("game-block-detach")

    self.generate (self.node, 1)
    txid = self.node.name_update ("p/x", json.dumps ({
      "g": {"detach": "detached"}
    }))

    topic, data = notifier.receive ()
    assert_equal (topic, "game-pending-move json detach")
    assertMove (data, txid, "x", "detached")

    blk = self.generate (self.node, 1)[0]
    self.node.invalidateblock (blk)

    topic, data = notifier.receive ()
    assert_equal (topic, "game-block-detach json detach")
    assert_equal (data["block"]["hash"], blk)
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txid, "x", "detached")

    topic, data = notifier.receive ()
    assert_equal (topic, "game-pending-move json detach")
    assertMove (data, txid, "x", "detached")

    notifier.assertNoMessage ()


if __name__ == '__main__':
    GamePendingTest ().main ()
