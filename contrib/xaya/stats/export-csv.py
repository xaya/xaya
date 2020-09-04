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

import stats

import jsonrpclib

import argparse
import csv
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
rpc = jsonrpclib.ServerProxy (args.rpc_url)


class CsvExporter (stats.StatsProcessor):

  def __init__ (self, rpc, blocksFile, namesFile, gamesFile):
    super ().__init__ (rpc)

    self.blocks = csv.DictWriter (blocksFile, [
      "blockhash",
      "height",
      "timestamp",
      "size",
      "ntx",
    ])
    self.blocks.writeheader ()

    self.nameOut = csv.DictWriter (namesFile, [
      "blockhash",
      "height",
      "name",
      "registered",
      "updates",
    ])
    self.nameOut.writeheader ()

    self.games = csv.DictWriter (gamesFile, [
      "blockhash",
      "height",
      "game",
      "name",
      "moves",
    ])
    self.games.writeheader ()

  def handleBlock (self, **kwargs):
    self.blocks.writerow (kwargs)

  def handleName (self, *, registered, **kwargs):
    if registered:
      kwargs["registered"] = 1
    else:
      kwargs["registered"] = 0
    self.nameOut.writerow (kwargs)

  def handleMoves (self, **kwargs):
    self.games.writerow (kwargs)


with open (args.csv_out_blocks, "w", newline="") as blocksFile, \
     open (args.csv_out_names, "w", newline="") as namesFile, \
     open (args.csv_out_games, "w", newline="") as gamesFile:

    exporter = CsvExporter (rpc, blocksFile, namesFile, gamesFile)
    exporter.processHeightRange (args.start, args.end)
