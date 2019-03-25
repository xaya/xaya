// Copyright (c) 2018-2019 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQGAMES_H
#define BITCOIN_ZMQ_ZMQGAMES_H

#include <sync.h>
#include <zmq/zmqpublishnotifier.h>

#include <set>
#include <string>

class CBlock;
class CBlockIndex;
class UniValue;

/**
 * ZMQ publisher that handles the attach/detach messages for the Xaya game
 * interface (https://github.com/xaya/Specs/blob/master/interface.md).
 */
class ZMQGameBlocksNotifier : public CZMQAbstractPublishNotifier
{

public:

  static const char* PREFIX_ATTACH;
  static const char* PREFIX_DETACH;

private:

  /** Lock for trackedGames.  */
  mutable CCriticalSection csTrackedGames;
  /** The set of games tracked by this notifier.  */
  std::set<std::string> trackedGames GUARDED_BY (csTrackedGames);

  /**
   * Sends a multipart message where the payload data is JSON.
   */
  bool SendMessage (const std::string& command, const UniValue& data);

public:

  ZMQGameBlocksNotifier () = delete;

  explicit ZMQGameBlocksNotifier (const std::set<std::string>& games)
    : trackedGames(games)
  {}

  /**
   * Sends the block attach or detach notifications.  They are essentially the
   * same, except that they have a different command string.
   */
  bool SendBlockNotifications (const std::set<std::string>& games,
                               const std::string& commandPrefix,
                               const std::string& reqtoken,
                               const CBlock& block);

  bool NotifyBlockAttached (const CBlock& block) override;
  bool NotifyBlockDetached (const CBlock& block) override;

  /* Methods for the trackedgames RPC.  */
  UniValue GetTrackedGames () const;
  void AddTrackedGame (const std::string& game);
  void RemoveTrackedGame (const std::string& game);

};

#endif // BITCOIN_ZMQ_ZMQGAMES_H
