// Copyright (c) 2016-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RPCWALLET_H
#define BITCOIN_WALLET_RPCWALLET_H

#include <interfaces/chain.h>
#include <wallet/walletutil.h>

#include <memory>
#include <string>
#include <vector>

class CCoinControl;
class CRPCTable;
class CWallet;
class CWalletTx;
class JSONRPCRequest;
class UniValue;
struct PartiallySignedTransaction;
class CTransaction;

void RegisterWalletRPCCommands(interfaces::Chain& chain, std::vector<std::unique_ptr<interfaces::Handler>>& handlers);

/**
 * Figures out what wallet, if any, to use for a JSONRPCRequest.
 *
 * @param[in] request JSONRPCRequest that wishes to access a wallet
 * @return nullptr if no wallet should be used, or a pointer to the CWallet
 */
std::shared_ptr<CWallet> GetWalletForJSONRPCRequest(const JSONRPCRequest& request);

std::string HelpRequiringPassphrase(const CWallet*);
void EnsureWalletIsUnlocked(const CWallet*);
bool EnsureWalletIsAvailable(const CWallet*, bool avoidException);
CTransactionRef SendMoneyToScript(interfaces::Chain::Lock& locked_chain,
                                  CWallet* pwallet, const CScript& scriptPubKey,
                                  const CTxIn* withInput, CAmount nValue,
                                  bool fSubtractFeeFromAmount,
                                  const CCoinControl& coin_control,
                                  mapValue_t mapValue);

UniValue getaddressinfo(const JSONRPCRequest& request);
UniValue signrawtransactionwithwallet(const JSONRPCRequest& request);
#endif //BITCOIN_WALLET_RPCWALLET_H
