# Interface of the Xaya Core Daemon for Games

The main interface of the Bitcoin Core daemon is through the various
provided [JSON RPC](http://www.jsonrpc.org/) methods as well as
[ZeroMQ](http://zeromq.org/) for
[notifications](https://github.com/bitcoin/bitcoin/blob/master/doc/zmq.md).

Xaya inherits these interfaces.  However, Xaya focuses on providing
the backbone for individual [game engines](games.md).  Thus it makes sense
to also provide an **additional interface that is optimised for this
purpose**, tailored specifically for the Xaya [game model](games.md).

## Sending Moves

For **sending moves** to the daemon (the "write" side of the interface),
the basic RPC methods inherited and adapted from Namecoin should be used:

    $ name_register p/name {}
    $ name_update p/name {"g":{"chess":"e4"}}
    $ name_update p/name {} {"destAddress":"CaQb9k5Amibwjuhbfd4bqdwycBMK95Mw8n"}

## Keeping the Game State Up-to-Date

The most important and fundamental task of each game engine is to keep the
current game state updated with the Xaya blockchain.  For this, it must
process moves from attached and detached blocks as discussed in the
Xaya [game model](games.md).

### Attaching and Detaching Publishers <a name="attach-detach"></a>

The Xaya daemon provides [ZeroMQ publishers](http://zeromq.org/) for
**attached and detached blocks**.  They provide subscribers with all
the required information to update game states.

For this, the daemon can be configured to **track a list of game IDs**.
These are all the games that the user is running.  Then, for each block
that is attached to the blockchain *and each tracked game*, the daemon sends
out a **`game-block-attach`** message that contains all the information
necessary for the corresponding game engine to step forward in time.

For the actual message, we reuse the multi-part format introduced by
[Bitcoin Core's ZeroMQ
interface](https://github.com/bitcoin/bitcoin/blob/master/doc/zmq.md).
In particular, the message is a
[ZeroMQ multipart message](http://api.zeromq.org/3-2:zmq-msg-send)
of the following format:

    game-block-attach json GAMEID|DATA|SEQ

Here, `|` denotes the boundary between distinct message parts.  The first
part is the **command string**.  It allows each game engine to subscribe to
`game-block-attach json GAMEID`
in order to receive exactly the updates relevant to it.
`json` denotes the format that is used; JSON is the only available format
for now, but more might be defined in the future.
`SEQ` is a **sequence number** encoded as *little-endian 32-bit integer*, which
counts the number of messages sent already for *a particular command string*
(including the game ID).  This allows receivers to detect missed messages.

The `DATA` part, finally, is a JSON object with the relevant information:

    {
      "block":
        {
          "hash": ATTACHED-BLOCK-HASH,
          "parent": PREVIOUS-BLOCK-HASH,
          "height": BLOCK-HEIGHT,
          "timestamp": BLOCK-TIME,
          "rngseed": RNG-SEED,
        },
      "cmd": ADMIN-COMMAND,
      "moves":
        [
          {
            "txid": TXID,
            "name": UPDATED-NAME,
            "move": MOVE,
            "out":
              {
                ADDRESS1: AMOUNT1,
                ADDRESS2: AMOUNT2,
                ...
              },
          },
          ...
        ],
    }

The placeholders have the following meaning:

* **`ATTACHED-BLOCK-HASH`:**
  The hash of the newly-attached block, i.e. the block that contains all the
  moves listed below.
* **`PREVIOUS-BLOCK-HASH`:**
  The hash of the previously-current block, i.e. the block on top of which
  the new one is attached.
* **`RNG-SEED`, `BLOCK-HEIGHT` and `BLOCK-TIME`:**
  Additional data about the attached block, which might be used by the game
  engine in the update logic.
* **`ADMIN-COMMAND`:**
  If the block contains an update the name game's `g/` name, then this
  field is set to the game-specific [*admin command*](games.md#games)
  specified with that update.
* **`TXID`:**
  The Xaya transaction ID of the transaction that performed the given move.
  This is mostly useful as a key and to correlate a single transaction
  through different endpoints of the API (if necessary).
* **`UPDATED-NAME`:**
  The account name that performed a move, without the `p/` prefix.
* **`MOVE`:**
  The actual [move data](games.md#moves), as it is given in `.g[GAMEID]`
  of the name update's value.
* **`ADDRESS`n and `AMOUNT`n:**
  Xaya addresses and amounts that were transacted in the move transaction,
  as described in the model for
  [currency transaction in games](games.md#currency).

Note that not all transactions from the block are included in the `moves` list.
It contains only those that are relevant for the current game, which are
all **name updates and registrations that mention the game ID in their value**.

Similarly, the daemon also provides a **`game-block-detach`** message for blocks
that are detached during a reorg:

    game-block-detach json GAMEID|DATA|SEQ

In this message, `DATA` is exactly the same data that was sent previously
when the same block was attached.  This means that `DATA.hash` is the hash
of the block being detached and `DATA.parent` is the block that will be current
after the detachment.

The game engine must [undo](games.md#undoing) the
block by either restoring the game state corresponding to `DATA.parent`
from its archive, or backwards-processing `DATA.moves` to go from the
game state of `DATA.hash` back to that of `DATA.parent`.

### Basic Operation <a name="up-to-date-operation"></a>

The typical mode of operation is that the game engine's current state
corresponds to the tip of the current Xaya blockchain.  In this case,
whenever a new block comes in and `game-block-attach` is published,
`DATA.parent` equals the block associated with the current game state.
Similarly, for `game-block-detach` during a reorg, `DATA.hash` is exactly
the block hash for the current game state.

For these cases, the game engine can simply process the incoming message
to update its game state accordingly.  This allows it to keep up-to-date
with the Xaya blockchain in real time.

**NOTE:**  Notifications sent because of genuine changes in the best chain
will not contain a `reqtoken` field (unlike notifications that were
[explicitly requested](#requested-updates)).  **Notifications with a `reqtoken`
field should normally be ignored unless they were explicitly requested!**

### Recovering from Out-of-Sync State <a name="requested-updates"></a>

However, the current state of an engine may go
out-of-sync with the Xaya daemon.  This could be because the game engine was
not running for some time even though the daemon was, and it missed some
block attach and detach operations.  This situation also occurs when the engine
for a new game is installed and attached to the Xaya daemon for the first time
and needs to do an initial sync.

If the game engine determines it is out-of-sync (for instance, because it
received a `game-block-attach` message with a `DATA.parent` block hash that
does not match its game state), it can explicitly request the updates it needs
to be resent through RPC:

    $ game_sendupdates GAMEID FROM-BLOCK [TO-BLOCK]

`FROM-BLOCK` should be the block hash that is associated with its current game
state (it can be the genesis block, known to correspond to an initial game
state, for a full sync).  If given, `TO-BLOCK` is the block hash that the
game wants to update to; it can be omitted, in which case it is assumed to be
the current tip of the blockchain.

If the Xaya daemon knows both block hashes and there is a sequence of block
attachments and detachments that bring `FROM-BLOCK` to `TO-BLOCK`, the RPC will
immediately return success and trigger sending those updates
in the background (through the same `game-block-attach` and `game-block-detach`
notifications that would be sent during
[normal operation](#up-to-date-operation)).
The RPC itself will return a JSON object that contains various information
about the updates that have been triggered.  In particular, these fields
will be returned:

* **`toblock`**:  The hash of the target block to which notifications have
  been triggered.  This will be some block hash "between" `FROM-BLOCK` and
  `TO-BLOCK` (if it was set) or the current best tip.
* **`ancestor`**:  The block hash of the last common ancestor of `FROM-BLOCK`
  and `TO-BLOCK`.  This can be useful for game engines to decide whether to
  roll the detached blocks backwards or instead look up a cached game
  state for the common ancestor and only process all attached blocks
  forward from there on.
* **`reqtoken`**:  A unique string for this request, which will be included in
  the `reqtoken` field of all notifications triggered as a result of this call.
  This can be used to distinguish notifications that are sent due to changes
  in the best chain (which won't have a `reqtoken` field) from notifications
  sent due to this `game_sendupdates` call.

If the requested block hashes are
unknown or no valid sequence can be found, the RPC returns an error.  In that
case, the game engine can try to recover by requesting updates from an
older state that it has in its archive, or by syncing from scratch in the
worst case.

For cases where the sequence of updates for the full request is very long
(e.g. when a game is synced from scratch), Xaya Core may decide to send only
a part of the updates.  In that case, the value of `toblock` returned from
the RPC indicates to which target block updates have been triggered.  Once
those have been received, the game daemon should send another `game_sendupdates`
request for the remaining blocks and continue to do so until it has arrived
at its desired target block.

**NOTE:** After sending a `game_sendupdates` request, a game engine should only
process notifications with the corresponding `reqtoken` until it is up-to-date
with the returned `toblock`.  From then on, it can resume
[normal operation](#up-to-date-operation).

#### Newly Created Games

A special case is that of a *completely new* game, which did not
exist before a certain block.  In this case, the game engine can hardcode
a block hash known to be before the start of the game, so that it only
requests updates from that block onwards on the initial sync.  This avoids
processing potentially years of old blocks known to be irrelevant.

The Xaya daemon can partially optimise this process itself:
For all blocks that are requested and known to be **before the game's `g/` name
was registered**, it can just send a message without any moves.  This makes
it possible to create the messages just from the in-memory tree of block headers
without the need to load and process full blocks from disk.

(However, this requires the daemon to keep a record of the registration of each
game ID.  It can only be enabled if `-namehistory` is turned on, because then
this information is readily available.)

## Name Ownership

Besides updating the game state itself, games engines where users can
actively play will likely also need to know which names the user owns,
i.e. has the private keys for in their wallet.  This can also change, as
names can be sent to or from the user's wallet.
Thus, it is necessary to provide also an interface that allows game
engines to inquire and stay up-to-date with the list of the user's names.

The Xaya daemon's interface provides two complementary methods for this.

First, the RPC method `name_list` inherited from Namecoin can be used to
request the **full list of names owned by the user**.  This allows the game
to get up-to-date immediately, for instance, at startup.
(Also `name_pending` may be relevant to inquire about pending operations
in the node's mempool.)

Second, the daemon exposes the **`player-ownership`** ZeroMQ publisher that
gets notified whenever *any `p/` name* changes its ownership status.
(This means that it was previously owned by the wallet and now it isn't,
or that it was not owned before and is now associated with
an address in the wallet.
It does not include address changes of *any* name, as this would be almost
every name update in the blockchain.)
Whenever a name update changes the ownership status of a name or a new name
owned by the wallet is registered, one of the following notifications is sent:

    player-ownership json pending|DATA|SEQ
    player-ownership json confirmed|DATA|SEQ

The "pending" notification is sent as soon as a relevant *unconfirmed*
transaction is seen, e.g. added to the mempool.  The "confirmed" notification
is sent when an ownership change has been confirmed.  The `DATA` value is
a string encoding a JSON object of the following form:

    {
      "txid": TXID,
      "name": NAME,
      "state": STATE,
    }

The placeholders have the following meaning:

* **`TXID`**:
  The Xaya transaction ID of the name update.
* **`NAME`**:
  The account name that changed ownership, without the `p/` prefix.
* **`STATE`**:
  A string indicating the name's state.
  This can be `"own"`, `"foreign"` or `"unregistered`".
  `"own"` indicates that the wallet holds the key for this name.
  `"foreign"` means that the name is *not* owned by the wallet.
  `"unregistered`" is for special situations during a reorg (see below).

If a name update changes ownership, *typically* first a
`player-ownership pending` notification will be sent when the transaction
is added to the mempool.  Later, when it is confirmed, a matching
`player-ownership confirmed` is sent.  It may, however, happen that the
transaction is only seen in the block confirming it, in which case only the
"confirmed" notification occurs.

When a block containing such a transaction is detached during a reorg,
a `player-ownership confirmed` notification is sent for the previous
ownership state; if the initial registration of the name is detached, the
state will be `"unregistered"`.  If the now-unconfirmed name transaction
is re-added to the mempool, a matching `player-ownership pending` notification
(for the new state) is also sent.

## Pending Moves

Games may also want to be notified about moves as soon as possible, even
if they are still unconfirmed.  This allows them to show, for instance,
a "forecast" of what other players will likely do in the future.
For this, the **`game-pending-move`** ZeroMQ publisher is exposed.  Whenever
a name operation referencing a game is added to the mempool (including when
it is re-added after a block detach), the following notification is sent
*for each tracked game*:

    game-pending-move json GAMEID|DATA|SEQ

`DATA` is a description of the move in the same form as in the `moves` array
for [`game-block-attach` notifications](#attach-detach).
