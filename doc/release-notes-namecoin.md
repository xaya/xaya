# Release Notes for Namecoin

## Version 0.19

## Version 0.18

- BIP16, CSV and Segwit will be activated at block height 475,000 on mainnet
  and 232,000 on testnet.
  See [#239](https://github.com/namecoin/namecoin-core/issues/239) for
  a discussion.

- The `options` argument for `name_new`, `name_firstupdate` and `name_update`
  can now be used to specify per-RPC encodings for names and values by setting
  the `nameEncoding` and `valueEncoding` fields, respectively.

- `name_scan` now accepts an optional `options` argument, which can be used
  to specify filtering conditions (based on number of confirmations, prefix and
  regexp matches of a name).
  See [#237](https://github.com/namecoin/namecoin-core/issues/237)
  for more details.

- `name_filter` has been removed.  Instead, `name_scan` with the newly added
  filtering options can be used.

- `ismine` is no longer added to RPC results if no wallet is associated
  to an RPC call.

## Version 0.17

- Previously, `createrawtransaction` supported a separate argument for creating
  name operations.  This has been removed, so that `createrawtransaction` now
  has the same interface as the upstream version in Bitcoin.  Instead, a new
  RPC method `namerawtransaction` has been added, which takes an already created
  transaction and changes its output to be a name operation.
  For more details, see
  [#181](https://github.com/namecoin/namecoin-core/issues/181).

- The optional "destination address" argument to `name_update` and
  `name_firstupdate` has been removed.  Instead, those methods as well
  as `name_new` now accept an optional `options` argument.  The destination
  address can now be specified by setting `destAddress` in these options.
  In addition, one can now also specify to send namecoins to addresses
  (similar to `sendmany`) in the same transaction by using the new `sendTo`
  option.
  See also the
  [basic proposal](https://github.com/namecoin/namecoin-core/issues/194), which
  is not yet completely implemented, and the concrete changes done in
  [#220](https://github.com/namecoin/namecoin-core/pull/220) and
  [#222](https://github.com/namecoin/namecoin-core/pull/222).

- `listunspent` now explicitly handles name outputs.  In particular, the coins
  associated to expired names are now always excluded.  Coins tied to active
  names are included only if the `includeNames` option is set, and they
  are marked as name operations in this case.
  More details can be found in
  [#192](https://github.com/namecoin/namecoin-core/issues/192).

- The `transferred` field in the output of `name_list` has been changed
  to `ismine` (with the negated value).  This makes it consistent with
  `name_pending`.  In addition, `ismine` has also been added to the other
  name RPCs like `name_show` or `name_scan`.
  See the [proposal](https://github.com/namecoin/namecoin-core/issues/219) and
  the [implementation](https://github.com/namecoin/namecoin-core/pull/236).

- `name_new` now checks whether a name exists already and by default rejects
  to register an already existing name.  To override this check and get back
  the old behaviour (where a `NAME_NEW` transaction can be sent for existing
  names), set the new `allowExisting` option to true.
  For more context, see the
  [corresponding issue](https://github.com/namecoin/namecoin-core/issues/54).

- Names and values in the RPC interface (and to a limited degree also the REST
  interface and `namecoin-tx`) can now be specified and requested in one of
  three encodings (`ascii`, `utf8` and `hex`).  This fixes a long-standing issue
  with names or values that were invalid UTF-8, by adding proper support for
  pure binary data as well as validation of the data before returning it as
  ASCII or UTF-8.  The encodings default to `ascii` now.  To get back behaviour
  close to the previous one, specify `-nameencoding=utf8` and
  `-valueencoding=utf8`.  The detailed specification of the new encoding options
  can be found in the
  [Github issue](https://github.com/namecoin/namecoin-core/issues/246).

- The `namecoin-tx` utility has now support for creating name operations based
  on the new commands `namenew`, ` namefirstupdate` and `nameupdate`.  For the
  exact usage, see the
  [proposal](https://github.com/namecoin/namecoin-core/issues/147#issuecomment-402429258).

- The "magic string" used for `signmessage` and `verifymessage` has been updated
  to be specific to Namecoin (previously, the one from Bitcoin was used).  This
  means that messages signed previously won't validate anymore.  It is, however,
  still possible to verify them customly; so old signatures can still be used
  as proofs in the future if necessary.
