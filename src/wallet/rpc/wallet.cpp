// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <core_io.h>
#include <key_io.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <univalue.h>
#include <util/translation.h>
#include <wallet/context.h>
#include <wallet/receive.h>
#include <wallet/rpc/util.h>
#include <wallet/rpc/wallet.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>

#include <optional>


namespace wallet {

static const std::map<uint64_t, std::string> WALLET_FLAG_CAVEATS{
    {WALLET_FLAG_AVOID_REUSE,
     "You need to rescan the blockchain in order to correctly mark used "
     "destinations in the past. Until this is done, some destinations may "
     "be considered unused, even if the opposite is the case."},
};

static RPCHelpMan getwalletinfo()
{
    return RPCHelpMan{"getwalletinfo",
                "Returns an object containing various wallet state info.\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {
                        {RPCResult::Type::STR, "walletname", "the wallet name"},
                        {RPCResult::Type::NUM, "walletversion", "the wallet version"},
                        {RPCResult::Type::STR, "format", "the database format (only sqlite)"},
                        {RPCResult::Type::NUM, "txcount", "the total number of transactions in the wallet"},
                        {RPCResult::Type::NUM, "keypoolsize", "how many new keys are pre-generated (only counts external keys)"},
                        {RPCResult::Type::NUM, "keypoolsize_hd_internal", /*optional=*/true, "how many new keys are pre-generated for internal use (used for change outputs, only appears if the wallet is using this feature, otherwise external keys are used)"},
                        {RPCResult::Type::NUM_TIME, "unlocked_until", /*optional=*/true, "the " + UNIX_EPOCH_TIME + " until which the wallet is unlocked for transfers, or 0 if the wallet is locked (only present for passphrase-encrypted wallets)"},
                        {RPCResult::Type::STR_AMOUNT, "paytxfee", "the transaction fee configuration, set in " + CURRENCY_UNIT + "/kvB"},
                        {RPCResult::Type::BOOL, "private_keys_enabled", "false if privatekeys are disabled for this wallet (enforced watch-only wallet)"},
                        {RPCResult::Type::BOOL, "avoid_reuse", "whether this wallet tracks clean/dirty coins in terms of reuse"},
                        {RPCResult::Type::OBJ, "scanning", "current scanning details, or false if no scan is in progress",
                        {
                            {RPCResult::Type::NUM, "duration", "elapsed seconds since scan start"},
                            {RPCResult::Type::NUM, "progress", "scanning progress percentage [0.0, 1.0]"},
                        }, /*skip_type_check=*/true},
                        {RPCResult::Type::BOOL, "descriptors", "whether this wallet uses descriptors for output script management"},
                        {RPCResult::Type::BOOL, "external_signer", "whether this wallet is configured to use an external signer such as a hardware wallet"},
                        {RPCResult::Type::BOOL, "blank", "Whether this wallet intentionally does not contain any keys, scripts, or descriptors"},
                        {RPCResult::Type::NUM_TIME, "birthtime", /*optional=*/true, "The start time for blocks scanning. It could be modified by (re)importing any descriptor with an earlier timestamp."},
                        {RPCResult::Type::ARR, "flags", "The flags currently set on the wallet",
                        {
                            {RPCResult::Type::STR, "flag", "The name of the flag"},
                        }},
                        RESULT_LAST_PROCESSED_BLOCK,
                    }},
                },
                RPCExamples{
                    HelpExampleCli("getwalletinfo", "")
            + HelpExampleRpc("getwalletinfo", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    UniValue obj(UniValue::VOBJ);

    size_t kpExternalSize = pwallet->KeypoolCountExternalKeys();
    obj.pushKV("walletname", pwallet->GetName());
    obj.pushKV("walletversion", pwallet->GetVersion());
    obj.pushKV("format", pwallet->GetDatabase().Format());
    obj.pushKV("txcount",       (int)pwallet->mapWallet.size());
    obj.pushKV("keypoolsize", (int64_t)kpExternalSize);

    if (pwallet->CanSupportFeature(FEATURE_HD_SPLIT)) {
        obj.pushKV("keypoolsize_hd_internal",   (int64_t)(pwallet->GetKeyPoolSize() - kpExternalSize));
    }
    if (pwallet->IsCrypted()) {
        obj.pushKV("unlocked_until", pwallet->nRelockTime);
    }
    obj.pushKV("paytxfee", ValueFromAmount(pwallet->m_pay_tx_fee.GetFeePerK()));
    obj.pushKV("private_keys_enabled", !pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    obj.pushKV("avoid_reuse", pwallet->IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE));
    if (pwallet->IsScanning()) {
        UniValue scanning(UniValue::VOBJ);
        scanning.pushKV("duration", Ticks<std::chrono::seconds>(pwallet->ScanningDuration()));
        scanning.pushKV("progress", pwallet->ScanningProgress());
        obj.pushKV("scanning", std::move(scanning));
    } else {
        obj.pushKV("scanning", false);
    }
    obj.pushKV("descriptors", pwallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS));
    obj.pushKV("external_signer", pwallet->IsWalletFlagSet(WALLET_FLAG_EXTERNAL_SIGNER));
    obj.pushKV("blank", pwallet->IsWalletFlagSet(WALLET_FLAG_BLANK_WALLET));
    if (int64_t birthtime = pwallet->GetBirthTime(); birthtime != UNKNOWN_TIME) {
        obj.pushKV("birthtime", birthtime);
    }

    // Push known flags
    UniValue flags(UniValue::VARR);
    uint64_t wallet_flags = pwallet->GetWalletFlags();
    for (uint64_t i = 0; i < 64; ++i) {
        uint64_t flag = uint64_t{1} << i;
        if (flag & wallet_flags) {
            if (flag & KNOWN_WALLET_FLAGS) {
                flags.push_back(WALLET_FLAG_TO_STRING.at(WalletFlags{flag}));
            } else {
                flags.push_back(strprintf("unknown_flag_%u", i));
            }
        }
    }
    obj.pushKV("flags", flags);

    AppendLastProcessedBlock(obj, *pwallet);
    return obj;
},
    };
}

