// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_SPEND_H
#define BITCOIN_WALLET_SPEND_H

#include <consensus/amount.h>
#include <wallet/coinselection.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>

#include <optional>

namespace wallet {
/** Get the marginal bytes if spending the specified output from this transaction.
 * use_max_sig indicates whether to use the maximum sized, 72 byte signature when calculating the
 * size of the input spend. This should only be set when watch-only outputs are allowed */
int GetTxSpendSize(const CWallet& wallet, const CWalletTx& wtx, unsigned int out, bool use_max_sig = false);

//Get the marginal bytes of spending the specified output
int CalculateMaximumSignedInputSize(const CTxOut& txout, const CWallet* pwallet, bool use_max_sig = false);
int CalculateMaximumSignedInputSize(const CTxOut& txout, const SigningProvider* pwallet, bool use_max_sig = false);

struct TxSize {
    int64_t vsize{-1};
    int64_t weight{-1};
};

/** Calculate the size of the transaction assuming all signatures are max size
* Use DummySignatureCreator, which inserts 71 byte signatures everywhere.
* NOTE: this requires that all inputs must be in mapWallet (eg the tx should
* be AllInputsMine). */
TxSize CalculateMaximumSignedTxSize(const CTransaction& tx, const CWallet* wallet, const std::vector<CTxOut>& txouts, const CCoinControl* coin_control = nullptr);
TxSize CalculateMaximumSignedTxSize(const CTransaction& tx, const CWallet* wallet, const CCoinControl* coin_control = nullptr) EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet);

struct CoinsResult {
    std::vector<COutput> coins;
    // Sum of all the coins amounts
    CAmount total_amount{0};
};
/**
 * Return vector of available COutputs.
 * By default, returns only the spendable coins.
 */
CoinsResult AvailableCoins(const CWallet& wallet,
                           const CCoinControl* coinControl = nullptr,
                           std::optional<CFeeRate> feerate = std::nullopt,
                           const CAmount& nMinimumAmount = 1,
                           const CAmount& nMaximumAmount = MAX_MONEY,
                           const CAmount& nMinimumSumAmount = MAX_MONEY,
                           const uint64_t nMaximumCount = 0,
                           bool only_spendable = true) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);

/**
 * Wrapper function for AvailableCoins which skips the `feerate` parameter. Use this function
 * to list all available coins (e.g. listunspent RPC) while not intending to fund a transaction.
 */
CoinsResult AvailableCoinsListUnspent(const CWallet& wallet, const CCoinControl* coinControl = nullptr, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t nMaximumCount = 0) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);

CAmount GetAvailableBalance(const CWallet& wallet, const CCoinControl* coinControl = nullptr);

/**
 * Find non-change parent output.
 */
const CTxOut& FindNonChangeParentOutput(const CWallet& wallet, const CTransaction& tx, int output) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
const CTxOut& FindNonChangeParentOutput(const CWallet& wallet, const COutPoint& outpoint) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);

/**
 * Return list of available coins and locked coins grouped by non-change output address.
 */
std::map<CTxDestination, std::vector<COutput>> ListCoins(const CWallet& wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);

std::vector<OutputGroup> GroupOutputs(const CWallet& wallet, const std::vector<COutput>& outputs, const CoinSelectionParams& coin_sel_params, const CoinEligibilityFilter& filter, bool positive_only);

/**
 * Attempt to find a valid input set that meets the provided eligibility filter and target.
 * Multiple coin selection algorithms will be run and the input set that produces the least waste
 * (according to the waste metric) will be chosen.
 *
 * param@[in]  wallet                 The wallet which provides solving data for the coins
 * param@[in]  nTargetValue           The target value
 * param@[in]  eligilibity_filter     A filter containing rules for which coins are allowed to be included in this selection
 * param@[in]  coins                  The vector of coins available for selection prior to filtering
 * param@[in]  coin_selection_params  Parameters for the coin selection
 * returns                            If successful, a SelectionResult containing the input set
 *                                    If failed, a nullopt
 */
std::optional<SelectionResult> AttemptSelection(const CWallet& wallet, const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, std::vector<COutput> coins,
                        const CoinSelectionParams& coin_selection_params);

/**
 * Select a set of coins such that nTargetValue is met and at least
 * all coins from coin_control are selected; never select unconfirmed coins if they are not ours
 * param@[in]   wallet                 The wallet which provides data necessary to spend the selected coins
 * param@[in]   vAvailableCoins        The vector of coins available to be spent
 * param@[in]   nTargetValue           The target value
 * param@[in]   coin_selection_params  Parameters for this coin selection such as feerates, whether to avoid partial spends,
 *                                     and whether to subtract the fee from the outputs.
 * returns                             If successful, a SelectionResult containing the selected coins
 *                                     If failed, a nullopt.
 */
std::optional<SelectionResult> SelectCoins(const CWallet& wallet, const std::vector<COutput>& vAvailableCoins, const CAmount& nTargetValue, const CCoinControl& coin_control,
                 const CoinSelectionParams& coin_selection_params) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);

struct CreatedTransactionResult
{
    CTransactionRef tx;
    CAmount fee;
    int change_pos;

    CreatedTransactionResult(CTransactionRef tx, CAmount fee, int change_pos)
        : tx(tx), fee(fee), change_pos(change_pos) {}
};

/**
 * Create a new transaction paying the recipients with a set of coins
 * selected by SelectCoins(); Also create the change output, when needed
 * @note passing change_pos as -1 will result in setting a random position
 */
std::optional<CreatedTransactionResult> CreateTransaction(CWallet& wallet, const std::vector<CRecipient>& vecSend, const CTxIn* withInput, int change_pos, bilingual_str& error, const CCoinControl& coin_control, FeeCalculation& fee_calc_out, bool sign = true);

/**
 * Insert additional inputs into the transaction by
 * calling CreateTransaction();
 */
bool FundTransaction(CWallet& wallet, CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosInOut, bilingual_str& error, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CCoinControl);
} // namespace wallet

#endif // BITCOIN_WALLET_SPEND_H
