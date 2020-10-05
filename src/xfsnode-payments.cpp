// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activexfsnode.h"
#include "darksend.h"
#include "xfsnode-payments.h"
#include "xfsnode-sync.h"
#include "xfsnodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CXFSnodePayments mnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapXFSnodeBlocks;
CCriticalSection cs_mapXFSnodePaymentVotes;

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In XFS some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock &block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet) {
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    if (fDebug) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0].GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

//    const Consensus::Params &consensusParams = Params().GetConsensus();
//
////    if (nBlockHeight < consensusParams.nSuperblockStartBlock) {
//        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
//        if (nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
//            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
//            // NOTE: make sure SPORK_13_OLD_SUPERBLOCK_FLAG is disabled when 12.1 starts to go live
//            if (xfsnodeSync.IsSynced() && !sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
//                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
//                LogPrint("gobject", "IsBlockValueValid -- Client synced but budget spork is disabled, checking block value against block reward\n");
//                if (!isBlockRewardValueMet) {
//                    strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, budgets are disabled",
//                                            nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//                }
//                return isBlockRewardValueMet;
//            }
//            LogPrint("gobject", "IsBlockValueValid -- WARNING: Skipping budget block value checks, accepting block\n");
//            // TODO: reprocess blocks to make sure they are legit?
//            return true;
//        }
//        // LogPrint("gobject", "IsBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
//        if (!isBlockRewardValueMet) {
//            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in budget cycle window",
//                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//        }
//        return isBlockRewardValueMet;
//    }

    // superblocks started

//    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
//    bool isSuperblockMaxValueMet = (block.vtx[0].GetValueOut() <= nSuperblockMaxValue);
//    bool isSuperblockMaxValueMet = false;

//    LogPrint("gobject", "block.vtx[0].GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0].GetValueOut(), nSuperblockMaxValue);

    if (!xfsnodeSync.IsSynced()) {
        // not enough data but at least it must NOT exceed superblock max value
//        if(CSuperblock::IsValidBlockHeight(nBlockHeight)) {
//            if(fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, checking superblock max bounds only\n");
//            if(!isSuperblockMaxValueMet) {
//                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
//                                        nBlockHeight, block.vtx[0].GetValueOut(), nSuperblockMaxValue);
//            }
//            return isSuperblockMaxValueMet;
//        }
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can

    if (sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
////        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
////            if(CSuperblockManager::IsValid(block.vtx[0], nBlockHeight, blockReward)) {
////                LogPrint("gobject", "IsBlockValueValid -- Valid superblock at height %d: %s", nBlockHeight, block.vtx[0].ToString());
////                // all checks are done in CSuperblock::IsValid, nothing to do here
////                return true;
////            }
////
////            // triggered but invalid? that's weird
////            LogPrintf("IsBlockValueValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, block.vtx[0].ToString());
////            // should NOT allow invalid superblocks, when superblocks are enabled
////            strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
////            return false;
////        }
//        LogPrint("gobject", "IsBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
//        if(!isBlockRewardValueMet) {
//            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
//                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//        }
    } else {
//        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
    }

    // it MUST be a regular block
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward) {
    // we can only check xfsnode payment /
    const Consensus::Params &consensusParams = Params().GetConsensus();

    if (nBlockHeight < consensusParams.nXFSnodePaymentsStartBlock) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsBlockPayeeValid -- xfsnode isn't start\n");
        return true;
    }
    if (!xfsnodeSync.IsSynced() && Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    //check for xfsnode payee
    if (mnpayments.IsTransactionValid(txNew, nBlockHeight, false)) {
        LogPrint("mnpayments", "IsBlockPayeeValid -- Valid xfsnode payment at height %d: %s", nBlockHeight, txNew.ToString());
        return true;
    } else {
        if(sporkManager.IsSporkActive(SPORK_8_XFSNODE_PAYMENT_ENFORCEMENT)){
            return false;
        } else {
            LogPrintf("XFSNode payment enforcement is disabled, accepting block\n");
            return true;
        }
    }
}

