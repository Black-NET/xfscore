// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEXFSNODE_H
#define ACTIVEXFSNODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

class CActiveXFSnode;

static const int ACTIVE_XFSNODE_INITIAL          = 0; // initial state
static const int ACTIVE_XFSNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_XFSNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_XFSNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_XFSNODE_STARTED          = 4;

extern CActiveXFSnode activeXFSnode;

// Responsible for activating the XFSnode and pinging the network
class CActiveXFSnode
{
public:
    enum xfsnode_type_enum_t {
        XFSNODE_UNKNOWN = 0,
        XFSNODE_REMOTE  = 1,
        XFSNODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    xfsnode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping XFSnode
    bool SendXFSnodePing();

public:
    // Keys for the active XFSnode
    CPubKey pubKeyXFSnode;
    CKey keyXFSnode;

    // Initialized while registering XFSnode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_XFSNODE_XXXX
    std::string strNotCapableReason;

    CActiveXFSnode()
        : eType(XFSNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyXFSnode(),
          keyXFSnode(),
          vin(),
          service(),
          nState(ACTIVE_XFSNODE_INITIAL)
    {}

    /// Manage state of active XFSnode
    void ManageState();

    // Change state if different and publish update
    void ChangeState(int newState);

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif
