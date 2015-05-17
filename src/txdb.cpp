// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "chainparams.h"
#include "hash.h"
#include "main.h"
#include "pow.h"
#include "uint256.h"

#include "script/names.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_NAME = 'n';
static const char DB_NAME_HISTORY = 'h';
static const char DB_NAME_EXPIRY = 'x';

static const char DB_BEST_BLOCK = 'B';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';


void static BatchWriteCoins(CLevelDBBatch &batch, const uint256 &hash, const CCoins &coins) {
    if (coins.IsPruned())
        batch.Erase(make_pair(DB_COINS, hash));
    else
        batch.Write(make_pair(DB_COINS, hash), coins);
}

void static BatchWriteHashBestChain(CLevelDBBatch &batch, const uint256 &hash) {
    batch.Write(DB_BEST_BLOCK, hash);
}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe) {
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

bool CCoinsViewDB::GetName(const valtype &name, CNameData& data) const {
    return db.Read(std::make_pair(DB_NAME, name), data);
}

bool CCoinsViewDB::GetNameHistory(const valtype &name, CNameHistory& data) const {
    assert (fNameHistory);
    return db.Read(std::make_pair(DB_NAME_HISTORY, name), data);
}

bool CCoinsViewDB::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const {
    names.clear();

    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<leveldb::Iterator> pcursor(const_cast<CLevelDBWrapper*>(&db)->NewIterator());

    const CNameCache::ExpireEntry seekEntry(nHeight, valtype ());
    const std::pair<char, CNameCache::ExpireEntry> seekKey(DB_NAME_EXPIRY,
                                                           seekEntry);
    CDataStream seekKeyStream(SER_DISK, CLIENT_VERSION);
    seekKeyStream.reserve(seekKeyStream.GetSerializeSize(seekKey));
    seekKeyStream << seekKey;
    leveldb::Slice slKey(&seekKeyStream[0], seekKeyStream.size());

    for (pcursor->Seek(slKey); pcursor->Valid(); pcursor->Next())
    {
        try
        {
            slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;

            if (chType != DB_NAME_EXPIRY)
              break;

            CNameCache::ExpireEntry entry;
            ssKey >> entry;

            assert (entry.nHeight >= nHeight);
            if (entry.nHeight > nHeight)
              break;

            const valtype& name = entry.name;
            if (names.count(name) > 0)
                return error("%s : duplicate name '%s' in expire index",
                             __func__, ValtypeToString(name).c_str());
            names.insert(name);
        } catch (const std::exception &e)
        {
            return error("%s : Deserialize or I/O error - %s",
                         __func__, e.what());
        }
    }

    return true;
}

class CDbNameIterator : public CNameIterator
{

private:

    /* The backing LevelDB iterator.  */
    leveldb::Iterator* iter;

public:

    ~CDbNameIterator();

    /**
     * Construct a new name iterator for the database.
     * @param db The database to create the iterator for.
     */
    CDbNameIterator(const CLevelDBWrapper& db);

    /* Implement iterator methods.  */
    void seek (const valtype& start);
    bool next (valtype& name, CNameData& data);

};

CDbNameIterator::~CDbNameIterator() {
    delete iter;
}

CDbNameIterator::CDbNameIterator(const CLevelDBWrapper& db)
    : iter(const_cast<CLevelDBWrapper*>(&db)->NewIterator())
{
    seek(valtype());
}

void CDbNameIterator::seek(const valtype& start) {
    const std::pair<char, valtype> seekKey(DB_NAME, start);
    CDataStream seekKeyStream(SER_DISK, CLIENT_VERSION);
    seekKeyStream.reserve(seekKeyStream.GetSerializeSize(seekKey));
    seekKeyStream << seekKey;

    leveldb::Slice slKey(&seekKeyStream[0], seekKeyStream.size());
    iter->Seek(slKey);
}

bool CDbNameIterator::next(valtype& name, CNameData& data) {
    if (!iter->Valid())
        return false;

    try
    {
        const leveldb::Slice& slKey = iter->key();
        CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(),
                          SER_DISK, CLIENT_VERSION);

        char chType;
        ssKey >> chType;

        if (chType != DB_NAME)
            return false;

        const leveldb::Slice& slValue = iter->value();
        CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(),
                            SER_DISK, CLIENT_VERSION);

        ssKey >> name;
        ssValue >> data;
    } catch (const std::exception& exc)
    {
        LogPrintf("%s : Deserialize or I/O error - %s", __func__, exc.what());
        return false;
    }

    iter->Next ();
    return true;
}