void FillBlockPayments(CMutableTransaction &txNew, int nBlockHeight, CAmount xfsnodePayment, CTxOut &txoutXFSnodeRet, std::vector <CTxOut> &voutSuperblockRet) {
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
//    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED) &&
//        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
//            LogPrint("gobject", "FillBlockPayments -- triggered superblock creation at height %d\n", nBlockHeight);
//            CSuperblockManager::CreateSuperblock(txNew, nBlockHeight, voutSuperblockRet);
//            return;
//    }

    // FILL BLOCK PAYEE WITH XFSNODE PAYMENT OTHERWISE
    mnpayments.FillBlockPayee(txNew, nBlockHeight, xfsnodePayment, txoutXFSnodeRet);
    LogPrint("mnpayments", "FillBlockPayments -- nBlockHeight %d xfsnodePayment %lld txoutXFSnodeRet %s txNew %s",
             nBlockHeight, xfsnodePayment, txoutXFSnodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight) {
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
//    if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
//        return CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
//    }

    // OTHERWISE, PAY XFSNODE
    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CXFSnodePayments::Clear() {
    LOCK2(cs_mapXFSnodeBlocks, cs_mapXFSnodePaymentVotes);
    mapXFSnodeBlocks.clear();
    mapXFSnodePaymentVotes.clear();
}

bool CXFSnodePayments::CanVote(COutPoint outXFSnode, int nBlockHeight) {
    LOCK(cs_mapXFSnodePaymentVotes);

    if (mapXFSnodesLastVote.count(outXFSnode) && mapXFSnodesLastVote[outXFSnode] == nBlockHeight) {
        return false;
    }

    //record this xfsnode voted
    mapXFSnodesLastVote[outXFSnode] = nBlockHeight;
    return true;
}

std::string CXFSnodePayee::ToString() const {
    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    std::string str;
    str += "(address: ";
    str += address2.ToString();
    str += ")\n";
    return str;
}

/**
*   FillBlockPayee
*
*   Fill XFSnode ONLY payment block
*/

void CXFSnodePayments::FillBlockPayee(CMutableTransaction &txNew, int nBlockHeight, CAmount xfsnodePayment, CTxOut &txoutXFSnodeRet) {
    // make sure it's not filled yet
    txoutXFSnodeRet = CTxOut();

    CScript payee;
    bool foundMaxVotedPayee = true;

    if (!mnpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no xfsnode detected...
        // LogPrintf("no xfsnode detected...\n");
        foundMaxVotedPayee = false;
        int nCount = 0;
        CXFSnode *winningNode = mnodeman.GetNextXFSnodeInQueueForPayment(nBlockHeight, true, nCount);
        if (!winningNode) {
            if(Params().NetworkIDString() != CBaseChainParams::REGTEST) {
                // ...and we can't calculate it on our own
                LogPrintf("CXFSnodePayments::FillBlockPayee -- Failed to detect xfsnode to pay\n");
                return;
            }
        }
        // fill payee with locally calculated winner and hope for the best
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
            LogPrintf("payee=%s\n", winningNode->ToString());
        }
        else
            payee = txNew.vout[0].scriptPubKey;//This is only for unit tests scenario on REGTEST
    }
    txoutXFSnodeRet = CTxOut(xfsnodePayment, payee);
    txNew.vout.push_back(txoutXFSnodeRet);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);
    if (foundMaxVotedPayee) {
        LogPrintf("CXFSnodePayments::FillBlockPayee::foundMaxVotedPayee -- XFSnode payment %lld to %s\n", xfsnodePayment, address2.ToString());
    } else {
        LogPrintf("CXFSnodePayments::FillBlockPayee -- XFSnode payment %lld to %s\n", xfsnodePayment, address2.ToString());
    }

}

int CXFSnodePayments::GetMinXFSnodePaymentsProto() {
    return sporkManager.IsSporkActive(SPORK_10_XFSNODE_PAY_UPDATED_NODES)
           ? MIN_XFSNODE_PAYMENT_PROTO_VERSION_2
           : MIN_XFSNODE_PAYMENT_PROTO_VERSION_1;
}

