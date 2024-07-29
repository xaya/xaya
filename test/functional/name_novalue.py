#!/usr/bin/env python3
# Licensed under CC0 (Public domain)

# Test that name_firstupdate and name_update handle missing
# name values gracefully. This test uses direct RPC calls.
# Testing the wrappers is out of scope.

from test_framework.names import NameTestFramework
from test_framework.util import *

class NameNovalueTest(NameTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.setup_name_test ([["-namehistory", "-limitnamechains=3"]])

    def run_test(self):
        node = self.nodes[0]
        self.generate (node, 200)

        new_name = node.name_new("d/name")
        self.generate (node, 12)
        self.log.info("Began registration of name.")

        # use node.name_firstupdate - we're not testing the framework
        node.name_firstupdate ("d/name", new_name[1], new_name[0])
        self.generate (node, 1)
        self.log.info("Name registered; no value provided.")

        self.checkName(0, "d/name", "", 30, False)
        self.log.info("Value equals empty string.")

        node.name_update("d/name", "1")
        self.generate (node, 1)
        self.log.info('Value changed to "1"; change is in chain.')

        self.checkName(0, "d/name", "1", 30, False)
        self.log.info('Value equals "1".')

        node.name_update("d/name")
        self.generate (node, 1)
        self.log.info("Updated; no value specified.")

        self.checkName(0, "d/name", "1", 30, False)
        self.log.info('Name was updated. Value still equals "1".')

        node.name_update("d/name", "2")
        node.name_update("d/name")
        node.name_update("d/name", "3")
        self.generate (node, 1)
        self.log.info('Stack of 3 registrations performed.')

        history = node.name_history("d/name")
        reference = ["", "1", "1", "2", "2", "3"]
        assert(list(map(lambda x: x['value'], history)) == reference)
        self.log.info('Updates correctly consider updates in the mempool.')


if __name__ == '__main__':
    NameNovalueTest (__file__).main ()
