# Blockchain Consensus Protocol

This document gives an overview of the low-level consensus protocol
implemented by the core Xaya daemon.

**NOTE:**  The final reference for the protocol is the Xaya Core implementation.
This document just highlights the most important parts in an easy-to-read form.

## Xaya Is Based on Namecoin <a name="names"></a>

Most parts of the Xaya consensus protocol are inherited from
[Namecoin](https://www.namecoin.org/), which itself inherits most of the
[Bitcoin protocol](https://bitcoin.org/).

As with Bitcoin, Namecoin and Xaya implement a distributed ledger by
tracking the current set of "unspent transaction outputs".  However, in addition
to pure currency transactions, they also implement a **name-value database**.
This database is stored and updated similar to the UTXO set.  Each entry
contains the following fields:

* **Name:**
  A byte array with [restrictions](#name-value-restrictions) that is the
  "key" for the name-value-database.  In Xaya, this is typically the
  account name of a player.  It can be up to 256 bytes.
* **Value:**
  Another, typically longer, byte array (also with
  [restrictions](#name-value-restrictions)) that holds data associated
  with the name.  In Xaya, this is used to store a player's latest moves
  or other actions.  It can be up to 2,048 bytes.
* **Address:**
  Similar to transaction outputs, each name is associated with a Xaya address
  (Bitcoin script) that "owns" it.  Only the owner has *write access* to this
  name's entry, while everyone can *read* it from the database.  The owner
  can send the name to a different address, either one of their own
  or to someone else's.

In addition to spending and creating currency outputs, each transaction can
optionally also **register** or **update** a name in the database.  A single
transaction can at most affect one name.  A name can be updated
at most once per block.

Specific details for the transaction format are not provided here because they
are the same as in Namecoin.

**In contrast to Namecoin, which registers names in a two-step process
(`name_new` and `name_firstupdate`), Xaya's name registrations are always
done in a single transaction (called `name_register`).  This transaction is
similar to a `name_update` in Namecoin except that it does not consume a name
input.**

## Basic Differences from Bitcoin

Some of the basic chain parameters and properties of the genesis block
in Xaya differ from those in Bitcoin and Namecoin.  Furthermore, some
of these details were changed in the past with scheduled forks.
The current parameters for `mainnet` are:

* PoW mining is done based on [**triple-purpose mining**](mining.md).
* The difficulty is updated for each block using the [**Dark Gravity Wave
  (DGW)**](https://github.com/xayaplatform/xaya/blob/a4ebc9b0daf72c79d3997901aadef0ca6bd01085/src/test/dualalgo_tests.cpp#L29)
  formula.
* Blocks are scheduled to be produced, on average, every **30 seconds**
  instead of every 10 minutes.  Difficulty retargets independently for
  each of the two possible [mining algorithms](mining.md).  Merge mined
  SHA-256d blocks are scheduled once every two minutes and standalone
  Neoscrypt blocks every 40 seconds.
* The initial block reward is set to **3.82934346 CHI**,
  and halves every **4.2 million** blocks, which corresponds to Bitcoin's
  halving of once every four years.  This reward was chosen to yield the
  [correct total coin
  supply](https://github.com/xaya/xaya/issues/70#issuecomment-441292533)
  following the token sale.
* The genesis block's coinbase transaction pays to a multisig address owned
  by the Xaya team.  Unlike Bitcoin and Namecoin, it is actually spendable,
  and does not observe the usual "block maturity" rule.
  * These coins will be distributed to the community according to the token
    sale and Huntercoin snapshot.  Unsold coins will be destroyed by sending
    them to a provably unspendable script (`OP_RETURN`).
* The maximum block weight is **400,000** instead of 4 million,
  corresponding to a block size of **100 KB** instead of 1 MB.  The maximum
  number of sigops in a block is **8,000**.
* The maximum size of a script element is **2,048 bytes** instead of 520 bytes,
  primarily to allow for larger name values.

## Activation of Soft Forks

Xaya immediately activates some of the soft forks introduced in Bitcoin
over time since we start with a fresh blockchain:

* [BIP 16](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki)
* [BIP 34](https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki)
* [BIP 65](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki)
* [BIP 66](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki)
* CSV ([BIP 68](https://github.com/bitcoin/bips/blob/master/bip-0068.mediawiki),
  [BIP 112](https://github.com/bitcoin/bips/blob/master/bip-0112.mediawiki) and
  [BIP 113](https://github.com/bitcoin/bips/blob/master/bip-0113.mediawiki))
* Segwit ([BIP
  141](https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki),
  [BIP 143](https://github.com/bitcoin/bips/blob/master/bip-0143.mediawiki) and
  [BIP 147](https://github.com/bitcoin/bips/blob/master/bip-0147.mediawiki))

(Note that some of these activate later or never on the **regtest network**
to allow for certain tests to be possible.)

## Name and Value Restrictions <a name="name-value-restrictions"></a>

Valid names and values in Xaya must satisfy additional constraints
compared to Namecoin, which only enforces maximum lengths.  In particular,
these conditions must be met by names and values:

* Names can be up to **256 bytes** long and values up to **2,048 bytes**.
* Names must have a namespace.  This means that they must start with
  one or more lower-case letters and `/`.  Expressed as a regexp, this
  means they must match: `[a-z]+\/.*`
* Names must be valid UTF-8 and must not contain unprintable ASCII characters,
  i.e. those with a value less than 0x20.
* Values must be valid [JSON](https://json.org/) and parse to a JSON **object**.
