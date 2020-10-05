// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XFSNODEMAN_H
#define XFSNODEMAN_H

#include "xfsnode.h"
#include "sync.h"

using namespace std;

class CXFSnodeMan;

extern CXFSnodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CXFSnodeMan
 */
class CXFSnodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CXFSnodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve xfsnode vin by index
    bool Get(int nIndex, CTxIn& vinXFSnode) const;

    /// Get index of a xfsnode vin
    int GetXFSnodeIndex(const CTxIn& vinXFSnode) const;

    void AddXFSnodeVIN(const CTxIn& vinXFSnode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CXFSnodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CXFSnode> vXFSnodes;
    // who's asked for the XFSnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForXFSnodeList;
    // who we asked for the XFSnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForXFSnodeList;
    // which XFSnodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForXFSnodeListEntry;
    // who we asked for the xfsnode verification
    std::map<CNetAddr, CXFSnodeVerification> mWeAskedForVerification;

    // these maps are used for xfsnode recovery from XFSNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CXFSnodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CXFSnodeIndex indexXFSnodes;

    CXFSnodeIndex indexXFSnodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when xfsnodes are added, cleared when CGovernanceManager is notified
    bool fXFSnodesAdded;

    /// Set when xfsnodes are removed, cleared when CGovernanceManager is notified
    bool fXFSnodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CXFSnodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CXFSnodeBroadcast> > mapSeenXFSnodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CXFSnodePing> mapSeenXFSnodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CXFSnodeVerification> mapSeenXFSnodeVerification;
    // keep track of dsq count to prevent xfsnodes from gaming darksend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vXFSnodes);
        READWRITE(mAskedUsForXFSnodeList);
        READWRITE(mWeAskedForXFSnodeList);
        READWRITE(mWeAskedForXFSnodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenXFSnodeBroadcast);
        READWRITE(mapSeenXFSnodePing);
        READWRITE(indexXFSnodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CXFSnodeMan();

    /// Add an entry
    bool Add(CXFSnode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all XFSnodes
    void Check();

    /// Check all XFSnodes and remove inactive
    void CheckAndRemove();

    /// Clear XFSnode vector
    void Clear();

    /// Count XFSnodes filtered by nProtocolVersion.
    /// XFSnode nProtocolVersion should match or be above the one specified in param here.
    int CountXFSnodes(int nProtocolVersion = -1);
    /// Count enabled XFSnodes filtered by nProtocolVersion.
    /// XFSnode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count XFSnodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CXFSnode* Find(const std::string &txHash, const std::string outputIndex);
    CXFSnode* Find(const CScript &payee);
    CXFSnode* Find(const CTxIn& vin);
    CXFSnode* Find(const CPubKey& pubKeyXFSnode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyXFSnode, CXFSnode& xfsnode);
    bool Get(const CTxIn& vin, CXFSnode& xfsnode);

    /// Retrieve xfsnode vin by index
    bool Get(int nIndex, CTxIn& vinXFSnode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexXFSnodes.Get(nIndex, vinXFSnode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a xfsnode vin
    int GetXFSnodeIndex(const CTxIn& vinXFSnode) {
        LOCK(cs);
        return indexXFSnodes.GetXFSnodeIndex(vinXFSnode);
    }

    /// Get old index of a xfsnode vin
    int GetXFSnodeIndexOld(const CTxIn& vinXFSnode) {
        LOCK(cs);
        return indexXFSnodesOld.GetXFSnodeIndex(vinXFSnode);
    }

    /// Get xfsnode VIN for an old index value
    bool GetXFSnodeVinForIndexOld(int nXFSnodeIndex, CTxIn& vinXFSnodeOut) {
        LOCK(cs);
        return indexXFSnodesOld.Get(nXFSnodeIndex, vinXFSnodeOut);
    }

    /// Get index of a xfsnode vin, returning rebuild flag
    int GetXFSnodeIndex(const CTxIn& vinXFSnode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexXFSnodes.GetXFSnodeIndex(vinXFSnode);
    }

    void ClearOldXFSnodeIndex() {
        LOCK(cs);
        indexXFSnodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    xfsnode_info_t GetXFSnodeInfo(const CTxIn& vin);

    xfsnode_info_t GetXFSnodeInfo(const CPubKey& pubKeyXFSnode);

    char* GetNotQualifyReason(CXFSnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount);

    UniValue GetNotQualifyReasonToUniValue(CXFSnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount);

    /// Find an entry in the xfsnode list that is next to be paid
    CXFSnode* GetNextXFSnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CXFSnode* GetNextXFSnodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CXFSnode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CXFSnode> GetFullXFSnodeVector() { LOCK(cs); return vXFSnodes; }

    std::vector<std::pair<int, CXFSnode> > GetXFSnodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetXFSnodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CXFSnode* GetXFSnodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessXFSnodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CXFSnode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CXFSnodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CXFSnodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CXFSnodeVerification& mnv);

    /// Return the number of (unique) XFSnodes
    int size() { return vXFSnodes.size(); }

    std::string ToString() const;

    /// Update xfsnode list and maps using provided CXFSnodeBroadcast
    void UpdateXFSnodeList(CXFSnodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateXFSnodeList(CNode* pfrom, CXFSnodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildXFSnodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    bool AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckXFSnode(const CTxIn& vin, bool fForce = false);
    void CheckXFSnode(const CPubKey& pubKeyXFSnode, bool fForce = false);

    int GetXFSnodeState(const CTxIn& vin);
    int GetXFSnodeState(const CPubKey& pubKeyXFSnode);

    bool IsXFSnodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetXFSnodeLastPing(const CTxIn& vin, const CXFSnodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the xfsnode index has been updated.
     * Must be called while not holding the CXFSnodeMan::cs mutex
     */
    void NotifyXFSnodeUpdates();

};

#endif