void CXFSnodePayments::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {

//    LogPrintf("CXFSnodePayments::ProcessMessage strCommand=%s\n", strCommand);
    // Ignore any payments messages until xfsnode list is synced
    if (!xfsnodeSync.IsXFSnodeListSynced()) return;

    if (fLiteMode) return; // disable all XFS specific functionality

    bool fTestNet = (Params().NetworkIDString() == CBaseChainParams::TESTNET);

    if (strCommand == NetMsgType::XFSNODEPAYMENTSYNC) { //XFSnode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after xfsnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!xfsnodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::XFSNODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("XFSNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            if (!fTestNet) Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::XFSNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrint("mnpayments", "XFSNODEPAYMENTSYNC -- Sent XFSnode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::XFSNODEPAYMENTVOTE) { // XFSnode Payments Vote for the Winner

        CXFSnodePaymentVote vote;
        vRecv >> vote;

        if (pfrom->nVersion < GetMinXFSnodePaymentsProto()) return;

        if (!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapXFSnodePaymentVotes);
            if (mapXFSnodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "XFSNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapXFSnodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapXFSnodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if (vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight + 20) {
            LogPrint("mnpayments", "XFSNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if (!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("mnpayments", "XFSNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if (!CanVote(vote.vinXFSnode.prevout, vote.nBlockHeight)) {
            LogPrintf("XFSNODEPAYMENTVOTE -- xfsnode already voted, xfsnode=%s\n", vote.vinXFSnode.prevout.ToStringShort());
            return;
        }

        xfsnode_info_t mnInfo = mnodeman.GetXFSnodeInfo(vote.vinXFSnode);
        if (!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("XFSNODEPAYMENTVOTE -- xfsnode is missing %s\n", vote.vinXFSnode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinXFSnode);
            return;
        }

        int nDos = 0;
        if (!vote.CheckSignature(mnInfo.pubKeyXFSnode, pCurrentBlockIndex->nHeight, nDos)) {
            if (nDos) {
                LogPrintf("XFSNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                if (!fTestNet) Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "XFSNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinXFSnode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("mnpayments", "XFSNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, vote.vinXFSnode.prevout.ToStringShort());

        if (AddPaymentVote(vote)) {
            vote.Relay();
            xfsnodeSync.AddedPaymentVote();
        }
    }
}

bool CXFSnodePaymentVote::Sign() {
    std::string strError;
    std::string strMessage = vinXFSnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, activeXFSnode.keyXFSnode)) {
        LogPrintf("CXFSnodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(activeXFSnode.pubKeyXFSnode, vchSig, strMessage, strError)) {
        LogPrintf("CXFSnodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CXFSnodePayments::GetBlockPayee(int nBlockHeight, CScript &payee) {
    if (mapXFSnodeBlocks.count(nBlockHeight)) {
        return mapXFSnodeBlocks[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this xfsnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CXFSnodePayments::IsScheduled(CXFSnode &mn, int nNotBlockHeight) {
    LOCK(cs_mapXFSnodeBlocks);

    if (!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapXFSnodeBlocks.count(h) && mapXFSnodeBlocks[h].GetBestPayee(payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CXFSnodePayments::AddPaymentVote(const CXFSnodePaymentVote &vote) {
    LogPrint("xfsnode-payments", "CXFSnodePayments::AddPaymentVote\n");
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    if (HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapXFSnodeBlocks, cs_mapXFSnodePaymentVotes);

    mapXFSnodePaymentVotes[vote.GetHash()] = vote;

    if (!mapXFSnodeBlocks.count(vote.nBlockHeight)) {
        CXFSnodeBlockPayees blockPayees(vote.nBlockHeight);
        mapXFSnodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapXFSnodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CXFSnodePayments::HasVerifiedPaymentVote(uint256 hashIn) {
    LOCK(cs_mapXFSnodePaymentVotes);
    std::map<uint256, CXFSnodePaymentVote>::iterator it = mapXFSnodePaymentVotes.find(hashIn);
    return it != mapXFSnodePaymentVotes.end() && it->second.IsVerified();
}

void CXFSnodeBlockPayees::AddPayee(const CXFSnodePaymentVote &vote) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CXFSnodePayee & payee, vecPayees)
    {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CXFSnodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CXFSnodeBlockPayees::GetBestPayee(CScript &payeeRet) {
    LOCK(cs_vecPayees);
    LogPrint("mnpayments", "CXFSnodeBlockPayees::GetBestPayee, vecPayees.size()=%s\n", vecPayees.size());
    if (!vecPayees.size()) {
        LogPrint("mnpayments", "CXFSnodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CXFSnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CXFSnodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CXFSnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

//    LogPrint("mnpayments", "CXFSnodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CXFSnodeBlockPayees::IsTransactionValid(const CTransaction &txNew, bool fMTP,int nHeight) {
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";


    CAmount nXFSnodePayment = GetXFSnodePayment(Params().GetConsensus(), fMTP,nHeight);

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CXFSnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }
    LogPrintf("nmaxsig = %s \n",nMaxSignatures);
    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    bool hasValidPayee = false;

    BOOST_FOREACH(CXFSnodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            hasValidPayee = true;

            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nXFSnodePayment == txout.nValue) {
                    LogPrint("mnpayments", "CXFSnodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrintf("CXFSnodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f XFS\n", strPayeesPossible, (float) nXFSnodePayment / COIN);
    return false;
}

std::string CXFSnodeBlockPayees::GetRequiredPaymentsString() {
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CXFSnodePayee & payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

std::string CXFSnodePayments::GetRequiredPaymentsString(int nBlockHeight) {
    LOCK(cs_mapXFSnodeBlocks);

    if (mapXFSnodeBlocks.count(nBlockHeight)) {
        return mapXFSnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CXFSnodePayments::IsTransactionValid(const CTransaction &txNew, int nBlockHeight, bool fMTP) {
    LOCK(cs_mapXFSnodeBlocks);

    if (mapXFSnodeBlocks.count(nBlockHeight)) {
        return mapXFSnodeBlocks[nBlockHeight].IsTransactionValid(txNew, fMTP,nBlockHeight);
    }

    return true;
}

void CXFSnodePayments::CheckAndRemove() {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_mapXFSnodeBlocks, cs_mapXFSnodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CXFSnodePaymentVote>::iterator it = mapXFSnodePaymentVotes.begin();
    while (it != mapXFSnodePaymentVotes.end()) {
        CXFSnodePaymentVote vote = (*it).second;

        if (pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CXFSnodePayments::CheckAndRemove -- Removing old XFSnode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapXFSnodePaymentVotes.erase(it++);
            mapXFSnodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CXFSnodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CXFSnodePaymentVote::IsValid(CNode *pnode, int nValidationHeight, std::string &strError) {
    CXFSnode *pmn = mnodeman.Find(vinXFSnode);

    if (!pmn) {
        strError = strprintf("Unknown XFSnode: prevout=%s", vinXFSnode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that XFSnode
        if (xfsnodeSync.IsXFSnodeListSynced()) {
            mnodeman.AskForMN(pnode, vinXFSnode);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if (nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_XFSNODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = mnpayments.GetMinXFSnodePaymentsProto();
    } else {
        // allow non-updated xfsnodes for old blocks
        nMinRequiredProtocol = MIN_XFSNODE_PAYMENT_PROTO_VERSION_1;
    }

    if (pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("XFSnode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only xfsnodes should try to check xfsnode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify xfsnode rank for future block votes only.
    if (!fXFSNode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetXFSnodeRank(vinXFSnode, nBlockHeight - 101, nMinRequiredProtocol, false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CXFSnodePaymentVote::IsValid -- Can't calculate rank for xfsnode %s\n",
                 vinXFSnode.prevout.ToStringShort());
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have xfsnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("XFSnode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if (nRank > MNPAYMENTS_SIGNATURES_TOTAL * 2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("XFSnode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, nRank);
            LogPrintf("CXFSnodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CXFSnodePayments::ProcessBlock(int nBlockHeight) {

    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if (fLiteMode || !fXFSNode) {
        return false;
    }

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about xfsnodes.
    if (!xfsnodeSync.IsXFSnodeListSynced()) {
        return false;
    }

    int nRank = mnodeman.GetXFSnodeRank(activeXFSnode.vin, nBlockHeight - 101, GetMinXFSnodePaymentsProto(), false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CXFSnodePayments::ProcessBlock -- Unknown XFSnode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CXFSnodePayments::ProcessBlock -- XFSnode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }

    // LOCATE THE NEXT XFSNODE WHICH SHOULD BE PAID

    LogPrintf("CXFSnodePayments::ProcessBlock -- Start: nBlockHeight=%d, xfsnode=%s\n", nBlockHeight, activeXFSnode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CXFSnode *pmn = mnodeman.GetNextXFSnodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) {
        LogPrintf("CXFSnodePayments::ProcessBlock -- ERROR: Failed to find xfsnode to pay\n");
        return false;
    }

    LogPrintf("CXFSnodePayments::ProcessBlock -- XFSnode found by GetNextXFSnodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CXFSnodePaymentVote voteNew(activeXFSnode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    // SIGN MESSAGE TO NETWORK WITH OUR XFSNODE KEYS

    if (voteNew.Sign()) {
        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CXFSnodePaymentVote::Relay() {
    // do not relay until synced
    if (!xfsnodeSync.IsWinnersListSynced()) {
        LogPrint("xfsnode", "CXFSnodePaymentVote::Relay - xfsnodeSync.IsWinnersListSynced() not sync\n");
        return;
    }
    CInv inv(MSG_XFSNODE_PAYMENT_VOTE, GetHash());
    RelayInv(inv);
}

bool CXFSnodePaymentVote::CheckSignature(const CPubKey &pubKeyXFSnode, int nValidationHeight, int &nDos) {
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinXFSnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    std::string strError = "";
    if (!darkSendSigner.VerifyMessage(pubKeyXFSnode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if (xfsnodeSync.IsXFSnodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CXFSnodePaymentVote::CheckSignature -- Got bad XFSnode payment signature, xfsnode=%s, error: %s", vinXFSnode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CXFSnodePaymentVote::ToString() const {
    std::ostringstream info;

    info << vinXFSnode.prevout.ToStringShort() <<
         ", " << nBlockHeight <<
         ", " << ScriptToAsmStr(payee) <<
         ", " << (int) vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CXFSnodePayments::Sync(CNode *pnode) {
    LOCK(cs_mapXFSnodeBlocks);

    if (!pCurrentBlockIndex) return;

    int nInvCount = 0;

    for (int h = pCurrentBlockIndex->nHeight; h < pCurrentBlockIndex->nHeight + 20; h++) {
        if (mapXFSnodeBlocks.count(h)) {
            BOOST_FOREACH(CXFSnodePayee & payee, mapXFSnodeBlocks[h].vecPayees)
            {
                std::vector <uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256 & hash, vecVoteHashes)
                {
                    if (!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_XFSNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CXFSnodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, XFSNODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CXFSnodePayments::RequestLowDataPaymentBlocks(CNode *pnode) {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_main, cs_mapXFSnodeBlocks);

    std::vector <CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while (pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if (!mapXFSnodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_XFSNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if (vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CXFSnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if (!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CXFSnodeBlockPayees>::iterator it = mapXFSnodeBlocks.begin();

    while (it != mapXFSnodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CXFSnodePayee & payee, it->second.vecPayees)
        {
            if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if (fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
//        DBG (
//            // Let's see why this failed
//            BOOST_FOREACH(CXFSnodePayee& payee, it->second.vecPayees) {
//                CTxDestination address1;
//                ExtractDestination(payee.GetPayee(), address1);
//                CBitcoinAddress address2(address1);
//                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
//            }
//            printf("block %d votes total %d\n", it->first, nTotalVotes);
//        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if (GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_XFSNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if (vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CXFSnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if (!vToFetch.empty()) {
        LogPrintf("CXFSnodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
    }
}

std::string CXFSnodePayments::ToString() const {
    std::ostringstream info;

    info << "Votes: " << (int) mapXFSnodePaymentVotes.size() <<
         ", Blocks: " << (int) mapXFSnodeBlocks.size();

    return info.str();
}

bool CXFSnodePayments::IsEnoughData() {
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CXFSnodePayments::GetStorageLimit() {
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CXFSnodePayments::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
    LogPrint("mnpayments", "CXFSnodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);
    
    ProcessBlock(pindex->nHeight + 5);
}