static RPCHelpMan listwalletdir()
{
    return RPCHelpMan{"listwalletdir",
                "Returns a list of wallets in the wallet directory.\n",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::ARR, "wallets", "",
                        {
                            {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR, "name", "The wallet name"},
                                {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to loading the wallet.",
                                {
                                    {RPCResult::Type::STR, "", ""},
                                }},
                            }},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listwalletdir", "")
            + HelpExampleRpc("listwalletdir", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue wallets(UniValue::VARR);
    for (const auto& [path, db_type] : ListDatabases(GetWalletDir())) {
        UniValue wallet(UniValue::VOBJ);
        wallet.pushKV("name", path.utf8string());
                UniValue warnings(UniValue::VARR);
        if (db_type == "bdb") {
            warnings.push_back("This wallet is a legacy wallet and will need to be migrated with migratewallet before it can be loaded");
        }
        wallet.pushKV("warnings", warnings);
        wallets.push_back(std::move(wallet));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("wallets", std::move(wallets));
    return result;
},
    };
}

static RPCHelpMan listwallets()
{
    return RPCHelpMan{"listwallets",
                "Returns a list of currently loaded wallets.\n"
                "For full information on the wallet, use \"getwalletinfo\"\n",
                {},
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::STR, "walletname", "the wallet name"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listwallets", "")
            + HelpExampleRpc("listwallets", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue obj(UniValue::VARR);

    WalletContext& context = EnsureWalletContext(request.context);
    for (const std::shared_ptr<CWallet>& wallet : GetWallets(context)) {
        LOCK(wallet->cs_wallet);
        obj.push_back(wallet->GetName());
    }

    return obj;
},
    };
}

