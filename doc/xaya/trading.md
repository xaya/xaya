# Atomic Trading

The ability to **trade in-game assets** (fungible game currencies as well as
non-fungible items) is an integral part of many blockchain games.  Thus,
it is important to support such trading also on the Xaya platform.
Of course, trades can be implemented easily in various ways depending
on trust:  Seller or buyer can send first and trust that the other party will
also send, or centralised exchange platforms can be used.  In this document,
we want to describe possible ways to implement **trustless** trading
using **atomic transactions** instead.

## Overview

The basic setting is as follows:  A *seller* owns some assets in the
game state of some application on the Xaya platform (or perhaps provides
services).  A *buyer* wants to buy those assets (or hire services),
**paying directly in CHI**.  (Trades and payment in currencies or assets
belonging to the game state itself can be implemented easily simply by designing
the game rules accordingly.  Only trades between CHI and game assets
are harder to do and will thus be discussed here.)

This can be accomplished in a **trustless manner** through an
*atomic transaction*:  Roughly speaking, buyer and seller cooperate to
build a single transaction together, which performs the desired update in
the game state (e.g. sending assets from seller to buyer)
*while at the same time* sending CHI from buyer to seller.
By doing that in a single transaction, it is guaranteed that either both
effects take place (buyer and seller get what they want) or none (in which
case noone of them lost anything).  It is not possible at all for one
party to defraud the other, e.g. for the seller to get CHI without
transferring the sold assets to the buyer.
Atomic transactions can be compared to
[CoinJoin's](https://en.bitcoin.it/wiki/CoinJoin) in Bitcoin.

There a multiple ways in which such an atomic transaction can be
structured, and different usecases in games to which they fit.
Those will be discussed in detail below.

## Types of Atomic Transactions

Let us start by considering different structures (on the technical level)
for atomic transactions.  Each one has different potential usecases, which
[will be described below](#usecases).

### <a name="game-state-reacts">Game State Reacts</a>

The simplest type of transaction is one where the buyer simply sends
a game move indicating their desired purchase and transfers CHI to
a pre-determined address of the seller.
The game rules can then be defined in such a way that they
[react properly to such payments](games.md#currency).

For instance, the game rules could define that anyone sending `x` CHI
to the game developer's address `Cabc` gets `x` pieces of gold in the game.
Then it is guaranteed for a buyer that they will get their gold pieces as long
as they send CHI.

This approach is very simple, but it is best suited to situations where
the offer is not cancellable and *unlimited in quantity*.  In a situation
where the seller has exactly one magic sword to sell, it could happen that
two prospective buyers send CHI at the same time.  Then the game state would not
be able to honour both purchases.
(There are ways to work around this by means of "reserving" offers first,
but they are cumbersome.)

### <a name="ant">Atomic Name Trades</a>

The second type of transaction is an
[*atomic name trade*](https://wiki.namecoin.info/?title=Atomic_Name-Trading)
as first described in the context of Namecoin.
In such a transaction, an **entire Xaya name with all attached assets**
in all games on the platform is sold.

This can be done as follows:  Buyer and seller together construct
a transaction of the following structure:

- The seller provides the name's current transaction output as input for
  the transaction.

- The seller also adds an output sending `x` CHI to an address he owns.

- The buyer adds an output updating the name to an address *of hers*.

- The buyer provides CHI inputs to the transaction providing at least `x` CHI
  (for the seller's output).  She can also add a change output as needed.

Then both parties check that the transaction has the desired structure for
the agreed deal and sign them (each party their associated inputs).  When
broadcast, this transaction will atomically transfer both the name and
the price for it.  If either seller or buyer double spend their inputs
before the transaction is confirmed, then neither effect takes place.

### <a name="anu-bids">"Bid" Atomic Name Updates</a>

*Atomic name updates* (ANUs) are very similar to [atomic name trades](#ant),
but they only *update the name's value* **without transferring it**.
In the simplest form, this allows prospective buyers to bid on assets
that someone else owns.  In particular, they can propose a contract
of the following form:

> If you update your name `p/seller` to the value
> `xyz`, then I will pay you `x` CHI.

In a typical situation, `xyz` would be a game-specific [move](games.md#moves)
that transfers the desired asset to the buyer in the game state.

To construct such a transaction, the buyer needs to do the following:

- Add the current output of the name as input (even though she does not have
  the keys to sign it).

- Add an output that updates the name to the desired value `xyz`, while keeping
  it at the same address (owned by the seller).

- Add an output of `x` CHI also the seller's address (e.g. which holds the
  name at the moment).

- Fund the transaction with her own CHI inputs and add a change output
  of hers as needed.

- Sign the inputs belonging to her (i.e. all except for the name input).

Then she can send that partially signed "bid" to the seller in some way
(on-chain or through some external communication method).  If the seller
accepts the offer, he simply needs to sign the name input and broadcast the
transaction.

Note that there is no interactive communication needed, it is enough to just
send the bid transaction from buyer to seller once.  In the end, only one
on-chain transaction needs to be made if the trade is accepted by the
seller; if it is not accepted, then all interaction can be purely
off-chain.

Bids of this form are especially useful to make offers for buying
non-fungible assets.  For general trades of in-game currency, they are
not perfectly applicable.  The reason for that is that each bid is specific
to one seller, so a prospective buyer cannot simply bid for buying
gold pieces from anyone.

### <a name="anu-asks">"Ask" Atomic Name Updates</a>

Similarly to [ANU bids](#anu-bids), sellers can themselves
propose to update their name to a certain value if anyone is willing to send
them `x` CHI for that.  In other words, they can propose an offer like this:

> Anyone is able to update my name `p/seller` to the value `xyz`, provided
> that they pay `x` CHI to my address that owns the name.

Constructing a partially signed transaction that represents such an ask is
a bit trickier than constructing a partially signed bid, since the inputs
of the buyer used to fund it cannot be known to the seller when constructing
the transaction.  This can still be achieved using special
[signature
flags](https://raghavsood.com/blog/2018/06/10/bitcoin-signature-types-sighash),
though:

- The seller adds his name as input for the transaction.

- The seller adds an output, which updates the name to `xyz` and sends
  `x` CHI *more than the amount currently held in the name coin* into the
  name's output.

- The seller then signs the input using the flags `SINGLE | ANYONECANPAY`.

The signature flags used imply that the signature stays valid even if more
inputs and outputs are added to the transaction, provided that the seller's
output stays untouched.  This ensures that the name cannot be stolen from
him and that he will get `x` CHI as payment (sent directly into the name
output).

A prospective buyer who wants to accept such an offer now has to do the
following:

- She needs to add inputs of her own to cover `x` CHI and any potential
  transaction fees.

- She may add a change output as needed.

- She then has to sign her inputs and broadcast the transaction.

Since the payment is sent into the seller's name output, it won't show up
immediately in the seller's wallet.  It can, however, be released any time
by him when updating the name again.  (In fact, it will automatically be
released by a standard `name_update`.)

Unfortunately, this type of transaction has a major caveat as it is:  The value
`xyz`, to which the name is updated, is fixed when creating the ask.  This
means that it is not possible for a buyer to specify that, for instance,
some item should be sent *to her* in the game.
The game rules can still be designed in such a way as to enable useful
trades, though.  This will be [discussed later](#offline-asks-anu).

### <a name="sentinel-inputs">Sentinel Inputs</a>

A variant of [ANU asks](#anu-asks) are *sentinel inputs*:
Here again a prospective seller provides a pair of one input and output
signed with `SINGLE | ANYONECANPAY`.  In contrast to the former, though,
the input / output pair does not involve names at all:

- The seller adds any currency input he owns into the transaction.

- He then adds a matching currency output, paying the same amount back to
  himself.

- He signs the input with `SINGLE | ANYONECAYPAY`.

As before, this partially-signed transaction allows anyone to add more inputs
and outputs.  It ensures, though, that the seller will always get back his
CHI and none can be stolen.

This transaction by itself does not do much useful.  It can, however, be used
to ensure **uniqueness of payments of CHI** to the seller (for unique / limited
items):  Everyone can include the input / output pair in their transaction
(e.g. when buying an item).  But Xaya's coin tracking rules ensure that the
sentinel input can be spent by only one transaction at most, so that
only one such buying transaction can ever be valid.
This can be used in conjunction with suitable
game-state rules to enable trading as [described later](#offline-asks-sentinel).

## <a name="usecases">Different Usecases for Trading</a>

Next we discuss how the various transaction types can be applied
to solve some common usecases of trading in games.

### Buying Unlimited Assets or Services

Perhaps the simplest usecase is that of selling some unlimited asset
or service.  This situation may be because the developer of the game is
selling something (which is unlimited in quantity) for CHI.  Or it could
be a guard "selling" the right for safe passage to players.

In such a situation, trading should simply be done by
[coding the game rules accordingly](#game-state-reacts).  In other words,
there will be rules in the game logic that state something like this:

> Anyone sending `x` CHI to my address `Cabc` will get `x` gold pieces
> in return.

Or perhaps:

> Any player who sent `x` CHI to `Cabc` in the last 100 blocks will not be
> attacked when walking by me.

The exact nature of the offer can either be hard-coded into the game
(e.g. for selling items by the developers), or it may be configurable
through moves the seller sends (e.g. for setting a price for safe passage).

In both situations, the buyer can simply transfer CHI as specified, and the
game backend will [see the transaction](games.md#currency)
and react accordingly.

### Interactive Trades

The first approach for trading with limited quantities is using
some interactive communication between buyer and seller to negotiate
and build an atomic transaction.
For instance, the market place could be built upon messages exchanged
through IRC, XMPP, a P2P network, a direct network connection or even
forum posts.

If such an interactive communication is possible, then buyer and seller
will first agree to a particular deal.  (Probably this involves publishing
offers by one of the parties initially, and the other party observing the
offers and then contacting the counterparty privately to finalise the trade.)
After the desired trade is known, both parties can simply build an atomic
transaction in the way of an [ANU bid](#anu-bids) and execute the trade
through it.

Where possible, using interactive communication is certainly desirable
since it makes building the atomic transaction simple, flexible and effective
(in every case, only a single on-chain transaction is required).
The big drawback of this approach is that both parties need to be
online at the same time (or the trade may take a long time to finalise).
Note that executing a previously defined order can be done by an automated
script integrated with a player's game wallet, though, so the human player
need not be involved actively.

### Offline Bids and Auctions

If interactive communication is not easily possible, then it is still
easy for prospective buyers to make offline **bids** to sellers
as [ANU bids](#anu-bids).  Those partially signed bid transactions can
then be sent to the seller through some communication channel, and then require
no further interaction between both parties afterwards (i.e. the seller
can then accept and broadcast or reject the offer just by himself).

This also enables selling of some item through an *auction*:
Bidders can simply prepare such partially signed transactions and send them
to the seller.  The latter can then publish them (or the highest bid so far),
and can at any time (e.g. when the auction is over) accept the highest bid
to sell the item.

Note that with this scheme, the seller has the final decision which offer
(if any) to accept.  This means that he may choose to not sell the item
after all, or that he may sell to another than the highest bid.
*Typically* he will do neither of these, though, as they are irrational
from a pure profit point of view.

### Offline Asks

More complex and also more interesting than offline bids are
**offline asks**.  These are probably more common, as they correspond
to prospective *sellers* listing items for sale (rather than prospective
buyers inquiring whether something is for sale at some price).  This is
what a typical (game) market place will consist of.

There are two related but not equivalent ways in which offline asks can be
implemented.  Both have some advantages and drawbacks,
so we discuss both of them.

#### <a name="offline-asks-anu">Atomic Name Updates</a>

Firstly, offline asks can be built from [ANU asks](#anu-asks).
As discussed above, the difficulty here is that the name update
must be pre-determined by the seller, so it cannot explicitly state
to whom some asset should be sent (as would be the case for an
[ANU-bid](#anu-bids)-style transaction instead).

A potential approach for still making this work is as follows:
Instead of specifying a direct transfer of assets in the seller's move
("send 100 gold pieces to `p/buyer`"), the move and the associated game
rules state something like this:

> Reserve 100 gold pieces from my assets.  If a transaction spends
> the second output of this transaction and also performs a move in
> the game with some name, then send those reserved gold pieces to that name.

Since the second output in an ANU ask is the buyer's change output (and in
any case added by the buyer), only the buyer will be able to spend that output
in a later transaction.  So the buyer can make sure that she spends that output
at a convenient later time together with making a move in the game with her
name, and at that time claim the bought asset.

In this way, it is possible to build an ask that can be taken by any buyer
even if the seller is currently offline.  The trade itself also requires
only one on-chain transaction, although a second one has to be made later
to fully claim the assets.  This second transaction can be done together
with a move the buyer would send anyway, though, so it does not necessarily
increase the blockchain usage (or cost extra transaction fees).

**Important:**
With such a scheme, it is crucial to make sure that spending the
output associated with the bought asset is actually done in a move
with that particular game (and not in some other name update or a
pure currency transaction)!  Otherwise, the asset will be lost.  This is
something that the game wallet has to ensure.

Note that a variation of this method can be applied to sell an
undefined quantity of some asset (i.e. where the buyer can choose how much
to buy).  For this, the ANU ask transaction should not send the desired
price into the name output.  Instead, it simply updates the name without
increasing the locked amount (acting like a [sentinel input](#sentinel-inputs)).
However, the move and its game-state
interpretation should then include correct payment.  For instance like this:

> If this transaction pays `x` (up to a maximum of 100) CHI to the
> address `Cabc`, then reserve `x` gold pieces from my assets and send
> them to whoever spends the second output of this transaction in the future.

#### <a name="offline-asks-sentinel">Sentinel Inputs</a>

It is also possible to use [sentinel inputs](#sentinel-inputs) for building
offline asks.  The basic idea is this:

- The seller chooses one of their existing currency outputs as sentinel.

- The seller then sends an ordinary move, which describes the offer they
  are making.  The game rules should implement logic like this:

  > If a move sent later spends the sentinel input and pays `x` CHI to
  > my address `Cabc`, then transfer `x` gold pieces from my assets
  > to the sender of that move.

- The seller constructs a partially signed transaction spending the
  sentinel input as [described above](#sentinel-inputs) and publishes it
  through some communication channel.

A buyer who wants to accept the offer can now simply extend the
partial transaction to include a move with her name (where she wishes
to receive the sold asset) and payment to the seller's address.
Due to the requirement of spending the sentinel input in the purchase
transaction, it is guaranteed that at most one buyer can accept the offer.

Unfortunately, this approach requires *two* on-chain transactions for the trade.
But in contrast to the previous method, there are no weird rules
required to assign ownership of assets when spending some input
in the future (together with the risk of losing the asset).

This method can be slightly optimised by not publishing the first transaction
on-chain immediately.  Instead, both the offer and sentinel transactions
can be published off-chain, so that the offer only needs to be put onto
the blockchain when a buyer actually wants to take it.

## Security Considerations

Finally, we want to emphasise some details that are important to consider
and get right by an implementation in order to avoid potential security
issues.

**Please note that these are not meant to be an exhaustive list!**
Individual games may have specific additional points to consider,
and we may have missed some generally important issues here as well.

### Game Rules

When a buyer accepts an offer or builds a bid for buying some asset,
it must be possible for them to ensure that the seller actually has the
asset (and thus that it can be transferred) upon execution of the trade.
For instance, it must not be possible for a seller who just received a
bid to sell a magic sword to transfer the sword to someone else and then
accept the bid, so that they would still get the CHI payment for it.

Since all transaction schemes described above with the exception of
sentinel inputs spend the seller's name, this can be ensured by defining
the game rules in such a way that assets cannot be transferred without
an explicit move from the owner's name.  If this is the case, then
the seller cannot accept a bid after updating his name, since that
would be a double spend of the name output (and thus invalid).
For trades based on sentinel inputs, there could instead be some rule
in the game logic that states that the assets offered for sale are locked
for a certain time interval; then buyers can safely accept the ask offer
during that time interval.

In any case, however, it is important to make sure that the buyer
knows the *current game state* (including the asset she is trying to buy)
at a block corresponding to the inputs that are being spent in the trade.
In other words, the game wallet or frontend must ensure that the buyer
does not look at some state, then decides to buy the assets shown,
but constructs a bid transaction based on a later block where the assets
may have changed.

### Accepting Bids

When accepting bids, a seller typically just signs their name input in
a transaction.  So before doing so, the seller (and his wallet application)
should verify the following:

- The output updating the name spends it to an address of the seller.

- The name's update value is as agreed (and not some other move).

- The desired price is paid to an address of the seller.

**No other inputs except for the name should be signed.**  In particular,
the application should explicitly ensure that, and not e.g. blindly
call `signrawtransactionwithwallet`.

### Accepting Asks

Since ask transactions are built without any inputs and outputs
of the buyer, the main thing to verify for a buyer when accepting an ask
is that the assets and the move transferring them are as she wants it.
(And in particular, that the asset will actually be transferred with the
given transaction according to the game rules.)

But also here, the buyer's application must ensure that **only inputs
added by the buyer** are signed and not accidentally one of the
inputs the seller should have signed.
