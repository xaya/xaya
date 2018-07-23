#!/bin/sh -e

PYTHONPATH="test/functional/test_framework"
contrib/namecoin/convertBechAddress.py $@