static RPCHelpMan loadwallet()
{
    return RPCHelpMan{
        "loadwallet",
        "Loads a wallet from a wallet file or directory."
                "\nNote that all wallet command-line options used when starting xayad will be"
                "\napplied to the new wallet.\n",
                {
                    {"filename", RPCArg::Type::STR, RPCArg::Optional::NO, "The path to the directory of the wallet to be loaded, either absolute or relative to the \"wallets\" directory. The \"wallets\" directory is set by the -walletdir option and defaults to the \"wallets\" folder within the data directory."},
                    {"load_on_startup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "name", "The wallet name if loaded successfully."},
                        {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to loading the wallet.",
                        {
                            {RPCResult::Type::STR, "", ""},
                        }},
                    }
                },
                RPCExamples{
                    "\nLoad wallet from the wallet dir:\n"
                    + HelpExampleCli("loadwallet", "\"walletname\"")
                    + HelpExampleRpc("loadwallet", "\"walletname\"")
                    + "\nLoad wallet using absolute path (Unix):\n"
                    + HelpExampleCli("loadwallet", "\"/path/to/walletname/\"")
                    + HelpExampleRpc("loadwallet", "\"/path/to/walletname/\"")
                    + "\nLoad wallet using absolute path (Windows):\n"
                    + HelpExampleCli("loadwallet", "\"DriveLetter:\\path\\to\\walletname\\\"")
                    + HelpExampleRpc("loadwallet", "\"DriveLetter:\\path\\to\\walletname\\\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    WalletContext& context = EnsureWalletContext(request.context);
    const std::string name(request.params[0].get_str());

    DatabaseOptions options;
    DatabaseStatus status;
    ReadDatabaseArgs(*context.args, options);
    options.require_existing = true;
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    std::optional<bool> load_on_start = request.params[1].isNull() ? std::nullopt : std::optional<bool>(request.params[1].get_bool());

    {
        LOCK(context.wallets_mutex);
        if (std::any_of(context.wallets.begin(), context.wallets.end(), [&name](const auto& wallet) { return wallet->GetName() == name; })) {
            throw JSONRPCError(RPC_WALLET_ALREADY_LOADED, "Wallet \"" + name + "\" is already loaded.");
        }
    }

    std::shared_ptr<CWallet> const wallet = LoadWallet(context, name, load_on_start, options, status, error, warnings);

    HandleWalletError(wallet, status, error);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("name", wallet->GetName());
    PushWarnings(warnings, obj);

    return obj;
},
    };
}

static RPCHelpMan setwalletflag()
{
            std::string flags;
            for (auto& it : STRING_TO_WALLET_FLAG)
                if (it.second & MUTABLE_WALLET_FLAGS)
                    flags += (flags == "" ? "" : ", ") + it.first;

    return RPCHelpMan{
        "setwalletflag",
        "Change the state of the given wallet flag for a wallet.\n",
                {
                    {"flag", RPCArg::Type::STR, RPCArg::Optional::NO, "The name of the flag to change. Current available flags: " + flags},
                    {"newvalue", RPCArg::Type::BOOL, RPCArg::Default{true}, "The new state."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "flag_name", "The name of the flag that was modified"},
                        {RPCResult::Type::BOOL, "flag_state", "The new state of the flag"},
                        {RPCResult::Type::STR, "warnings", /*optional=*/true, "Any warnings associated with the change"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("setwalletflag", "avoid_reuse")
                  + HelpExampleRpc("setwalletflag", "\"avoid_reuse\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    std::string flag_str = request.params[0].get_str();
    bool value = request.params[1].isNull() || request.params[1].get_bool();

    if (!STRING_TO_WALLET_FLAG.count(flag_str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Unknown wallet flag: %s", flag_str));
    }

    auto flag = STRING_TO_WALLET_FLAG.at(flag_str);

    if (!(flag & MUTABLE_WALLET_FLAGS)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet flag is immutable: %s", flag_str));
    }

    UniValue res(UniValue::VOBJ);

    if (pwallet->IsWalletFlagSet(flag) == value) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet flag is already set to %s: %s", value ? "true" : "false", flag_str));
    }

    res.pushKV("flag_name", flag_str);
    res.pushKV("flag_state", value);

    if (value) {
        pwallet->SetWalletFlag(flag);
    } else {
        pwallet->UnsetWalletFlag(flag);
    }

    if (flag && value && WALLET_FLAG_CAVEATS.count(flag)) {
        res.pushKV("warnings", WALLET_FLAG_CAVEATS.at(flag));
    }

    return res;
},
    };
}

static RPCHelpMan createwallet()
{
    return RPCHelpMan{
        "createwallet",
        "Creates and loads a new wallet.\n",
        {
            {"wallet_name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name for the new wallet. If this is a path, the wallet will be created at the path location."},
            {"disable_private_keys", RPCArg::Type::BOOL, RPCArg::Default{false}, "Disable the possibility of private keys (only watchonlys are possible in this mode)."},
            {"blank", RPCArg::Type::BOOL, RPCArg::Default{false}, "Create a blank wallet. A blank wallet has no keys."},
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Encrypt the wallet with this passphrase."},
            {"avoid_reuse", RPCArg::Type::BOOL, RPCArg::Default{false}, "Keep track of coin reuse, and treat dirty and clean coins differently with privacy considerations in mind."},
            {"descriptors", RPCArg::Type::BOOL, RPCArg::Default{true}, "If set, must be \"true\""},
            {"load_on_startup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
            {"external_signer", RPCArg::Type::BOOL, RPCArg::Default{false}, "Use an external signer such as a hardware wallet. Requires -signer to be configured. Wallet creation will fail if keys cannot be fetched. Requires disable_private_keys and descriptors set to true."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "name", "The wallet name if created successfully. If the wallet was created using a full path, the wallet_name will be the full path."},
                {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to creating and loading the wallet.",
                {
                    {RPCResult::Type::STR, "", ""},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("createwallet", "\"testwallet\"")
            + HelpExampleRpc("createwallet", "\"testwallet\"")
            + HelpExampleCliNamed("createwallet", {{"wallet_name", "descriptors"}, {"avoid_reuse", true}, {"load_on_startup", true}})
            + HelpExampleRpcNamed("createwallet", {{"wallet_name", "descriptors"}, {"avoid_reuse", true}, {"load_on_startup", true}})
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    WalletContext& context = EnsureWalletContext(request.context);
    uint64_t flags = 0;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        flags |= WALLET_FLAG_DISABLE_PRIVATE_KEYS;
    }

    if (!request.params[2].isNull() && request.params[2].get_bool()) {
        flags |= WALLET_FLAG_BLANK_WALLET;
    }
    SecureString passphrase;
    passphrase.reserve(100);
    std::vector<bilingual_str> warnings;
    if (!request.params[3].isNull()) {
        passphrase = std::string_view{request.params[3].get_str()};
        if (passphrase.empty()) {
            // Empty string means unencrypted
            warnings.emplace_back(Untranslated("Empty string given as passphrase, wallet will not be encrypted."));
        }
    }

    if (!request.params[4].isNull() && request.params[4].get_bool()) {
        flags |= WALLET_FLAG_AVOID_REUSE;
    }
    flags |= WALLET_FLAG_DESCRIPTORS;
    if (!self.Arg<bool>("descriptors")) {
        throw JSONRPCError(RPC_WALLET_ERROR, "descriptors argument must be set to \"true\"; it is no longer possible to create a legacy wallet.");
    }
    if (!request.params[7].isNull() && request.params[7].get_bool()) {
#ifdef ENABLE_EXTERNAL_SIGNER
        flags |= WALLET_FLAG_EXTERNAL_SIGNER;
#else
        throw JSONRPCError(RPC_WALLET_ERROR, "Compiled without external signing support (required for external signing)");
#endif
    }

    DatabaseOptions options;
    DatabaseStatus status;
    ReadDatabaseArgs(*context.args, options);
    options.require_create = true;
    options.create_flags = flags;
    options.create_passphrase = passphrase;
    bilingual_str error;
    std::optional<bool> load_on_start = request.params[6].isNull() ? std::nullopt : std::optional<bool>(request.params[6].get_bool());
    const std::shared_ptr<CWallet> wallet = CreateWallet(context, request.params[0].get_str(), load_on_start, options, status, error, warnings);
    if (!wallet) {
        RPCErrorCode code = status == DatabaseStatus::FAILED_ENCRYPT ? RPC_WALLET_ENCRYPTION_FAILED : RPC_WALLET_ERROR;
        throw JSONRPCError(code, error.original);
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("name", wallet->GetName());
    PushWarnings(warnings, obj);

    return obj;
},
    };
}

static RPCHelpMan unloadwallet()
{
    return RPCHelpMan{"unloadwallet",
                "Unloads the wallet referenced by the request endpoint or the wallet_name argument.\n"
                "If both are specified, they must be identical.",
                {
                    {"wallet_name", RPCArg::Type::STR, RPCArg::DefaultHint{"the wallet name from the RPC endpoint"}, "The name of the wallet to unload. If provided both here and in the RPC endpoint, the two must be identical."},
                    {"load_on_startup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged."},
                },
                RPCResult{RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Warning messages, if any, related to unloading the wallet.",
                    {
                        {RPCResult::Type::STR, "", ""},
                    }},
                }},
                RPCExamples{
                    HelpExampleCli("unloadwallet", "wallet_name")
            + HelpExampleRpc("unloadwallet", "wallet_name")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::string wallet_name{EnsureUniqueWalletName(request, self.MaybeArg<std::string>("wallet_name"))};

    WalletContext& context = EnsureWalletContext(request.context);
    std::shared_ptr<CWallet> wallet = GetWallet(context, wallet_name);
    if (!wallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Requested wallet does not exist or is not loaded");
    }

    std::vector<bilingual_str> warnings;
    {
        WalletRescanReserver reserver(*wallet);
        if (!reserver.reserve()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
        }

        // Release the "main" shared pointer and prevent further notifications.
        // Note that any attempt to load the same wallet would fail until the wallet
        // is destroyed (see CheckUniqueFileid).
        std::optional<bool> load_on_start{self.MaybeArg<bool>("load_on_startup")};
        if (!RemoveWallet(context, wallet, load_on_start, warnings)) {
            throw JSONRPCError(RPC_MISC_ERROR, "Requested wallet already unloaded");
        }
    }

    WaitForDeleteWallet(std::move(wallet));

    UniValue result(UniValue::VOBJ);
    PushWarnings(warnings, result);

    return result;
},
    };
}

RPCHelpMan simulaterawtransaction()
{
    return RPCHelpMan{
        "simulaterawtransaction",
        "Calculate the balance change resulting in the signing and broadcasting of the given transaction(s).\n",
        {
            {"rawtxs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED, "An array of hex strings of raw transactions.\n",
                {
                    {"rawtx", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, ""},
                },
            },
            {"options", RPCArg::Type::OBJ_NAMED_PARAMS, RPCArg::Optional::OMITTED, "",
                {
                    {"include_watchonly", RPCArg::Type::BOOL, RPCArg::Default{false}, "(DEPRECATED) No longer used"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_AMOUNT, "balance_change", "The wallet balance change (negative means decrease)."},
            }
        },
        RPCExamples{
            HelpExampleCli("simulaterawtransaction", "[\"myhex\"]")
            + HelpExampleRpc("simulaterawtransaction", "[\"myhex\"]")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> rpc_wallet = GetWalletForJSONRPCRequest(request);
    if (!rpc_wallet) return UniValue::VNULL;
    const CWallet& wallet = *rpc_wallet;

    LOCK(wallet.cs_wallet);

    isminefilter filter = ISMINE_SPENDABLE;

    const auto& txs = request.params[0].get_array();
    CAmount changes{0};
    std::map<COutPoint, CAmount> new_utxos; // UTXO:s that were made available in transaction array
    std::set<COutPoint> spent;

    for (size_t i = 0; i < txs.size(); ++i) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, txs[i].get_str(), /* try_no_witness */ true, /* try_witness */ true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction hex string decoding failure.");
        }

        // Fetch previous transactions (inputs)
        std::map<COutPoint, Coin> coins;
        for (const CTxIn& txin : mtx.vin) {
            coins[txin.prevout]; // Create empty map entry keyed by prevout.
        }
        wallet.chain().findCoins(coins);

        // Fetch debit; we are *spending* these; if the transaction is signed and
        // broadcast, we will lose everything in these
        for (const auto& txin : mtx.vin) {
            const auto& outpoint = txin.prevout;
            if (spent.count(outpoint)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction(s) are spending the same output more than once");
            }
            if (new_utxos.count(outpoint)) {
                changes -= new_utxos.at(outpoint);
                new_utxos.erase(outpoint);
            } else {
                if (coins.at(outpoint).IsSpent()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "One or more transaction inputs are missing or have been spent already");
                }
                changes -= wallet.GetDebit(txin, filter);
            }
            spent.insert(outpoint);
        }

        // Iterate over outputs; we are *receiving* these, if the wallet considers
        // them "mine"; if the transaction is signed and broadcast, we will receive
        // everything in these
        // Also populate new_utxos in case these are spent in later transactions

        const auto& hash = mtx.GetHash();
        for (size_t i = 0; i < mtx.vout.size(); ++i) {
            const auto& txout = mtx.vout[i];
            bool is_mine = 0 < (wallet.IsMine(txout) & filter);
            changes += new_utxos[COutPoint(hash, i)] = is_mine ? txout.nValue : 0;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("balance_change", ValueFromAmount(changes));

    return result;
}
    };
}

static RPCHelpMan migratewallet()
{
    return RPCHelpMan{
        "migratewallet",
        "Migrate the wallet to a descriptor wallet.\n"
        "A new wallet backup will need to be made.\n"
        "\nThe migration process will create a backup of the wallet before migrating. This backup\n"
        "file will be named <wallet name>-<timestamp>.legacy.bak and can be found in the directory\n"
        "for this wallet. In the event of an incorrect migration, the backup can be restored using restorewallet."
        "\nEncrypted wallets must have the passphrase provided as an argument to this call.\n"
        "\nThis RPC may take a long time to complete. Increasing the RPC client timeout is recommended.",
        {
            {"wallet_name", RPCArg::Type::STR, RPCArg::DefaultHint{"the wallet name from the RPC endpoint"}, "The name of the wallet to migrate. If provided both here and in the RPC endpoint, the two must be identical."},
            {"passphrase", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The wallet passphrase"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "wallet_name", "The name of the primary migrated wallet"},
                {RPCResult::Type::STR, "watchonly_name", /*optional=*/true, "The name of the migrated wallet containing the watchonly scripts"},
                {RPCResult::Type::STR, "solvables_name", /*optional=*/true, "The name of the migrated wallet containing solvable but not watched scripts"},
                {RPCResult::Type::STR, "backup_path", "The location of the backup of the original wallet"},
            }
        },
        RPCExamples{
            HelpExampleCli("migratewallet", "")
            + HelpExampleRpc("migratewallet", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const std::string wallet_name{EnsureUniqueWalletName(request, self.MaybeArg<std::string>("wallet_name"))};

            SecureString wallet_pass;
            wallet_pass.reserve(100);
            if (!request.params[1].isNull()) {
                wallet_pass = std::string_view{request.params[1].get_str()};
            }

            WalletContext& context = EnsureWalletContext(request.context);
            util::Result<MigrationResult> res = MigrateLegacyToDescriptor(wallet_name, wallet_pass, context);
            if (!res) {
                throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(res).original);
            }

            UniValue r{UniValue::VOBJ};
            r.pushKV("wallet_name", res->wallet_name);
            if (res->watchonly_wallet) {
                r.pushKV("watchonly_name", res->watchonly_wallet->GetName());
            }
            if (res->solvables_wallet) {
                r.pushKV("solvables_name", res->solvables_wallet->GetName());
            }
            r.pushKV("backup_path", res->backup_path.utf8string());

            return r;
        },
    };
}

RPCHelpMan gethdkeys()
{
    return RPCHelpMan{
        "gethdkeys",
        "List all BIP 32 HD keys in the wallet and which descriptors use them.\n",
        {
            {"options", RPCArg::Type::OBJ_NAMED_PARAMS, RPCArg::Optional::OMITTED, "", {
                {"active_only", RPCArg::Type::BOOL, RPCArg::Default{false}, "Show the keys for only active descriptors"},
                {"private", RPCArg::Type::BOOL, RPCArg::Default{false}, "Show private keys"}
            }},
        },
        RPCResult{RPCResult::Type::ARR, "", "", {
            {
                {RPCResult::Type::OBJ, "", "", {
                    {RPCResult::Type::STR, "xpub", "The extended public key"},
                    {RPCResult::Type::BOOL, "has_private", "Whether the wallet has the private key for this xpub"},
                    {RPCResult::Type::STR, "xprv", /*optional=*/true, "The extended private key if \"private\" is true"},
                    {RPCResult::Type::ARR, "descriptors", "Array of descriptor objects that use this HD key",
                    {
                        {RPCResult::Type::OBJ, "", "", {
                            {RPCResult::Type::STR, "desc", "Descriptor string representation"},
                            {RPCResult::Type::BOOL, "active", "Whether this descriptor is currently used to generate new addresses"},
                        }},
                    }},
                }},
            }
        }},
        RPCExamples{
            HelpExampleCli("gethdkeys", "") + HelpExampleRpc("gethdkeys", "")
            + HelpExampleCliNamed("gethdkeys", {{"active_only", "true"}, {"private", "true"}}) + HelpExampleRpcNamed("gethdkeys", {{"active_only", "true"}, {"private", "true"}})
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const std::shared_ptr<const CWallet> wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;

            LOCK(wallet->cs_wallet);

            UniValue options{request.params[0].isNull() ? UniValue::VOBJ : request.params[0]};
            const bool active_only{options.exists("active_only") ? options["active_only"].get_bool() : false};
            const bool priv{options.exists("private") ? options["private"].get_bool() : false};
            if (priv) {
                EnsureWalletIsUnlocked(*wallet);
            }


            std::set<ScriptPubKeyMan*> spkms;
            if (active_only) {
                spkms = wallet->GetActiveScriptPubKeyMans();
            } else {
                spkms = wallet->GetAllScriptPubKeyMans();
            }

            std::map<CExtPubKey, std::set<std::tuple<std::string, bool, bool>>> wallet_xpubs;
            std::map<CExtPubKey, CExtKey> wallet_xprvs;
            for (auto* spkm : spkms) {
                auto* desc_spkm{dynamic_cast<DescriptorScriptPubKeyMan*>(spkm)};
                CHECK_NONFATAL(desc_spkm);
                LOCK(desc_spkm->cs_desc_man);
                WalletDescriptor w_desc = desc_spkm->GetWalletDescriptor();

                // Retrieve the pubkeys from the descriptor
                std::set<CPubKey> desc_pubkeys;
                std::set<CExtPubKey> desc_xpubs;
                w_desc.descriptor->GetPubKeys(desc_pubkeys, desc_xpubs);
                for (const CExtPubKey& xpub : desc_xpubs) {
                    std::string desc_str;
                    bool ok = desc_spkm->GetDescriptorString(desc_str, false);
                    CHECK_NONFATAL(ok);
                    wallet_xpubs[xpub].emplace(desc_str, wallet->IsActiveScriptPubKeyMan(*spkm), desc_spkm->HasPrivKey(xpub.pubkey.GetID()));
                    if (std::optional<CKey> key = priv ? desc_spkm->GetKey(xpub.pubkey.GetID()) : std::nullopt) {
                        wallet_xprvs[xpub] = CExtKey(xpub, *key);
                    }
                }
            }

            UniValue response(UniValue::VARR);
            for (const auto& [xpub, descs] : wallet_xpubs) {
                bool has_xprv = false;
                UniValue descriptors(UniValue::VARR);
                for (const auto& [desc, active, has_priv] : descs) {
                    UniValue d(UniValue::VOBJ);
                    d.pushKV("desc", desc);
                    d.pushKV("active", active);
                    has_xprv |= has_priv;

                    descriptors.push_back(std::move(d));
                }
                UniValue xpub_info(UniValue::VOBJ);
                xpub_info.pushKV("xpub", EncodeExtPubKey(xpub));
                xpub_info.pushKV("has_private", has_xprv);
                if (priv) {
                    xpub_info.pushKV("xprv", EncodeExtKey(wallet_xprvs.at(xpub)));
                }
                xpub_info.pushKV("descriptors", std::move(descriptors));

                response.push_back(std::move(xpub_info));
            }

            return response;
        },
    };
}

static RPCHelpMan createwalletdescriptor()
{
    return RPCHelpMan{"createwalletdescriptor",
        "Creates the wallet's descriptor for the given address type. "
        "The address type must be one that the wallet does not already have a descriptor for."
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"type", RPCArg::Type::STR, RPCArg::Optional::NO, "The address type the descriptor will produce. Options are \"legacy\", \"p2sh-segwit\", \"bech32\", and \"bech32m\"."},
            {"options", RPCArg::Type::OBJ_NAMED_PARAMS, RPCArg::Optional::OMITTED, "", {
                {"internal", RPCArg::Type::BOOL, RPCArg::DefaultHint{"Both external and internal will be generated unless this parameter is specified"}, "Whether to only make one descriptor that is internal (if parameter is true) or external (if parameter is false)"},
                {"hdkey", RPCArg::Type::STR, RPCArg::DefaultHint{"The HD key used by all other active descriptors"}, "The HD key that the wallet knows the private key of, listed using 'gethdkeys', to use for this descriptor's key"},
            }},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::ARR, "descs", "The public descriptors that were added to the wallet",
                    {{RPCResult::Type::STR, "", ""}}
                }
            },
        },
        RPCExamples{
            HelpExampleCli("createwalletdescriptor", "bech32m")
            + HelpExampleRpc("createwalletdescriptor", "bech32m")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            std::optional<OutputType> output_type = ParseOutputType(request.params[0].get_str());
            if (!output_type) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[0].get_str()));
            }

            UniValue options{request.params[1].isNull() ? UniValue::VOBJ : request.params[1]};
            UniValue internal_only{options["internal"]};
            UniValue hdkey{options["hdkey"]};

            std::vector<bool> internals;
            if (internal_only.isNull()) {
                internals.push_back(false);
                internals.push_back(true);
            } else {
                internals.push_back(internal_only.get_bool());
            }

            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(*pwallet);

            CExtPubKey xpub;
            if (hdkey.isNull()) {
                std::set<CExtPubKey> active_xpubs = pwallet->GetActiveHDPubKeys();
                if (active_xpubs.size() != 1) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to determine which HD key to use from active descriptors. Please specify with 'hdkey'");
                }
                xpub = *active_xpubs.begin();
            } else {
                xpub = DecodeExtPubKey(hdkey.get_str());
                if (!xpub.pubkey.IsValid()) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to parse HD key. Please provide a valid xpub");
                }
            }

            std::optional<CKey> key = pwallet->GetKey(xpub.pubkey.GetID());
            if (!key) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Private key for %s is not known", EncodeExtPubKey(xpub)));
            }
            CExtKey active_hdkey(xpub, *key);

            std::vector<std::reference_wrapper<DescriptorScriptPubKeyMan>> spkms;
            WalletBatch batch{pwallet->GetDatabase()};
            for (bool internal : internals) {
                WalletDescriptor w_desc = GenerateWalletDescriptor(xpub, *output_type, internal);
                uint256 w_id = DescriptorID(*w_desc.descriptor);
                if (!pwallet->GetScriptPubKeyMan(w_id)) {
                    spkms.emplace_back(pwallet->SetupDescriptorScriptPubKeyMan(batch, active_hdkey, *output_type, internal));
                }
            }
            if (spkms.empty()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Descriptor already exists");
            }

            // Fetch each descspkm from the wallet in order to get the descriptor strings
            UniValue descs{UniValue::VARR};
            for (const auto& spkm : spkms) {
                std::string desc_str;
                bool ok = spkm.get().GetDescriptorString(desc_str, false);
                CHECK_NONFATAL(ok);
                descs.push_back(desc_str);
            }
            UniValue out{UniValue::VOBJ};
            out.pushKV("descs", std::move(descs));
            return out;
        }
    };
}

