// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/consensus.h"
#include "zerocoin_params.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "libzerocoin/bitcoin_bignum/bignum.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"
#include "arith_uint256.h"


static CBlock CreateGenesisBlock(const char *pszTimestamp, const CScript &genesisOutputScript, uint32_t nTime, uint32_t nNonce,
        uint32_t nBits, int32_t nVersion, const CAmount &genesisReward,
        std::vector<unsigned char> extraNonce) {
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 504365040 << CBigNum(4).getvch() << std::vector < unsigned char >
    ((const unsigned char *) pszTimestamp, (const unsigned char *) pszTimestamp + strlen(pszTimestamp)) << extraNonce;
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount &genesisReward,
        std::vector<unsigned char> extraNonce) {
    const char *pszTimestamp = "Severe acute respiratory syndrome coronavirus 2 - SARS-CoV-2";
    const CScript genesisOutputScript = CScript();
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward,
                              extraNonce);
}


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";

        consensus.chainType = Consensus::chainMain;

        consensus.nSubsidyHalvingFirst = 302438;
        consensus.nSubsidyHalvingInterval = 420000;
        consensus.nSubsidyHalvingStopBlock = 3646849;

        consensus.nMajorityEnforceBlockUpgrade = 8100;
        consensus.nMajorityRejectBlockOutdated = 10260;
        consensus.nMajorityWindow = 10800;
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        //static const int64 nInterval = nTargetTimespan / nTargetSpacing;
        consensus.nPowTargetTimespan = 40 * 60; // 40 minutes between retargets 
        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute
        consensus.nDgwPastBlocks = 30; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 10260; // 95% of 10800
        consensus.nMinerConfirmationWindow = 10800; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1580217929; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1611840329; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1580217929; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1611840329; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1580217929; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1611840329; // November 15th, 2017.

        // The best chain should have at least this much work
        //consensus.nMinimumChainWork = uint256S("0000000000000000000000000000000000000000000000002ee3ae8b33a68f5f");
        consensus.nMinimumChainWork = uint256S("0x0");

        consensus.nCheckBugFixedAtBlock = ZC_CHECK_BUG_FIXED_AT_BLOCK;
        consensus.nXFSnodePaymentsBugFixedAtBlock = ZC_XFSNODE_PAYMENT_BUG_FIXED_AT_BLOCK;
	    consensus.nSpendV15StartBlock = ZC_V1_5_STARTING_BLOCK;
	    consensus.nSpendV2ID_1 = ZC_V2_SWITCH_ID_1;
	    consensus.nSpendV2ID_10 = ZC_V2_SWITCH_ID_10;
	    consensus.nSpendV2ID_25 = ZC_V2_SWITCH_ID_25;
	    consensus.nSpendV2ID_50 = ZC_V2_SWITCH_ID_50;
	    consensus.nSpendV2ID_100 = ZC_V2_SWITCH_ID_100;
	    consensus.nModulusV2StartBlock = ZC_MODULUS_V2_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_MEMPOOL_STOP_BLOCK;
	    consensus.nModulusV1StopBlock = ZC_MODULUS_V1_STOP_BLOCK;
        consensus.nMultipleSpendInputsInOneTxStartBlock = ZC_MULTIPLE_SPEND_INPUT_STARTING_BLOCK;
        consensus.nDontAllowDupTxsStartBlock = 1;

        // xfsnode params
        consensus.nXFSnodePaymentsStartBlock = HF_XFSNODE_PAYMENT_START; // not true, but it's ok as long as it's less then nXFSnodePaymentsIncreaseBlock


        consensus.nDisableZerocoinStartBlock = 1;

        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "04290423a6ef6be3367257ea7f1b78d3e252103f6cf5659c26b187ed7b4e6380ace84a453c3a9b65aebb4cd1b039cf8dc1caff815f8cdfa3fd3278da007913ec04";
        //Stake stuff
        consensus.nFirstPOSBlock = 52;
        consensus.nStakeTimestampMask = 0xf; // 15
        consensus.posLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nDisableZCoinClientCheckTime = 1593515602; //Date and time (GMT): Tuesday, June 2, 2020 11:07:41 PM
        consensus.nBlacklistEnableHeight = 1;

        pchMessageStart[0] = 0xc1;
        pchMessageStart[1] = 0x1a;
        pchMessageStart[2] = 0xf1;
        pchMessageStart[3] = 0xff;
        nDefaultPort = 8668;
        nPruneAfterHeight = 100000;

        std::vector<unsigned char> extraNonce(4);
	    extraNonce[0] = 0xaa;
        extraNonce[1] = 0xc1;
        extraNonce[2] = 0xf1;
        extraNonce[3] = 0xcb;
        genesis = CreateGenesisBlock(ZC_GENESIS_BLOCK_TIME, 7382559, 0x1e00ffff, 2, 0 * COIN, extraNonce);

        consensus.hashGenesisBlock = genesis.GetHash();    

        assert(consensus.hashGenesisBlock == uint256S("0x0000002483dee2a97df95f61d810f2115bc83134d3c399168d682ea1da6d0fd8"));
        assert(genesis.hashMerkleRoot     == uint256S("ed2efdba33e9bc206cba44a213440e29a0902740f4f4209bdea833c48673606f"));
        //Initial seeders for use
        vSeeds.push_back(CDNSSeedData("peer5.xfscore.gq", "peer5.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer1.xfscore.gq", "peer1.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer2.xfscore.gq", "peer2.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer3.xfscore.gq", "peer3.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer4.xfscore.gq", "peer4.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer6.xfscore.gq", "peer6.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer7.xfscore.gq", "peer7.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer8.xfscore.gq", "peer8.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer9.xfscore.gq", "peer9.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer10.xfscore.gq", "peer10.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer11.xfscore.gq", "peer11.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer12.xfscore.gq", "peer12.xfscore.gq", false));
        vSeeds.push_back(CDNSSeedData("peer13.xfscore.gq", "peer13.xfscore.gq", false));

        // Note that of those with the service bits flag, most only support a subset of possible options
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 75); //XFS address starts with 'X'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 5);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 210);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container < std::vector < unsigned char > > ();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;
        nConsecutivePoWHeight = 40000;
        nMaxPoWBlocks = 1000;
        checkpointData = (CCheckpointData) {
                boost::assign::map_list_of
                    (0, genesis.GetHash())
                    //(200,uint256S("0x2eac965dcd0e10574dc05f44ee14756e5224bf521358e5455f33da1ad8a9536c"))
                    ,

                1593268902, // * UNIX timestamp of last checkpoint block
                148510,    // * total number of transactions between genesis and last checkpoint
                //   (the tx=... number in the SetBestChain debug.log lines)
                1440     // * estimated number of transactions per day after checkpoint
        };
        consensus.nSpendV15StartBlock = ZC_V1_5_STARTING_BLOCK;
        consensus.nSpendV2ID_1 = ZC_V2_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_STOP_BLOCK;

        // Sigma related values.
        consensus.nSigmaStartBlock = ZC_SIGMA_STARTING_BLOCK;
        consensus.nSigmaPaddingBlock = ZC_SIGMA_PADDING_BLOCK;
        consensus.nDisableUnpaddedSigmaBlock = ZC_SIGMA_DISABLE_UNPADDED_BLOCK;
        consensus.nOldSigmaBanBlock = ZC_OLD_SIGMA_BAN_BLOCK;
        consensus.nZerocoinV2MintMempoolGracefulPeriod = ZC_V2_MINT_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2MintGracefulPeriod = ZC_V2_MINT_GRACEFUL_PERIOD;
        consensus.nZerocoinV2SpendMempoolGracefulPeriod = ZC_V2_SPEND_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2SpendGracefulPeriod = ZC_V2_SPEND_GRACEFUL_PERIOD;
        consensus.nMaxSigmaInputPerBlock = ZC_SIGMA_INPUT_LIMIT_PER_BLOCK;
        consensus.nMaxValueSigmaSpendPerBlock = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK;
        consensus.nMaxSigmaInputPerTransaction = ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION;
        consensus.nMaxValueSigmaSpendPerTransaction = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION;
        consensus.nZerocoinToSigmaRemintWindowSize = 0;

        // Dandelion related values.
        consensus.nDandelionEmbargoMinimum = DANDELION_EMBARGO_MINIMUM;
        consensus.nDandelionEmbargoAvgAdd = DANDELION_EMBARGO_AVG_ADD;
        consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
        consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
        consensus.nDandelionFluff = DANDELION_FLUFF;
    }
};