CNameIterator* CCoinsViewDB::IterateNames() const {
    return new CDbNameIterator(db);
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CNameCache &names) {
    CLevelDBBatch batch;
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            BatchWriteCoins(batch, it->first, it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    if (!hashBlock.IsNull())
        BatchWriteHashBestChain(batch, hashBlock);

    names.writeBatch(batch);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) const {
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<leveldb::Iterator> pcursor(const_cast<CLevelDBWrapper*>(&db)->NewIterator());
    pcursor->SeekToFirst();

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = GetBestBlock();
    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType == DB_COINS) {
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                CCoins coins;
                ssValue >> coins;
                uint256 txhash;
                ssKey >> txhash;
                ss << txhash;
                ss << VARINT(coins.nVersion);
                ss << (coins.fCoinBase ? 'c' : 'n');
                ss << VARINT(coins.nHeight);
                stats.nTransactions++;
                for (unsigned int i=0; i<coins.vout.size(); i++) {
                    const CTxOut &out = coins.vout[i];
                    if (!out.IsNull()) {
                        stats.nTransactionOutputs++;
                        ss << VARINT(i+1);
                        ss << out;
                        nTotalAmount += out.nValue;
                    }
                }
                stats.nSerializedSize += 32 + slValue.size();
                ss << VARINT(0);
            }
            pcursor->Next();
        } catch (const std::exception& e) {
            return error("%s: Deserialize or I/O error - %s", __func__, e.what());
        }
    }
    stats.nHeight = mapBlockIndex.find(GetBestBlock())->second->nHeight;
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CCoinsViewDB::ValidateNameDB() const
{
    const uint256 blockHash = GetBestBlock();
    int nHeight;
    if (blockHash.IsNull())
        nHeight = 0;
    else
        nHeight = mapBlockIndex.find(blockHash)->second->nHeight;

    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<leveldb::Iterator> pcursor(const_cast<CLevelDBWrapper*>(&db)->NewIterator());
    pcursor->SeekToFirst();

    /* Loop over the total database and read interesting
       things to memory.  We later use that to check
       everything against each other.  */

    std::map<valtype, unsigned> nameHeightsIndex;
    std::map<valtype, unsigned> nameHeightsData;
    std::set<valtype> namesInDB;
    std::set<valtype> namesInUTXO;
    std::set<valtype> namesWithHistory;

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        try
        {
            const leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(),
                              SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;

            const leveldb::Slice slValue = pcursor->value();
            CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(),
                                SER_DISK, CLIENT_VERSION);

            switch (chType)
            {
            case DB_COINS:
            {
                CCoins coins;
                ssValue >> coins;
                BOOST_FOREACH(const CTxOut& txout, coins.vout)
                    if (!txout.IsNull())
                    {
                        const CNameScript nameOp(txout.scriptPubKey);
                        if (nameOp.isNameOp() && nameOp.isAnyUpdate())
                        {
                            const valtype& name = nameOp.getOpName();
                            if (namesInUTXO.count(name) > 0)
                                return error("%s : name %s duplicated in UTXO set",
                                             __func__, ValtypeToString(name).c_str());
                            namesInUTXO.insert(nameOp.getOpName());
                        }
                    }
                break;
            }

            case DB_NAME:
            {
                valtype name;
                ssKey >> name;
                CNameData data;
                ssValue >> data;

                if (nameHeightsData.count(name) > 0)
                    return error("%s : name %s duplicated in name index",
                                 __func__, ValtypeToString(name).c_str());
                nameHeightsData.insert(std::make_pair(name, data.getHeight()));
                
                /* Expiration is checked at height+1, because that matches
                   how the UTXO set is cleared in ExpireNames.  */
                assert(namesInDB.count(name) == 0);
                if (!data.isExpired(nHeight + 1))
                    namesInDB.insert(name);
                break;
            }

            case DB_NAME_HISTORY:
            {
                valtype name;
                ssKey >> name;

                if (namesWithHistory.count(name) > 0)
                    return error("%s : name %s has duplicate history",
                                 __func__, ValtypeToString(name).c_str());
                namesWithHistory.insert(name);
                break;
            }

            case DB_NAME_EXPIRY:
            {
                CNameCache::ExpireEntry entry;
                ssKey >> entry;
                const valtype& name = entry.name;

                if (nameHeightsIndex.count(name) > 0)
                    return error("%s : name %s duplicated in expire idnex",
                                 __func__, ValtypeToString(name).c_str());

                nameHeightsIndex.insert(std::make_pair(name, entry.nHeight));
                break;
            }

            default:
                break;
            }

            pcursor->Next();
        } catch (std::exception &e)
        {
            return error("%s : Deserialize or I/O error - %s",
                         __func__, e.what());
        }
    }

    /* Now verify the collected data.  */

    assert (nameHeightsData.size() >= namesInDB.size());

    if (nameHeightsIndex != nameHeightsData)
        return error("%s : name height data mismatch", __func__);

    BOOST_FOREACH(const valtype& name, namesInDB)
        if (namesInUTXO.count(name) == 0)
            return error("%s : name '%s' in DB but not UTXO set",
                         __func__, ValtypeToString(name).c_str());
    BOOST_FOREACH(const valtype& name, namesInUTXO)
        if (namesInDB.count(name) == 0)
            return error("%s : name '%s' in UTXO set but not DB",
                         __func__, ValtypeToString(name).c_str());

    if (fNameHistory)
    {
        BOOST_FOREACH(const valtype& name, namesWithHistory)
            if (nameHeightsData.count(name) == 0)
                return error("%s : history entry for name '%s' not in main DB",
                             __func__, ValtypeToString(name).c_str());
    } else if (!namesWithHistory.empty ())
        return error("%s : name_history entries in DB, but"
                     " -namehistory not set", __func__);

    LogPrintf("Checked name database, %u unexpired names, %u total.\n",
              namesInDB.size(), nameHeightsData.size());
    LogPrintf("Names with history: %u\n", namesWithHistory.size());

    return true;
}

