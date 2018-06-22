// Copyright (c) 2018 The Xyon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POWDATA_H
#define BITCOIN_POWDATA_H

#include <consensus/params.h>
#include <primitives/pureheader.h>
#include <serialize.h>
#include <uint256.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

/** Possible PoW algorithms and their ID.  */
enum class PowAlgo : uint8_t
{
  INVALID = 0,

  SHA256D = 1,
  NEOSCRYPT = 2,

  FLAG_MERGE_MINED = 0x80,
};

/* Conversion between (core) PowAlgo and string representations of it for
   the external interfaces.  The methods throw std::invalid_argument in case
   the conversion fails.  */
PowAlgo PowAlgoFromString (const std::string& str);
std::string PowAlgoToString (PowAlgo algo);

/**
 * The basic PoW data attached to a block header.
 */
class PowData
{

private:

  static constexpr int mmFlag = static_cast<int> (PowAlgo::FLAG_MERGE_MINED);

  PowAlgo algo;
  uint32_t nBits;

  /** The block header satisfying PoW if this is stand-alone mined.  */
  std::shared_ptr<CPureBlockHeader> fakeHeader;

public:

  inline PowData ()
    : algo(PowAlgo::NEOSCRYPT)
  {}

  PowData (const PowData&) = default;
  PowData& operator= (const PowData&) = default;

  ADD_SERIALIZE_METHODS;

  template <typename Stream, typename Operation>
    inline void
    SerializationOp (Stream& s, Operation ser_action)
  {
    uint8_t intAlgo;
    if (!ser_action.ForRead ())
      intAlgo = static_cast<uint8_t> (algo);
    READWRITE (intAlgo);
    if (ser_action.ForRead ())
      algo = static_cast<PowAlgo> (intAlgo);

    READWRITE (nBits);

    assert (!isMergeMined ());
    if (ser_action.ForRead ())
      fakeHeader = std::make_shared<CPureBlockHeader> ();
    assert (fakeHeader != nullptr);
    READWRITE (*fakeHeader);
  }

  inline bool
  isMergeMined () const
  {
    return (static_cast<int> (algo) & mmFlag) != 0;
  }

  inline PowAlgo
  getCoreAlgo () const
  {
    return static_cast<PowAlgo> (static_cast<int> (algo) & ~mmFlag);
  }

  inline uint32_t
  getBits () const
  {
    return nBits;
  }

  inline void
  setBits (const uint32_t b)
  {
    nBits = b;
  }

  inline const CPureBlockHeader&
  getFakeHeader () const
  {
    assert (!isMergeMined ());
    assert (fakeHeader != nullptr);
    return *fakeHeader;
  }

  /**
   * Sets the given block header as fake header for a stand-alone mined block.
   * This also unsets the merge-mining flag.
   */
  void setFakeHeader (std::unique_ptr<CPureBlockHeader> hdr);

  /**
   * Sets a newly created fake header for the given main block and returns
   * a reference to it.  This can be used to conveniently attach and then
   * mine a fake header.
   */
  CPureBlockHeader& initFakeHeader (const CPureBlockHeader& block);

  /**
   * Verifies whether the PoW contained in this object is valid for the given
   * main-block hash and consensus parameters.
   */
  bool isValid (const uint256& hash, const Consensus::Params& params) const;

};

#endif // BITCOIN_POWDATA_H
