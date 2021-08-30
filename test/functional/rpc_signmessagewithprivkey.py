#!/usr/bin/env python3
# Copyright (c) 2016-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RPC commands for signing messages with private key."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

import base64

class SignMessagesWithPrivTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        message = 'This is just a test message'

        self.log.info('test signing with priv_key')
        priv_key = 'b9Re9AqiHcCp1L1UB9QwFfSHdc5d9vS7km7PX6Vt3sPAjmCAQzYc'
        address = 'cZZY6ATUpST3PWrVnequMHTytE2S7uZGYL'
        expected_signature = 'H16OYOEyKo8Sz3UWB6Qc8kNn3omIw+a6yCtufZGG27d2em1k0Mw8a6L7Im8d/Nnpehv0xwjsAUkecRE0VlUg6/8='
        signature = self.nodes[0].signmessagewithprivkey(priv_key, message)
        assert_equal(expected_signature, signature)
        assert self.nodes[0].verifymessage(address, signature, message)

        self.log.info('test parameter validity and error codes')
        # signmessagewithprivkey has two required parameters
        for num_params in [0, 1, 3, 4, 5]:
            param_list = ["dummy"]*num_params
            assert_raises_rpc_error(-1, "signmessagewithprivkey", self.nodes[0].signmessagewithprivkey, *param_list)
        # verifymessage has three required parameters
        for num_params in [0, 1, 2, 4, 5]:
            param_list = ["dummy"]*num_params
            assert_raises_rpc_error(-1, "verifymessage", self.nodes[0].verifymessage, *param_list)
        # invalid key or address provided
        assert_raises_rpc_error(-5, "Invalid private key", self.nodes[0].signmessagewithprivkey, "invalid_key", message)
        assert_raises_rpc_error(-5, "Invalid address", self.nodes[0].verifymessage, "invalid_addr", signature, message)
        # malformed signature provided
        assert_raises_rpc_error(-3, "Malformed base64 encoding", self.nodes[0].verifymessage, 'cZZY6ATUpST3PWrVnequMHTytE2S7uZGYL', "invalid_sig", message)

        self.log.info('test extracting address from signature')
        res = self.nodes[0].verifymessage("", signature, message)
        assert_equal(res, {
            "valid": True,
            "address": address,
        })
        assert_equal(res["address"], address)

        self.log.info('test extracting address from invalid signature')
        res = self.nodes[0].verifymessage("", base64.b64encode(b"some data").decode("ascii"), message)
        assert_equal(res, {
            "valid": False,
        })

if __name__ == '__main__':
    SignMessagesWithPrivTest().main()
