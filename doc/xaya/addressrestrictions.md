# Address Restrictions

**This proposal is obsolete, as trading should use
[atomic name updates](trading.md) instead.  It is kept here for archival
purposes only.**

This document describes a proposed change to the network consensus rules
leading to a *soft fork* of the Xaya blockchain.  The new rules allow
placing **restrictions on receiving CHI on certain addresses**.  This
facilitates building in-game market places.

## Overview

Xaya games [can react to voluntary transfers of CHI](games.md#currency), but
they cannot force transfers.  This still allows an individual game to
include a market place for trading assets with CHI within its rules:
Players can list items by specifying a price and their address, and then
the game rules specify that the item is transferred to a buyer if the price
is paid to the given address.  This is fully decentralised and trustless,
based on atomic transactions.

There is one potential issue with this scheme, though:  If a sold item
is unique, it could happen that two prospective buyers send CHI at roughly
the same time, but only one of them can get the item in the game state.
The other one would have to be refunded, but that can only be done
voluntarily by the seller and not in a trustless way.  A potential solution
here is to introduce *reservations* of items:  When an item is for sale
on the in-game market place and Alice wants to buy it, she first sends a
transaction that does not yet transfer any CHI but signals her intent to
buy the item.  After confirmation, the item is reserved for her for a certain
time (e.g. 100 blocks).  During that time, Alice can then transfer the CHI
and be sure that she will get the item in exchange.  Any other prospective
buyer would see that the item is reserved for Alice, and thus won't send any
CHI that would have to be refunded.

Reservations work, but they are cumbersome to implement, require unnecessary
transactions in the blockchain and open up a venue for potential abuse and
DoS attacks on a game's market place.  Thus, we propose an alternative solution:

**The consensus rules of the Xaya network should allow sellers of items to
specify that they want to receive only a single CHI transaction at their
payment address.  All further payments will be invalid transactions.
This will allow games to implement simple market places without having
to rely on reservations (or refunds).**

Such optional *address restrictions* can be imposed with a soft fork of
the Xaya network.  Before going into the detailed specification below,
here is a brief overview of how those restrictions will work:

* The chain state of Xaya Core will keep track of active restrictions.
* Each restriction consists of a script (address) on which it is placed,
  an expiration block height and the actual restriction data (e.g. how many
  transactions are allowed or how much CHI can be sent in total).
* All restrictions are required to expire with a certain maximum
  time-to-live.  This allows to remove them from the chain state again
  in a timely manner.
* Restrictions are placed by updating a name with certain JSON in its
  value.  The restriction is put on *the address that held the name
  before the update*.  This ensures that *only the owner of an address can
  restrict it*.
* Address restrictions *do not apply to name transactions* sent to
  the address.  In other words, the locked 0.01 CHI in a name's
  "coloured coin" are not counted towards the limits.
* Name updates with JSON for an invalid restriction or transfers of CHI
  that violate an active restriction are invalid transactions and thus not
  allowed to be confirmed in blocks or put into the mempool of nodes.

## Detailed Restriction Rules

Each address restriction in the chain state consists of the following data:

* A **script** that determines the restricted "address" (but also non-address
  scripts can be used)
* The **block height** `E` at which the restriction ends
* The actual data about the restriction.  This can be one or more of the
  following individual limits, which are *all* imposed together
  (as a logical *AND*):
  * Maximum number `N` of transactions to the address
  * Maximum amount `A` of CHI to be received
  * Minimum amount `s` of CHI sent in any single transaction

Restrictions are enforced from the *block after when they are confirmed* up
to and including the block at height `E`.

For a new block that is validated and attached, all transactions are
processed in order.  For each one, the following checks and updates to
the chain state are performed:

1. For each output of the transaction, look up in the chain state if there
   is a restriction matching the *exact* `scriptPubKey` it sends to.
   * This does not strip any name prefixes, which also means that sending names
     to a restricted address is fine since the output `scriptPubKey` will be
     different (including a name prefix).
1. If there is a matching restriction, all of the following checks must
   pass (for the limits that are actually part of the restriction)
   or otherwise the transaction is invalid:
   * The restriction's `N` value must be greater than zero
   * The amount sent in the output must not be greater than `A`
   * The amount sent in the output must be at least `s`
1. After verifying each output, the restriction is updated in the tentative
   chain state as follows:
   * `N` is decreased by one
   * `A` is decreased by the output's amount

## Creating Restrictions

Address restrictions are created if a *name update* operation
(registrations are not able to create restrictions)
in the current block contains the field `addressRestriction` in its JSON
value (at the top-level object).  This field must be an object with at
least one of the following fields:

* **`maxTx`**: A strictly positive integer specifying `N`
* **`maxAmount`**: A strictly positive integer specifying `A` as a number
  of CHI satoshis
* **`minAmount`**: A strictly positive integer specifying `s` as a number
  of CHI satoshis

In addition, the field **`ttlBlocks`** must be set to a strictly positive
integer that is *at most 1,000*.
**(TODO: Specify the actual maximum TTL we want to allow!)**

The restricted `scriptPubKey` corresponding to such a transaction is the
script associated to the **name input** of the transaction, **with the name
prefix removed**.  In other words, it is the address that held the name
before the update.

Any name operation (including registrations) that contains a
`addressRestriction` field in its top-level JSON object is valid
only if:

1. It is a name update and not a name registration.
1. Its format matches the description above:
   * It contains a `ttlBlocks` field.
   * It contains one or more of the `maxTx`, `maxAmount` and `minAmount` fields.
   * It contains no other fields.
   * All fields have values that are valid according to the descriptions above.
1. If the restriction specifies `A` and `s`, then `s <= A` must be the case.
1. There is no currently active restriction on the associated `scriptPubKey`.
   * This includes restrictions created by previous transactions in the
     same block.  While each name can only be updated once per block, it would
     otherwise be possible to place a restriction on the same script with
     updates to two different names (held at the same address).

If such a valid transaction is confirmed at block height `B`, then an associated
address restriction is created in the chain state *after processing the
containing block* and with `E = B + ttlBlocks`.

## Usage Examples

Let us now discuss some typical examples of situations in which
address restrictions could be used.

### Selling a Single, Unique Item

When Alice wants to list a single, unique item for sale in a game's
market place, she can use an address restriction to ensure that only
one payment can be made to her.  To do so, she would create a restriction
with `N = 1` and `s = P`, where `P` is the listed price of the item.

Setting `N` ensures that exactly one transaction can be sent to her.
But without setting also `s`, it would be possible for someone to send
a very small amount to Alice's address and thus "block" the address for
future payments, effectively cancelling her market offer.

### Selling Multiple Items

If Bob wants to sell multiple (up to `C`) items of the same type for a
price of `P` each, he can set `A = C * P` and `s = P`.  This makes sure that
one or multiple sellers can buy the items either at once or in smaller
chunks, up to at most all `C`.  As in the previous example, the minimum
size restriction `s` ensures that very small transfers cannot be used
to mess with the system.

In theory, however, it is still possible for someone to send a little more
than `P` to Bob.  This would then buy one of the items and leave Bob with
a little extra change.  It would also mean that in the end, Bob may have his
address fully blocked with some of the items still available for sale.
But it is ensured that each transaction buys at least one item, so that
any "attacks" on the offer cost a non-negligible amount of money.

### Selling a Divisible Good

The third potential type of order on an in-game market place is selling
of *divisible goods* like in-game currency.  Let's say that, similar to the
previous example, a total of `C` is available for sale at a unit price
of `P`.  For such an order, Chalie would again set `A = C * P` to ensure
that at most the available quantity is sold.  He should also set `s` to
some amount, corresponding to the minimum amount sold in one chunk.
This time, however, that amount is up for him to choose.
