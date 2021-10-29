#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Tests the "trackedgames" RPC command for game ZMQ notifications."""

from test_framework.util import (
  assert_equal,
  zmq_port,
)
from test_framework.xaya_zmq import (
  XayaZmqTest,
  ZmqSubscriber,
)


class TrackedGamesTest (XayaZmqTest):

  def set_test_params (self):
    self.num_nodes = 1

  def setup_nodes (self):
    self.address = "tcp://127.0.0.1:%d" % zmq_port (1)

    args = []
    args.append ("-zmqpubgameblocks=%s" % self.address)
    args.extend (["-trackgame=%s" % g for g in ["a", "b", "other"]])
    self.add_nodes (self.num_nodes, extra_args=[args])
    self.start_nodes ()

    self.node = self.nodes[0]

  def run_test (self):
    # Make the checks for BitcoinTestFramework subclasses happy.
    super ().run_test ()

  def run_test_with_zmq (self, ctx):
    games = {}
    for g in ["a", "b", "ignored"]:
      games[g] = ZmqSubscriber (ctx, self.address, g)
      games[g].subscribe ("game-block-attach")
      games[g].subscribe ("game-block-detach")

    # Test initial set as configured by the startup options.
    self.log.info ("Testing trackedgames...")
    assert_equal (set (self.node.trackedgames ()), set (["a", "b", "other"]))

    # Remove some tracked (and non-tracked) games.
    self.node.trackedgames ("remove", "b")
    self.node.trackedgames ("remove", "not-there")
    assert_equal (set (self.node.trackedgames ()), set (["a", "other"]))

    # Add a game that was previously not tracked.
    self.node.trackedgames ("add", "ignored")
    self.node.trackedgames ("add", "a")
    assert_equal (set (self.node.trackedgames ()),
                  set (["a", "ignored", "other"]))

    # Trigger an update to make sure the modified list is taken into account.
    self.generate (self.node, 1)
    topic, _ = games["a"].receive ()
    assert_equal (topic, "game-block-attach json a")
    topic, _ = games["ignored"].receive ()
    assert_equal (topic, "game-block-attach json ignored")

    # Restore original setting.
    self.node.trackedgames ("add", "b")
    self.node.trackedgames ("remove", "ignored")
    assert_equal (set (self.node.trackedgames ()), set (["a", "b", "other"]))

    # After all the real tests, verify no more notifications are there.
    # This especially verifies that the "ignored" game we are subscribed to
    # has no notifications (because it is not tracked by the daemon).
    self.log.info ("Verifying that there are no unexpected messages...")
    for _, sub in games.items ():
      sub.assertNoMessage ()

    # Restart the node without any active ZMQ notifications.  The tracked games
    # should still work fine.
    self.log.info ("Testing without game ZMQ notifications...")
    args = ["-trackgame=a", "-zmqpubhashblock=%s" % self.address]
    self.restart_node (0, extra_args=args)
    self.node.trackedgames ("add", "b")
    assert_equal (set (self.node.trackedgames ()), set (["a", "b"]))
    game = ZmqSubscriber (ctx, self.address, "a")
    game.subscribe ("game-block-attach")
    self.generate (self.node, 1)
    game.assertNoMessage ()


if __name__ == '__main__':
    TrackedGamesTest ().main ()
