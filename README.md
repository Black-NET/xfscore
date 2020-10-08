XFS
===============

[![latest-release](https://img.shields.io/github/release/Black-NET/xfscore)](https://github.com/Black-NET/xfscore/releases)
[![GitHub last-release](https://img.shields.io/github/release-date/Black-NET/xfscore)](https://github.com/Black-NET/xfscore/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/Black-NET/xfscore/total)](https://github.com/Black-NET/xfscore/releases)
[![GitHub commits-since-last-version](https://img.shields.io/github/commits-since/Black-NET/xfscore/latest/master)](https://github.com/Black-NET/xfscore/graphs/commit-activity)
[![GitHub commits-per-month](https://img.shields.io/github/commit-activity/m/Black-NET/xfscore)](https://github.com/Black-NET/xfscore/graphs/code-frequency)
[![GitHub last-commit](https://img.shields.io/github/last-commit/Black-NET/xfscore)](https://github.com/Black-NET/xfscore/commits/master)

# Windows & Linux release binary

https://github.com/Black-NET/xfscore/releases/tag/XFS.Core.v.1.0.0.4

[![latest-release](https://xfscore.gq/assets/img.github/Download.xfs-qt-win.64.v.1.0.0.4.PNG)](https://github.com/Black-NET/xfscore/files/5348398/xfs-qt-win64.zip)[![GitHub downloads](https://img.shields.io/github/downloads/Black-NET/xfscore/total)](https://github.com/Black-NET/xfscore/files/5348398/xfs-qt-win64.zip)

[![latest-release](https://xfscore.gq/assets/img.github/xfs-ubuntu18-binary.PNG)](https://github.com/Black-NET/xfscore/files/5348400/xfs-ubuntu18-binary.zip)[![GitHub downloads](https://img.shields.io/github/downloads/Black-NET/xfscore/total)](https://github.com/Black-NET/xfscore/files/5348400/xfs-ubuntu18-binary.zip)

[![latest-release](https://xfscore.gq/assets/img.github/xfs-macos-binary.PNG)](https://github.com/Black-NET/xfscore/files/5348395/xfs-macos-binary.zip)[![GitHub downloads](https://img.shields.io/github/downloads/Black-NET/xfscore/total)](https://github.com/Black-NET/xfscore/files/5348395/xfs-macos-binary.zip)

[![latest-release](https://xfscore.gq/assets/img.github/xfscore-source.PNG)](https://github.com/Black-NET/xfscore/archive/XFS.Core.v.1.0.0.4.zip)[![GitHub downloads](https://img.shields.io/github/downloads/Black-NET/xfscore/total)](https://github.com/Black-NET/xfscore/archive/XFS.Core.v.1.0.0.4.zip)


## Pools

https://eurohash.eu

https://pool.xfscore.gq

----------------------
## Link

https://xfscore.gq

https://forum.xfscore.gq

http://explorer.xfscore.gq

https://wallet.xfscore.gq


Linux Build Instructions and Notes
==================================

Dependencies
----------------------
1.  Update packages

        sudo apt-get update

2.  Install required packages

        sudo apt-get install build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev libzmq3-dev libminizip-dev

3.  Install Berkeley DB 4.8

        sudo apt-get install software-properties-common
        sudo add-apt-repository ppa:bitcoin/bitcoin
        sudo apt-get update
        sudo apt-get install libdb4.8-dev libdb4.8++-dev

4.  Install QT 5

        sudo apt-get install libminiupnpc-dev
        sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler libqrencode-dev

Build
----------------------
1.  Clone the source:

        git clone https://github.com/Black-NET/xfscore

2.  Build XFS Core:

    Configure and build the headless XFS binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.
        
        ./autogen.sh
        ./configure
        make


