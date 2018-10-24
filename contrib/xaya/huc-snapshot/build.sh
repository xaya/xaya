#!/bin/sh -e
# Simple script to build the standalone key converter HTML file from the
# source files.

browserify keyConversion.js -o keyConversion-full.js
cat keyConverter-head.html keyConversion-full.js keyConverter-tail.html \
  >keyConverter.html
rm -f keyConversion-full.js
