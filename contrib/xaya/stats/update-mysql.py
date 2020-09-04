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

import pymysql
import jsonrpclib

import argparse
import contextlib
import logging
import sys

parser = argparse.ArgumentParser ()
parser.add_argument ("--rpc_url", required=True,
                     help="JSON-RPC URL for Xaya Core")
parser.add_argument ("--blocks_behind", type=int, default=50,
                     help="Number of blocks to stay behind tip for reorgs")
parser.add_argument ("--mysql_host", required=True,
                     help="Host for the MySQL connection")
parser.add_argument ("--mysql_user", required=True,
                     help="Username for the MySQL connection")
parser.add_argument ("--mysql_password", required=True,
                     help="Password for the MySQL connection")
parser.add_argument ("--mysql_database", required=True,
                     help="Database name for the MySQL connection")
args = parser.parse_args ()

logging.basicConfig (level=logging.INFO, stream=sys.stderr)
log = logging.getLogger ()
rpc = jsonrpclib.ServerProxy (args.rpc_url)


class MysqlInserter (stats.StatsProcessor):

  def __init__ (self, rpc, db):
    super ().__init__ (rpc)
    self.db = db

  def handleBlock (self, *, blockhash, height, timestamp, size, ntx):
    with self.db as cursor:
      cursor.execute ("""
        INSERT INTO `blocks`
          (`blockhash`, `height`, `timestamp`, `size`, `ntx`)
          VALUES (%s, %s, %s, %s, %s)
      """, (blockhash, height, timestamp, size, ntx))

  def handleName (self, *, blockhash, height, name, registered, updates):
    with self.db as cursor:
      cursor.execute ("""
        INSERT INTO `names`
          (`blockhash`, `height`, `name`, `registered`, `updates`)
          VALUES (%s, %s, %s, %s, %s)
      """, (blockhash, height, name, registered, updates))

  def handleMoves (self, *, blockhash, height, game, name, moves):
    with self.db as cursor:
      cursor.execute ("""
        INSERT INTO `games`
          (`blockhash`, `height`, `game`, `name`, `moves`)
          VALUES (%s, %s, %s, %s, %s)
      """, (blockhash, height, game, name, moves))

  def processHeight (self, h):
    super ().processHeight (h)
    db.commit ()


with contextlib.closing (pymysql.connect (host=args.mysql_host,
                                          user=args.mysql_user,
                                          passwd=args.mysql_password,
                                          db=args.mysql_database)) as db:

    with db as cursor:
      cursor.execute ("""
        SELECT COUNT(*), MAX(`height`)
          FROM `blocks`
      """)
      assert cursor.rowcount == 1
      cnt, maxHeight = cursor.fetchone ()
      if cnt > 0:
        log.info ("Best height in database: %d" % maxHeight)
        startHeight = maxHeight + 1
      else:
        startHeight = 0
    log.info ("Start height: %d" % startHeight)

    bestHeight = rpc.getblockcount ()
    log.info ("Current blockchain height: %d" % bestHeight)
    endHeight = bestHeight - args.blocks_behind

    if endHeight < startHeight:
      log.warning ("No blocks to process")
    else:
      log.info ("Processing heights %d to %d" % (startHeight, endHeight))
      proc = MysqlInserter (rpc, db)
      proc.processHeightRange (startHeight, endHeight)
