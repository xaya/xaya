// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/pureheader.h>

#include <crypto/neoscrypt.h>
#include <hash.h>
#include <streams.h>
#include <utilstrencodings.h>

#include <algorithm>
#include <cassert>

uint256 CPureBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CPureBlockHeader::GetPowHash () const
{
  std::vector<unsigned char> data;
  CVectorWriter writer(SER_GETHASH, PROTOCOL_VERSION, data, 0);
  writer << *this;

  assert (data.size () % 4 == 0);
  for (size_t i = 0; i < data.size (); i += 4)
    {
      std::swap (data[i], data[i + 3]);
      std::swap (data[i + 1], data[i + 2]);
    }

  constexpr int profile = 0;
  uint256 hash;
  neoscrypt (&data[0], hash.begin(), profile);

  return hash;
}