// addresses
RPCHelpMan getaddressinfo();
RPCHelpMan getnewaddress();
RPCHelpMan getrawchangeaddress();
RPCHelpMan setlabel();
RPCHelpMan listaddressgroupings();
RPCHelpMan keypoolrefill();
RPCHelpMan getaddressesbylabel();
RPCHelpMan listlabels();
#ifdef ENABLE_EXTERNAL_SIGNER
RPCHelpMan walletdisplayaddress();
#endif // ENABLE_EXTERNAL_SIGNER

// backup
RPCHelpMan importprunedfunds();
RPCHelpMan removeprunedfunds();
RPCHelpMan importdescriptors();
RPCHelpMan listdescriptors();
RPCHelpMan backupwallet();
RPCHelpMan restorewallet();

// coins
RPCHelpMan getreceivedbyaddress();
RPCHelpMan getreceivedbylabel();
RPCHelpMan getbalance();
RPCHelpMan lockunspent();
RPCHelpMan listlockunspent();
RPCHelpMan getbalances();
RPCHelpMan listunspent();

// encryption
RPCHelpMan walletpassphrase();
RPCHelpMan walletpassphrasechange();
RPCHelpMan walletlock();
RPCHelpMan encryptwallet();

// spend
RPCHelpMan sendtoaddress();
RPCHelpMan sendmany();
RPCHelpMan settxfee();
RPCHelpMan fundrawtransaction();
RPCHelpMan bumpfee();
RPCHelpMan psbtbumpfee();
RPCHelpMan send();
RPCHelpMan sendall();
RPCHelpMan walletprocesspsbt();
RPCHelpMan walletcreatefundedpsbt();
RPCHelpMan signrawtransactionwithwallet();