static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";

        consensus.chainType = Consensus::chainTestnet;

        consensus.nSubsidyHalvingFirst = 302438;
        consensus.nSubsidyHalvingInterval = 420000;
        consensus.nSubsidyHalvingStopBlock = 3646849;

        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        //Proof-of-Stake related values
        consensus.nFirstPOSBlock = 135;//TODO akshaynexus :This needs to be decided
        consensus.nStakeTimestampMask = 0xf; // 15
        consensus.nPowTargetTimespan = 5 * 60; // 5 minutes between retargets
        consensus.nPowTargetSpacing = 1 * 60; // 1 minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1493596800; // May 1st 2017

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0");

        consensus.nSpendV15StartBlock = 5000;
        consensus.nCheckBugFixedAtBlock = 1;
        consensus.nXFSnodePaymentsBugFixedAtBlock = 1;

        consensus.nSpendV2ID_1 = ZC_V2_TESTNET_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_TESTNET_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_TESTNET_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_TESTNET_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_TESTNET_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_TESTNET_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_TESTNET_STOP_BLOCK;
        consensus.nMultipleSpendInputsInOneTxStartBlock = 1;
        consensus.nDontAllowDupTxsStartBlock = 18825;

        // XFSnode params testnet
        consensus.nXFSnodePaymentsStartBlock = 2200;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet


        consensus.nDisableZerocoinStartBlock = 50500;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "04ff38adc0c2fab7544d1123d664c2458d50c12b46988ef2214a6288883e19e56fb739ee3382f72a77cd9116169011cd739f84a3870781f856ae5d54f47e16cee8";
        strXFSnodePaymentsPubKey = "04ff38adc0c2fab7544d1123d664c2458d50c12b46988ef2214a6288883e19e56fb739ee3382f72a77cd9116169011cd739f84a3870781f856ae5d54f47e16cee8";

        pchMessageStart[0] = 0xc1;
        pchMessageStart[1] = 0x1a;
        pchMessageStart[2] = 0xf1;
        pchMessageStart[3] = 0xff;
        nDefaultPort = 18168;
        nPruneAfterHeight = 1000;

        std::vector<unsigned char> extraNonce(4);
	    extraNonce[0] = 0xaa;
        extraNonce[1] = 0xc1;
        extraNonce[2] = 0xf1;
        extraNonce[3] = 0xcb;
        genesis = CreateGenesisBlock(ZC_GENESIS_BLOCK_TIME, 7382559, 0x1e00ffff, 2, 0 * COIN, extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();
        
        assert(consensus.hashGenesisBlock ==
                uint256S("0x0000002483dee2a97df95f61d810f2115bc83134d3c399168d682ea1da6d0fd8"));
        assert(genesis.hashMerkleRoot ==
                uint256S("ed2efdba33e9bc206cba44a213440e29a0902740f4f4209bdea833c48673606f"));
        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        // index test seeds
        //vSeeds.push_back(CDNSSeedData("beta1.web.org", "beta1.web.org", false));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 65);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 178);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 185);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container < std::vector < unsigned char > > ();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        checkpointData = (CCheckpointData) {
                boost::assign::map_list_of
                    (0, uint256S("0x0000002483dee2a97df95f61d810f2115bc83134d3c399168d682ea1da6d0fd8")),
                    ZC_GENESIS_BLOCK_TIME,
                    0,
                    100.0
        };

        consensus.nSpendV15StartBlock = ZC_V1_5_TESTNET_STARTING_BLOCK;
        consensus.nSpendV2ID_1 = ZC_V2_TESTNET_SWITCH_ID_1;
        consensus.nSpendV2ID_10 = ZC_V2_TESTNET_SWITCH_ID_10;
        consensus.nSpendV2ID_25 = ZC_V2_TESTNET_SWITCH_ID_25;
        consensus.nSpendV2ID_50 = ZC_V2_TESTNET_SWITCH_ID_50;
        consensus.nSpendV2ID_100 = ZC_V2_TESTNET_SWITCH_ID_100;
        consensus.nModulusV2StartBlock = ZC_MODULUS_V2_TESTNET_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = ZC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
        consensus.nModulusV1StopBlock = ZC_MODULUS_V1_TESTNET_STOP_BLOCK;
        // Sigma related values.
        consensus.nSigmaStartBlock = ZC_SIGMA_TESTNET_STARTING_BLOCK;
        consensus.nSigmaPaddingBlock = ZC_SIGMA_TESTNET_PADDING_BLOCK;
        consensus.nDisableUnpaddedSigmaBlock = ZC_SIGMA_TESTNET_DISABLE_UNPADDED_BLOCK;
        consensus.nOldSigmaBanBlock = 70416;
        consensus.nZerocoinV2MintMempoolGracefulPeriod = ZC_V2_MINT_TESTNET_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2MintGracefulPeriod = ZC_V2_MINT_TESTNET_GRACEFUL_PERIOD;
        consensus.nZerocoinV2SpendMempoolGracefulPeriod = ZC_V2_SPEND_TESTNET_GRACEFUL_MEMPOOL_PERIOD;
        consensus.nZerocoinV2SpendGracefulPeriod = ZC_V2_SPEND_TESTNET_GRACEFUL_PERIOD;
            consensus.nMaxSigmaInputPerBlock = ZC_SIGMA_INPUT_LIMIT_PER_BLOCK;
            consensus.nMaxValueSigmaSpendPerBlock = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK;
            consensus.nMaxSigmaInputPerTransaction = ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION;
            consensus.nMaxValueSigmaSpendPerTransaction = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION;
            consensus.nZerocoinToSigmaRemintWindowSize = 50000;

            // Dandelion related values.
            consensus.nDandelionEmbargoMinimum = DANDELION_TESTNET_EMBARGO_MINIMUM;
            consensus.nDandelionEmbargoAvgAdd = DANDELION_TESTNET_EMBARGO_AVG_ADD;
            consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
            consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
            consensus.nDandelionFluff = DANDELION_FLUFF;
    }
};

