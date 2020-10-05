// Copyright (c) 2019-2020 akshaynexus
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "blacklist.h"
#include "util.h"
std::vector<std::string> blacklistedAddrs {
    //"XYe8FWCx6cdbN937xEuWxTJoFJCp9Zkpzz",
    //"XXx8Vc6R1zNKif2PMrTD67tTyvxnFwyrQq",
	//"XKSzmXVNbBPGNmhMZN69iwnQzqnwn6tsWG",
	//"XN9Rkq2ynVrudCVtiN5y8c7eKckzVmh3Zo",
	//"XHT8mbEtyYjudYE5cYAebEDonEKNy2JT6R",
	//"XTuQ14aW64ZmbntsiMvzas1DJenvSuQEtV",
	//"XCFGzEdAadCxrGWgRjU4t5JGbX8SaeCjtn",
	//"XBvQq5XRXmgR11zhLkTR5V3UpSaYq8uAnW",
	//"XXYKXBydQ6SvC62nMqTAxFDMst36PWv4pr",
	//"XByhXWTwnCh6sqzoZ1S8snygaAky4P4CLP",
	//"XWJEAiNKCad68ZV8oziVDWDun32NFsnoMZ",
	//"XYKvhXzxFrSyiqADL5xnBkhTPKCRNG9PxC",
	//"XRZL66HDWyGJ3QsurGcfykQuUyYBxNBBuz",
    //"XBvfaAah3KTzXND6vVhKRERUydNirsZzkC"
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