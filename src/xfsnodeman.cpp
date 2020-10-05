// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activexfsnode.h"
#include "addrman.h"
#include "darksend.h"
//#include "governance.h"
#include "xfsnode-payments.h"
#include "xfsnode-sync.h"
#include "xfsnode.h"
#include "xfsnodeconfig.h"
#include "xfsnodeman.h"
#include "netfulfilledman.h"
#include "util.h"
#include "validationinterface.h"

/** XFSnode manager */
CXFSnodeMan mnodeman;

const std::string CXFSnodeMan::SERIALIZATION_VERSION_STRING = "CXFSnodeMan-Version-4";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CXFSnode*>& t1,
                    const std::pair<int, CXFSnode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CXFSnode*>& t1,
                    const std::pair<int64_t, CXFSnode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CXFSnodeIndex::CXFSnodeIndex()
    : nSize(0),
      mapIndex(),
      mapReverseIndex()
{}

bool CXFSnodeIndex::Get(int nIndex, CTxIn& vinXFSnode) const
{
    rindex_m_cit it = mapReverseIndex.find(nIndex);
    if(it == mapReverseIndex.end()) {
        return false;
    }
    vinXFSnode = it->second;
    return true;
}

int CXFSnodeIndex::GetXFSnodeIndex(const CTxIn& vinXFSnode) const
{
    index_m_cit it = mapIndex.find(vinXFSnode);
    if(it == mapIndex.end()) {
        return -1;
    }
    return it->second;
}

void CXFSnodeIndex::AddXFSnodeVIN(const CTxIn& vinXFSnode)
{
    index_m_it it = mapIndex.find(vinXFSnode);
    if(it != mapIndex.end()) {
        return;
    }
    int nNextIndex = nSize;
    mapIndex[vinXFSnode] = nNextIndex;
    mapReverseIndex[nNextIndex] = vinXFSnode;
    ++nSize;
}

void CXFSnodeIndex::Clear()
{
    mapIndex.clear();
    mapReverseIndex.clear();
    nSize = 0;
}
struct CompareByAddr

{
    bool operator()(const CXFSnode* t1,
                    const CXFSnode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

void CXFSnodeIndex::RebuildIndex()
{
    nSize = mapIndex.size();
    for(index_m_it it = mapIndex.begin(); it != mapIndex.end(); ++it) {
        mapReverseIndex[it->second] = it->first;
    }
}

CXFSnodeMan::CXFSnodeMan() : cs(),
  vXFSnodes(),
  mAskedUsForXFSnodeList(),
  mWeAskedForXFSnodeList(),
  mWeAskedForXFSnodeListEntry(),
  mWeAskedForVerification(),
  mMnbRecoveryRequests(),
  mMnbRecoveryGoodReplies(),
  listScheduledMnbRequestConnections(),
  nLastIndexRebuildTime(0),
  indexXFSnodes(),
  indexXFSnodesOld(),
  fIndexRebuilt(false),
  fXFSnodesAdded(false),
  fXFSnodesRemoved(false),
//  vecDirtyGovernanceObjectHashes(),
  nLastWatchdogVoteTime(0),
  mapSeenXFSnodeBroadcast(),
  mapSeenXFSnodePing(),
  nDsqCount(0)
{}

bool CXFSnodeMan::Add(CXFSnode &mn)
{
    LOCK(cs);

    CXFSnode *pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("xfsnode", "CXFSnodeMan::Add -- Adding new XFSnode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
        vXFSnodes.push_back(mn);
        indexXFSnodes.AddXFSnodeVIN(mn.vin);
        fXFSnodesAdded = true;
        return true;
    }

    return false;
}

void CXFSnodeMan::AskForMN(CNode* pnode, const CTxIn &vin)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForXFSnodeListEntry.find(vin.prevout);
    if (it1 != mWeAskedForXFSnodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CXFSnodeMan::AskForMN -- Asking same peer %s for missing xfsnode entry again: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CXFSnodeMan::AskForMN -- Asking new peer %s for missing xfsnode entry: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CXFSnodeMan::AskForMN -- Asking peer %s for missing xfsnode entry for the first time: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
    }
    mWeAskedForXFSnodeListEntry[vin.prevout][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, vin);
}

void CXFSnodeMan::Check()
{
    LOCK(cs);

//    LogPrint("xfsnode", "CXFSnodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
        mn.Check();
    }
}

