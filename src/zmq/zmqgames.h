// Copyright (c) 2018-2019 The Xaya developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQGAMES_H
#define BITCOIN_ZMQ_ZMQGAMES_H

#include <sync.h>
#include <zmq/zmqpublishnotifier.h>

#include <set>
#include <string>
#include <vector>

class CBlock;
class CBlockIndex;
class UniValue;

/**
 * Helper class to manage the list of tracked game IDs.
 */
class TrackedGames
{

private:

  /** The set of tracked game IDs.  */
  std::set<std::string> games GUARDED_BY (cs);

  /** Lock for this instance.  */
  mutable CCriticalSection cs;

  friend class ZMQGameBlocksNotifier;

public:

  TrackedGames () = delete;
  TrackedGames (const TrackedGames&) = delete;
  void operator= (const TrackedGames&) = delete;

  explicit TrackedGames (const std::vector<std::string>& g)
    : games(g.begin (), g.end ())
  {}

  UniValue Get () const;

  void Add (const std::string& game);
  void Remove (const std::string& game);

};

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

  /** Reference to the list of tracked games.  */
  const TrackedGames& trackedGames;

  /**
   * Sends a multipart message where the payload data is JSON.
   */
  bool SendMessage (const std::string& command, const UniValue& data);

public:

  ZMQGameBlocksNotifier () = delete;
  ZMQGameBlocksNotifier (const ZMQGameBlocksNotifier&) = delete;
  void operator= (const ZMQGameBlocksNotifier&) = delete;

  explicit ZMQGameBlocksNotifier (const TrackedGames& tg)
    : trackedGames(tg)
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

};

#endif // BITCOIN_ZMQ_ZMQGAMES_H
