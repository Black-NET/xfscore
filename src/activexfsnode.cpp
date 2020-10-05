// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activexfsnode.h"
#include "consensus/consensus.h"
#include "xfsnode.h"
#include "xfsnode-sync.h"
#include "xfsnode-payments.h"
#include "xfsnodeman.h"
#include "protocol.h"
#include "validationinterface.h"

extern CWallet *pwalletMain;

// Keep track of the active XFSnode
CActiveXFSnode activeXFSnode;

void CActiveXFSnode::ManageState() {
    LogPrint("xfsnode", "CActiveXFSnode::ManageState -- Start\n");
    if (!fXFSNode) {
        LogPrint("xfsnode", "CActiveXFSnode::ManageState -- Not a xfsnode, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !xfsnodeSync.GetBlockchainSynced()) {
        ChangeState(ACTIVE_XFSNODE_SYNC_IN_PROCESS);
        LogPrintf("CActiveXFSnode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_XFSNODE_SYNC_IN_PROCESS) {
        ChangeState(ACTIVE_XFSNODE_INITIAL);
    }

    LogPrint("xfsnode", "CActiveXFSnode::ManageState -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == XFSNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == XFSNODE_REMOTE) {
        ManageStateRemote();
    } else if (eType == XFSNODE_LOCAL) {
        // Try Remote Start first so the started local xfsnode can be restarted without recreate xfsnode broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_XFSNODE_STARTED)
            ManageStateLocal();
    }

    SendXFSnodePing();
}

std::string CActiveXFSnode::GetStateString() const {
    switch (nState) {
        case ACTIVE_XFSNODE_INITIAL:
            return "INITIAL";
        case ACTIVE_XFSNODE_SYNC_IN_PROCESS:
            return "SYNC_IN_PROCESS";
        case ACTIVE_XFSNODE_INPUT_TOO_NEW:
            return "INPUT_TOO_NEW";
        case ACTIVE_XFSNODE_NOT_CAPABLE:
            return "NOT_CAPABLE";
        case ACTIVE_XFSNODE_STARTED:
            return "STARTED";
        default:
            return "UNKNOWN";
    }
}

void CActiveXFSnode::ChangeState(int state) {
    if(nState!=state){
        nState = state;
    }
}

std::string CActiveXFSnode::GetStatus() const {
    switch (nState) {
        case ACTIVE_XFSNODE_INITIAL:
            return "Node just started, not yet activated";
        case ACTIVE_XFSNODE_SYNC_IN_PROCESS:
            return "Sync in progress. Must wait until sync is complete to start XFSnode";
        case ACTIVE_XFSNODE_INPUT_TOO_NEW:
            return strprintf("XFSnode input must have at least %d confirmations",
                             Params().GetConsensus().nXFSnodeMinimumConfirmations);
        case ACTIVE_XFSNODE_NOT_CAPABLE:
            return "Not capable xfsnode: " + strNotCapableReason;
        case ACTIVE_XFSNODE_STARTED:
            return "XFSnode successfully started";
        default:
            return "Unknown";
    }
}

std::string CActiveXFSnode::GetTypeString() const {
    std::string strType;
    switch (eType) {
        case XFSNODE_UNKNOWN:
            strType = "UNKNOWN";
            break;
        case XFSNODE_REMOTE:
            strType = "REMOTE";
            break;
        case XFSNODE_LOCAL:
            strType = "LOCAL";
            break;
        default:
            strType = "UNKNOWN";
            break;
    }
    return strType;
}

bool CActiveXFSnode::SendXFSnodePing() {
    if (!fPingerEnabled) {
        LogPrint("xfsnode",
                 "CActiveXFSnode::SendXFSnodePing -- %s: xfsnode ping service is disabled, skipping...\n",
                 GetStateString());
        return false;
    }

    if (!mnodeman.Has(vin)) {
        strNotCapableReason = "XFSnode not in xfsnode list";
        ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
        LogPrintf("CActiveXFSnode::SendXFSnodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CXFSnodePing mnp(vin);
    if (!mnp.Sign(keyXFSnode, pubKeyXFSnode)) {
        LogPrintf("CActiveXFSnode::SendXFSnodePing -- ERROR: Couldn't sign XFSnode Ping\n");
        return false;
    }

    // Update lastPing for our xfsnode in XFSnode list
    if (mnodeman.IsXFSnodePingedWithin(vin, XFSNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveXFSnode::SendXFSnodePing -- Too early to send XFSnode Ping\n");
        return false;
    }

    mnodeman.SetXFSnodeLastPing(vin, mnp);

    LogPrintf("CActiveXFSnode::SendXFSnodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveXFSnode::ManageStateInitial() {
    LogPrint("xfsnode", "CActiveXFSnode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
        strNotCapableReason = "XFSnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CXFSnode::IsValidNetAddr(service);
        if (!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode * pnode, vNodes)
            {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CXFSnode::IsValidNetAddr(service);
                    if (fFoundLocal) break;
                }
            }
        }
    }

    if (!fFoundLocal) {
        ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(),
                                            mainnetDefaultPort);
            LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(),
                                        mainnetDefaultPort);
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveXFSnode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());
    //TODO
    if (!ConnectNode(CAddress(service, NODE_NETWORK), NULL, false, true)) {
        ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = XFSNODE_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < XFSNODE_COIN_REQUIRED * COIN) {
        LogPrintf("CActiveXFSnode::ManageStateInitial -- %s: Wallet balance is < 5000 XFS\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode
    if (pwalletMain->GetXFSnodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = XFSNODE_LOCAL;
    }

    LogPrint("xfsnode", "CActiveXFSnode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveXFSnode::ManageStateRemote() {
    LogPrint("xfsnode",
             "CActiveXFSnode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyXFSnode.GetID() = %s\n",
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyXFSnode.GetID().ToString());

    mnodeman.CheckXFSnode(pubKeyXFSnode);
    xfsnode_info_t infoMn = mnodeman.GetXFSnodeInfo(pubKeyXFSnode);

    if (infoMn.fInfoValid) {
        if (infoMn.nProtocolVersion < MIN_XFSNODE_PAYMENT_PROTO_VERSION_1 || infoMn.nProtocolVersion > MIN_XFSNODE_PAYMENT_PROTO_VERSION_2) {
            ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveXFSnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
            // LogPrintf("service: %s\n", service.ToString());
            // LogPrintf("infoMn.addr: %s\n", infoMn.addr.ToString());
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this xfsnode changed recently.";
            LogPrintf("CActiveXFSnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CXFSnode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
            strNotCapableReason = strprintf("XFSnode in %s state", CXFSnode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveXFSnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_XFSNODE_STARTED) {
            LogPrintf("CActiveXFSnode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            ChangeState(ACTIVE_XFSNODE_STARTED);
        }
    } else {
        ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
        strNotCapableReason = "XFSnode not in xfsnode list";
        LogPrintf("CActiveXFSnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveXFSnode::ManageStateLocal() {
    LogPrint("xfsnode", "CActiveXFSnode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_XFSNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetXFSnodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge < Params().GetConsensus().nXFSnodeMinimumConfirmations) {
            ChangeState(ACTIVE_XFSNODE_INPUT_TOO_NEW);
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveXFSnode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CXFSnodeBroadcast mnb;
        std::string strError;
        if (!CXFSnodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyXFSnode,
                                     pubKeyXFSnode, strError, mnb)) {
            ChangeState(ACTIVE_XFSNODE_NOT_CAPABLE);
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveXFSnode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        ChangeState(ACTIVE_XFSNODE_STARTED);

        //update to xfsnode list
        LogPrintf("CActiveXFSnode::ManageStateLocal -- Update XFSnode List\n");
        mnodeman.UpdateXFSnodeList(mnb);
        mnodeman.NotifyXFSnodeUpdates();

        //send to all peers
        LogPrintf("CActiveXFSnode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.RelayXFSNode();
    }
}
