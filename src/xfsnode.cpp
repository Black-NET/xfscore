// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activexfsnode.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "darksend.h"
#include "init.h"
//#include "governance.h"
#include "xfsnode.h"
#include "xfsnode-payments.h"
#include "xfsnodeconfig.h"
#include "xfsnode-sync.h"
#include "xfsnodeman.h"
#include "util.h"
#include "validationinterface.h"

#include <boost/lexical_cast.hpp>


CXFSnode::CXFSnode() :
        vin(),
        addr(),
        pubKeyCollateralAddress(),
        pubKeyXFSnode(),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(XFSNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(PROTOCOL_VERSION),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CXFSnode::CXFSnode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyXFSnodeNew, int nProtocolVersionIn) :
        vin(vinNew),
        addr(addrNew),
        pubKeyCollateralAddress(pubKeyCollateralAddressNew),
        pubKeyXFSnode(pubKeyXFSnodeNew),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(XFSNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(nProtocolVersionIn),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CXFSnode::CXFSnode(const CXFSnode &other) :
        vin(other.vin),
        addr(other.addr),
        pubKeyCollateralAddress(other.pubKeyCollateralAddress),
        pubKeyXFSnode(other.pubKeyXFSnode),
        lastPing(other.lastPing),
        vchSig(other.vchSig),
        sigTime(other.sigTime),
        nLastDsq(other.nLastDsq),
        nTimeLastChecked(other.nTimeLastChecked),
        nTimeLastPaid(other.nTimeLastPaid),
        nTimeLastWatchdogVote(other.nTimeLastWatchdogVote),
        nActiveState(other.nActiveState),
        nCacheCollateralBlock(other.nCacheCollateralBlock),
        nBlockLastPaid(other.nBlockLastPaid),
        nProtocolVersion(other.nProtocolVersion),
        nPoSeBanScore(other.nPoSeBanScore),
        nPoSeBanHeight(other.nPoSeBanHeight),
        fAllowMixingTx(other.fAllowMixingTx),
        fUnitTest(other.fUnitTest) {}

CXFSnode::CXFSnode(const CXFSnodeBroadcast &mnb) :
        vin(mnb.vin),
        addr(mnb.addr),
        pubKeyCollateralAddress(mnb.pubKeyCollateralAddress),
        pubKeyXFSnode(mnb.pubKeyXFSnode),
        lastPing(mnb.lastPing),
        vchSig(mnb.vchSig),
        sigTime(mnb.sigTime),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(mnb.sigTime),
        nActiveState(mnb.nActiveState),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(mnb.nProtocolVersion),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

//CSporkManager sporkManager;
//
// When a new xfsnode broadcast is sent, update our information
//
bool CXFSnode::UpdateFromNewBroadcast(CXFSnodeBroadcast &mnb) {
    if (mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyXFSnode = mnb.pubKeyXFSnode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.lastPing == CXFSnodePing() || (mnb.lastPing != CXFSnodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos))) {
        SetLastPing(mnb.lastPing);
        mnodeman.mapSeenXFSnodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our XFSnode privkey...
    if (fXFSNode && pubKeyXFSnode == activeXFSnode.pubKeyXFSnode) {
        nPoSeBanScore = -XFSNODE_POSE_BAN_MAX_SCORE;
        if (nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeXFSnode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CXFSnode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a XFSnode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CXFSnode::CalculateScore(const uint256 &blockHash) {
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CXFSnode::Check(bool fForce) {
    LOCK(cs);

    if (ShutdownRequested()) return;

    if (!fForce && (GetTime() - nTimeLastChecked < XFSNODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state\n", vin.prevout.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if (IsOutpointSpent()) return;

    int nHeight = 0;
    if (!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) return;

        CCoins coins;
        if (!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
            (unsigned int) vin.prevout.n >= coins.vout.size() ||
            coins.vout[vin.prevout.n].IsNull()) {
            SetStatus(XFSNODE_OUTPOINT_SPENT);
            LogPrint("xfsnode", "CXFSnode::Check -- Failed to find XFSnode UTXO, xfsnode=%s\n", vin.prevout.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if (IsPoSeBanned()) {
        if (nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // XFSnode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CXFSnode::Check -- XFSnode %s is unbanned and back in list now\n", vin.prevout.ToStringShort());
        DecreasePoSeBanScore();
    } else if (nPoSeBanScore >= XFSNODE_POSE_BAN_MAX_SCORE) {
        SetStatus(XFSNODE_POSE_BAN);
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CXFSnode::Check -- XFSnode %s is banned till block %d now\n", vin.prevout.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurXFSnode = fXFSNode && activeXFSnode.pubKeyXFSnode == pubKeyXFSnode;

    // xfsnode doesn't meet payment protocol requirements ...
/*    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinXFSnodePaymentsProto() ||
                          // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                          (fOurXFSnode && nProtocolVersion < PROTOCOL_VERSION); */

    // xfsnode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinXFSnodePaymentsProto() ||
                          // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                          (fOurXFSnode && (nProtocolVersion < MIN_XFSNODE_PAYMENT_PROTO_VERSION_1 || nProtocolVersion > MIN_XFSNODE_PAYMENT_PROTO_VERSION_2));

    if (fRequireUpdate) {
        SetStatus(XFSNODE_UPDATE_REQUIRED);
        if (nActiveStatePrev != nActiveState) {
            LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old xfsnodes on start, give them a chance to receive updates...
    bool fWaitForPing = !xfsnodeSync.IsXFSnodeListSynced() && !IsPingedWithin(XFSNODE_MIN_MNP_SECONDS);

    if (fWaitForPing && !fOurXFSnode) {
        // ...but if it was already expired before the initial check - return right away
        if (IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state, waiting for ping\n", vin.prevout.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own xfsnode
    if (!fWaitForPing || fOurXFSnode) {

        if (!IsPingedWithin(XFSNODE_NEW_START_REQUIRED_SECONDS)) {
            SetStatus(XFSNODE_NEW_START_REQUIRED);
            if (nActiveStatePrev != nActiveState) {
                LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = xfsnodeSync.IsSynced() && mnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetTime() - nTimeLastWatchdogVote) > XFSNODE_WATCHDOG_MAX_SECONDS));

//        LogPrint("xfsnode", "CXFSnode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetTime()=%d, fWatchdogExpired=%d\n",
//                vin.prevout.ToStringShort(), nTimeLastWatchdogVote, GetTime(), fWatchdogExpired);

        if (fWatchdogExpired) {
            SetStatus(XFSNODE_WATCHDOG_EXPIRED);
            if (nActiveStatePrev != nActiveState) {
                LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        if (!IsPingedWithin(XFSNODE_EXPIRATION_SECONDS)) {
            SetStatus(XFSNODE_EXPIRED);
            if (nActiveStatePrev != nActiveState) {
                LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if (lastPing.sigTime - sigTime < XFSNODE_MIN_MNP_SECONDS) {
        SetStatus(XFSNODE_PRE_ENABLED);
        if (nActiveStatePrev != nActiveState) {
            LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    SetStatus(XFSNODE_ENABLED); // OK
    if (nActiveStatePrev != nActiveState) {
        LogPrint("xfsnode", "CXFSnode::Check -- XFSnode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
    }
}

bool CXFSnode::IsValidNetAddr() {
    return IsValidNetAddr(addr);
}

bool CXFSnode::IsValidForPayment() {
    if (nActiveState == XFSNODE_ENABLED) {
        return true;
    }
//    if(!sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG) &&
//       (nActiveState == XFSNODE_WATCHDOG_EXPIRED)) {
//        return true;
//    }

    return false;
}

bool CXFSnode::IsValidNetAddr(CService addrIn) {
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
           (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

bool CXFSnode::IsMyXFSnode(){
    BOOST_FOREACH(CXFSnodeConfig::CXFSnodeEntry mne, xfsnodeConfig.getEntries()) {
        const std::string& txHash = mne.getTxHash();
        const std::string& outputIndex = mne.getOutputIndex();

        if(txHash==vin.prevout.hash.ToString().substr(0,64) &&
           outputIndex==to_string(vin.prevout.n))
            return true;
    }
    return false;
}

xfsnode_info_t CXFSnode::GetInfo() {
    xfsnode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyXFSnode = pubKeyXFSnode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nTimeLastPing = lastPing.sigTime;
    info.nActiveState = nActiveState;
    info.nProtocolVersion = nProtocolVersion;
    info.fInfoValid = true;
    return info;
}

std::string CXFSnode::StateToString(int nStateIn) {
    switch (nStateIn) {
        case XFSNODE_PRE_ENABLED:
            return "PRE_ENABLED";
        case XFSNODE_ENABLED:
            return "ENABLED";
        case XFSNODE_EXPIRED:
            return "EXPIRED";
        case XFSNODE_OUTPOINT_SPENT:
            return "OUTPOINT_SPENT";
        case XFSNODE_UPDATE_REQUIRED:
            return "UPDATE_REQUIRED";
        case XFSNODE_WATCHDOG_EXPIRED:
            return "WATCHDOG_EXPIRED";
        case XFSNODE_NEW_START_REQUIRED:
            return "NEW_START_REQUIRED";
        case XFSNODE_POSE_BAN:
            return "POSE_BAN";
        default:
            return "UNKNOWN";
    }
}

std::string CXFSnode::GetStateString() const {
    return StateToString(nActiveState);
}

std::string CXFSnode::GetStatus() const {
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

void CXFSnode::SetStatus(int newState) {
    if(nActiveState!=newState){
        nActiveState = newState;
        if(IsMyXFSnode())
            GetMainSignals().UpdatedXFSnode(*this);
    }
}

void CXFSnode::SetLastPing(CXFSnodePing newXFSnodePing) {
    if(lastPing!=newXFSnodePing){
        lastPing = newXFSnodePing;
        if(IsMyXFSnode())
            GetMainSignals().UpdatedXFSnode(*this);
    }
}

void CXFSnode::SetTimeLastPaid(int64_t newTimeLastPaid) {
     if(nTimeLastPaid!=newTimeLastPaid){
        nTimeLastPaid = newTimeLastPaid;
        if(IsMyXFSnode())
            GetMainSignals().UpdatedXFSnode(*this);
    }   
}

void CXFSnode::SetBlockLastPaid(int newBlockLastPaid) {
     if(nBlockLastPaid!=newBlockLastPaid){
        nBlockLastPaid = newBlockLastPaid;
        if(IsMyXFSnode())
            GetMainSignals().UpdatedXFSnode(*this);
    }   
}

void CXFSnode::SetRank(int newRank) {
     if(nRank!=newRank){
        nRank = newRank;
        if(nRank < 0 || nRank > mnodeman.size()) nRank = 0;
        if(IsMyXFSnode())
            GetMainSignals().UpdatedXFSnode(*this);
    }   
}

std::string CXFSnode::ToString() const {
    std::string str;
    str += "xfsnode{";
    str += addr.ToString();
    str += " ";
    str += std::to_string(nProtocolVersion);
    str += " ";
    str += vin.prevout.ToStringShort();
    str += " ";
    str += CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString();
    str += " ";
    str += std::to_string(lastPing == CXFSnodePing() ? sigTime : lastPing.sigTime);
    str += " ";
    str += std::to_string(lastPing == CXFSnodePing() ? 0 : lastPing.sigTime - sigTime);
    str += " ";
    str += std::to_string(nBlockLastPaid);
    str += "}\n";
    return str;
}

UniValue CXFSnode::ToJSON() const {
    UniValue ret(UniValue::VOBJ);
    std::string payee = CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString();
    COutPoint outpoint = vin.prevout;
    UniValue outpointObj(UniValue::VOBJ);
    UniValue authorityObj(UniValue::VOBJ);
    outpointObj.push_back(Pair("txid", outpoint.hash.ToString().substr(0,64)));
    outpointObj.push_back(Pair("index", to_string(outpoint.n)));

    std::string authority = addr.ToString();
    std::string ip   = authority.substr(0, authority.find(":"));
    std::string port = authority.substr(authority.find(":")+1, authority.length());
    authorityObj.push_back(Pair("ip", ip));
    authorityObj.push_back(Pair("port", port));
    
    // get myXFSnode data
    bool isMine = false;
    string label;
    int fIndex=0;
    BOOST_FOREACH(CXFSnodeConfig::CXFSnodeEntry mne, xfsnodeConfig.getEntries()) {
        CTxIn myVin = CTxIn(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
        if(outpoint.ToStringShort()==myVin.prevout.ToStringShort()){
            isMine = true;
            label = mne.getAlias();
            break;
        }
        fIndex++;
    }

    ret.push_back(Pair("rank", nRank));
    ret.push_back(Pair("outpoint", outpointObj));
    ret.push_back(Pair("status", GetStatus()));
    ret.push_back(Pair("protocolVersion", nProtocolVersion));
    ret.push_back(Pair("payeeAddress", payee));
    ret.push_back(Pair("lastSeen", (int64_t) lastPing.sigTime * 1000));
    ret.push_back(Pair("activeSince", (int64_t)(sigTime * 1000)));
    ret.push_back(Pair("lastPaidTime", (int64_t) GetLastPaidTime() * 1000));
    ret.push_back(Pair("lastPaidBlock", GetLastPaidBlock()));
    ret.push_back(Pair("authority", authorityObj));
    ret.push_back(Pair("isMine", isMine));
    if(isMine){
        ret.push_back(Pair("label", label));
        ret.push_back(Pair("position", fIndex));
    }

    UniValue qualify(UniValue::VOBJ);

    CXFSnode* xfsnode = const_cast <CXFSnode*> (this);
    qualify = mnodeman.GetNotQualifyReasonToUniValue(*xfsnode, chainActive.Tip()->nHeight, true, mnodeman.CountEnabled());
    ret.push_back(Pair("qualify", qualify));

    return ret;
}

int CXFSnode::GetCollateralAge() {
    int nHeight;
    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain || !chainActive.Tip()) return -1;
        nHeight = chainActive.Height();
    }

    if (nCacheCollateralBlock == 0) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge > 0) {
            nCacheCollateralBlock = nHeight - nInputAge;
        } else {
            return nInputAge;
        }
    }

    return nHeight - nCacheCollateralBlock;
}

void CXFSnode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack) {
    if (!pindex) {
        LogPrintf("CXFSnode::UpdateLastPaid pindex is NULL\n");
        return;
    }

    const Consensus::Params &params = Params().GetConsensus();
    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    LogPrint("xfsnode", "CXFSnode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());

    LOCK(cs_mapXFSnodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
//        LogPrintf("mnpayments.mapXFSnodeBlocks.count(BlockReading->nHeight)=%s\n", mnpayments.mapXFSnodeBlocks.count(BlockReading->nHeight));
//        LogPrintf("mnpayments.mapXFSnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)=%s\n", mnpayments.mapXFSnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2));
        if (mnpayments.mapXFSnodeBlocks.count(BlockReading->nHeight) &&
            mnpayments.mapXFSnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
            // LogPrintf("i=%s, BlockReading->nHeight=%s\n", i, BlockReading->nHeight);
            CBlock block;
            if (!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) // shouldn't really happen
            {
                LogPrintf("ReadBlockFromDisk failed\n");
                continue;
            }
            CAmount nXFSnodePayment = GetXFSnodePayment(params, false,BlockReading->nHeight);

            BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
            if (mnpayee == txout.scriptPubKey && nXFSnodePayment == txout.nValue) {
                SetBlockLastPaid(BlockReading->nHeight);
                SetTimeLastPaid(BlockReading->nTime);
                LogPrint("xfsnode", "CXFSnode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
                return;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this xfsnode wasn't found in latest mnpayments blocks
    // or it was found in mnpayments blocks but wasn't found in the blockchain.
    // LogPrint("xfsnode", "CXFSnode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
}

bool CXFSnodeBroadcast::Create(std::string strService, std::string strKeyXFSnode, std::string strTxHash, std::string strOutputIndex, std::string &strErrorRet, CXFSnodeBroadcast &mnbRet, bool fOffline) {
    LogPrintf("CXFSnodeBroadcast::Create\n");
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyXFSnodeNew;
    CKey keyXFSnodeNew;
    //need correct blocks to send ping
    if (!fOffline && !xfsnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start XFSnode";
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    //TODO
    if (!darkSendSigner.GetKeysFromSecret(strKeyXFSnode, keyXFSnodeNew, pubKeyXFSnodeNew)) {
        strErrorRet = strprintf("Invalid xfsnode key %s", strKeyXFSnode);
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetXFSnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for xfsnode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for xfsnode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for xfsnode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyXFSnodeNew, pubKeyXFSnodeNew, strErrorRet, mnbRet);
}

bool CXFSnodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyXFSnodeNew, CPubKey pubKeyXFSnodeNew, std::string &strErrorRet, CXFSnodeBroadcast &mnbRet) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("xfsnode", "CXFSnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyXFSnodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyXFSnodeNew.GetID().ToString());


    CXFSnodePing mnp(txin);
    if (!mnp.Sign(keyXFSnodeNew, pubKeyXFSnodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, xfsnode=%s", txin.prevout.ToStringShort());
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CXFSnodeBroadcast();
        return false;
    }

    int nHeight = chainActive.Height();
    if (nHeight < ZC_MODULUS_V2_START_BLOCK) {
        mnbRet = CXFSnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyXFSnodeNew, MIN_PEER_PROTO_VERSION);
    } else {
        mnbRet = CXFSnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyXFSnodeNew, PROTOCOL_VERSION);
    }

    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address, xfsnode=%s", txin.prevout.ToStringShort());
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CXFSnodeBroadcast();
        return false;
    }
    mnbRet.SetLastPing(mnp);
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, xfsnode=%s", txin.prevout.ToStringShort());
        LogPrintf("CXFSnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CXFSnodeBroadcast();
        return false;
    }

    return true;
}

bool CXFSnodeBroadcast::SimpleCheck(int &nDos) {
    nDos = 0;

    // make sure addr is valid
    if (!IsValidNetAddr()) {
        LogPrintf("CXFSnodeBroadcast::SimpleCheck -- Invalid addr, rejected: xfsnode=%s  addr=%s\n",
                  vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CXFSnodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: xfsnode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if (lastPing == CXFSnodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        SetStatus(XFSNODE_EXPIRED);
    }

    if (nProtocolVersion < mnpayments.GetMinXFSnodePaymentsProto()) {
        LogPrintf("CXFSnodeBroadcast::SimpleCheck -- ignoring outdated XFSnode: xfsnode=%s  nProtocolVersion=%d\n", vin.prevout.ToStringShort(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrintf("CXFSnodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyXFSnode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrintf("CXFSnodeBroadcast::SimpleCheck -- pubKeyXFSnode has the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrintf("CXFSnodeBroadcast::SimpleCheck -- Ignore Not Empty ScriptSig %s\n", vin.ToString());
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != mainnetDefaultPort) return false;
    } else if (addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CXFSnodeBroadcast::Update(CXFSnode *pmn, int &nDos) {
    nDos = 0;

    if (pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenXFSnodeBroadcast in CXFSnodeMan::CheckMnbAndUpdateXFSnodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if (pmn->sigTime > sigTime) {
        LogPrintf("CXFSnodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for XFSnode %s %s\n",
                  sigTime, pmn->sigTime, vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // xfsnode is banned by PoSe
    if (pmn->IsPoSeBanned()) {
        LogPrintf("CXFSnodeBroadcast::Update -- Banned by PoSe, xfsnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if (pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CXFSnodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CXFSnodeBroadcast::Update -- CheckSignature() failed, xfsnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // if ther was no xfsnode broadcast recently or if it matches our XFSnode privkey...
    if (!pmn->IsBroadcastedWithin(XFSNODE_MIN_MNB_SECONDS) || (fXFSNode && pubKeyXFSnode == activeXFSnode.pubKeyXFSnode)) {
        // take the newest entry
        LogPrintf("CXFSnodeBroadcast::Update -- Got UPDATED XFSnode entry: addr=%s\n", addr.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            RelayXFSNode();
        }
        xfsnodeSync.AddedXFSnodeList();
        GetMainSignals().UpdatedXFSnode(*pmn);
    }

    return true;
}

bool CXFSnodeBroadcast::CheckOutpoint(int &nDos) {
    // we are a xfsnode with the same vin (i.e. already activated) and this mnb is ours (matches our XFSnode privkey)
    // so nothing to do here for us
    if (fXFSNode && vin.prevout == activeXFSnode.vin.prevout && pubKeyXFSnode == activeXFSnode.pubKeyXFSnode) {
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CXFSnodeBroadcast::CheckOutpoint -- CheckSignature() failed, xfsnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            LogPrint("xfsnode", "CXFSnodeBroadcast::CheckOutpoint -- Failed to aquire lock, addr=%s", addr.ToString());
            mnodeman.mapSeenXFSnodeBroadcast.erase(GetHash());
            return false;
        }

        CCoins coins;
        if (!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
            (unsigned int) vin.prevout.n >= coins.vout.size() ||
            coins.vout[vin.prevout.n].IsNull()) {
            LogPrint("xfsnode", "CXFSnodeBroadcast::CheckOutpoint -- Failed to find XFSnode UTXO, xfsnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (coins.vout[vin.prevout.n].nValue != XFSNODE_COIN_REQUIRED * COIN) {
            LogPrint("xfsnode", "CXFSnodeBroadcast::CheckOutpoint -- XFSnode UTXO should have 5000 XFS, xfsnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (chainActive.Height() - coins.nHeight + 1 < Params().GetConsensus().nXFSnodeMinimumConfirmations) {
            LogPrintf("CXFSnodeBroadcast::CheckOutpoint -- XFSnode UTXO must have at least %d confirmations, xfsnode=%s\n",
                      Params().GetConsensus().nXFSnodeMinimumConfirmations, vin.prevout.ToStringShort());
            // maybe we miss few blocks, let this mnb to be checked again later
            mnodeman.mapSeenXFSnodeBroadcast.erase(GetHash());
            return false;
        }
    }

    LogPrint("xfsnode", "CXFSnodeBroadcast::CheckOutpoint -- XFSnode UTXO verified\n");

    // make sure the vout that was signed is related to the transaction that spawned the XFSnode
    //  - this is expensive, so it's only done once per XFSnode
    if (!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubKeyCollateralAddress)) {
        LogPrintf("CXFSnodeMan::CheckOutpoint -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 5000 XFS tx got nXFSnodeMinimumConfirmations
    uint256 hashBlock = uint256();
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex *pMNIndex = (*mi).second; // block for 5000 XFS tx -> 1 confirmation
            CBlockIndex *pConfIndex = chainActive[pMNIndex->nHeight + Params().GetConsensus().nXFSnodeMinimumConfirmations - 1]; // block where tx got nXFSnodeMinimumConfirmations
            if (pConfIndex->GetBlockTime() > sigTime) {
                LogPrintf("CXFSnodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for XFSnode %s %s\n",
                          sigTime, Params().GetConsensus().nXFSnodeMinimumConfirmations, pConfIndex->GetBlockTime(), vin.prevout.ToStringShort(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CXFSnodeBroadcast::Sign(CKey &keyCollateralAddress) {
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyXFSnode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CXFSnodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CXFSnodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CXFSnodeBroadcast::CheckSignature(int &nDos) {
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyXFSnode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    LogPrint("xfsnode", "CXFSnodeBroadcast::CheckSignature -- strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CXFSnodeBroadcast::CheckSignature -- Got bad XFSnode announce signature, error: %s\n", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CXFSnodeBroadcast::RelayXFSNode() {
    LogPrintf("CXFSnodeBroadcast::RelayXFSNode\n");
    CInv inv(MSG_XFSNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

CXFSnodePing::CXFSnodePing(CTxIn &vinNew) {
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    vin = vinNew;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector < unsigned char > ();
}

bool CXFSnodePing::Sign(CKey &keyXFSnode, CPubKey &pubKeyXFSnode) {
    std::string strError;
    std::string strXFSNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyXFSnode)) {
        LogPrintf("CXFSnodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyXFSnode, vchSig, strMessage, strError)) {
        LogPrintf("CXFSnodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CXFSnodePing::CheckSignature(CPubKey &pubKeyXFSnode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if (!darkSendSigner.VerifyMessage(pubKeyXFSnode, vchSig, strMessage, strError)) {
        LogPrintf("CXFSnodePing::CheckSignature -- Got bad XFSnode ping signature, xfsnode=%s, error: %s\n", vin.prevout.ToStringShort(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CXFSnodePing::SimpleCheck(int &nDos) {
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CXFSnodePing::SimpleCheck -- Signature rejected, too far into the future, xfsnode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    {
//        LOCK(cs_main);
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("xfsnode", "CXFSnodePing::SimpleCheck -- XFSnode ping is invalid, unknown block hash: xfsnode=%s blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("xfsnode", "CXFSnodePing::SimpleCheck -- XFSnode ping verified: xfsnode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CXFSnodePing::CheckAndUpdate(CXFSnode *pmn, bool fFromNewBroadcast, int &nDos) {
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- Couldn't find XFSnode entry, xfsnode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    if (!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- xfsnode protocol is outdated, xfsnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- xfsnode is completely expired, new start is required, xfsnode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            // LogPrintf("CXFSnodePing::CheckAndUpdate -- XFSnode ping is invalid, block hash is too old: xfsnode=%s  blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- New ping: xfsnode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.prevout.ToStringShort());
    // update only if there is no known ping for this xfsnode or
    // last ping was more then XFSNODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(XFSNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- XFSnode ping arrived too early, xfsnode=%s\n", vin.prevout.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyXFSnode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that XFSNODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if (!xfsnodeSync.IsXFSnodeListSynced() && !pmn->IsPingedWithin(XFSNODE_EXPIRATION_SECONDS / 2)) {
        // let's bump sync timeout
        LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- bumping sync timeout, xfsnode=%s\n", vin.prevout.ToStringShort());
        xfsnodeSync.AddedXFSnodeList();
        GetMainSignals().UpdatedXFSnode(*pmn);
    }

    // let's store this ping as the last one
    LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- XFSnode ping accepted, xfsnode=%s\n", vin.prevout.ToStringShort());
    pmn->SetLastPing(*this);

    // and update mnodeman.mapSeenXFSnodeBroadcast.lastPing which is probably outdated
    CXFSnodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenXFSnodeBroadcast.count(hash)) {
        mnodeman.mapSeenXFSnodeBroadcast[hash].second.SetLastPing(*this);
    }

    pmn->Check(true); // force update, ignoring cache
    if (!pmn->IsEnabled()) return false;

    LogPrint("xfsnode", "CXFSnodePing::CheckAndUpdate -- XFSnode ping acceepted and relayed, xfsnode=%s\n", vin.prevout.ToStringShort());
    Relay();

    return true;
}

void CXFSnodePing::Relay() {
    CInv inv(MSG_XFSNODE_PING, GetHash());
    RelayInv(inv);
}

//void CXFSnode::AddGovernanceVote(uint256 nGovernanceObjectHash)
//{
//    if(mapGovernanceObjectsVotedOn.count(nGovernanceObjectHash)) {
//        mapGovernanceObjectsVotedOn[nGovernanceObjectHash]++;
//    } else {
//        mapGovernanceObjectsVotedOn.insert(std::make_pair(nGovernanceObjectHash, 1));
//    }
//}

//void CXFSnode::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
//{
//    std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.find(nGovernanceObjectHash);
//    if(it == mapGovernanceObjectsVotedOn.end()) {
//        return;
//    }
//    mapGovernanceObjectsVotedOn.erase(it);
//}

void CXFSnode::UpdateWatchdogVoteTime() {
    LOCK(cs);
    nTimeLastWatchdogVote = GetTime();
}

/**
*   FLAG GOVERNANCE ITEMS AS DIRTY
*
*   - When xfsnode come and go on the network, we must flag the items they voted on to recalc it's cached flags
*
*/
//void CXFSnode::FlagGovernanceItemsAsDirty()
//{
//    std::vector<uint256> vecDirty;
//    {
//        std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.begin();
//        while(it != mapGovernanceObjectsVotedOn.end()) {
//            vecDirty.push_back(it->first);
//            ++it;
//        }
//    }
//    for(size_t i = 0; i < vecDirty.size(); ++i) {
//        mnodeman.AddDirtyGovernanceObjectHash(vecDirty[i]);
//    }
//}
