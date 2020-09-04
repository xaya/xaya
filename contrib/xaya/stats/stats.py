#   Xaya Stats - extract Xaya blockchain statistics
#   Copyright (C) 2020  The Xaya developers
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU Affero General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU Affero General Public License for more details.
#
#   You should have received a copy of the GNU Affero General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

import codecs
import json
import logging


class StatsProcessor:
  """
  Utility class to retrieve general statistics about the usage
  of Xaya from a Xaya Core daemon, and processing it in some form.
  The stats will be passed to methods of this class, and subclasses
  should take care of then handling them as needed.
  """

  def __init__ (self, rpc):
    self.log = logging.getLogger ("stats")
    self.rpc = rpc

    data = self.rpc.getnetworkinfo ()
    self.log.info ("Connected to Xaya Core version %d" % data["version"])

  def handleBlock (self, *, blockhash, height, timestamp, size, ntx):
    """
    Called for each processed block with basic stats about it.
    """
    pass

  def handleName (self, *, blockhash, height, name, registered, updates):
    """
    Called for each block and name that was touched in that block.
    """
    pass

  def handleMoves (self, *, blockhash, height, game, name, moves):
    """
    Called for each block, game and name with the total number of
    moves done in that block.
    """
    pass

  def processHeightRange (self, fromHeight, toHeight):
    """
    Processes all blocks in an inclusive range of block heights.
    """

    for h in range (fromHeight, toHeight + 1):
      self.processHeight (h)

  def processHeight (self, h):
    """
    Runs processing for a single given block height.
    """

    if h % 1000 == 0:
      self.log.info ("Processing height %d..." % h)

    blockhash = self.rpc.getblockhash (h)
    blk = self.rpc.getblock (blockhash, 2)

    names = {}
    for t in blk["tx"]:
      for o in t["vout"]:
        if "nameOp" not in o["scriptPubKey"]:
          continue
        nmop = o["scriptPubKey"]["nameOp"]

        assert nmop["name_encoding"] != "hex"
        assert nmop["value_encoding"] == "hex"

        if nmop["name"] not in names:
          names[nmop["name"]] = {"reg": 0, "upd": 0, "moves": {}}

        if nmop["op"] == "name_register":
          names[nmop["name"]]["reg"] += 1
        elif nmop["op"] == "name_update":
          names[nmop["name"]]["upd"] += 1

        if nmop["name"][:2] != "p/":
          continue

        val = json.loads (codecs.decode (nmop["value"], "hex"))
        if "g" not in val:
          continue
        if type (val["g"]) != dict:
          continue

        for g in val["g"].keys ():
          if g not in names[nmop["name"]]["moves"]:
            names[nmop["name"]]["moves"][g] = 1
          else:
            names[nmop["name"]]["moves"][g] += 1

    self.handleBlock (blockhash=blockhash, height=h,
                      timestamp=blk["time"],
                      size=blk["size"], ntx=len (blk["tx"]))

    for nm, d in names.items ():
      self.handleName (blockhash=blockhash, height=h, name=nm,
                       registered=(d["reg"] > 0),
                       updates=d["upd"])

      for g, m in d["moves"].items ():
        self.handleMoves (blockhash=blockhash, height=h, game=g, name=nm,
                          moves=m)

