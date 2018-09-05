#!/usr/bin/env python3
# Copyright (c) 2018 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test the "game-block" ZMQ notifications."""

from test_framework.test_framework import (
  BitcoinTestFramework,
  skip_if_no_bitcoind_zmq,
  skip_if_no_py3_zmq,
)
from test_framework.messages import (
  COIN,
  COutPoint,
  CTransaction,
  CTxIn,
  CTxOut,
)
from test_framework.util import (
  assert_equal,
  assert_raises_rpc_error,
  bytes_to_hex_str,
  hex_str_to_bytes,
)
from test_framework.script import (
  CScript,
  OP_TRUE,
)

from decimal import Decimal
import codecs
import json
import struct


def assertMove (obj, txid, name, move):
  """
  Utility method to assert the value of a move JSON without verifying
  the "out" field (which is unpredictable due to the change output).
  """

  assert_equal (obj["txid"], txid)
  assert_equal (obj["name"], name)
  assert_equal (obj["move"], move)


class GameSubscriber:

  def __init__ (self, ctx, addr, game):
    self.sequence = {}
    self.game = game

    import zmq
    self.socket = ctx.socket (zmq.SUB)
    self.socket.set (zmq.RCVTIMEO, 60000)
    self.socket.connect (addr)
    self.socket.setsockopt_string (zmq.SUBSCRIBE,
                                   "game-block-attach json %s" % game)
    self.socket.setsockopt_string (zmq.SUBSCRIBE,
                                   "game-block-detach json %s" % game)

  def receive (self):
    topic, body, seq = self.socket.recv_multipart ()

    topic = codecs.decode (topic, "ascii")
    parts = topic.split (" ")
    assert_equal (len (parts), 3)
    assert_equal (parts[1], "json")
    assert_equal (parts[2], self.game)
    assert parts[0] in ["game-block-attach", "game-block-detach"]

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