static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";

        consensus.chainType = Consensus::chainRegtest;

        consensus.nSubsidyHalvingFirst = 302438;
        consensus.nSubsidyHalvingInterval = 420000;
        consensus.nSubsidyHalvingStopBlock = 3646849;

        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.powLimit = uint256S("7fffff0000000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 60 * 60 * 1000; // 60 minutes between retargets
        consensus.nPowTargetSpacing = 1; // 10 minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nXFSnodePaymentsStartBlock = 120;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0");
        // XFSnode code
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin

        consensus.nCheckBugFixedAtBlock = 120;
        consensus.nXFSnodePaymentsBugFixedAtBlock = 1;
        consensus.nSpendV15StartBlock = 1;
        consensus.nSpendV2ID_1 = 2;
        consensus.nSpendV2ID_10 = 3;
        consensus.nSpendV2ID_25 = 3;
        consensus.nSpendV2ID_50 = 3;
        consensus.nSpendV2ID_100 = 3;
        consensus.nModulusV2StartBlock = 130;
        consensus.nModulusV1MempoolStopBlock = 135;
        consensus.nModulusV1StopBlock = 140;
        consensus.nMultipleSpendInputsInOneTxStartBlock = 1;
        consensus.nDontAllowDupTxsStartBlock = 1;

        consensus.nDisableZerocoinStartBlock = INT_MAX;

        pchMessageStart[0] = 0xc1;
        pchMessageStart[1] = 0x1a;
        pchMessageStart[2] = 0xf1;
        pchMessageStart[3] = 0xff;
        nDefaultPort = 18444;
        nPruneAfterHeight = 1000;

        std::vector<unsigned char> extraNonce(4);
	    extraNonce[0] = 0xaa;
        extraNonce[1] = 0xc1;
        extraNonce[2] = 0xf1;
        extraNonce[3] = 0xcb;
        genesis = CreateGenesisBlock(ZC_GENESIS_BLOCK_TIME, 7382559, 0x1e00ffff, 2, 0 * COIN, extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock ==
              uint256S("0x0000002483dee2a97df95f61d810f2115bc83134d3c399168d682ea1da6d0fd8"));
        assert(genesis.hashMerkleRoot ==
              uint256S("ed2efdba33e9bc206cba44a213440e29a0902740f4f4209bdea833c48673606f"));
        //Disable consecutive checks
        nConsecutivePoWHeight = INT_MAX;
        nMaxPoWBlocks = 101;
        consensus.nFirstPOSBlock = nConsecutivePoWHeight;//TODO akshaynexus :This needs to be decided

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
                (0, uint256S("0x0000002483dee2a97df95f61d810f2115bc83134d3c399168d682ea1da6d0fd8")),
                0,
                0,
                0
        };
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 65);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 178);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container < std::vector < unsigned char > > ();

        nSpendV15StartBlock = ZC_V1_5_TESTNET_STARTING_BLOCK;
        nSpendV2ID_1 = ZC_V2_TESTNET_SWITCH_ID_1;
        nSpendV2ID_10 = ZC_V2_TESTNET_SWITCH_ID_10;
        nSpendV2ID_25 = ZC_V2_TESTNET_SWITCH_ID_25;
        nSpendV2ID_50 = ZC_V2_TESTNET_SWITCH_ID_50;
        nSpendV2ID_100 = ZC_V2_TESTNET_SWITCH_ID_100;
        nModulusV2StartBlock = ZC_MODULUS_V2_TESTNET_START_BLOCK;
        nModulusV1MempoolStopBlock = ZC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
        nModulusV1StopBlock = ZC_MODULUS_V1_TESTNET_STOP_BLOCK;

        // Sigma related values.
        consensus.nSigmaStartBlock = 400;
        consensus.nSigmaPaddingBlock = 550;
        consensus.nDisableUnpaddedSigmaBlock = 510;
        consensus.nOldSigmaBanBlock = 450;
        consensus.nZerocoinV2MintMempoolGracefulPeriod = 2;
        consensus.nZerocoinV2MintGracefulPeriod = 5;
        consensus.nZerocoinV2SpendMempoolGracefulPeriod = 10;
        consensus.nZerocoinV2SpendGracefulPeriod = 20;
            consensus.nMaxSigmaInputPerBlock = ZC_SIGMA_INPUT_LIMIT_PER_BLOCK;
            consensus.nMaxValueSigmaSpendPerBlock = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_BLOCK;
            consensus.nMaxSigmaInputPerTransaction = ZC_SIGMA_INPUT_LIMIT_PER_TRANSACTION;
            consensus.nMaxValueSigmaSpendPerTransaction = ZC_SIGMA_VALUE_SPEND_LIMIT_PER_TRANSACTION;
            consensus.nZerocoinToSigmaRemintWindowSize = 1000;

            // Dandelion related values.
            consensus.nDandelionEmbargoMinimum = 0;
            consensus.nDandelionEmbargoAvgAdd = 1;
            consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
            consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
            consensus.nDandelionFluff = DANDELION_FLUFF;
    }

    void UpdateBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout) {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
    }
};

static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(const std::string &chain) {
    if (chain == CBaseChainParams::MAIN)
        return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
        return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
        return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string &network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

void UpdateRegtestBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout) {
    regTestParams.UpdateBIP9Parameters(d, nStartTime, nTimeout);
}
