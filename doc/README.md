Namecoin Core 0.13.99
=====================

Setup
---------------------
[Namecoin Core](http://namecoin.org/) is the official Namecoin client and it builds the backbone of the network. However, it downloads and stores the entire history of Namecoin transactions (which is currently several GBs); depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more.

Running
---------------------
The following are some helpful notes on how to run Namecoin on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/namecoin-qt` (GUI) or
- `bin/namecoind` (headless)

### Windows

Unpack the files into a directory, and then run namecoin-qt.exe.

### OS X

Drag Namecoin-Qt to your applications folder, and then run Namecoin-Qt.

### Need Help?

* See the documentation at the [Namecoin Wiki](https://wiki.namecoin.org/index.php?title=Welcome)
for help and more information.
* Ask for help on [#namecoin](http://webchat.freenode.net?channels=namecoin) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net?channels=namecoin).
* Ask for help on the [Namecoin forums](https://forum.namecoin.info/index.php), in the [Technical Support board](https://forum.namecoin.info/viewforum.php?f=7).

Building
---------------------
The following are developer notes on how to build Bitcoin on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OS X Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Namecoin repo's [root README](https://github.com/namecoin/namecore/blob/master/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/bitcoin/doxygen/)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss on the [Namecoin forums](https://forum.namecoin.info/index.php), in the [Development & Technical Discussion board](https://forum.namecoin.info/viewforum.php?f=8).
* Discuss on [#namecoin-dev](http://webchat.freenode.net/?channels=namecoin-dev) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net/?channels=namecoin-dev).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
