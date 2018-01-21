#!/usr/bin/env python3
# Copyright (c) 2018 The Chimaera developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Computes the PoW hash for Chimaera using the chimaera-hash CLI tool."""

import codecs
import os
import subprocess

def forHeader (hdrData):
  """Computes the PoW hash for the header given as bytes."""
  binary = os.getenv ("CHIMAERA-HASH", "chimaera-hash")
  hexStr = codecs.encode (hdrData, 'hex_codec')
  args = [binary, hexStr]
  process = subprocess.Popen (args, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              universal_newlines=True)
  out, err = process.communicate ()
  returncode = process.poll ()
  if returncode:
    raise subprocess.CalledProcessError (returncode, binary, output=err)

  res = codecs.decode (out.rstrip (), 'hex_codec')
  return res[::-1]
