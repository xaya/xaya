// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "memusage.h"
#include "random.h"
#include "undo.h"
#include "util.h"

#include <assert.h>

/**
 * calculate number of bytes for the bitmask, and its number of non-zero bytes
 * each bit in the bitmask represents the availability of one output, but the
 * availabilities of the first two outputs are encoded separately
 */
void CCoins::CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const {
    unsigned int nLastUsedByte = 0;
    for (unsigned int b = 0; 2+b*8 < vout.size(); b++) {
        bool fZero = true;
        for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++) {
            if (!vout[2+b*8+i].IsNull()) {
                fZero = false;
                continue;
            }
        }
        if (!fZero) {
            nLastUsedByte = b + 1;
            nNonzeroBytes++;
        }
    }
    nBytes += nLastUsedByte;
}

bool CCoins::Spend(uint32_t nPos, CTxInUndo* undo)
{
    if (nPos >= vout.size() || vout[nPos].IsNull())
        return false;

    if (undo)
        *undo = CTxInUndo(vout[nPos]);

    vout[nPos].SetNull();
    Cleanup();

    if (undo && vout.empty())
    {
        undo->nHeight = nHeight;
        undo->fCoinBase = fCoinBase;
        undo->nVersion = nVersion;
    }

    return true;
}

bool CCoinsView::GetCoins(const uint256 &txid, CCoins &coins) const { return false; }
bool CCoinsView::HaveCoins(const uint256 &txid) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
bool CCoinsView::GetName(const valtype &name, CNameData &data) const { return false; }
bool CCoinsView::GetNameHistory(const valtype &name, CNameHistory &data) const { return false; }
bool CCoinsView::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const { return false; }
CNameIterator* CCoinsView::IterateNames() const { assert (false); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CNameCache &names) { return false; }
CCoinsViewCursor *CCoinsView::Cursor() const { return 0; }
bool CCoinsView::ValidateNameDB() const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins) const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid) const { return base->HaveCoins(txid); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
bool CCoinsViewBacked::GetName(const valtype &name, CNameData &data) const { return base->GetName(name, data); }
bool CCoinsViewBacked::GetNameHistory(const valtype &name, CNameHistory &data) const { return base->GetNameHistory(name, data); }
bool CCoinsViewBacked::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const { return base->GetNamesForHeight(nHeight, names); }
CNameIterator* CCoinsViewBacked::IterateNames() const { return base->IterateNames(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CNameCache &names) { return base->BatchWrite(mapCoins, hashBlock, names); }
CCoinsViewCursor *CCoinsViewBacked::Cursor() const { return base->Cursor(); }
bool CCoinsViewBacked::ValidateNameDB() const { return base->ValidateNameDB(); }

SaltedTxidHasher::SaltedTxidHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), hasModifier(false), cachedCoinsUsage(0) { }

CCoinsViewCache::~CCoinsViewCache()
{
    assert(!hasModifier);
}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

CCoinsMap::const_iterator CCoinsViewCache::FetchCoins(const uint256 &txid) const {
    CCoinsMap::iterator it = cacheCoins.find(txid);
    if (it != cacheCoins.end())
        return it;
    CCoins tmp;
    if (!base->GetCoins(txid, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
    tmp.swap(ret->second.coins);
    if (ret->second.coins.IsPruned()) {
        // The parent only has an empty entry for this txid; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coins.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoins(const uint256 &txid, CCoins &coins) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it != cacheCoins.end()) {
        coins = it->second.coins;
        return true;
    }
    return false;
}

CCoinsModifier CCoinsViewCache::ModifyCoins(const uint256 &txid) {
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    size_t cachedCoinUsage = 0;
    if (ret.second) {
        if (!base->GetCoins(txid, ret.first->second.coins)) {
            // The parent view does not have this entry; mark it as fresh.
            ret.first->second.coins.Clear();
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        } else if (ret.first->second.coins.IsPruned()) {
            // The parent view only has a pruned entry for this; mark it as fresh.
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        }
    } else {
        cachedCoinUsage = ret.first->second.coins.DynamicMemoryUsage();
    }
    // Assume that whenever ModifyCoins is called, the entry will be modified.
    ret.first->second.flags |= CCoinsCacheEntry::DIRTY;
    return CCoinsModifier(*this, ret.first, cachedCoinUsage);
}

/* ModifyNewCoins allows for faster coin modification when creating the new
 * outputs from a transaction.  It assumes that BIP 30 (no duplicate txids)
 * applies and has already been tested for (or the test is not required due to
 * BIP 34, height in coinbase).  If we can assume BIP 30 then we know that any
 * non-coinbase transaction we are adding to the UTXO must not already exist in
 * the utxo unless it is fully spent.  Thus we can check only if it exists DIRTY
 * at the current level of the cache, in which case it is not safe to mark it
 * FRESH (b/c then its spentness still needs to flushed).  If it's not dirty and
 * doesn't exist or is pruned in the current cache, we know it either doesn't
 * exist or is pruned in parent caches, which is the definition of FRESH.  The
 * exception to this is the two historical violations of BIP 30 in the chain,
 * both of which were coinbases.  We do not mark these fresh so we we can ensure
 * that they will still be properly overwritten when spent.
 */
CCoinsModifier CCoinsViewCache::ModifyNewCoins(const uint256 &txid, bool coinbase) {
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    if (!coinbase) {
        // New coins must not already exist.
        if (!ret.first->second.coins.IsPruned())
            throw std::logic_error("ModifyNewCoins should not find pre-existing coins on a non-coinbase unless they are pruned!");

        if (!(ret.first->second.flags & CCoinsCacheEntry::DIRTY)) {
            // If the coin is known to be pruned (have no unspent outputs) in
            // the current view and the cache entry is not dirty, we know the
            // coin also must be pruned in the parent view as well, so it is safe
            // to mark this fresh.
            ret.first->second.flags |= CCoinsCacheEntry::FRESH;
        }
    }
    ret.first->second.coins.Clear();
    ret.first->second.flags |= CCoinsCacheEntry::DIRTY;
    return CCoinsModifier(*this, ret.first, 0);
}

const CCoins* CCoinsViewCache::AccessCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it == cacheCoins.end()) {
        return NULL;
    } else {
        return &it->second.coins;
    }
}

bool CCoinsViewCache::HaveCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    // We're using vtx.empty() instead of IsPruned here for performance reasons,
    // as we only care about the case where a transaction was replaced entirely
    // in a reorganization (which wipes vout entirely, as opposed to spending
    // which just cleans individual outputs).
    return (it != cacheCoins.end() && !it->second.coins.vout.empty());
}

