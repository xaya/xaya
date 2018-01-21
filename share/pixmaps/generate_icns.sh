#!/bin/bash

# Prereqs: sudo apt-get install icnsutils
# Run this script from the directory where it is located.

png2icns ../../src/qt/res/icons/bitcoin.icns ./bitcoin16.png ./bitcoin32.png ./bitcoin128.png ./bitcoin256.png ./bitcoin512.png ./bitcoin1024.png
