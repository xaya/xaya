// Copyright (c) 2018-2021 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POWDATA_H
#define BITCOIN_POWDATA_H

#include <auxpow.h>
#include <primitives/pureheader.h>
#include <serialize.h>
#include <uint256.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

namespace Consensus
{
class Params;
}

namespace powdata_tests
{
class PowDataForTest;
}

/** Possible PoW algorithms and their ID.  */
enum class PowAlgo : uint8_t
{
  INVALID = 0,

  SHA256D = 1,
  NEOSCRYPT = 2,

  FLAG_MERGE_MINED = 0x80,
};

/**
 * Returns the relative factor (actually, the binary log of it) of how much
 * harder the given PowAlgo is than SHA256D.  This is used to correct the
 * chain work to make the different algorithms comparable.
 */
int powAlgoLog2Weight (PowAlgo algo);

/**
 * Returns the maximal target hash for the given PoW algo (corresponding to
 * the minimal difficulty).  This is based on the chainparams-configured
 * powLimitNeoscrypt and then adapted based on the relative PoW difficulties.
 */
uint256 powLimitForAlgo (PowAlgo algo, const Consensus::Params& params);

/* Conversion between (core) PowAlgo and string representations of it for
   the external interfaces.  The methods throw std::invalid_argument in case
   the conversion fails.  */
PowAlgo PowAlgoFromString (const std::string& str);
std::string PowAlgoToString (PowAlgo algo);

/* Implement serialisation for PowAlgo.  The standard framework does not work
   since it is a typesafe enum.  */

template <typename Stream>
  inline void
  Serialize (Stream& os, const PowAlgo algo)
{
  Serialize (os, static_cast<uint8_t> (algo));
}

template <typename Stream>
  inline void
  Unserialize (Stream& is, PowAlgo& algo)
{
  uint8_t val;
  Unserialize (is, val);
  algo = static_cast<PowAlgo> (val);
}

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
  /** The auxpow object if this is merge-mined.  */
  std::shared_ptr<CAuxPow> auxpow;

  friend class powdata_tests::PowDataForTest;

public:

  inline PowData ()
    : algo(PowAlgo::INVALID)
  {}

  PowData (const PowData&) = default;
  PowData& operator= (const PowData&) = default;

  SERIALIZE_METHODS (PowData, obj)
  {
    READWRITE (obj.algo, obj.nBits);

    if (obj.isMergeMined ())
      {
        SER_READ (obj, obj.fakeHeader.reset ());
        SER_READ (obj, obj.auxpow = std::make_shared<CAuxPow> ());
        assert (obj.auxpow != nullptr);
        READWRITE (*obj.auxpow);

      }
    else
      {
        SER_READ (obj, obj.auxpow.reset ());
        SER_READ (obj, obj.fakeHeader = std::make_shared<CPureBlockHeader> ());
        assert (obj.fakeHeader != nullptr);
        READWRITE (*obj.fakeHeader);
      }
  }

  inline bool
  isNull () const
  {
    return algo == PowAlgo::INVALID;
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

  void setCoreAlgo (PowAlgo a);

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

  inline const CAuxPow&
  getAuxpow () const
  {
    assert (isMergeMined ());
    assert (auxpow != nullptr);
    return *auxpow;
  }

  /**
   * Sets the auxpow object and the merge-mining flag.
   */
  void setAuxpow (std::unique_ptr<CAuxPow> apow);

  /**
   * Sets a newly created auxpow committing to the given main header and
   * return a reference to its parent block header that can then be mined.
   */
  CPureBlockHeader& initAuxpow (const CPureBlockHeader& block);

  /**
   * Verifies whether the given PoW header has valid PoW with respect to this
   * data's target and algorithm.  This only verifies the PoW, but does not
   * check that any commitment to the right block header is made.
   */
  bool checkProofOfWork (const CPureBlockHeader& hdr,
                         const Consensus::Params& params) const;

  /**
   * Checks whether a given hash matches the target bits.  This is moved
   * from upstream's pow.cpp file here, so that powdata.cpp does not
   * depend on pow.cpp from the "server" library.
   */
  static bool checkProofOfWork (PowAlgo algo,
                                const uint256& hash, unsigned nBits,
                                const Consensus::Params& params);

};

#endif // BITCOIN_POWDATA_H