bool CCoinsViewCache::HaveCoinsInCache(const uint256 &txid) const {
    CCoinsMap::const_iterator it = cacheCoins.find(txid);
    return it != cacheCoins.end();
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::GetName(const valtype &name, CNameData& data) const {
    if (cacheNames.isDeleted(name))
        return false;
    if (cacheNames.get(name, data))
        return true;

    /* Note: This does not attempt to cache name queries.  The cache
       only keeps track of changes!  */

    return base->GetName(name, data);
}

bool CCoinsViewCache::GetNameHistory(const valtype &name, CNameHistory& data) const {
    if (cacheNames.getHistory(name, data))
        return true;

    /* Note: This does not attempt to cache backend queries.  The cache
       only keeps track of changes!  */

    return base->GetNameHistory(name, data);
}

bool CCoinsViewCache::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const {
    /* Query the base view first, and then apply the cached changes (if
       there are any).  */

    if (!base->GetNamesForHeight(nHeight, names))
        return false;

    cacheNames.updateNamesForHeight(nHeight, names);
    return true;
}

CNameIterator* CCoinsViewCache::IterateNames() const {
    return cacheNames.iterateNames(base->IterateNames());
}

/* undo is set if the change is due to disconnecting blocks / going back in
   time.  The ordinary case (!undo) means that we update the name normally,
   going forward in time.  This is important for keeping track of the
   name history.  */
void CCoinsViewCache::SetName(const valtype &name, const CNameData& data, bool undo) {
    CNameData oldData;
    if (GetName(name, oldData))
    {
        cacheNames.removeExpireIndex(name, oldData.getHeight());

        /* Update the name history.  If we are undoing, we expect that
           the top history item matches the data being set now.  If we
           are not undoing, push the overwritten data onto the history stack.
           Note that we only have to do this if the name already existed
           in the database.  Otherwise, no special action is required
           for the name history.  */
        if (fNameHistory)
        {
            CNameHistory history;
            if (!GetNameHistory(name, history))
            {
                /* Ensure that the history stack is indeed (still) empty
                   and was not modified by the failing GetNameHistory call.  */
                assert(history.empty());
            }

            if (undo)
                history.pop(data);
            else
                history.push(oldData);

            cacheNames.setHistory(name, history);
        }
    } else
        assert (!undo);

    cacheNames.set(name, data);
    cacheNames.addExpireIndex(name, data.getHeight());
}

void CCoinsViewCache::DeleteName(const valtype &name) {
    CNameData oldData;
    if (GetName(name, oldData))
        cacheNames.removeExpireIndex(name, oldData.getHeight());
    else
        assert(false);

    if (fNameHistory)
    {
        /* When deleting a name, the history should already be clean.  */
        CNameHistory history;
        assert (!GetNameHistory(name, history) || history.empty());
    }

    cacheNames.remove(name);
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn, const CNameCache &names) {
    assert(!hasModifier);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coins.IsPruned())) {
                    // Otherwise we will need to create it in the parent
                    // and move the data up and mark it as dirty
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coins.swap(it->second.coins);
                    cachedCoinsUsage += entry.coins.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY;
                    // We can mark it FRESH in the parent if it was FRESH in the child
                    // Otherwise it might have just been flushed from the parent's cache
                    // and already exist in the grandparent
                    if (it->second.flags & CCoinsCacheEntry::FRESH)
                        entry.flags |= CCoinsCacheEntry::FRESH;
                }
            } else {
                // Assert that the child cache entry was not marked FRESH if the
                // parent cache entry has unspent outputs. If this ever happens,
                // it means the FRESH flag was misapplied and there is a logic
                // error in the calling code.
                if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coins.IsPruned())
                    throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");

                // Found the entry in the parent cache
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.coins.swap(it->second.coins);
                    cachedCoinsUsage += itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                    // NOTE: It is possible the child has a FRESH flag here in
                    // the event the entry we found in the parent is pruned. But
                    // we must not copy that FRESH flag to the parent as that
                    // pruned state likely still needs to be communicated to the
                    // grandparent.
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    hashBlock = hashBlockIn;
    cacheNames.apply(names);
    return true;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, cacheNames);
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    cacheNames.clear();
    return fOk;
}