void CXFSnodeMan::CheckAndRemove()
{
    if(!xfsnodeSync.IsXFSnodeListSynced()) return;

    LogPrintf("CXFSnodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateXFSnodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent xfsnodes, prepare structures and make requests to reasure the state of inactive ones
        std::vector<CXFSnode>::iterator it = vXFSnodes.begin();
        std::vector<std::pair<int, CXFSnode> > vecXFSnodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES xfsnode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        while(it != vXFSnodes.end()) {
            CXFSnodeBroadcast mnb = CXFSnodeBroadcast(*it);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if ((*it).IsOutpointSpent()) {
                LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- Removing XFSnode: %s  addr=%s  %i now\n", (*it).GetStateString(), (*it).addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenXFSnodeBroadcast.erase(hash);
                mWeAskedForXFSnodeListEntry.erase((*it).vin.prevout);

                // and finally remove it from the list
//                it->FlagGovernanceItemsAsDirty();
                it = vXFSnodes.erase(it);
                fXFSnodesRemoved = true;
            } else {
                bool fAsk = pCurrentBlockIndex &&
                            (nAskForMnbRecovery > 0) &&
                            xfsnodeSync.IsSynced() &&
                            it->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecXFSnodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(pCurrentBlockIndex->nHeight);
                        vecXFSnodeRanks = GetXFSnodeRanks(nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL xfsnodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecXFSnodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForXFSnodeListEntry.count(it->vin.prevout) && mWeAskedForXFSnodeListEntry[it->vin.prevout].count(vecXFSnodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecXFSnodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- Recovery initiated, xfsnode=%s\n", it->vin.prevout.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for XFSNODE_NEW_START_REQUIRED xfsnodes
        LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CXFSnodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- reprocessing mnb, xfsnode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenXFSnodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateXFSnodeList(NULL, itMnbReplies->second[0], nDos);
                }
                LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- removing mnb recovery reply, xfsnode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in XFSNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the XFSnode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForXFSnodeList.begin();
        while(it1 != mAskedUsForXFSnodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForXFSnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the XFSnode list
        it1 = mWeAskedForXFSnodeList.begin();
        while(it1 != mWeAskedForXFSnodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForXFSnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which XFSnodes we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForXFSnodeListEntry.begin();
        while(it2 != mWeAskedForXFSnodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForXFSnodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CXFSnodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenXFSnodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenXFSnodePing
        std::map<uint256, CXFSnodePing>::iterator it4 = mapSeenXFSnodePing.begin();
        while(it4 != mapSeenXFSnodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- Removing expired XFSnode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenXFSnodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenXFSnodeVerification
        std::map<uint256, CXFSnodeVerification>::iterator itv2 = mapSeenXFSnodeVerification.begin();
        while(itv2 != mapSeenXFSnodeVerification.end()){
            if((*itv2).second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS){
                LogPrint("xfsnode", "CXFSnodeMan::CheckAndRemove -- Removing expired XFSnode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenXFSnodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CXFSnodeMan::CheckAndRemove -- %s\n", ToString());

        if(fXFSnodesRemoved) {
            CheckAndRebuildXFSnodeIndex();
        }
    }

    if(fXFSnodesRemoved) {
        NotifyXFSnodeUpdates();
    }
}

void CXFSnodeMan::Clear()
{
    LOCK(cs);
    vXFSnodes.clear();
    mAskedUsForXFSnodeList.clear();
    mWeAskedForXFSnodeList.clear();
    mWeAskedForXFSnodeListEntry.clear();
    mapSeenXFSnodeBroadcast.clear();
    mapSeenXFSnodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexXFSnodes.Clear();
    indexXFSnodesOld.Clear();
}

int CXFSnodeMan::CountXFSnodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinXFSnodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
        if(mn.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CXFSnodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinXFSnodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
        if(mn.nProtocolVersion < nProtocolVersion || !mn.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 xfsnodes are allowed in 12.1, saving this for later
int CXFSnodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes)
        if ((nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CXFSnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForXFSnodeList.find(pnode->addr);
            if(it != mWeAskedForXFSnodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CXFSnodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }
    
    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForXFSnodeList[pnode->addr] = askAgain;

    LogPrint("xfsnode", "CXFSnodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CXFSnode* CXFSnodeMan::Find(const std::string &txHash, const std::string outputIndex)
{
    LOCK(cs);

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes)
    {
        COutPoint outpoint = mn.vin.prevout;

        if(txHash==outpoint.hash.ToString().substr(0,64) &&
           outputIndex==to_string(outpoint.n))
            return &mn;
    }
    return NULL;
}

CXFSnode* CXFSnodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CXFSnode* CXFSnodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CXFSnode* CXFSnodeMan::Find(const CPubKey &pubKeyXFSnode)
{
    LOCK(cs);

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes)
    {
        if(mn.pubKeyXFSnode == pubKeyXFSnode)
            return &mn;
    }
    return NULL;
}

bool CXFSnodeMan::Get(const CPubKey& pubKeyXFSnode, CXFSnode& xfsnode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CXFSnode* pMN = Find(pubKeyXFSnode);
    if(!pMN)  {
        return false;
    }
    xfsnode = *pMN;
    return true;
}

bool CXFSnodeMan::Get(const CTxIn& vin, CXFSnode& xfsnode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    xfsnode = *pMN;
    return true;
}

xfsnode_info_t CXFSnodeMan::GetXFSnodeInfo(const CTxIn& vin)
{
    xfsnode_info_t info;
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

xfsnode_info_t CXFSnodeMan::GetXFSnodeInfo(const CPubKey& pubKeyXFSnode)
{
    xfsnode_info_t info;
    LOCK(cs);
    CXFSnode* pMN = Find(pubKeyXFSnode);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bool CXFSnodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    return (pMN != NULL);
}

char* CXFSnodeMan::GetNotQualifyReason(CXFSnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount)
{
    if (!mn.IsValidForPayment()) {
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'not valid for payment'");
        return reasonStr;
    }
    // //check protocol version
    if (mn.nProtocolVersion < mnpayments.GetMinXFSnodePaymentsProto()) {
        // LogPrintf("Invalid nProtocolVersion!\n");
        // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
        // LogPrintf("mnpayments.GetMinXFSnodePaymentsProto=%s!\n", mnpayments.GetMinXFSnodePaymentsProto());
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'Invalid nProtocolVersion', nProtocolVersion=%d", mn.nProtocolVersion);
        return reasonStr;
    }
    //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    if (mnpayments.IsScheduled(mn, nBlockHeight)) {
        // LogPrintf("mnpayments.IsScheduled!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'is scheduled'");
        return reasonStr;
    }
    //it's too new, wait for a cycle
    if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
        // LogPrintf("it's too new, wait for a cycle!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'too new', sigTime=%s, will be qualifed after=%s",
                DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
        return reasonStr;
    }
    //make sure it has at least as many confirmations as there are xfsnodes
    if (mn.GetCollateralAge() < nMnCount) {
        // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
        // LogPrintf("nMnCount=%s!\n", nMnCount);
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'collateralAge < znCount', collateralAge=%d, znCount=%d", mn.GetCollateralAge(), nMnCount);
        return reasonStr;
    }
    return NULL;
}

// Same method, different return type, to avoid XFSnode operator issues.
// TODO: discuss standardizing the JSON type here, as it's done everywhere else in the code.
UniValue CXFSnodeMan::GetNotQualifyReasonToUniValue(CXFSnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount)
{
    UniValue ret(UniValue::VOBJ);
    UniValue data(UniValue::VOBJ);
    string description;

    if (!mn.IsValidForPayment()) {
        description = "not valid for payment";
    }

    //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    else if (mnpayments.IsScheduled(mn, nBlockHeight)) {
        description = "Is scheduled";
    }

    // //check protocol version
    else if (mn.nProtocolVersion < mnpayments.GetMinXFSnodePaymentsProto()) {
        description = "Invalid nProtocolVersion";

        data.push_back(Pair("nProtocolVersion", mn.nProtocolVersion));
    }

    //it's too new, wait for a cycle
    else if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
        // LogPrintf("it's too new, wait for a cycle!\n");
        description = "Too new";

        //TODO unix timestamp
        data.push_back(Pair("sigTime", mn.sigTime));
        data.push_back(Pair("qualifiedAfter", mn.sigTime + (nMnCount * 2.6 * 60)));
    }
    //make sure it has at least as many confirmations as there are xfsnodes
    else if (mn.GetCollateralAge() < nMnCount) {
        description = "collateralAge < znCount";

        data.push_back(Pair("collateralAge", mn.GetCollateralAge()));
        data.push_back(Pair("znCount", nMnCount));
    }

    ret.push_back(Pair("result", description.empty()));
    if(!description.empty()){
        ret.push_back(Pair("description", description));
    }
    if(!data.empty()){
        ret.push_back(Pair("data", data));
    }

    return ret;
}

//
// Deterministically select the oldest/best xfsnode to pay on the network
//
CXFSnode* CXFSnodeMan::GetNextXFSnodeInQueueForPayment(bool fFilterSigTime, int& nCount)
{
    if(!pCurrentBlockIndex) {
        nCount = 0;
        return NULL;
    }
    return GetNextXFSnodeInQueueForPayment(pCurrentBlockIndex->nHeight, fFilterSigTime, nCount);
}

CXFSnode* CXFSnodeMan::GetNextXFSnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    CXFSnode *pBestXFSnode = NULL;
    std::vector<std::pair<int, CXFSnode*> > vecXFSnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */
    int nMnCount = CountEnabled();
    int index = 0;
    BOOST_FOREACH(CXFSnode &mn, vXFSnodes)
    {
        index += 1;
        // LogPrintf("index=%s, mn=%s\n", index, mn.ToString());
        /*if (!mn.IsValidForPayment()) {
            LogPrint("xfsnodeman", "XFSnode, %s, addr(%s), not-qualified: 'not valid for payment'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        // //check protocol version
        if (mn.nProtocolVersion < mnpayments.GetMinXFSnodePaymentsProto()) {
            // LogPrintf("Invalid nProtocolVersion!\n");
            // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
            // LogPrintf("mnpayments.GetMinXFSnodePaymentsProto=%s!\n", mnpayments.GetMinXFSnodePaymentsProto());
            LogPrint("xfsnodeman", "XFSnode, %s, addr(%s), not-qualified: 'invalid nProtocolVersion'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (mnpayments.IsScheduled(mn, nBlockHeight)) {
            // LogPrintf("mnpayments.IsScheduled!\n");
            LogPrint("xfsnodeman", "XFSnode, %s, addr(%s), not-qualified: 'IsScheduled'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
            // LogPrintf("it's too new, wait for a cycle!\n");
            LogPrint("xfsnodeman", "XFSnode, %s, addr(%s), not-qualified: 'it's too new, wait for a cycle!', sigTime=%s, will be qualifed after=%s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
            continue;
        }
        //make sure it has at least as many confirmations as there are xfsnodes
        if (mn.GetCollateralAge() < nMnCount) {
            // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
            // LogPrintf("nMnCount=%s!\n", nMnCount);
            LogPrint("xfsnodeman", "XFSnode, %s, addr(%s), not-qualified: 'mn.GetCollateralAge() < nMnCount', CollateralAge=%d, nMnCount=%d\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), mn.GetCollateralAge(), nMnCount);
            continue;
        }*/
        char* reasonStr = GetNotQualifyReason(mn, nBlockHeight, fFilterSigTime, nMnCount);
        if (reasonStr != NULL) {
            LogPrint("xfsnodeman", "XFSnode, %s, addr(%s), qualify %s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), reasonStr);
            delete [] reasonStr;
            continue;
        }
        vecXFSnodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }
    nCount = (int)vecXFSnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount / 3) {
        // LogPrintf("Need Return, nCount=%s, nMnCount/3=%s\n", nCount, nMnCount/3);
        return GetNextXFSnodeInQueueForPayment(nBlockHeight, false, nCount);
    }

    // Sort them low to high
    sort(vecXFSnodeLastPaid.begin(), vecXFSnodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CXFSnode::GetNextXFSnodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int, CXFSnode*)& s, vecXFSnodeLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestXFSnode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestXFSnode;
}

CXFSnode* CXFSnodeMan::FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinXFSnodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CXFSnodeMan::FindRandomNotInVec -- %d enabled xfsnodes, %d xfsnodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CXFSnode*> vpXFSnodesShuffled;
    BOOST_FOREACH(CXFSnode &mn, vXFSnodes) {
        vpXFSnodesShuffled.push_back(&mn);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpXFSnodesShuffled.begin(), vpXFSnodesShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CXFSnode* pmn, vpXFSnodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("xfsnode", "CXFSnodeMan::FindRandomNotInVec -- found, xfsnode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("xfsnode", "CXFSnodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CXFSnodeMan::GetXFSnodeRank(const CTxIn& vin, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CXFSnode*> > vecXFSnodeScores;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive) {
            if(!mn.IsEnabled()) continue;
        }
        else {
            if(!mn.IsValidForPayment()) continue;
        }
        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecXFSnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecXFSnodeScores.rbegin(), vecXFSnodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CXFSnode*)& scorePair, vecXFSnodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<std::pair<int, CXFSnode> > CXFSnodeMan::GetXFSnodeRanks(int nBlockHeight, int nMinProtocol)
{
    std::vector<std::pair<int64_t, CXFSnode*> > vecXFSnodeScores;
    std::vector<std::pair<int, CXFSnode> > vecXFSnodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return vecXFSnodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {

        if(mn.nProtocolVersion < nMinProtocol || !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecXFSnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecXFSnodeScores.rbegin(), vecXFSnodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CXFSnode*)& s, vecXFSnodeScores) {
        nRank++;
        s.second->SetRank(nRank);
        vecXFSnodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecXFSnodeRanks;
}

CXFSnode* CXFSnodeMan::GetXFSnodeByRank(int nRank, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CXFSnode*> > vecXFSnodeScores;

    LOCK(cs);

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CXFSnode::GetXFSnodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // Fill scores
    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {

        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive && !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecXFSnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecXFSnodeScores.rbegin(), vecXFSnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CXFSnode*)& s, vecXFSnodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CXFSnodeMan::ProcessXFSnodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fXFSnode) {
            if(darkSendPool.pSubmittedToXFSnode != NULL && pnode->addr == darkSendPool.pSubmittedToXFSnode->addr) continue;
            // LogPrintf("Closing XFSnode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

std::pair<CService, std::set<uint256> > CXFSnodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CXFSnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

//    LogPrint("xfsnode", "CXFSnodeMan::ProcessMessage, strCommand=%s\n", strCommand);
    if(fLiteMode) return; // disable all XFS specific functionality
    if(!xfsnodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MNANNOUNCE) { //XFSnode Broadcast
        CXFSnodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        LogPrintf("MNANNOUNCE -- XFSnode announce, xfsnode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateXFSnodeList(pfrom, mnb, nDos)) {
            // use announced XFSnode as a peer
            addrman.Add(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fXFSnodesAdded) {
            NotifyXFSnodeUpdates();
        }
    } else if (strCommand == NetMsgType::MNPING) { //XFSnode Ping

        CXFSnodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        LogPrint("xfsnode", "MNPING -- XFSnode ping, xfsnode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenXFSnodePing.count(nHash)) return; //seen
        mapSeenXFSnodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("xfsnode", "MNPING -- XFSnode ping, xfsnode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this XFSnode
        CXFSnode* pmn = mnodeman.Find(mnp.vin);

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a xfsnode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get XFSnode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after xfsnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!xfsnodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("xfsnode", "DSEG -- XFSnode list, xfsnode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForXFSnodeList.find(pfrom->addr);
                if (i != mAskedUsForXFSnodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForXFSnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network xfsnode
            if (mn.IsUpdateRequired()) continue; // do not send outdated xfsnodes

            LogPrint("xfsnode", "DSEG -- Sending XFSnode entry: xfsnode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CXFSnodeBroadcast mnb = CXFSnodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_XFSNODE_ANNOUNCE, hash));
            pfrom->PushInventory(CInv(MSG_XFSNODE_PING, mn.lastPing.GetHash()));
            nInvCount++;

            if (!mapSeenXFSnodeBroadcast.count(hash)) {
                mapSeenXFSnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));
            }

            if (vin == mn.vin) {
                LogPrintf("DSEG -- Sent 1 XFSnode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, XFSNODE_SYNC_LIST, nInvCount);
            LogPrintf("DSEG -- Sent %d XFSnode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("xfsnode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // XFSnode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CXFSnodeVerification mnv;
        vRecv >> mnv;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some xfsnode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some xfsnode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of xfsnodes via unique direct requests.

void CXFSnodeMan::DoFullVerificationStep()
{
    if(activeXFSnode.vin == CTxIn()) return;
    if(!xfsnodeSync.IsSynced()) return;

    std::vector<std::pair<int, CXFSnode> > vecXFSnodeRanks = GetXFSnodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecXFSnodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CXFSnode> >::iterator it = vecXFSnodeRanks.begin();
    while(it != vecXFSnodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("xfsnode", "CXFSnodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeXFSnode.vin) {
            nMyRank = it->first;
            LogPrint("xfsnode", "CXFSnodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d xfsnodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this xfsnode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS xfsnodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecXFSnodeRanks.size()) return;

    std::vector<CXFSnode*> vSortedByAddr;
    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
        vSortedByAddr.push_back(&mn);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecXFSnodeRanks.begin() + nOffset;
    while(it != vecXFSnodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("xfsnode", "CXFSnodeMan::DoFullVerificationStep -- Already %s%s%s xfsnode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecXFSnodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("xfsnode", "CXFSnodeMan::DoFullVerificationStep -- Verifying xfsnode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecXFSnodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("xfsnode", "CXFSnodeMan::DoFullVerificationStep -- Sent verification requests to %d xfsnodes\n", nCount);
}

// This function tries to find xfsnodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CXFSnodeMan::CheckSameAddr()
{
    if(!xfsnodeSync.IsSynced() || vXFSnodes.empty()) return;

    std::vector<CXFSnode*> vBan;
    std::vector<CXFSnode*> vSortedByAddr;

    {
        LOCK(cs);

        CXFSnode* pprevXFSnode = NULL;
        CXFSnode* pverifiedXFSnode = NULL;

        BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
            vSortedByAddr.push_back(&mn);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CXFSnode* pmn, vSortedByAddr) {
            // check only (pre)enabled xfsnodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevXFSnode) {
                pprevXFSnode = pmn;
                pverifiedXFSnode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevXFSnode->addr) {
                if(pverifiedXFSnode) {
                    // another xfsnode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this xfsnode with the same ip is verified, ban previous one
                    vBan.push_back(pprevXFSnode);
                    // and keep a reference to be able to ban following xfsnodes with the same ip
                    pverifiedXFSnode = pmn;
                }
            } else {
                pverifiedXFSnode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevXFSnode = pmn;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CXFSnode* pmn, vBan) {
        LogPrintf("CXFSnodeMan::CheckSameAddr -- increasing PoSe ban score for xfsnode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CXFSnodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<CXFSnode*>& vSortedByAddr)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("xfsnode", "CXFSnodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = ConnectNode(addr, NULL, false, true);
    if(pnode == NULL) {
        LogPrintf("CXFSnodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CXFSnodeVerification mnv(addr, GetRandInt(999999), pCurrentBlockIndex->nHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CXFSnodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CXFSnodeMan::SendVerifyReply(CNode* pnode, CXFSnodeVerification& mnv)
{
    // only xfsnodes can sign this, why would someone ask regular node?
    if(!fXFSNode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
//        // peer should not ask us that often
        LogPrintf("XFSnodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("XFSnodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeXFSnode.service.ToString(), mnv.nonce, blockHash.ToString());

    if(!darkSendSigner.SignMessage(strMessage, mnv.vchSig1, activeXFSnode.keyXFSnode)) {
        LogPrintf("XFSnodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!darkSendSigner.VerifyMessage(activeXFSnode.pubKeyXFSnode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("XFSnodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CXFSnodeMan::ProcessVerifyReply(CNode* pnode, CXFSnodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CXFSnodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CXFSnodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CXFSnodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("XFSnodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

//    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CXFSnodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CXFSnode* prealXFSnode = NULL;
        std::vector<CXFSnode*> vpXFSnodesToBan;
        std::vector<CXFSnode>::iterator it = vXFSnodes.begin();
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(), mnv.nonce, blockHash.ToString());
        while(it != vXFSnodes.end()) {
            if(CAddress(it->addr, NODE_NETWORK) == pnode->addr) {
                if(darkSendSigner.VerifyMessage(it->pubKeyXFSnode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealXFSnode = &(*it);
                    if(!it->IsPoSeVerified()) {
                        it->DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated xfsnode
                    if(activeXFSnode.vin == CTxIn()) continue;
                    // update ...
                    mnv.addr = it->addr;
                    mnv.vin1 = it->vin;
                    mnv.vin2 = activeXFSnode.vin;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!darkSendSigner.SignMessage(strMessage2, mnv.vchSig2, activeXFSnode.keyXFSnode)) {
                        LogPrintf("XFSnodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!darkSendSigner.VerifyMessage(activeXFSnode.pubKeyXFSnode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("XFSnodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mnv.Relay();

                } else {
                    vpXFSnodesToBan.push_back(&(*it));
                }
            }
            ++it;
        }
        // no real xfsnode found?...
        if(!prealXFSnode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CXFSnodeMan::ProcessVerifyReply -- ERROR: no real xfsnode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CXFSnodeMan::ProcessVerifyReply -- verified real xfsnode %s for addr %s\n",
                    prealXFSnode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CXFSnode* pmn, vpXFSnodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("xfsnode", "CXFSnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealXFSnode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        LogPrintf("CXFSnodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake xfsnodes, addr %s\n",
                    (int)vpXFSnodesToBan.size(), pnode->addr.ToString());
    }
}

void CXFSnodeMan::ProcessVerifyBroadcast(CNode* pnode, const CXFSnodeVerification& mnv)
{
    std::string strError;

    if(mapSeenXFSnodeVerification.find(mnv.GetHash()) != mapSeenXFSnodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenXFSnodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
        LogPrint("xfsnode", "XFSnodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.vin1.prevout == mnv.vin2.prevout) {
        LogPrint("xfsnode", "XFSnodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("XFSnodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank = GetXFSnodeRank(mnv.vin2, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION);

    if (nRank == -1) {
        LogPrint("xfsnode", "CXFSnodeMan::ProcessVerifyBroadcast -- Can't calculate rank for xfsnode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("xfsnode", "CXFSnodeMan::ProcessVerifyBroadcast -- Mastrernode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CXFSnode* pmn1 = Find(mnv.vin1);
        if(!pmn1) {
            LogPrintf("CXFSnodeMan::ProcessVerifyBroadcast -- can't find xfsnode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CXFSnode* pmn2 = Find(mnv.vin2);
        if(!pmn2) {
            LogPrintf("CXFSnodeMan::ProcessVerifyBroadcast -- can't find xfsnode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CXFSnodeMan::ProcessVerifyBroadcast -- addr %s do not match %s\n", mnv.addr.ToString(), pnode->addr.ToString());
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn1->pubKeyXFSnode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("XFSnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for xfsnode1 failed, error: %s\n", strError);
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn2->pubKeyXFSnode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("XFSnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for xfsnode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CXFSnodeMan::ProcessVerifyBroadcast -- verified xfsnode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pnode->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
            if(mn.addr != mnv.addr || mn.vin.prevout == mnv.vin1.prevout) continue;
            mn.IncreasePoSeBanScore();
            nCount++;
            LogPrint("xfsnode", "CXFSnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mn.vin.prevout.ToStringShort(), mn.addr.ToString(), mn.nPoSeBanScore);
        }
        LogPrintf("CXFSnodeMan::ProcessVerifyBroadcast -- PoSe score incresed for %d fake xfsnodes, addr %s\n",
                    nCount, pnode->addr.ToString());
    }
}

std::string CXFSnodeMan::ToString() const
{
    std::ostringstream info;

    info << "XFSnodes: " << (int)vXFSnodes.size() <<
            ", peers who asked us for XFSnode list: " << (int)mAskedUsForXFSnodeList.size() <<
            ", peers we asked for XFSnode list: " << (int)mWeAskedForXFSnodeList.size() <<
            ", entries in XFSnode list we asked for: " << (int)mWeAskedForXFSnodeListEntry.size() <<
            ", xfsnode index size: " << indexXFSnodes.GetSize() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CXFSnodeMan::UpdateXFSnodeList(CXFSnodeBroadcast mnb)
{
    try {
        LogPrintf("CXFSnodeMan::UpdateXFSnodeList\n");
        LOCK2(cs_main, cs);
        mapSeenXFSnodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
        mapSeenXFSnodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

        LogPrintf("CXFSnodeMan::UpdateXFSnodeList -- xfsnode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

        CXFSnode *pmn = Find(mnb.vin);
        if (pmn == NULL) {
            CXFSnode mn(mnb);
            if (Add(mn)) {
                xfsnodeSync.AddedXFSnodeList();
                GetMainSignals().UpdatedXFSnode(mn);
            }
        } else {
            CXFSnodeBroadcast mnbOld = mapSeenXFSnodeBroadcast[CXFSnodeBroadcast(*pmn).GetHash()].second;
            if (pmn->UpdateFromNewBroadcast(mnb)) {
                xfsnodeSync.AddedXFSnodeList();
                GetMainSignals().UpdatedXFSnode(*pmn);
                mapSeenXFSnodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "UpdateXFSnodeList");
    }
}

bool CXFSnodeMan::CheckMnbAndUpdateXFSnodeList(CNode* pfrom, CXFSnodeBroadcast mnb, int& nDos)
{
    // Need LOCK2 here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- xfsnode=%s\n", mnb.vin.prevout.ToStringShort());

        uint256 hash = mnb.GetHash();
        if (mapSeenXFSnodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- xfsnode=%s seen\n", mnb.vin.prevout.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if (GetTime() - mapSeenXFSnodeBroadcast[hash].first > XFSNODE_NEW_START_REQUIRED_SECONDS - XFSNODE_MIN_MNP_SECONDS * 2) {
                LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- xfsnode=%s seen update\n", mnb.vin.prevout.ToStringShort());
                mapSeenXFSnodeBroadcast[hash].first = GetTime();
                xfsnodeSync.AddedXFSnodeList();
                GetMainSignals().UpdatedXFSnode(mnb);
            }
            // did we ask this node for it?
            if (pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- mnb=%s seen request\n", hash.ToString());
                if (mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if (mnb.lastPing.sigTime > mapSeenXFSnodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CXFSnode mnTemp = CXFSnode(mnb);
                        mnTemp.Check();
                        LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetTime() - mnb.lastPing.sigTime) / 60, mnTemp.GetStateString());
                        if (mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- xfsnode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenXFSnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- xfsnode=%s new\n", mnb.vin.prevout.ToStringShort());

        if (!mnb.SimpleCheck(nDos)) {
            LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- SimpleCheck() failed, xfsnode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }

        // search XFSnode list
        CXFSnode *pmn = Find(mnb.vin);
        if (pmn) {
            CXFSnodeBroadcast mnbOld = mapSeenXFSnodeBroadcast[CXFSnodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos)) {
                LogPrint("xfsnode", "CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- Update() failed, xfsnode=%s\n", mnb.vin.prevout.ToStringShort());
                return false;
            }
            if (hash != mnbOld.GetHash()) {
                mapSeenXFSnodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } // end of LOCK(cs);

    if(mnb.CheckOutpoint(nDos)) {
        if(Add(mnb)){
            GetMainSignals().UpdatedXFSnode(mnb);  
        }
        xfsnodeSync.AddedXFSnodeList();
        // if it matches our XFSnode privkey...
        if(fXFSNode && mnb.pubKeyXFSnode == activeXFSnode.pubKeyXFSnode) {
            mnb.nPoSeBanScore = -XFSNODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- Got NEW XFSnode entry: xfsnode=%s  sigTime=%lld  addr=%s\n",
                            mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                activeXFSnode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.RelayXFSNode();
    } else {
        LogPrintf("CXFSnodeMan::CheckMnbAndUpdateXFSnodeList -- Rejected XFSnode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CXFSnodeMan::UpdateLastPaid()
{
    LOCK(cs);
    if(fLiteMode) return;
    if(!pCurrentBlockIndex) {
        // LogPrintf("CXFSnodeMan::UpdateLastPaid, pCurrentBlockIndex=NULL\n");
        return;
    }

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a xfsnode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fXFSNode) ? mnpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    LogPrint("mnpayments", "CXFSnodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
                             pCurrentBlockIndex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CXFSnode& mn, vXFSnodes) {
        mn.UpdateLastPaid(pCurrentBlockIndex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !xfsnodeSync.IsWinnersListSynced();
}

void CXFSnodeMan::CheckAndRebuildXFSnodeIndex()
{
    LOCK(cs);

    if(GetTime() - nLastIndexRebuildTime < MIN_INDEX_REBUILD_TIME) {
        return;
    }

    if(indexXFSnodes.GetSize() <= MAX_EXPECTED_INDEX_SIZE) {
        return;
    }

    if(indexXFSnodes.GetSize() <= int(vXFSnodes.size())) {
        return;
    }

    indexXFSnodesOld = indexXFSnodes;
    indexXFSnodes.Clear();
    for(size_t i = 0; i < vXFSnodes.size(); ++i) {
        indexXFSnodes.AddXFSnodeVIN(vXFSnodes[i].vin);
    }

    fIndexRebuilt = true;
    nLastIndexRebuildTime = GetTime();
}

void CXFSnodeMan::UpdateWatchdogVoteTime(const CTxIn& vin)
{
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->UpdateWatchdogVoteTime();
    nLastWatchdogVoteTime = GetTime();
}

bool CXFSnodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any xfsnodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= XFSNODE_WATCHDOG_MAX_SECONDS;
}

void CXFSnodeMan::CheckXFSnode(const CTxIn& vin, bool fForce)
{
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

void CXFSnodeMan::CheckXFSnode(const CPubKey& pubKeyXFSnode, bool fForce)
{
    LOCK(cs);
    CXFSnode* pMN = Find(pubKeyXFSnode);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

int CXFSnodeMan::GetXFSnodeState(const CTxIn& vin)
{
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN)  {
        return CXFSnode::XFSNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

int CXFSnodeMan::GetXFSnodeState(const CPubKey& pubKeyXFSnode)
{
    LOCK(cs);
    CXFSnode* pMN = Find(pubKeyXFSnode);
    if(!pMN)  {
        return CXFSnode::XFSNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

bool CXFSnodeMan::IsXFSnodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN) {
        return false;
    }
    return pMN->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CXFSnodeMan::SetXFSnodeLastPing(const CTxIn& vin, const CXFSnodePing& mnp)
{
    LOCK(cs);
    CXFSnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->SetLastPing(mnp);
    mapSeenXFSnodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CXFSnodeBroadcast mnb(*pMN);
    uint256 hash = mnb.GetHash();
    if(mapSeenXFSnodeBroadcast.count(hash)) {
        mapSeenXFSnodeBroadcast[hash].second.SetLastPing(mnp);
    }
}

void CXFSnodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("xfsnode", "CXFSnodeMan::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    CheckSameAddr();
    
    if(fXFSNode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid();
    }
}

void CXFSnodeMan::NotifyXFSnodeUpdates()
{
    // Avoid double locking
    bool fXFSnodesAddedLocal = false;
    bool fXFSnodesRemovedLocal = false;
    {
        LOCK(cs);
        fXFSnodesAddedLocal = fXFSnodesAdded;
        fXFSnodesRemovedLocal = fXFSnodesRemoved;
    }

    if(fXFSnodesAddedLocal) {
//        governance.CheckXFSnodeOrphanObjects();
//        governance.CheckXFSnodeOrphanVotes();
    }
    if(fXFSnodesRemovedLocal) {
//        governance.UpdateCachesAndClean();
    }

    LOCK(cs);
    fXFSnodesAdded = false;
    fXFSnodesRemoved = false;
}
