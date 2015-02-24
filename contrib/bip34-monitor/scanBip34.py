#!/usr/bin/env python

#   BIP34 monitor - scan for BIP34 upgrade status
#   Copyright (C) 2015  Daniel Kraft <d@domob.eu>
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

import jsonrpc
import urllib

numBlocks = 1000

username = urllib.quote_plus ("namecoin")
password = urllib.quote_plus ("password")
url = "http://%s:%s@localhost:8336/" % (username, password)

rpc = jsonrpc.proxy.ServiceProxy (url)
tips = rpc.getchaintips ()
tip = None
for t in tips:
  if t['status'] == 'active':
    tip = t
    break
assert tip is not None

scanned = 0
versions = dict ()
curHash = tip['hash']
while scanned < numBlocks:
  data = rpc.getblock (curHash)

  baseVersion = data['version'] % (1 << 8)
  if baseVersion not in versions:
    versions[baseVersion] = 1
  else:
    versions[baseVersion] += 1

  curHash = data['previousblockhash']
  scanned += 1

for (version, cnt) in versions.items ():
  print "%d: %d" % (version, cnt)
