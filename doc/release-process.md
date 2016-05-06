Release Process
====================

Before every release candidate:

* ( **Not in Namecoin yet.** ) Update translations (ping wumpus on IRC) see [translation_process.md](https://github.com/namecoin/namecoin-core/blob/master/doc/translation_process.md#syncing-with-transifex)

Before every minor and major release:

* ( **Not in Namecoin yet.** ) Update [bips.md](bips.md) to account for changes since the last release.
* Update version in sources (see below)
* Write release notes (see below)

Before every major release:

* Update hardcoded [seeds](/contrib/seeds/README.md), see [this pull request](https://github.com/bitcoin/bitcoin/pull/7415) for an example. 

### First time / New builders

Check out the source code in the following directory hierarchy.

    cd /path/to/your/toplevel/build
    git clone https://github.com/namecoin/gitian.sigs.git
    #git clone https://github.com/namecoin/namecoin-detached-sigs.git # Namecoin doesn't use detached sigs yet, so don't do this.
    git clone https://github.com/devrandom/gitian-builder.git
    git clone https://github.com/namecoin/namecoin-core.git

### Namecoin maintainers/release engineers, update version in sources

Update the following:

- `configure.ac`:
    - `_CLIENT_VERSION_MAJOR`
    - `_CLIENT_VERSION_MINOR`
    - `_CLIENT_VERSION_REVISION`
    - Don't forget to set `_CLIENT_VERSION_IS_RELEASE` to `true`
- `src/clientversion.h`: (this mirrors `configure.ac` - see issue #3539)
    - `CLIENT_VERSION_MAJOR`
    - `CLIENT_VERSION_MINOR`
    - `CLIENT_VERSION_REVISION`
    - Don't forget to set `CLIENT_VERSION_IS_RELEASE` to `true`
- `doc/README.md` and `doc/README_windows.txt`
- `doc/Doxyfile`: `PROJECT_NUMBER` contains the full version
- `contrib/gitian-descriptors/*.yml`: usually one'd want to do this on master after branching off the release - but be sure to at least do it before a new major release

Write release notes. git shortlog helps a lot, for example:

    git shortlog --no-merges nc(current version, e.g. 0.7.2)..nc(new version, e.g. 0.8.0)

( **Not in Namecoin yet.** ) (or ping @wumpus on IRC, he has specific tooling to generate the list of merged pulls
and sort them into categories based on labels)

Generate list of authors:

    git log --format='%aN' "$*" | sort -ui | sed -e 's/^/- /'

Tag version (or release candidate) in git

    git tag -s nc(new version, e.g. 0.8.0)

### Setup and perform Gitian builds

Setup Gitian descriptors:

    pushd ./namecoin-core
    export SIGNER=(your Gitian key, ie bluematt, sipa, etc)
    export VERSION=(new version, e.g. 0.8.0)
    git fetch
    git checkout nc${VERSION}
    popd

Ensure your gitian.sigs are up-to-date if you wish to gverify your builds against other Gitian signatures.

    pushd ./gitian.sigs
    git pull
    popd

Ensure gitian-builder is up-to-date to take advantage of new caching features (`e9741525c` or later is recommended).

    pushd ./gitian-builder
    git pull
    popd

### Fetch and create inputs: (first time, or when dependency versions change)

    pushd ./gitian-builder
    mkdir -p inputs
    wget -P inputs https://bitcoincore.org/cfields/osslsigncode-Backports-to-1.7.1.patch
    wget -P inputs http://downloads.sourceforge.net/project/osslsigncode/osslsigncode/osslsigncode-1.7.1.tar.gz
    popd

 ( **Not in Namecoin yet.** ) Register and download the Apple SDK: see [OS X readme](README_osx.txt) for details.

https://developer.apple.com/devcenter/download.action?path=/Developer_Tools/xcode_6.1.1/xcode_6.1.1.dmg

 ( **Not in Namecoin yet.** ) Using a Mac, create a tarball for the 10.9 SDK and copy it to the inputs directory:

    tar -C /Volumes/Xcode/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/ -czf MacOSX10.9.sdk.tar.gz MacOSX10.9.sdk

### Optional: Seed the Gitian sources cache and offline git repositories

By default, Gitian will fetch source files as needed. To cache them ahead of time:

    pushd ./gitian-builder
    make -C ../namecoin-core/depends download SOURCES_PATH=`pwd`/cache/common
    popd

Only missing files will be fetched, so this is safe to re-run for each build.

NOTE: Offline builds must use the --url flag to ensure Gitian fetches only from local URLs. For example:

    pushd ./gitian-builder
    ./bin/gbuild --url namecoin=/path/to/namecoin,signature=/path/to/sigs {rest of arguments}
    popd

The gbuild invocations below <b>DO NOT DO THIS</b> by default.

### Build and sign Namecoin Core for Linux, Windows, and OS X:

    pushd ./gitian-builder
    ./bin/gbuild --commit namecoin=nc${VERSION} ../namecoin-core/contrib/gitian-descriptors/gitian-linux.yml
    ./bin/gsign --signer $SIGNER --release ${VERSION}-linux --destination ../gitian.sigs/ ../namecoin-core/contrib/gitian-descriptors/gitian-linux.yml
    mv build/out/namecoin-*.tar.gz build/out/src/namecoin-*.tar.gz ../

    ./bin/gbuild --commit namecoin=nc${VERSION} ../namecoin-core/contrib/gitian-descriptors/gitian-win.yml
    ./bin/gsign --signer $SIGNER --release ${VERSION}-win-unsigned --destination ../gitian.sigs/ ../namecoin-core/contrib/gitian-descriptors/gitian-win.yml
    mv build/out/namecoin-*-win-unsigned.tar.gz inputs/namecoin-win-unsigned.tar.gz
    mv build/out/namecoin-*.zip build/out/namecoin-*.exe ../

( **OS X is not in Namecoin yet.** )

    ./bin/gbuild --commit namecoin=nc${VERSION} ../namecoin-core/contrib/gitian-descriptors/gitian-osx.yml
    ./bin/gsign --signer $SIGNER --release ${VERSION}-osx-unsigned --destination ../gitian.sigs/ ../namecoin-core/contrib/gitian-descriptors/gitian-osx.yml
    mv build/out/namecoin-*-osx-unsigned.tar.gz inputs/namecoin-osx-unsigned.tar.gz
    mv build/out/namecoin-*.tar.gz build/out/namecoin-*.dmg ../
    popd

Build output expected:

  1. source tarball (`namecoin-${VERSION}.tar.gz`)
  2. linux 32-bit and 64-bit dist tarballs (`namecoin-${VERSION}-linux[32|64].tar.gz`)
  3. windows 32-bit and 64-bit unsigned installers and dist zips (`namecoin-${VERSION}-win[32|64]-setup-unsigned.exe`, `namecoin-${VERSION}-win[32|64].zip`)
  4. ( **Not in Namecoin yet.** ) OS X unsigned installer and dist tarball (`namecoin-${VERSION}-osx-unsigned.dmg`, `namecoin-${VERSION}-osx64.tar.gz`)
  5. Gitian signatures (in `gitian.sigs/${VERSION}-<linux|{win,osx}-unsigned>/(your Gitian key)/`)

### Verify other gitian builders signatures to your own. (Optional)

Add other gitian builders keys to your gpg keyring

    gpg --import namecoin-core/contrib/gitian-keys/*.pgp

Verify the signatures

    pushd ./gitian-builder
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-linux ../namecoin-core/contrib/gitian-descriptors/gitian-linux.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-win-unsigned ../namecoin-core/contrib/gitian-descriptors/gitian-win.yml
    #./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-osx-unsigned ../namecoin-core/contrib/gitian-descriptors/gitian-osx.yml # OS X is not in Namecoin yet, so don't do this.
    popd

### Next steps:

Commit your signature to gitian.sigs:

    pushd gitian.sigs
    git add ${VERSION}-linux/${SIGNER}
    git add ${VERSION}-win-unsigned/${SIGNER}
    #git add ${VERSION}-osx-unsigned/${SIGNER} # OS X is not in Namecoin yet, so don't do this.
    git commit -a
    git push  # Assuming you can push to the gitian.sigs tree
    popd

( **Not in Namecoin yet.** ) Wait for Windows/OS X detached signatures:

- Once the Windows/OS X builds each have 3 matching signatures, they will be signed with their respective release keys.
- Detached signatures will then be committed to the [namecoin-detached-sigs](https://github.com/namecoin/namecoin-detached-sigs) repository, which can be combined with the unsigned apps to create signed binaries.

( **Not in Namecoin yet.** ) Create (and optionally verify) the signed OS X binary:

    pushd ./gitian-builder
    ./bin/gbuild -i --commit signature=nc${VERSION} ../namecoin-core/contrib/gitian-descriptors/gitian-osx-signer.yml
    ./bin/gsign --signer $SIGNER --release ${VERSION}-osx-signed --destination ../gitian.sigs/ ../namecoin-core/contrib/gitian-descriptors/gitian-osx-signer.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-osx-signed ../namecoin-core/contrib/gitian-descriptors/gitian-osx-signer.yml
    mv build/out/namecoin-osx-signed.dmg ../namecoin-${VERSION}-osx.dmg
    popd

( **Not in Namecoin yet.** ) Create (and optionally verify) the signed Windows binaries:

    pushd ./gitian-builder
    ./bin/gbuild -i --commit signature=nc${VERSION} ../namecoin-core/contrib/gitian-descriptors/gitian-win-signer.yml
    ./bin/gsign --signer $SIGNER --release ${VERSION}-win-signed --destination ../gitian.sigs/ ../namecoin-core/contrib/gitian-descriptors/gitian-win-signer.yml
    ./bin/gverify -v -d ../gitian.sigs/ -r ${VERSION}-win-signed ../namecoin-core/contrib/gitian-descriptors/gitian-win-signer.yml
    mv build/out/namecoin-*win64-setup.exe ../namecoin-${VERSION}-win64-setup.exe
    mv build/out/namecoin-*win32-setup.exe ../namecoin-${VERSION}-win32-setup.exe
    popd

( **Not in Namecoin yet.** ) Commit your signature for the signed OS X/Windows binaries:

    pushd gitian.sigs
    git add ${VERSION}-osx-signed/${SIGNER}
    git add ${VERSION}-win-signed/${SIGNER}
    git commit -a
    git push  # Assuming you can push to the gitian.sigs tree
    popd

### After 3 or more people have gitian-built and their results match:

- Create `SHA256SUMS.asc` for the builds, and GPG-sign it:

```bash
sha256sum * > SHA256SUMS
```

The list of files should be:
```
namecoin-${VERSION}-linux32.tar.gz
namecoin-${VERSION}-linux64.tar.gz
namecoin-${VERSION}-osx64.tar.gz     # Not in Namecoin yet.
namecoin-${VERSION}-osx.dmg          # Not in Namecoin yet.
namecoin-${VERSION}.tar.gz
namecoin-${VERSION}-win32-setup.exe
namecoin-${VERSION}-win32.zip
namecoin-${VERSION}-win64-setup.exe
namecoin-${VERSION}-win64.zip
```

- GPG-sign it, delete the unsigned file:
```
gpg --digest-algo sha256 --clearsign SHA256SUMS # outputs SHA256SUMS.asc
rm SHA256SUMS
```
(the digest algorithm is forced to sha256 to avoid confusion of the `Hash:` header that GPG adds with the SHA256 used for the files)
Note: check that SHA256SUMS itself doesn't end up in SHA256SUMS, which is a spurious/nonsensical entry.

( **The following is not in Namecoin yet.** )

- Upload zips and installers, as well as `SHA256SUMS.asc` from last step, to the bitcoin.org server
  into `/var/www/bin/bitcoin-core-${VERSION}`

- A `.torrent` will appear in the directory after a few minutes. Optionally help seed this torrent. To get the `magnet:` URI use:
```bash
transmission-show -m <torrent file>
```
Insert the magnet URI into the announcement sent to mailing lists. This permits
people without access to `bitcoin.org` to download the binary distribution.
Also put it into the `optional_magnetlink:` slot in the YAML file for
bitcoin.org (see below for bitcoin.org update instructions). 

- Update bitcoin.org version

  - First, check to see if the Bitcoin.org maintainers have prepared a
    release: https://github.com/bitcoin-dot-org/bitcoin.org/labels/Releases

      - If they have, it will have previously failed their Travis CI
        checks because the final release files weren't uploaded.
        Trigger a Travis CI rebuild---if it passes, merge.

  - If they have not prepared a release, follow the Bitcoin.org release
    instructions: https://github.com/bitcoin-dot-org/bitcoin.org#release-notes

  - After the pull request is merged, the website will automatically show the newest version within 15 minutes, as well
    as update the OS download links. Ping @saivann/@harding (saivann/harding on Freenode) in case anything goes wrong

- Announce the release:

  - bitcoin-dev and bitcoin-core-dev mailing list

  - Bitcoin Core announcements list https://bitcoincore.org/en/list/announcements/join/

  - bitcoincore.org blog post

  - Update title of #bitcoin on Freenode IRC

  - Optionally twitter, reddit /r/Bitcoin, ... but this will usually sort out itself

  - Notify BlueMatt so that he can start building [the PPAs](https://launchpad.net/~bitcoin/+archive/ubuntu/bitcoin)

  - Add release notes for the new version to the directory `doc/release-notes` in git master

  - Celebrate
