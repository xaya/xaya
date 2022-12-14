#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Xaya developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Computes the PoW hash for Xaya."""

from . import messages

# This package can be found here:  https://github.com/xaya/neoscrypt_python
import neoscrypt

def forHeader (algo, hdrData):
  """Computes the PoW hash for the header given as bytes."""

  if algo == "sha256d":
    return messages.hash256 (hdrData)[::-1]

  if algo == "neoscrypt":
    swapped = bytes ()
    for i in range (0, len (hdrData), 4):
      swapped += hdrData[i : i + 4][::-1]
    return neoscrypt.getPoWHash (swapped)

  raise RuntimeError (f"Invalid hash algorithm: {algo}")
