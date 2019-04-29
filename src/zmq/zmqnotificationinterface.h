// Copyright (c) 2015-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
#define BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H

#include <validationinterface.h>
#include <zmq/zmqgames.h>

#include <string>
#include <map>
#include <memory>
#include <list>

class CBlockIndex;
class CZMQAbstractNotifier;
class ZMQGameBlocksNotifier;

class CZMQNotificationInterface final : public CValidationInterface
{
public:
    virtual ~CZMQNotificationInterface();

    std::list<const CZMQAbstractNotifier*> GetActiveNotifiers() const;

    static CZMQNotificationInterface* Create();

    inline TrackedGames* GetTrackedGames() {
        return trackedGames.get();
    }

    inline ZMQGameBlocksNotifier* GetGameBlocksNotifier() {
        return gameBlocksNotifier;
    }

protected:
    bool Initialize();
    void Shutdown();

    // CValidationInterface
    void TransactionAddedToMempool(const CTransactionRef& tx) override;
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexConnected, const std::vector<CTransactionRef>& vtxConflicted, const std::vector<CTransactionRef>& vNameConflicts) override;
    void BlockDisconnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexDelete, const std::vector<CTransactionRef>& vNameConflicts) override;
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;

private:
    CZMQNotificationInterface();

    void *pcontext;
    std::list<CZMQAbstractNotifier*> notifiers;

    /**
     * The game blocks notifier, if any.  This is used to send on-demand
     * notifications for game_sendupdates.
     */
    ZMQGameBlocksNotifier* gameBlocksNotifier;

    /** The tracked games for notifications.  */
    std::unique_ptr<TrackedGames> trackedGames;
};

extern CZMQNotificationInterface* g_zmq_notification_interface;

#endif // BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
