// Copyright (c) 2018 The Chimaera developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/* Command-line utility to compute the PoW hash of a block header given in hex.
   This is used for the regtests to access the PoW hash from Python.  */

#include <core_io.h>
#include <primitives/pureheader.h>
#include <uint256.h>

#include <cstdlib>
#include <iostream>

int main (int argc, char** argv)
{
  if (argc != 2)
    {
      std::cerr << "USAGE: chimaera-hash BLOCK-HEADER-HEX" << std::endl;
      return EXIT_FAILURE;
    }
  const std::string hex(argv[1]);

  CPureBlockHeader header;
  if (!DecodeHexHeader (header, hex))
    {
      std::cerr << "Failed to decode block header." << std::endl;
      return EXIT_FAILURE;
    }

  const uint256 hash = header.GetPowHash ();
  std::cout << hash.GetHex () << std::endl;

  return EXIT_SUCCESS;
}