// signmessage
RPCHelpMan signmessage();

// transactions
RPCHelpMan listreceivedbyaddress();
RPCHelpMan listreceivedbylabel();
RPCHelpMan listtransactions();
RPCHelpMan listsinceblock();
RPCHelpMan gettransaction();
RPCHelpMan abandontransaction();
RPCHelpMan rescanblockchain();
RPCHelpMan abortrescan();

// auxpow
RPCHelpMan getauxblock();
RPCHelpMan getwork();

// name
RPCHelpMan name_list();
RPCHelpMan name_register();
RPCHelpMan name_update();
RPCHelpMan queuerawtransaction();
RPCHelpMan dequeuetransaction();
RPCHelpMan listqueuedtransactions();
RPCHelpMan sendtoname();

std::span<const CRPCCommand> GetWalletRPCCommands()
{
    static const CRPCCommand commands[]{
        {"rawtransactions", &fundrawtransaction},
        {"wallet", &abandontransaction},
        {"wallet", &abortrescan},
        {"wallet", &backupwallet},
        {"wallet", &bumpfee},
        {"wallet", &psbtbumpfee},
        {"wallet", &createwallet},
        {"wallet", &createwalletdescriptor},
        {"wallet", &restorewallet},
        {"wallet", &encryptwallet},
        {"wallet", &getaddressesbylabel},
        {"wallet", &getaddressinfo},
        {"wallet", &getbalance},
        {"wallet", &gethdkeys},
        {"wallet", &getnewaddress},
        {"wallet", &getrawchangeaddress},
        {"wallet", &getreceivedbyaddress},
        {"wallet", &getreceivedbylabel},
        {"wallet", &gettransaction},
        {"wallet", &getbalances},
        {"wallet", &getwalletinfo},
        {"wallet", &importdescriptors},
        {"wallet", &importprunedfunds},
        {"wallet", &keypoolrefill},
        {"wallet", &listaddressgroupings},
        {"wallet", &listdescriptors},
        {"wallet", &listlabels},
        {"wallet", &listlockunspent},
        {"wallet", &listreceivedbyaddress},
        {"wallet", &listreceivedbylabel},
        {"wallet", &listsinceblock},
        {"wallet", &listtransactions},
        {"wallet", &listunspent},
        {"wallet", &listwalletdir},
        {"wallet", &listwallets},
        {"wallet", &loadwallet},
        {"wallet", &lockunspent},
        {"wallet", &migratewallet},
        {"wallet", &removeprunedfunds},
        {"wallet", &rescanblockchain},
        {"wallet", &send},
        {"wallet", &sendmany},
        {"wallet", &sendtoaddress},
        {"wallet", &setlabel},
        {"wallet", &settxfee},
        {"wallet", &setwalletflag},
        {"wallet", &signmessage},
        {"wallet", &signrawtransactionwithwallet},
        {"wallet", &simulaterawtransaction},
        {"wallet", &sendall},
        {"wallet", &unloadwallet},
        {"wallet", &walletcreatefundedpsbt},
#ifdef ENABLE_EXTERNAL_SIGNER
        {"wallet", &walletdisplayaddress},
#endif // ENABLE_EXTERNAL_SIGNER
        {"wallet", &walletlock},
        {"wallet", &walletpassphrase},
        {"wallet", &walletpassphrasechange},
        {"wallet", &walletprocesspsbt},

        /** Auxpow wallet functions */
        {"mining", &getauxblock},
        {"mining", &getwork},

        // Name-related wallet calls.
        {"names", &name_list},
        {"names", &name_register},
        {"names", &name_update},
        {"names", &queuerawtransaction},
        {"names", &dequeuetransaction},
        {"names", &listqueuedtransactions},
        {"names", &sendtoname},
    };
    return commands;
}
} // namespace wallet
