// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "testutil.h"

#ifdef WIN32
#include <shlobj.h>
#endif

#include "fs.h"

fs::path GetTempPath() {
    return fs::temp_directory_path();
}

//! equality test
bool operator==(const Coin &a, const Coin &b) {
    // Empty Coin objects are always equal.
    if (a.IsSpent() && b.IsSpent()) return true;
    return a.fCoinBase == b.fCoinBase &&
           a.nHeight == b.nHeight &&
           a.out == b.out;
}
