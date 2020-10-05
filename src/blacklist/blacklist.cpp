// Copyright (c) 2019-2020 akshaynexus
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "blacklist.h"
#include "util.h"
std::vector<std::string> blacklistedAddrs {
    "XYe8FWCx6cdbN937xEuWxTJoFJCp9Zkpzz", // Premined DEV AlexXFSCore blacklisted
    "XXx8Vc6R1zNKif2PMrTD67tTyvxnFwyrQq", // blacklisted
    "XKSzmXVNbBPGNmhMZN69iwnQzqnwn6tsWG", // blacklisted
    "XN9Rkq2ynVrudCVtiN5y8c7eKckzVmh3Zo", // blacklisted
    "XHT8mbEtyYjudYE5cYAebEDonEKNy2JT6R", // blacklisted
    "XTuQ14aW64ZmbntsiMvzas1DJenvSuQEtV", // blacklisted
    "XCFGzEdAadCxrGWgRjU4t5JGbX8SaeCjtn", // blacklisted
    "XBvQq5XRXmgR11zhLkTR5V3UpSaYq8uAnW", // blacklisted
    "XXYKXBydQ6SvC62nMqTAxFDMst36PWv4pr", // blacklisted
    "XByhXWTwnCh6sqzoZ1S8snygaAky4P4CLP", // blacklisted
    "XWJEAiNKCad68ZV8oziVDWDun32NFsnoMZ", // blacklisted
    "XYKvhXzxFrSyiqADL5xnBkhTPKCRNG9PxC", // blacklisted
    "XRZL66HDWyGJ3QsurGcfykQuUyYBxNBBuz", // blacklisted
    "XBvfaAah3KTzXND6vVhKRERUydNirsZzkC"  // blacklisted
};
bool ContainsBlacklistedAddr(std::string addr){
    //Iterate through blacklisted addresses
    for (Iter it = blacklistedAddrs.begin(); it!=blacklistedAddrs.end(); ++it) {
        if(*it == addr) {
            LogPrintf("ContainsBlacklistedAddr() Found Blacklisted addr %s\n", addr);
            return true;
        }
    }
    return false;
}
