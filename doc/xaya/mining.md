# Triple-Purpose Mining

Xaya is commited to proof-of-work (PoW) for securing its blockchain.  As there
are [various drawbacks](#existing-schemes) to commonly-used PoW schemes, Xaya
implements a new design that unifies the best of all worlds.

The following provides a [high-level design](#overview) overview and then
describes the [technical details](#technical) of the Xaya **triple-purpose
mining** implementation.

## Common PoW Schemes and Their Drawbacks <a name="existing-schemes"></a>

Blockchain projects that use PoW typically choose from one of two common
schemes:  Merged mining with a more prominent chain (e.g. Bitcoin), or if *not*
merge-mining, a combination of one or more "ASIC resistant" hash algorithms.
Both of these choices have their unique advantages and drawbacks.

### Merged Mining

[Merged mining](https://en.bitcoin.it/wiki/Merged_mining_specification) allows
existing miners of a PoW blockchain (e.g. Bitcoin) to mine a second blockchain
**at the same time and almost for free**.
This allows multiple blockchains to share hashing power, securing all of them
instead of reducing the security of each chain by splintering the mining
community.

As a result, merge-mined blockchains have a **very high hash rate and are more
resistant to 51% attacks**.  For instance, Namecoin (the blockchain that
pioneered merged mining) had at one point in time a
[hash rate even higher than
Bitcoin](https://www.reddit.com/r/Namecoin/comments/7cge6l/namecoin_hashrate_is_now_higher_than_bitcoin/).  This happened because the existing pool of Bitcoin
miners split between mining Bitcoin and Bitcoin Cash, while miners of both
chains could merge-mine Namecoin.

On the other hand, if at least some of the big Bitcoin mining pools decide
to merge-mine a new blockchain, they will almost surely control by orders
of magnitude more hash power than any individual miner could contribute.
(This is also what makes merged mining secure!)
However, this also means that the aspect of distributing coins to a wider
community, which is also a goal of mining, is lost.  Further, sudden large drops
in the hash rate are possible when one of the pools ceases merged mining,
which can slow the blockchain down considerably.

### Stand-Alone Mining

If a blockchain decides against merged mining, this means that each and every
miner for this blockchain must be convinced to dedicate their hash power
to the new chain **exclusively**.  This typically means that, especially in the
beginning of a new project, only smaller miners join.

While that is beneficial for decentralisation of mining and to distribute
coins widely, it invariably means that the total hash rate is very low and
thus that the chain is **more susceptible to 51% attacks**.  Even if
an "ASIC resistant" mining algorithm is chosen (such that existing
Bitcoin mining chips cannot be used), it's still relatively cheap for an
attacker to gain a large hash rate for a short time by renting GPUs
or CPUs from a cloud provider.

## High-Level Overview for Triple-Purpose Mining <a name="overview"></a>

To combine the best of both worlds (merged and stand-alone mining), we propose
**triple-purpose mining** as a new scheme that forms a good compromise between
the two extremes:

**Xaya blocks can be either merge-mined with SHA-256d (Bitcoin), or they
can be mined stand-alone with Neoscrypt.**
The *chain ID* for merge mining Xaya is `1829`.
There are no particular rules enforcing a certain sequence of blocks for each
algorithm (unlike some existing multi-algorithm projects), but the difficulty
for each algorithm is retargeted independently.  This means that, on average and
independent of the distribution of hash rate between the two algorithms, there
is one SHA-256d and one Neoscrypt block every minute (leading to an average
rate of one block every 30 seconds).
Furthermore, block rewards are not equal; instead, **75%** of the total PoW
coin supply goes to stand-alone Neoscrypt blocks, while **25%** goes to
merge-mined SHA-256d blocks.

The three main benefits ("purposes") of this are:

1. Due to merge mining, the total hash rate is very high and the chain is thus
   highly resistant to 51% attacks.  (Note that, as in Bitcoin, it is not the
   actual *length* of a chain that counts, but the *work* it contains.
   This means that even if the Neoscrypt hash rate is very low and a single
   miner controls almost 100% of
   it, then they will likely still have much less than 50% of the total hash
   rate and thus not be able to run a 51% attack.)
2. Stand-alone mining with Neoscrypt and a relatively high block reward for
   these blocks makes it possible to distribute coins to a wide community
   of individual miners.  However, since merge mining is essentially free,
   there is still sufficient incentive for mining pools to merge-mine even with
   only 25% of total PoW coins going to them.
3. By producing one block per minute individually from both mining algorithms,
   we greatly increase the resilience of the blockchain against stalling
   when one of the algorithms has a sharp drop in hash rate.  (Even if *all*
   mining of one algorithm were to stop temporarily, the blockchain would
   still continue to produce blocks on average once per minute.)

## Technical Details <a name="technical"></a>

[Huntercoin](http://huntercoin.org/) already implements dual-algorithm
mining (although allowing both algorithms to be merge-mined).  However,
the exact scheme used for merge-mining there (inherited from Namecoin)
is not ideal.  Since it uses the `nVersion` field of the block header to
signal merge mining (and, in the case of Huntercoin, which algorithm
is used), it conflicts with
[BIP 9](https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki).
Thus, merge mining in Xaya is implemented differently and does not
use the `nVersion` field of the main block header.

### PoW Data in the Block Header <a name="pow-data"></a>

For PoW in Xaya, the hash of the actual block header never matters.
Instead, each block header is always followed by special **PoW data**.
This contains metadata about the PoW (the algorithm used and whether or not
it was merge-mined) as well as the actual data proving the work by committing
to the SHA-256d hash of the actual block header.

The first byte of the PoW data indicates the **mining algorithm**:

Value  | Algorithm
------ | ---------
`0x01` | SHA-256d
`0x02` | Neoscrypt

In addition, when the block is merge-mined, the highest-value bit (`0x80`)
is also set to indicate this.  This means that the only valid values
are `0x81` (merge-mined SHA-256d) and `0x02` (stand-alone Neoscrypt).

The difficulty target for the chosen algorithm follows in the **`nBits` field**.
*The `nBits` field in the block header is unused, and must be set to zero.*
(Without the PoW data, the block header alone does not specify the mining
algorithm, so it doesn't make sense to specify the difficulty in it.)
When validating a block header with the PoW data, the usual rules apply:

* The block header `nBits` field must be zero.
* The PoW data `nBits` field must match the expected difficulty for the
  selected algorithm, following the difficulty retargeting for that algorithm.
* The PoW must match the `nBits` difficulty target specified in the PoW data.

The format of the **remainder of the PoW data** depends on whether or not
merged mining is used.  If it is, then an **"auxpow" data structure** as
per [Namecoin's merged
mining](https://en.bitcoin.it/wiki/Merged_mining_specification) follows.
If the block is stand-alone mined, then **80 bytes** follow, such that:

1. Their hash according to the selected algorithm (Neoscrypt) satisfies the
   difficulty target.
2. Bytes 37 to 68, where the Merkle root hash would be in a Bitcoin block
   header, contain exactly the hash of the Xaya block header.

Apart from the block hash being in the specified position, there are no other
requirements for these bytes.  They can set other fields similar to how
a block header would set them, but are not required to.

However, note that such a proof can *never* be used also as auxpow itself,
since it has to contain the block hash in the position of the Merkle root.
This makes it impossible (barring a SHA-256d collision) to connect a coinbase
transaction to it, which would be required for a valid auxpow.
This would prevent using a single proof for two blocks (as stand-alone and
auxpow), even if it were possible to use a single algorithm both for merged
and stand-alone mining.

This particular format for attaching PoW to block headers has various benefits:

* It does not put *any* constraints at all on the actual block header, which
  prevents
  [conflicts with BIP 9](https://forum.namecoin.org/viewtopic.php?f=5&t=2466)
  as well as similar problems in the future.
* It reuses the existing format for merged mining as far as possible, and,
  in particular, allows merge-mining Xaya together with existing chains
  as Namecoin does.
* The data that is hashed for stand-alone mining has the same format
  as a Bitcoin block header, so that existing software and mining infrastructure
  built for Bitcoin-like blocks can be used.
* Instead of the actual block header, we always feed derived data committing to
  its hash to the mining application.  This makes it easier to add more data
  to the block header in a future hard fork if desired (e.g. for
  implementing Ephemeral Timestamps).

### Mining Interfaces of the Core Daemon

The core daemon provides different RPC methods that can be used
by external mining infrastructure:

#### `getblocktemplate`

The `getblocktemplate` RPC method is available as in upstream Bitcoin and can
be used by advanced users.  This requires the external miner to construct the
full resulting block, including the correct [PoW data](#pow-data), themselves.
(This requirement is similar for miners using `getblocktemplate` with existing
merge-mined coins like Namecoin.)

#### `createauxblock` and `submitauxblock`

For merge-mining Xaya, the RPC methods `createauxblock` and `submitauxblock`
are provided similar to Namecoin.  They handle the construction of the block
with the correct format, as long as the miner can construct the auxpow
itself (as is required for Namecoin).

#### `getwork`

For out-of-the-box stand-alone mining, Xaya provides the
[`getwork`](https://en.bitcoin.it/wiki/Getwork) RPC method that was previously
used in Bitcoin.  It constructs the PoW data as described above internally and
returns the "fake block header" data that needs to be hashed, such that
existing mining tools can readily process it.
