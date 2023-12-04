// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/pureheader.h>

#include <crypto/neoscrypt.h>
#include <hash.h>
#include <powdata.h>
#include <streams.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>

uint256 CPureBlockHeader::GetHash() const
{
    return (HashWriter{} << *this).GetHash();
}

namespace
{

uint256
GetNeoscryptHash (const CPureBlockHeader& hdr)
{
  std::vector<unsigned char> data;
  VectorWriter writer(data, 0);
  writer << hdr;

  /* We swap the byte order similar to what getwork does, as that seems to be
     how common mining software implements neoscrypt.  It does not really matter
     from the PoW point of view, so we can just choose to be compatible.  */
  SwapGetWorkEndianness (data);

  constexpr int profile = 0;
  uint256 hash;
  neoscrypt (&data[0], hash.begin(), profile);

  return hash;
}

} // anonymous namespace

uint256 CPureBlockHeader::GetPowHash (const PowAlgo algo) const
{
  switch (algo)
    {
    case PowAlgo::SHA256D:
      return GetHash ();
    case PowAlgo::NEOSCRYPT:
      return GetNeoscryptHash (*this);
    default:
      assert (false);
    }
}

void
SwapGetWorkEndianness (std::vector<unsigned char>& data)
{
  assert (data.size () % 4 == 0);
  for (size_t i = 0; i < data.size (); i += 4)
    {
      std::swap (data[i], data[i + 3]);
      std::swap (data[i + 1], data[i + 2]);
    }
}
