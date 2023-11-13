XAYA
====

Setup
---------------------
[Xaya](https://xaya.io/) provides the official Xaya client and it builds the backbone of the network. However, it downloads and stores the entire history of Xaya transactions; depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more.

To download Namecoin Core, visit [namecoin.org](https://namecoin.org/download/).

Running
---------------------
The following are some helpful notes on how to run Xaya on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/xaya-qt` (GUI) or
- `bin/xayad` (headless)

### Windows

Unpack the files into a directory, and then run xaya-qt.exe.

### macOS

Drag Xaya-Qt to your applications folder, and then run Xaya-Qt.

### Need Help?

* See the documentation at the [Xaya Site](https://xaya.io)
for help and more information.

Building
---------------------
The following are developer notes on how to build Bitcoin Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [FreeBSD Build Notes](build-freebsd.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Android Build Notes](build-android.md)

Development
---------------------
The Xaya repo's [root README](https://github.com/xaya/xaya/blob/master/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://doxygen.bitcoincore.org/)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)
- [Internal Design Docs](design/)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [bitcoin.conf Configuration File](bitcoin-conf.md)
- [CJDNS Support](cjdns.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [Multisig Tutorial](multisig-tutorial.md)
- [Offline Signing Tutorial](offline-signing-tutorial.md)
- [P2P bad ports definition and list](p2p-bad-ports.md)
- [PSBT support](psbt.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Transaction Relay Policy](policy/README.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
