#!/usr/bin/env python3
# Licensed under CC0 (Public domain)

# Test that name_new and name_firstupdate use deterministic salts.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameDeterministicSaltTest(NameTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.setup_name_test ([[]])

    def add_wallet_options (self, parser):
        # Make sure we only allow (and use as default) legacy wallets,
        # as otherwise the hdseed setting won't work.
        super ().add_wallet_options (parser, descriptors=False, legacy=True)

    def run_test(self):
        node = self.nodes[0]
        node.sethdseed(seed="cQDxbmQfwRV3vP1mdnVHq37nJekHLsuD3wdSQseBRA2ct4MFk5Pq")
        assert(node.listwallets() != [])

        self.generate (node, 200)

        self.log.info("Begin registration of name.")
        new_name = node.name_new('d/wikileaks')

        self.log.info("Make sure salt matches.")
        assert_equal(new_name[1], '40d0f82281da919314fbb3331d4c43f5005ecc0e')

        self.generate (node, 12)
        self.log.info("Leave both fields blank (null).")
        node.name_firstupdate ("d/wikileaks")
        self.generate (node, 1)
        self.checkName(0, "d/wikileaks", "", 30, False)
        self.log.info("Name registered; no value provided.")

        self.log.info("Now let's register a name and give it the TXID, but leave it to figure out the rand value.")

        new_name = node.name_new('d/name2')
        self.generate (node, 12)
        node.name_firstupdate ("d/name2", None, new_name[0])
        self.generate (node, 1)
        self.checkName(0, "d/name2", "", 30, False)

        self.log.info("Now let's register a name and give it the rand value, but leave it to figure out the txid.")

        new_name = node.name_new('d/name3')
        self.generate (node, 12)
        node.name_firstupdate ("d/name3", new_name[1])
        self.generate (node, 1)
        self.checkName(0, "d/name3", "", 30, False)

        self.log.info("Neither gave any exceptions.")

        self.log.info("Now let's register a name and give it a wrong (but existing) TXID.")

        new_name1 = node.name_new('d/name4')
        new_name2 = node.name_new('d/dummy')
        self.generate (node, 12)
        assert_raises_rpc_error(-25, "generated rand for txid does not match", node.name_firstupdate, "d/name4", None, new_name2[0])

        self.log.info("Gave exception, as expected.")


if __name__ == '__main__':
    NameDeterministicSaltTest(__file__).main()
