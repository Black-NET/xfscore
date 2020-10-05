// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activexfsnode.h"
#include "checkpoints.h"
#include "main.h"
#include "xfsnode.h"
#include "xfsnode-payments.h"
#include "xfsnode-sync.h"
#include "xfsnodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"
#include "validationinterface.h"

class CXFSnodeSync;

static bool fBlockchainSynced = false;

CXFSnodeSync xfsnodeSync;

bool CXFSnodeSync::CheckNodeHeight(CNode *pnode, bool fDisconnectStuckNodes) {
    CNodeStateStats stats;
    if (!GetNodeStateStats(pnode->id, stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if (pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if (fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CXFSnodeSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                      pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        } else {
            LogPrintf("CXFSnodeSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                      pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        }
        return false;
    } else if (pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer announced more headers than we have blocks currently
        LogPrint("xfsnode", "CXFSnodeSync::CheckNodeHeight -- skipping peer, who announced more headers than we have blocks currently, nHeight=%d, nSyncHeight=%d, peer=%d\n",
                  pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->id);
        return false;
    }

    return true;
}

bool CXFSnodeSync::GetBlockchainSynced(bool fBlockAccepted){
    bool currentBlockchainSynced = fBlockchainSynced;
    IsBlockchainSynced(fBlockAccepted);
    if(currentBlockchainSynced != fBlockchainSynced){
        GetMainSignals().UpdateSyncStatus();
    }
    return fBlockchainSynced;
}

bool CXFSnodeSync::IsBlockchainSynced(bool fBlockAccepted) {
    static int64_t nTimeLastProcess = GetTime();
    static int nSkipped = 0;
    static bool fFirstBlockAccepted = false;

    // If the last call to this function was more than 60 minutes ago 
    // (client was in sleep mode) reset the sync process
    if (GetTime() - nTimeLastProcess > 60 * 60) {
        LogPrintf("CXFSnodeSync::IsBlockchainSynced time-check fBlockchainSynced=%s\n", 
                  fBlockchainSynced);
        Reset();
        fBlockchainSynced = false;
    }

    if (!pCurrentBlockIndex || !pindexBestHeader || fImporting || fReindex) 
        return false;

    if (fBlockAccepted) {
        // This should be only triggered while we are still syncing.
        if (!IsSynced()) {
            // We are trying to download smth, reset blockchain sync status.
            fFirstBlockAccepted = true;
            fBlockchainSynced = false;
            nTimeLastProcess = GetTime();
            return false;
        }
    } else {
        // Dont skip on REGTEST to make the tests run faster.
        if(Params().NetworkIDString() != CBaseChainParams::REGTEST) {
            // skip if we already checked less than 1 tick ago.
            if (GetTime() - nTimeLastProcess < XFSNODE_SYNC_TICK_SECONDS) {
                nSkipped++;
                return fBlockchainSynced;
            }
        }
    }

    LogPrint("xfsnode-sync", 
             "CXFSnodeSync::IsBlockchainSynced -- state before check: %ssynced, skipped %d times\n", 
             fBlockchainSynced ? "" : "not ", 
             nSkipped);

    nTimeLastProcess = GetTime();
    nSkipped = 0;

    if (fBlockchainSynced){
        return true;
    }

    if (fCheckpointsEnabled && 
        pCurrentBlockIndex->nHeight < Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints())) {
        
        return false;
    }

    std::vector < CNode * > vNodesCopy = CopyNodeVector();
    // We have enough peers and assume most of them are synced
    if (vNodesCopy.size() >= XFSNODE_SYNC_ENOUGH_PEERS) {
        // Check to see how many of our peers are (almost) at the same height as we are
        int nNodesAtSameHeight = 0;
        BOOST_FOREACH(CNode * pnode, vNodesCopy)
        {
            // Make sure this peer is presumably at the same height
            if (!CheckNodeHeight(pnode)) {
                continue;
            }
            nNodesAtSameHeight++;
            // if we have decent number of such peers, most likely we are synced now
            if (nNodesAtSameHeight >= XFSNODE_SYNC_ENOUGH_PEERS) {
                LogPrintf("CXFSnodeSync::IsBlockchainSynced -- found enough peers on the same height as we are, done\n");
                fBlockchainSynced = true;
                ReleaseNodeVector(vNodesCopy);
                return fBlockchainSynced;
            }
        }
    }
    ReleaseNodeVector(vNodesCopy);

    // wait for at least one new block to be accepted
    if (!fFirstBlockAccepted){ 
        fBlockchainSynced = false;
        return false;
    }

    // same as !IsInitialBlockDownload() but no cs_main needed here
    int64_t nMaxBlockTime = std::max(pCurrentBlockIndex->GetBlockTime(), pindexBestHeader->GetBlockTime());
    fBlockchainSynced = pindexBestHeader->nHeight - pCurrentBlockIndex->nHeight < 24 * 6 &&
                        GetTime() - nMaxBlockTime < Params().MaxTipAge();
    return fBlockchainSynced;
}

void CXFSnodeSync::Fail() {
    nTimeLastFailure = GetTime();
    nRequestedXFSnodeAssets = XFSNODE_SYNC_FAILED;
    GetMainSignals().UpdateSyncStatus();
}

void CXFSnodeSync::Reset() {
    nRequestedXFSnodeAssets = XFSNODE_SYNC_INITIAL;
    nRequestedXFSnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastXFSnodeList = GetTime();
    nTimeLastPaymentVote = GetTime();
    nTimeLastGovernanceItem = GetTime();
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CXFSnodeSync::GetAssetName() {
    switch (nRequestedXFSnodeAssets) {
        case (XFSNODE_SYNC_INITIAL):
            return "XFSNODE_SYNC_INITIAL";
        case (XFSNODE_SYNC_SPORKS):
            return "XFSNODE_SYNC_SPORKS";
        case (XFSNODE_SYNC_LIST):
            return "XFSNODE_SYNC_LIST";
        case (XFSNODE_SYNC_MNW):
            return "XFSNODE_SYNC_MNW";
        case (XFSNODE_SYNC_FAILED):
            return "XFSNODE_SYNC_FAILED";
        case XFSNODE_SYNC_FINISHED:
            return "XFSNODE_SYNC_FINISHED";
        default:
            return "UNKNOWN";
    }
}

void CXFSnodeSync::SwitchToNextAsset() {
    switch (nRequestedXFSnodeAssets) {
        case (XFSNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case (XFSNODE_SYNC_INITIAL):
            ClearFulfilledRequests();
            nRequestedXFSnodeAssets = XFSNODE_SYNC_SPORKS;
            LogPrintf("CXFSnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case (XFSNODE_SYNC_SPORKS):
            nTimeLastXFSnodeList = GetTime();
            nRequestedXFSnodeAssets = XFSNODE_SYNC_LIST;
            LogPrintf("CXFSnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case (XFSNODE_SYNC_LIST):
            nTimeLastPaymentVote = GetTime();
            nRequestedXFSnodeAssets = XFSNODE_SYNC_MNW;
            LogPrintf("CXFSnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;

        case (XFSNODE_SYNC_MNW):
            nTimeLastGovernanceItem = GetTime();
            LogPrintf("CXFSnodeSync::SwitchToNextAsset -- Sync has finished\n");
            nRequestedXFSnodeAssets = XFSNODE_SYNC_FINISHED;
            break;
    }
    nRequestedXFSnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    GetMainSignals().UpdateSyncStatus();
}

std::string CXFSnodeSync::GetSyncStatus() {
    switch (xfsnodeSync.nRequestedXFSnodeAssets) {
        case XFSNODE_SYNC_INITIAL:
            return _("Synchronization pending...");
        case XFSNODE_SYNC_SPORKS:
            return _("Synchronizing sporks...");
        case XFSNODE_SYNC_LIST:
            return _("Synchronizing xfsnodes...");
        case XFSNODE_SYNC_MNW:
            return _("Synchronizing xfsnode payments...");
        case XFSNODE_SYNC_FAILED:
            return _("Synchronization failed");
        case XFSNODE_SYNC_FINISHED:
            return _("Synchronization finished");
        default:
            return "";
    }
}

void CXFSnodeSync::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if (IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CXFSnodeSync::ClearFulfilledRequests() {
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH(CNode * pnode, vNodes)
    {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "xfsnode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "xfsnode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    }
}

void CXFSnodeSync::ProcessTick() {
    static int nTick = 0;
    if (nTick++ % XFSNODE_SYNC_TICK_SECONDS != 0) return;
    if (!pCurrentBlockIndex) return;

    //the actual count of xfsnodes we have currently
    int nMnCount = mnodeman.CountXFSnodes();

    LogPrint("ProcessTick", "CXFSnodeSync::ProcessTick -- nTick %d nMnCount %d\n", nTick, nMnCount);

    // INITIAL SYNC SETUP / LOG REPORTING
    double nSyncProgress = double(nRequestedXFSnodeAttempt + (nRequestedXFSnodeAssets - 1) * 8) / (8 * 4);
    LogPrint("ProcessTick", "CXFSnodeSync::ProcessTick -- nTick %d nRequestedXFSnodeAssets %d nRequestedXFSnodeAttempt %d nSyncProgress %f\n", nTick, nRequestedXFSnodeAssets, nRequestedXFSnodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(pCurrentBlockIndex->nHeight, nSyncProgress);

    // RESET SYNCING INCASE OF FAILURE
    {
        if (IsSynced()) {
            /*
                Resync if we lost all xfsnodes from sleep/wake or failed to sync originally
            */
            if (nMnCount == 0) {
                LogPrintf("CXFSnodeSync::ProcessTick -- WARNING: not enough data, restarting sync\n");
                Reset();
            } else {
                std::vector < CNode * > vNodesCopy = CopyNodeVector();
                ReleaseNodeVector(vNodesCopy);
                return;
            }
        }

        //try syncing again
        if (IsFailed()) {
            if (nTimeLastFailure + (1 * 60) < GetTime()) { // 1 minute cooldown after failed sync
                Reset();
            }
            return;
        }
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !IsBlockchainSynced() && nRequestedXFSnodeAssets > XFSNODE_SYNC_SPORKS) {
        nTimeLastXFSnodeList = GetTime();
        nTimeLastPaymentVote = GetTime();
        nTimeLastGovernanceItem = GetTime();
        return;
    }
    if (nRequestedXFSnodeAssets == XFSNODE_SYNC_INITIAL || (nRequestedXFSnodeAssets == XFSNODE_SYNC_SPORKS && IsBlockchainSynced())) {
        SwitchToNextAsset();
    }

    std::vector < CNode * > vNodesCopy = CopyNodeVector();

    BOOST_FOREACH(CNode * pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "xfsnode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "xfsnode" connection
        // initialted from another node, so skip it too.
        if (pnode->fXFSnode || (fXFSNode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if (Params().NetworkIDString() == CBaseChainParams::REGTEST) {
            if (nRequestedXFSnodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if (nRequestedXFSnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (nRequestedXFSnodeAttempt < 6) {
                int nMnCount = mnodeman.CountXFSnodes();
                pnode->PushMessage(NetMsgType::XFSNODEPAYMENTSYNC, nMnCount); //sync payment votes
            } else {
                nRequestedXFSnodeAssets = XFSNODE_SYNC_FINISHED;
                GetMainSignals().UpdateSyncStatus();
            }
            nRequestedXFSnodeAttempt++;
            ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if (netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CXFSnodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC (we skip this mode now)

            if (!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                pnode->PushMessage(NetMsgType::GETSPORKS);
                LogPrintf("CXFSnodeSync::ProcessTick -- nTick %d nRequestedXFSnodeAssets %d -- requesting sporks from peer %d\n", nTick, nRequestedXFSnodeAssets, pnode->id);
                continue; // always get sporks first, switch to the next node without waiting for the next tick
            }

            // MNLIST : SYNC XFSNODE LIST FROM OTHER CONNECTED CLIENTS

            if (nRequestedXFSnodeAssets == XFSNODE_SYNC_LIST) {
                // check for timeout first
                if (nTimeLastXFSnodeList < GetTime() - XFSNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CXFSnodeSync::ProcessTick -- nTick %d nRequestedXFSnodeAssets %d -- timeout\n", nTick, nRequestedXFSnodeAssets);
                    if (nRequestedXFSnodeAttempt == 0) {
                        LogPrintf("CXFSnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without xfsnode list, fail here and try later
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "xfsnode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "xfsnode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinXFSnodePaymentsProto()) continue;
                nRequestedXFSnodeAttempt++;

                mnodeman.DsegUpdate(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC XFSNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if (nRequestedXFSnodeAssets == XFSNODE_SYNC_MNW) {
                LogPrint("mnpayments", "CXFSnodeSync::ProcessTick -- nTick %d nRequestedXFSnodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedXFSnodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);
                // check for timeout first
                // This might take a lot longer than XFSNODE_SYNC_TIMEOUT_SECONDS minutes due to new blocks,
                // but that should be OK and it should timeout eventually.
                if (nTimeLastPaymentVote < GetTime() - XFSNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CXFSnodeSync::ProcessTick -- nTick %d nRequestedXFSnodeAssets %d -- timeout\n", nTick, nRequestedXFSnodeAssets);
                    if (nRequestedXFSnodeAttempt == 0) {
                        LogPrintf("CXFSnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if (nRequestedXFSnodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CXFSnodeSync::ProcessTick -- nTick %d nRequestedXFSnodeAssets %d -- found enough data\n", nTick, nRequestedXFSnodeAssets);
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "xfsnode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "xfsnode-payment-sync");

                if (pnode->nVersion < mnpayments.GetMinXFSnodePaymentsProto()) continue;
                nRequestedXFSnodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::XFSNODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

        }
    }
    // looped through all nodes, release them
    ReleaseNodeVector(vNodesCopy);
}

void CXFSnodeSync::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
}
