#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test REST interface
#

from test_framework import BitcoinTestFramework
from util import *
import auxpow

import binascii
import json
import urllib

try:
    import http.client as httplib
except ImportError:
    import httplib
try:
    import urllib.parse as urlparse
except ImportError:
    import urlparse

def http_get_call(host, port, path, response_object = 0):
    conn = httplib.HTTPConnection(host, port)
    conn.request('GET', path)

    if response_object:
        return conn.getresponse()

    return conn.getresponse().read()


class RESTTest (BitcoinTestFramework):
    FORMAT_SEPARATOR = "."

    def run_test(self):
        auxpow.mineAuxpowBlock(self.nodes[0])
        self.sync_all()

        url = urlparse.urlparse(self.nodes[0].url)
        bb_hash = self.nodes[0].getbestblockhash()

        # check binary format
        response = http_get_call(url.hostname, url.port, '/rest/block/'+bb_hash+self.FORMAT_SEPARATOR+"bin", True)
        assert_equal(response.status, 200)
        assert_greater_than(int(response.getheader('content-length')), 80)
        response_str = response.read()

        # compare with block header
        response_header = http_get_call(url.hostname, url.port, '/rest/headers/1/'+bb_hash+self.FORMAT_SEPARATOR+"bin", True)
        assert_equal(response_header.status, 200)
        headerLen = int(response_header.getheader('content-length'))
        assert_greater_than(headerLen, 80)
        response_header_str = response_header.read()
        assert_equal(response_str[0:headerLen], response_header_str)

        # check block hex format
        response_hex = http_get_call(url.hostname, url.port, '/rest/block/'+bb_hash+self.FORMAT_SEPARATOR+"hex", True)
        assert_equal(response_hex.status, 200)
        assert_greater_than(int(response_hex.getheader('content-length')), 160)
        response_hex_str = response_hex.read().strip()
        assert_equal(response_str.encode("hex"), response_hex_str)

        # compare with hex block header
        response_header_hex = http_get_call(url.hostname, url.port, '/rest/headers/1/'+bb_hash+self.FORMAT_SEPARATOR+"hex", True)
        assert_equal(response_header_hex.status, 200)
        assert_greater_than(int(response_header_hex.getheader('content-length')), 160)
        response_header_hex_str = response_header_hex.read().strip()
        headerLen = len (response_header_hex_str)
        assert_equal(response_hex_str[0:headerLen], response_header_hex_str)
        assert_equal(response_header_str.encode("hex"), response_header_hex_str)

        # check json format
        json_string = http_get_call(url.hostname, url.port, '/rest/block/'+bb_hash+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        assert_equal(json_obj['hash'], bb_hash)

        # do tx test
        tx_hash = json_obj['tx'][0]['txid'];
        json_string = http_get_call(url.hostname, url.port, '/rest/tx/'+tx_hash+self.FORMAT_SEPARATOR+"json")
        json_obj = json.loads(json_string)
        assert_equal(json_obj['txid'], tx_hash)

        # check hex format response
        hex_string = http_get_call(url.hostname, url.port, '/rest/tx/'+tx_hash+self.FORMAT_SEPARATOR+"hex", True)
        assert_equal(response.status, 200)
        assert_greater_than(int(response.getheader('content-length')), 10)

        # check block tx details
        # let's make 3 tx and mine them on node 1
        txs = []
        txs.append(self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 11))
        txs.append(self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 11))
        txs.append(self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 11))
        self.sync_all()

        # now mine the transactions
        newblockhash = self.nodes[1].setgenerate(True, 1)
        self.sync_all()

        #check if the 3 tx show up in the new block
        json_string = http_get_call(url.hostname, url.port, '/rest/block/'+newblockhash[0]+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        for tx in json_obj['tx']:
            if not 'coinbase' in tx['vin'][0]: #exclude coinbase
                assert_equal(tx['txid'] in txs, True)

        #check the same but without tx details
        json_string = http_get_call(url.hostname, url.port, '/rest/block/notxdetails/'+newblockhash[0]+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        for tx in txs:
            assert_equal(tx in json_obj['tx'], True)

        # Test name handling.
        self.name_tests(url)

    def name_tests(self, url):
        """
        Run REST tests specific to names.
        """

        # Start by registering a test name.
        name = "d/some weird.name++"
        binData = binascii.unhexlify("0001")
        value = "correct value\nwith newlines\nand binary: " + binData
        newData = self.nodes[0].name_new(name)
        self.nodes[0].setgenerate(True, 10)
        self.nodes[0].name_firstupdate(name, newData[1], newData[0], value)
        self.nodes[0].setgenerate(True, 5)
        nameData = self.nodes[0].name_show(name)
        assert_equal(nameData['name'], name)
        assert_equal(nameData['value'], value)

        # Different variants of the encoded name that should all work.
        variants = [urllib.quote_plus(name), "d/some+weird.name%2b%2B"]

        for encName in variants:

            # Query JSON data of the name.
            query = '/rest/name/' + encName + self.FORMAT_SEPARATOR + 'json'
            res = http_get_call(url.hostname, url.port, query, True)
            assert_equal(res.status, 200)
            data = json.loads(res.read())
            assert_equal(data, nameData)

            # Query plain value.
            query = '/rest/name/' + encName + self.FORMAT_SEPARATOR + 'bin'
            res = http_get_call(url.hostname, url.port, query, True)
            assert_equal(res.status, 200)
            assert_equal(res.read(), value)

            # Query hex value.
            query = '/rest/name/' + encName + self.FORMAT_SEPARATOR + 'hex'
            res = http_get_call(url.hostname, url.port, query, True)
            assert_equal(res.status, 200)
            assert_equal(res.read(), binascii.hexlify(value) + "\n")

        # Check invalid encoded names.
        invalid = ['%', '%2', '%2x', '%x2']
        for encName in invalid:
            query = '/rest/name/' + encName + self.FORMAT_SEPARATOR + 'bin'
            res = http_get_call(url.hostname, url.port, query, True)
            assert_equal(res.status, httplib.BAD_REQUEST)

if __name__ == '__main__':
    RESTTest ().main ()