class GameBlocksTest (BitcoinTestFramework):

  _address = "tcp://127.0.0.1:28332"

  def set_test_params (self):
    self.num_nodes = 1

  def setup_nodes (self):
    skip_if_no_py3_zmq ()
    skip_if_no_bitcoind_zmq (self)

    import zmq
    self.zmq_context = zmq.Context ()
    self.games = {}
    for g in ["a", "b", "ignored"]:
      self.games[g] = GameSubscriber (self.zmq_context, self._address, g)

    args = []
    args.append ("-zmqpubgameblocks=%s" % self._address)
    args.extend (["-trackgame=%s" % g for g in ["a", "b", "other"]])
    self.extra_args = [args]
    self.add_nodes (self.num_nodes, self.extra_args)
    self.start_nodes ()

    self.node = self.nodes[0]

  def run_test (self):
    try:
      self._test_currencyIgnored ()
      self._test_register ()
      self._test_blockData ()
      self._test_multipleUpdates ()
      self._test_moveWithCurrency ()
      self._test_reorg ()
      self._test_sendUpdates ()
      self._test_trackedgamesRPC ()

      # After all the real tests, verify no more notifications are there.
      # This especially verifies that the "ignored" game we are subscribed to
      # has no notifications (because it is not tracked by the daemon).
      self.log.info ("Verifying that there are no unexpected messages...")
      for _, sub in self.games.items ():
        sub.assertNoMessage ()
    finally:
      self.log.debug ("Destroying ZMQ context")
      self.zmq_context.destroy (linger=None)

  def _test_currencyIgnored (self):
    """
    Tests that a currency transaction does not show up as move in the
    notifications.
    """

    self.log.info ("Testing pure currency transaction...")

    addr = self.node.getnewaddress ()
    self.node.sendtoaddress (addr, 1.5)
    self.node.generate (1)

    for g in ["a", "b"]:
      _, data = self.games[g].receive ()
      assert_equal (data["moves"], [])

  def _test_register (self):
    """
    Registers test names and verifies that this already triggers
    a notification (not just name_update's).
    """

    self.log.info ("Registering names...")

    txid = self.node.name_register ("p/x", json.dumps ({"g":{"a":42}}))
    self.node.name_register ("p/y", json.dumps ({"g":{"other":False}}))
    self.node.generate (1)

    _, data = self.games["a"].receive ()
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txid, "x", 42)

    _, data = self.games["b"].receive ()
    assert_equal (data["moves"], [])

  def _test_blockData (self):
    """
    Verifies the block-related data (parent/child hashes, rngseed) in the main
    message that is sent against the blockchain.
    """

    self.log.info ("Verifying block data...")

    parent = self.node.getbestblockhash ()
    blkHash = self.node.generate (1)[0]
    data = self.node.getblock (blkHash)
    expected = {
      "block":
        {
          "hash": blkHash,
          "parent": parent,
          "height": self.node.getblockcount (),
          "timestamp": data['time'],
          "rngseed": data['rngseed'],
        },
      "moves": []
    }

    for g in ["a", "b"]:
      _, data = self.games[g].receive ()
      assert_equal (data, expected)

  def _test_multipleUpdates (self):
    """
    Tests the case of multiple name updates in the block and multiple
    games referenced for a single name.
    """

    self.log.info ("Testing multiple updates in one block...")

    txidX = self.node.name_update ("p/x", json.dumps ({
      "stuff": "foo",
      "g":
        {
          "a": [42, False],
          "b": {"test": True},
        },
    }))
    txidY = self.node.name_update ("p/y", json.dumps ({"g":{"b":6.25}}))
    blk = self.node.generate (1)[0]

    # Get the order of our two transactions in the block, so that we can check
    # that the order in the notification matches it.
    txids = self.node.getblock (blk)["tx"][1:]
    assert_equal (set (txids), set ([txidX, txidY]))
    indX = txids.index (txidX)
    indY = txids.index (txidY)

    _, data = self.games["a"].receive ()
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txidX, "x", [42, False])

    _, data = self.games["b"].receive ()
    assert_equal (len (data["moves"]), 2)
    assertMove (data["moves"][indX], txidX, "x", {"test": True})
    assertMove (data["moves"][indY], txidY, "y", 6.25)

  def _test_moveWithCurrency (self):
    """
    Sends currency to predefined addresses together with a move and checks
    the "out" field produced by the notifications.
    """

    self.log.info ("Sending move with currency...")

    addr1 = self.node.getnewaddress ()
    addr2 = "dHNvNaqcD7XPDnoRjAoyfcMpHRi5upJD7p"

    # Send a move and spend coins in the same transaction.  We use a raw
    # transaction, so that we can actually send *two* outputs to the *same*
    # address.  This should work, and return it only once in the notification
    # with the total amount.  We use amounts with precision down to satoshis
    # to check this works correctly.

    hex1 = self.node.getaddressinfo (addr1)["scriptPubKey"]
    hex2 = self.node.getaddressinfo (addr2)["scriptPubKey"]
    scr1 = CScript (hex_str_to_bytes (hex1))
    scr2 = CScript (hex_str_to_bytes (hex2))

    tx = CTransaction ()
    name = self.node.name_show ("p/x")
    tx.vin.append (CTxIn (COutPoint (int (name["txid"], 16), name["vout"])))
    tx.vout.append (CTxOut (12345678, scr1))
    tx.vout.append (CTxOut (142424242, scr2))
    tx.vout.append (CTxOut (COIN, scr1))
    tx.vout.append (CTxOut (COIN // 2, CScript ([OP_TRUE])))
    tx.vout.append (CTxOut (COIN // 100, scr1))
    rawtx = bytes_to_hex_str (tx.serialize ())

    nameOp = {
      "op": "name_update",
      "name": "p/x",
      "value": json.dumps ({"g":{"a":"move"}}),
    }
    rawtx = self.node.namerawtransaction (rawtx, 4, nameOp)["hex"]

    rawtx = self.node.fundrawtransaction (rawtx)["hex"]
    signed = self.node.signrawtransactionwithwallet (rawtx)
    assert signed["complete"]
    txid = self.node.sendrawtransaction (signed["hex"])
    self.node.generate (1)

    _, data = self.games["a"].receive ()
    assert_equal (len (data["moves"]), 1)
    assertMove (data["moves"][0], txid, "x", "move")
    out = data["moves"][0]["out"]
    assert_equal (len (out), 3)
    quant = Decimal ('1.00000000')
    assert_equal (Decimal (out[addr1]).quantize (quant), Decimal ('1.12345678'))
    assert_equal (Decimal (out[addr2]).quantize (quant), Decimal ('1.42424242'))

    _, data = self.games["b"].receive ()
    assert_equal (data["moves"], [])

  def buildChain (self, n):
    """
    Builds a chain of length n and records the attach sequence we get
    for games a and b.
    """

    blks = []
    attachA = []
    attachB = []
    for i in range (n):
      blks.extend (self.node.generate (1))
      topic, data = self.games["a"].receive ()
      assert_equal (topic, "game-block-attach json a")
      if len (attachA) > 0:
        assert_equal (attachA[-1]['block']['hash'], data['block']['parent'])
      attachA.append (data)

      topic, data = self.games["b"].receive ()
      assert_equal (topic, "game-block-attach json b")
      if len (attachB) > 0:
        assert_equal (attachB[-1]['block']['hash'], data['block']['parent'])
      attachB.append (data)

    return blks, attachA, attachB

  def verifyDetach (self, game, attach):
    """
    Verify that we receive the correct detach sequence for the given game,
    corresponding to the previously recorded attachments.
    """

    for d in reversed (attach):
      topic, data = self.games[game].receive ()
      assert_equal (topic, "game-block-detach json %s" % game)
      assert_equal (data, d)

  def verifyAttach (self, game, attach):
    """
    Verify that we receive again the recorded attach sequence for the game.
    """

    for a in attach:
      topic, data = self.games[game].receive ()
      assert_equal (topic, "game-block-attach json %s" % game)
      assert_equal (data, a)

  def buildAndVerifyReorg (self):
    """
    Constructs a short and long chain, triggering a reorg.  Verifies the
    expected notifications and returns them.

    This is basically the reorg test, but also used for testing
    game_sendupdates.
    """

    # Start by building a long chain that we will later reorg to.  Include a
    # move (just because, not really important) and record the attach
    # notifications we get.
    self.node.name_update ("p/x", json.dumps ({"g":{"a": True}}))
    longBlks, longAttachA, longAttachB = self.buildChain (10)

    # Invalidate the first block, which should trigger detaches.
    self.node.invalidateblock (longBlks[0])
    self.verifyDetach ("a", longAttachA)
    self.verifyDetach ("b", longAttachB)

    # Build a shorter chain.
    shortBlks, shortAttachA, shortAttachB = self.buildChain (5)

    # Trigger the reorg and verify the notifications.
    self.node.reconsiderblock (longBlks[0])
    self.verifyDetach ("a", shortAttachA)
    self.verifyDetach ("b", shortAttachB)
    self.verifyAttach ("a", longAttachA)
    self.verifyAttach ("b", longAttachB)

    res = {
      "long":
        {
          "blocks": longBlks,
          "attachA": longAttachA,
          "attachB": longAttachB,
        },
      "short":
        {
          "blocks": shortBlks,
          "attachA": shortAttachA,
          "attachB": shortAttachB,
        },
    }
    return res

  def _test_reorg (self):
    """
    Produces a reorg and checks the resulting sequence of notifications
    (including those for detached blocks).
    """

    self.log.info ("Testing reorg...")
    self.buildAndVerifyReorg ()

  def _test_sendUpdates (self):
    """
    Tests on-demand notifications using game_sendupdates.
    """

    self.log.info ("Testing game_sendupdates...")

    # Build up a fork for testing.
    ancestor = self.node.getbestblockhash ()
    res = self.buildAndVerifyReorg ()
    assert_equal (self.node.getbestblockhash (), res["long"]["blocks"][-1])

    # Trigger on-demand updates in both directions for the fork.
    resA = self.node.game_sendupdates ("a", res["short"]["blocks"][-1])
    resB = self.node.game_sendupdates ("b", res["long"]["blocks"][-1],
                                       res["short"]["blocks"][-1])
    tokenA = resA['reqtoken']
    tokenB = resB['reqtoken']
    assert tokenA != tokenB

    # Check the return values.
    assert_equal (resA, {
      "toblock": res["long"]["blocks"][-1],
      "ancestor": ancestor,
      "reqtoken": tokenA,
      "steps":
        {
          "attach": 10,
          "detach": 5,
        },
    })
    assert_equal (resB, {
      "toblock": res["short"]["blocks"][-1],
      "ancestor": ancestor,
      "reqtoken": tokenB,
      "steps":
        {
          "attach": 5,
          "detach": 10,
        },
    })

    # Add the tokens in to the expected chains.
    def addToken (token, chain):
      for blk in chain:
        assert 'reqtoken' not in blk
        blk['reqtoken'] = token
    addToken (tokenA, res["short"]["attachA"])
    addToken (tokenA, res["long"]["attachA"])
    addToken (tokenB, res["short"]["attachB"])
    addToken (tokenB, res["long"]["attachB"])

    # Check the updates for the games themselves.
    self.verifyDetach ("a", res["short"]["attachA"])
    self.verifyAttach ("a", res["long"]["attachA"])
    self.verifyDetach ("b", res["long"]["attachB"])
    self.verifyAttach ("b", res["short"]["attachB"])

    # Trigger updates for a game without subscribers.
    genesis = self.node.getblockhash (0)
    self.node.game_sendupdates ("other", genesis)

    # Updates for a game that is not normally tracked should still work.
    res = self.node.game_sendupdates ("ignored", genesis)
    assert_equal (res["steps"]["detach"], 0)
    for i in range (res["steps"]["attach"]):
      topic, data = self.games["ignored"].receive ()
      assert_equal (topic, "game-block-attach json ignored")
      assert_equal (data["block"]["parent"], self.node.getblockhash (i))
      assert_equal (data["block"]["hash"], self.node.getblockhash (i + 1))

    # Check the case of there being no update.
    tip = self.node.getbestblockhash ()
    res = self.node.game_sendupdates ("a", tip)
    del res['reqtoken']
    assert_equal (res, {
      "toblock": tip,
      "ancestor": tip,
      "steps":
        {
          "attach": 0,
          "detach": 0,
        },
    })

    # Verify error for invalid block hashes.
    invalidBlock = "00" * 32
    assert_raises_rpc_error (-5, "fromblock not found",
                             self.node.game_sendupdates, "a", invalidBlock)
    assert_raises_rpc_error (-5, "toblock not found",
                             self.node.game_sendupdates,
                             "a", genesis, invalidBlock)

  def _test_trackedgamesRPC (self):
    """
    Tests the trackedgames RPC, which can be used to read and modify
    the list of tracked games dynamically.
    """

    self.log.info ("Testing the trackedgames RPC...")

    # Test initial set as configured by the startup options.
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
    self.node.generate (1)
    topic, _ = self.games["a"].receive ()
    assert_equal (topic, "game-block-attach json a")
    topic, _ = self.games["ignored"].receive ()
    assert_equal (topic, "game-block-attach json ignored")

    # Restore original setting.
    self.node.trackedgames ("add", "b")
    self.node.trackedgames ("remove", "ignored")
    assert_equal (set (self.node.trackedgames ()), set (["a", "b", "other"]))


if __name__ == '__main__':
    GameBlocksTest ().main ()
