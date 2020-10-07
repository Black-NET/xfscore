XFS
===============

[![latest-release](https://img.shields.io/github/release/xfscore/wallet)](https://github.com/xfscore/releases)
[![GitHub last-release](https://img.shields.io/github/release-date/xfscore/wallet)](https://github.com/xfscore/wallet/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/xfscore/wallet/total)](https://github.com/xfscore/wallet/releases)
[![GitHub commits-since-last-version](https://img.shields.io/github/commits-since/xfscore/wallet/latest/master)](https://github.com/xfscore/wallet/graphs/commit-activity)
[![GitHub commits-per-month](https://img.shields.io/github/commit-activity/m/xfscore/wallet)](https://github.com/xfscore/wallet/graphs/code-frequency)
[![GitHub last-commit](https://img.shields.io/github/last-commit/xfscore/wallet)](https://github.com/xfscore/wallet/commits/master)

# Windows & Linux release binary

https://github.com/xfscore/wallet/releases/tag/v1.0.0.4

## Pools

https://eurohash.eu

https://pool.xfscore.org

----------------------

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

        git clone https://github.com/xfscore/wallet

2.  Build XFS Core:

    Configure and build the headless XFS binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.
        
        ./autogen.sh
        ./configure
        make