void
CNameCache::writeBatch (CLevelDBBatch& batch) const
{
  for (EntryMap::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    batch.Write (std::make_pair (DB_NAME, i->first), i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    batch.Erase (std::make_pair (DB_NAME, *i));

  assert (fNameHistory || history.empty ());
  for (std::map<valtype, CNameHistory>::const_iterator i = history.begin ();
       i != history.end (); ++i)
    if (i->second.empty ())
      batch.Erase (std::make_pair (DB_NAME_HISTORY, i->first));
    else
      batch.Write (std::make_pair (DB_NAME_HISTORY, i->first), i->second);

  for (std::map<ExpireEntry, bool>::const_iterator i = expireIndex.begin ();
       i != expireIndex.end (); ++i)
    if (i->second)
      batch.Write (std::make_pair (DB_NAME_EXPIRY, i->first));
    else
      batch.Erase (std::make_pair (DB_NAME_EXPIRY, i->first));
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{
    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(DB_BLOCK_INDEX, uint256());
    pcursor->Seek(ssKeySet.str());

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType == DB_BLOCK_INDEX) {
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                CDiskBlockIndex diskindex;
                ssValue >> diskindex;

                // Construct block index object
                CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                /* Bitcoin checks the PoW here.  We don't do this because
                   the CDiskBlockIndex does not contain the auxpow.
                   This check isn't important, since the data on disk should
                   already be valid and can be trusted.  */

                pcursor->Next();
            } else {
                break; // if shutdown requested or finished loading block index
            }
        } catch (const std::exception& e) {
            return error("%s: Deserialize or I/O error - %s", __func__, e.what());
        }
    }

    return true;
}
