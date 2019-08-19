#!/usr/bin/env python3

#   Convert Address - convert Bitcoin to Namecoin addresses
#   Copyright (C) 2016-2019  Daniel Kraft <d@domob.eu>
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

import argparse
import sys

from bitcoin import b58check_to_hex, hex_to_b58check

parser = argparse.ArgumentParser ()
parser.add_argument ("--address", required=True,
                     help="the address to convert")
parser.add_argument ("--magic-byte", dest="magic", default=28, type=int,
                     help="the target 'magic' version byte")
parser.add_argument ("--update-file", dest="updatefile", default="",
                     help="if set, replace all occurances in the given file")
args = parser.parse_args ()

keyHex = b58check_to_hex (args.address)
newAddr = hex_to_b58check (keyHex, args.magic)
print (newAddr)

if args.updatefile != "":
  with open (args.updatefile, "r") as f:
    lines = f.readlines ()
  with open (args.updatefile, "w") as f:
    for l in lines:
      f.write (str.replace (l, args.address, newAddr))