void CCoinsViewCache::Uncache(const uint256& hash)
{
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && it->second.flags == 0) {
        cachedCoinsUsage -= it->second.coins.DynamicMemoryUsage();
        cacheCoins.erase(it);
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    // Do not take name operations into account here.
    return cacheCoins.size();
}

const CTxOut &CCoinsViewCache::GetOutputFor(const CTxIn& input) const
{
    const CCoins* coins = AccessCoins(input.prevout.hash);
    assert(coins && coins->IsAvailable(input.prevout.n));
    return coins->vout[input.prevout.n];
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += GetOutputFor(tx.vin[i]).nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins* coins = AccessCoins(prevout.hash);
            if (!coins || !coins->IsAvailable(prevout.n)) {
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::GetPriority(const CTransaction &tx, int nHeight, CAmount &inChainInputValue) const
{
    inChainInputValue = 0;
    if (tx.IsCoinBase())
        return 0.0;
    double dResult = 0.0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        const CCoins* coins = AccessCoins(txin.prevout.hash);
        assert(coins);
        if (!coins->IsAvailable(txin.prevout.n)) continue;
        if (coins->nHeight <= nHeight) {
            dResult += (double)(coins->vout[txin.prevout.n].nValue) * (nHeight-coins->nHeight);
            inChainInputValue += coins->vout[txin.prevout.n].nValue;
        }
    }
    return tx.ComputePriority(dResult);
}

CCoinsModifier::CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage) : cache(cache_), it(it_), cachedCoinUsage(usage) {
    assert(!cache.hasModifier);
    cache.hasModifier = true;
}

CCoinsModifier::~CCoinsModifier()
{
    assert(cache.hasModifier);
    cache.hasModifier = false;
    it->second.coins.Cleanup();
    cache.cachedCoinsUsage -= cachedCoinUsage; // Subtract the old usage
    if ((it->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
        cache.cacheCoins.erase(it);
    } else {
        // If the coin still exists after the modification, add the new usage
        cache.cachedCoinsUsage += it->second.coins.DynamicMemoryUsage();
    }
}

CCoinsViewCursor::~CCoinsViewCursor()
{
}
