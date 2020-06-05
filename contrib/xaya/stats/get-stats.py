#!/usr/bin/env python3

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

# For this script to work, the valueencoding of Xaya Core should
# be set to "hex".

import jsonrpclib

import argparse
import codecs
import csv
import json
import logging
import sys

parser = argparse.ArgumentParser ()
parser.add_argument ("--rpc_url", required=True,
                     help="JSON-RPC URL for Xaya Core")
parser.add_argument ("--start", type=int, required=True,
                     help="Starting block height")
parser.add_argument ("--end", type=int, required=True,
                     help="End block height")
parser.add_argument ("--csv_out_blocks", required=True,
                     help="CSV output file for general per-block data")
parser.add_argument ("--csv_out_names", required=True,
                     help="CSV output file for per-name data")
parser.add_argument ("--csv_out_games", required=True,
                     help="CSV output file for per-game data")
args = parser.parse_args ()

logging.basicConfig (level=logging.INFO, stream=sys.stderr)
log = logging.getLogger ()

rpc = jsonrpclib.ServerProxy (args.rpc_url)
data = rpc.getnetworkinfo ()
log.info ("Connected to Xaya Core version %d" % data["version"])

with open (args.csv_out_blocks, "w", newline="") as blocksFile, \
     open (args.csv_out_names, "w", newline="") as namesFile, \
     open (args.csv_out_games, "w", newline="") as gamesFile:

  blocks = csv.DictWriter (blocksFile, [
    "height",
    "timestamp",
    "size",
    "tx",
  ])
  blocks.writeheader ()

  nameOut = csv.DictWriter (namesFile, [
    "height",
    "name",
    "registration",
    "updates",
  ])
  nameOut.writeheader ()

  games = csv.DictWriter (gamesFile, [
    "height",
    "game",
    "name",
    "moves",
  ])
  games.writeheader ()

  for h in range (args.start, args.end + 1):
    if h % 1000 == 0:
      log.info ("Processing height %d..." % h)

    blk = rpc.getblock (rpc.getblockhash (h), 2)

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

    blocks.writerow ({
      "height": h,
      "timestamp": blk["time"],
      "size": blk["size"],
      "tx": len (blk["tx"]),
    })

    for nm, d in names.items ():
      nameOut.writerow ({
        "height": h,
        "name": nm,
        "registration": d["reg"],
        "updates": d["upd"],
      })

      for g, m in d["moves"].items ():
        games.writerow ({
          "height": h,
          "game": g,
          "name": nm,
          "moves": m,
        })
