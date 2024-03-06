// Copyright (c) 2021 yanmaani
// Licensed under CC0 (Public domain)

#ifndef BITCOIN_WALLET_RPC_WALLETNAMES_H
#define BITCOIN_WALLET_RPC_WALLETNAMES_H

namespace wallet
{

bool getNameSalt(const CKey& key, const valtype& name, valtype& rand);

} // namespace wallet

#endif //BITCOIN_WALLET_RPC_WALLETNAMES_H
