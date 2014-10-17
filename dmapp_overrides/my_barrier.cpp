// * BeginRiceCopyright *****************************************************
//
// Copyright ((c)) 2002-2014, Rice University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage.
//
// ******************************************************* EndRiceCopyright *

#include <stdio.h>
#include <execinfo.h>
#include <stdlib.h>
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#include <map>
#include <vector>
#include <stdint.h>
#include <sys/stat.h>
#include <iostream>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sstream>
#include <limits.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sstream>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include<dmapp.h>
#include<mpi.h>
#include <errno.h>
#include <alloca.h>
#include <google/dense_hash_map>
#include <google/dense_hash_set>
#include "barrier_deletion.h"

#include <fstream>
#include <vector>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <time.h>
#include "comex.h"

using namespace std;
using namespace std::tr1;
using google::dense_hash_map;
using google::dense_hash_set;

extern "C" {
#define SCALING_SETFREQ_FILE_PATTERN "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_setspeed"
    
#define BARRIER_FN_NAME "MPI_Barrier"
#define ALLGATHER_FN_NAME "MPI_Allgather"
#define ALLREDUCE_FN_NAME "MPI_Allreduce"
#define TIME_SPENT(start, end) (end.tv_sec * 1000000 + end.tv_usec - start.tv_sec*1000000 - start.tv_usec)
#define REAL_FUNCTION(name)  __real_ ## name
#define WRAPPED_FUNCTION(name)  __wrap_ ## name
    
    
    
//#define USE_CONTEXT_IN_MPI_REDUCTION
    
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
#define ALL_REDUCE_BUFFER(buffer, status, ctxt)  do{ buffer[0] = (((uint64_t)(status) << 32 ) | GLOBAL_STATE.GetBarrierInstance());  buffer[1] = ctxt;} while(0)
#define ALL_REDUCE_GET_STATUS(buffer) ((buffer[0]) >> 32 )
#define ALL_REDUCE_GET_INSTANCE(buffer) ((buffer[0]) & 0xffffffff)
#define ALL_REDUCE_GET_CONTEXT(buffer) ((buffer[1]))
#else
#define ALL_REDUCE_BUFFER(status) ( ((uint64_t)(status) << 32 ) | GLOBAL_STATE.GetBarrierInstance())
#define ALL_REDUCE_GET_STATUS(buffer) ( (buffer) >> 32 )
#define ALL_REDUCE_GET_INSTANCE(buffer) ((buffer) & 0xffffffff)
#endif
    
    
#define REPLAY_FILE_NAME "replay.txt"
#define SKIPPABLE_CTXTS_FILE_NAME "skippables.txt"
#define PARTICIPATED_CTXTS_FILE_NAME "participated.txt"
#define GUIDED_OPTIMIZATION_CTXT_HASHES_FILE_NAME "ctxt_hashes.txt"
    
    //#define ENABLE_LOGGING
    
    //#define ENABLE_REPLAY
    
//#define GUIDED_OPTIMIZATION
    
    /******** Globals variables **********/
    
#ifdef ENABLE_REPLAY
    enum REPLAY_MODE {REPLAY_MODE_RECORD, REPLAY_MODE_REPLAY};
#define REPLAY_SKIP 'S'
#define REPLAY_PARTICIPATE 'P'
#endif
    
#ifdef GUIDED_OPTIMIZATION
    enum GUIDED_OPTIMIZATION_MODE {GUIDED_OPTIMIZATION_MODE_ANALYZE, GUIDED_OPTIMIZATION_MODE_OPTIMIZE};
#endif
    
    static int myRank = -1;
    static MPI_Op myMPIOp;
    
    struct GLOBAL_STATE_t {
        dense_hash_map<uint64_t, uint64_t> barrierSkipCache;
        dense_hash_map<uint64_t, uint64_t>::iterator barrierSkipCacheIterator;
        
#ifdef GUIDED_OPTIMIZATION
        unordered_set<uint64_t>  guidedOptimizationSkipCtxtHashSet;
        unordered_set<uint64_t>::iterator  guidedOptimizationSkipCtxtHashSetIterator;
        map<void *, size_t> sharedDataRange;
#endif
        
        uint64_t barrierInstance;
        uint64_t enabledBarrierInstance;
        uint64_t skippable;
        uint64_t reSync;
        uint64_t badDecison;
        uint64_t lastParticipatedBarrier;
        uint64_t lastBarrierSeenKey;
        uint64_t systemPageSize;
        
        void * stackBottom;
        bool isEnabled;
        
        // Constructor
        GLOBAL_STATE_t():
        barrierInstance(0),
        enabledBarrierInstance(0),
        skippable(0),
        reSync(0),
        badDecison(0),
        lastParticipatedBarrier(0),
        stackBottom(0),
        isEnabled(false) {
            
            systemPageSize = sysconf(_SC_PAGESIZE);
            
        }
        
        void Enable() {
            isEnabled = true;
        }
        void Disable() {
            isEnabled = false;
        }
        bool IsEnabled() {
            return isEnabled;
        }
        
        void SetStackBottom(void* bottom) {
            stackBottom = bottom;
        }
        
        void * GetStackBottom(){
            return stackBottom;
        }
        
        uint64_t GetPageSize () const{
            return systemPageSize;
        }
        
        void SetLastParticipatedBarrier(uint64_t lpb) {
            lastParticipatedBarrier = lpb;
        }
        uint64_t GetLastParticipatedBarrier() {
            return lastParticipatedBarrier;
        }
        
        uint64_t IncrementBadDecision() {
            return badDecison++;
        }
        uint64_t GetBadDecision() {
            return badDecison;
        }
        
        uint64_t IncrementSkippable() {
            return skippable ++;
        }
        uint64_t DecrementSkippable() {
            return skippable --;
        }
        uint64_t GetSkippable() {
            return skippable;
        }
        
        uint64_t IncrementReSync() {
            return reSync ++;
        }
        uint64_t GetReSync() {
            return reSync;
        }
        
        uint64_t IncrementEnabledBarrierInstance() {
            uint64_t val = enabledBarrierInstance;
            
            if(IsEnabled())
                enabledBarrierInstance ++;
            
            return val;
        }
        uint64_t GetEnabledBarrierInstance() {
            return enabledBarrierInstance;
        }
        
        uint64_t IncrementBarrierInstance() {
            IncrementEnabledBarrierInstance();
            return barrierInstance ++;
        }
        uint64_t GetBarrierInstance() {
            return barrierInstance;
        }
#ifdef ENABLE_REPLAY
        REPLAY_MODE replayMode;
        void SetReplayMode(REPLAY_MODE mode){
            replayMode = mode;
        }
        REPLAY_MODE GetReplayMode(){
            return replayMode;
        }
#endif
        
#ifdef GUIDED_OPTIMIZATION
        GUIDED_OPTIMIZATION_MODE guidedOptimizationMode;
        
        void SetGuidedOptimizationMode(GUIDED_OPTIMIZATION_MODE mode){
            guidedOptimizationMode = mode;
        }
        
        GUIDED_OPTIMIZATION_MODE GetGuidedOptimizationMode(){
            return guidedOptimizationMode;
        }
        
        void InsertInSharedDataRange(void * addr, size_t sz) {
            map<void *, size_t>::iterator up = sharedDataRange.upper_bound(addr);
            // Ensure this range is not in the map
            if(up != sharedDataRange.end()) {
                assert(up->first  >= addr + sz);
            }
            if ( up != sharedDataRange.begin()) {
                up --;
                assert(addr  >= up->first + up->second);
                up ++;
            }
            sharedDataRange[addr] = sz;
#if 0
            if(myRank == 0) {
                printf("\n Added %p .. %lu\n", addr, sz);
            }
#endif
        }
        
        void DeleteFromSharedDataRange(void * addr, size_t sz){
#if 0
            if(myRank == 0) {
                printf("\n Deleting %p .. %lu\n", addr, sz);
            }
#endif
            int num = sharedDataRange.erase(addr);
            assert(num == 1);
#if 0
            if(myRank == 0) {
                printf("\n Deleted %p .. %lu\n", addr, sz);
            }
#endif
        }
        
        void DumpSharedDataRange() {
            printf("\n DumpSharedDataRange\n");
            for(map<void *, size_t>::iterator it = sharedDataRange.begin(), e = sharedDataRange.end(); it != e; it++){
                printf("\n Addr = %p, len = %lu",it->first, it->second);
            }
        }
        void FindAddrInSharedDataRange(void * addr, void * & startAddr, size_t & sz){
#if 0
            if(myRank == 0) {
                printf("\n Looking for addr %p\n", addr);
                DumpSharedDataRange();
            }
#endif
            map<void *, size_t>::iterator ub = sharedDataRange.upper_bound(addr);
            
            if(ub != sharedDataRange.end() && ub != sharedDataRange.begin()) {
                ub --;
                assert(ub->first <= addr && addr < ub->first + ub->second);
                ub++;
            }
            
            ub --;
            
            startAddr = ub->first;
            sz = ub->second;
            
#if 0
            if(myRank == 0) {
                printf("\n Found %p .. %lu\n", addr, sz);
            }
#endif
        }
        
        void PageProtectAllSharedData(int flags){
            for(map<void *, size_t>::iterator it = sharedDataRange.begin(), e = sharedDataRange.end(); it != e; it++){
                int rc = mprotect(it->first, it->second, flags);
                if(rc) {
                    perror("mprotect failed in PageProtectAllSharedData");
                    assert(0 && "failed in PageProtectAllSharedData");
                }
            }
        }
#endif
        
    } ;
    GLOBAL_STATE_t GLOBAL_STATE;
    uint64_t barrierInstance = 0;    
    
    
    
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
    static void MyMPIReductionOp(void* a, void* b, int* len, MPI_Datatype* type) {
        uint64_t * a1 = (uint64_t*)a;
        uint64_t * b1 = (uint64_t*)b;
        uint32_t statusA = ALL_REDUCE_GET_STATUS(a1);
        uint32_t statusB = ALL_REDUCE_GET_STATUS(b1);
        uint32_t instanceA = ALL_REDUCE_GET_INSTANCE(a1);
        uint32_t instanceB = ALL_REDUCE_GET_INSTANCE(b1);
        // Min of status
        uint64_t retStatus = statusA < statusB ? statusA : statusB;
        // Min of barrier instance
        uint64_t retInstance = instanceA < instanceB ? instanceA : instanceB;
        ((uint64_t*)b)[0] = (((retStatus) << 32) | retInstance);
        
        uint64_t ctxt1 = ALL_REDUCE_GET_CONTEXT(a1);
        uint64_t ctxt2 = ALL_REDUCE_GET_CONTEXT(b1);
        // If context's dont match return 0.
        if(ctxt1 != ctxt2)
            ((uint64_t*)b)[1] = 0;
    }
#else
    static void MyMPIReductionOp(void* a, void* b, int* len, MPI_Datatype* type) {
        uint64_t a1 = *((uint64_t*)a);
        uint64_t b1 = *((uint64_t*)b);
        uint32_t statusA = ALL_REDUCE_GET_STATUS(a1);
        uint32_t statusB = ALL_REDUCE_GET_STATUS(b1);
        uint32_t instanceA = ALL_REDUCE_GET_INSTANCE(a1);
        uint32_t instanceB = ALL_REDUCE_GET_INSTANCE(b1);
        // Min of status
        uint64_t retStatus = statusA < statusB ? statusA : statusB;
        // Min of barrier instance
        uint64_t retInstance = instanceA < instanceB ? instanceA : instanceB;
        *((uint64_t*)b) = (((retStatus) << 32) | retInstance);
    }
#endif
    
    static void DumpMaps();
    
    static void PrintStats() {
        if(myRank == 0) {
            printf("\n Total Barriers = %lu, Enabled = %lu, Skippable =%lu, reSync = %lu, bad decision = %lu", GLOBAL_STATE.GetBarrierInstance(), GLOBAL_STATE.GetEnabledBarrierInstance(), GLOBAL_STATE.GetSkippable(), GLOBAL_STATE.GetReSync(), GLOBAL_STATE.GetBadDecision());
        }
    }
    __thread uint64_t gAnalysisStatus;
    
#define SKIP (10)
#define PARTICIPATE (0)
    
#define USE_LIBUNWIND
#define ENABLE_LOGGING
    //#define USE_CUSTOM_UNWINDER
    
    /******** Function definitions **********/
    
#if defined(SIMPLE_CONTEXT)
    inline uint64_t GetContextHash() {
        assert(0 && "No longer maintained. Dont call me");
        uint64_t returnAddress = (uint64_t) __builtin_return_address(0);
        uint64_t stackPointer = (uint64_t) __builtin_frame_address(0);
        uint64_t key = ((returnAddress & 0xffffffff) << (31)) | (stackPointer & 0xffffffff);
        return key;
    }
    
#elif defined(USE_CUSTOM_UNWINDER)
    
#define ASM_LABEL(name)         \
asm volatile (".globl " #name );    \
asm volatile ( #name ":" )
    
    
    extern int REAL_FUNCTION(main)(int argc, char ** argv);
    extern void * main_fence1 __attribute__((weak));
    extern void * main_fence2 __attribute__((weak));
    
    /*
     *  Returns: 1 if address is within the body of the function at the
     *  bottom of the application's call stack, else 0.
     */
    int HasStackEnded(void * insPtr, void * framePtr) {
        if (&main_fence1 <= insPtr && insPtr <= &main_fence2)
            return 1;
        if (framePtr > GLOBAL_STATE.GetStackBottom())
            return 1;
        return 0;
    }
    
    int WRAPPED_FUNCTION(main)(int argc, char ** argv) {
        ASM_LABEL(main_fence1);
        GLOBAL_STATE.SetStackBottom(alloca(8));
        strncpy((char *) GLOBAL_STATE.GetStackBottom(), "stakbot", 8);
        return REAL_FUNCTION(main)(argc, argv);
        ASM_LABEL(main_fence2);
    }

 
    static inline uint64_t GetContextHash() {
        void ** curStackPointer = (void **) __builtin_frame_address(0);
        void * curRA = *(curStackPointer+1);
        uint64_t hash = 0;
#if 0
        if(myRank == 0)
            printf("\nUnwind Start \n");
#endif
        // Iterate over return addresses and sum them to get a hash
        while(!HasStackEnded(curRA, curStackPointer)) {
            /* better hashing provided by Wim */
            hash += (uint64_t)curRA;
            hash += (hash << 10);
            hash ^= (hash >> 6);
            
            curStackPointer = (void **) (*curStackPointer);
            curRA = *(curStackPointer+1);
        }
        hash += (hash << 3);
        hash ^= (hash >> 11);
        hash += (hash << 15);
#if 0
        if(myRank == 0)
            printf("\nUnwind End \n");
#endif
        return hash;
    }
    
    static inline void PrintBT() {
        unw_cursor_t cursor;
        unw_context_t uc;
        unw_word_t ip, sp;
        unw_getcontext(&uc);
        unw_init_local(&cursor, &uc);
        printf("\n --------------- \n");
        
        while(unw_step(&cursor) > 0) {
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            printf(" %lx", ip);
            //        std::stringstream command;
            //                command << "/usr/bin/addr2line -C -f -e " << " /global/homes/m/mc29/nwchem-6.3_opt/bin/LINUX64/nwchem " << " " << std::hex << ip;
            //                      system(command.str().c_str());
        }
        
        printf("\n ---------------\n");
    }
    
    
    
#elif defined(USE_LIBUNWIND)
#define BUMP_BT
   /* 
    extern int REAL_FUNCTION(main)(int argc, char ** argv);
    int WRAPPED_FUNCTION(main)(int argc, char ** argv) {
        return REAL_FUNCTION(main)(argc, argv);
    }
   */ 
    static inline uint64_t GetContextHash() {
        unw_cursor_t cursor;
        unw_context_t uc;
        unw_word_t ip, sp;
        bool first = true;
        unw_getcontext(&uc);
        unw_init_local(&cursor, &uc);
        uint64_t hash = 0;
        
        // Iterate over return addresses and sum them to get a hash
        while(unw_step(&cursor) > 0) {
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            /*if(first) {
             unw_get_reg(&cursor, UNW_REG_SP, &sp);
             first = false;
             } */
            hash += ip;
            hash += (hash << 10);
            hash ^= (hash >> 6);
        }
        
        hash += (hash << 3);
        hash ^= (hash >> 11);
        hash += (hash << 15);
        return hash;
    }
    
    
    
    struct RedundancyKey {
        uint64_t lastBarrier;
        uint64_t curBarrier;
    };
    
    struct Hasher {
        size_t operator()(RedundancyKey const& s) const {
            size_t hash = s.lastBarrier + s.curBarrier;
            return hash;
        }
    };
    
    struct EqualFn {
        bool operator()(RedundancyKey const& a, RedundancyKey const& b) const {
            return a.lastBarrier == b.lastBarrier && a.curBarrier == b.curBarrier;
        }
    };
    
    static unordered_map<uint64_t, vector<void*> > backtraceMap;
    static unordered_map<uint64_t, uint64_t > participatedBarriersMap;
    typedef unordered_map<RedundancyKey, uint64_t, Hasher, EqualFn> RedundancyMap_t;
    static RedundancyMap_t redundancyMap;
    static vector<string> log_vector;   
 
    static inline uint64_t GetContextHashWithBackTrace(bool getBackTrace=true) {
        unw_cursor_t cursor;
        unw_context_t uc;
        unw_word_t ip, sp;
        bool first = true;
        unw_getcontext(&uc);
        unw_init_local(&cursor, &uc);
        uint64_t hash = 0;
        vector<void*> btVec;
        
        while(unw_step(&cursor) > 0) {
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            hash += ip;
            hash += (hash << 10);
            hash ^= (hash >> 6);
            
            if(getBackTrace) {
                btVec.push_back((void*) ip);
            }
        }
        hash += (hash << 3);
        hash ^= (hash >> 11);
        hash += (hash << 15);
        
        if(getBackTrace) {
            // if this hash is never seen before, record it.
            unordered_map<uint64_t, vector<void*> >::iterator  ii = backtraceMap.find(hash);
            
            if(ii == backtraceMap.end()) {
                backtraceMap[hash] = btVec;
                //if (myRank ==0) printf("\n New Key in backtraceMap vec = %lx: %d\n",hash, btVec.size()) ;
            }
        }
        
        //hash  = ((hash & 0xffffffff) << (31)) | ( ((uint64_t) sp) & 0xffffffff);
        return  hash;
    }
    
    
    static inline void RecordInRedundancyMap(RedundancyKey key) {
        RedundancyMap_t::iterator  ri = redundancyMap.find(key);
        if(ri  != redundancyMap.end()) {
            ri->second ++;
            /*
             if (myRank ==0) {
             uint64_t lpb = ri->first.lastBarrier;
             uint64_t cb = ri->first.curBarrier;
             printf("\n lpb = %lx cb = %lx sec = %lu\n", lpb, cb, ri->second);
             }
             */
        } else {
            if(myRank == 0) {
                //printf("\n New Key in redundancy map = %lx : %lx\n", key.lastBarrier, key.curBarrier);
            }
            
            redundancyMap[key] = 1;
        }
    }
    
    static inline void RecordKeyInRedundancyMap(uint64_t curBarrierHash) {
        RedundancyKey key = {GLOBAL_STATE.GetLastParticipatedBarrier(), curBarrierHash};
        RecordInRedundancyMap(key);
    }
    
    static inline void DeleteFromRedundancyMap(RedundancyKey key) {
        
        redundancyMap.erase(key);
        /*
         
         RedundancyMap_t::iterator  ri = redundancyMap.find(key);
         
         if(ri  != redundancyMap.end()) {
         return;
         } else {
         redundancyMap.erase(ri);
         }
         */
        //printf("\n  Deleted RecordInParticipateBarriersMap \n");
        
    }
    
    static inline void RecordInParticipateBarriersMap(uint64_t curBarrierHash) {
        unordered_map<uint64_t, uint64_t >::iterator  ri = participatedBarriersMap.find(curBarrierHash);
        
        if(ri  != participatedBarriersMap.end()) {
            ri->second ++;
        } else {
            participatedBarriersMap[curBarrierHash] = 1;
        }
    }
    
    static void DumpStack(uint64_t key, FILE * fp) {
        assert(backtraceMap.find(key) != backtraceMap.end());
        vector <void*>& stack = backtraceMap[key];
        
        //printf("\n %d", stack.size());
        for(int i = 0; i < stack.size(); i++)
            fprintf(fp, "\t %lx", stack[i]);
    }
    
    
    
    static void DumpRedundancyMap() {
        FILE * fp = fopen(SKIPPABLE_CTXTS_FILE_NAME, "w");
        assert(fp);
        fprintf(fp, "#occurance : percent : HASH1 (last barrier) : Ret-addr-list1 : HASH2 (skippable barrier): Ret-addr-list2 ");
        for(RedundancyMap_t::iterator ri = redundancyMap.begin(); ri != redundancyMap.end(); ri++) {
            uint64_t lpb = ri->first.lastBarrier;
            uint64_t cb = ri->first.curBarrier;
            //printf("\n lpb = %lx cb = %lx\n", lpb, cb);
            fprintf(fp, "\n%lu : %fl : %lu : ", ri->second,  ri->second * 100.0 / GLOBAL_STATE.GetSkippable(), lpb);
            
            if(lpb != 0)
                DumpStack(lpb, fp);
            else
                fprintf(fp, " 0 ");
            
            fprintf(fp, ": %lu : ", cb);
            DumpStack(cb, fp);
        }
        fclose(fp);
    }
    
    static void DumpParticipaedBarriersMap() {
        FILE * fp = fopen(PARTICIPATED_CTXTS_FILE_NAME, "w");
        assert(fp);
        fprintf(fp, "#occurance : percent : HASH : Ret-addr-list");
        for(unordered_map<uint64_t, uint64_t >::iterator pi = participatedBarriersMap.begin(); pi != participatedBarriersMap.end(); pi++) {
            uint64_t cb = pi->first;
            fprintf(fp, "\n%lu : %fl : %lu : ", pi->second,  pi->second * 100.0 / (  GLOBAL_STATE.GetEnabledBarrierInstance() - GLOBAL_STATE.GetSkippable()), cb);
            DumpStack(cb, fp);
        }
        fclose(fp);
    }
    
    static void DumpMaps(){
        DumpRedundancyMap();
        DumpParticipaedBarriersMap();
    }
    
    
    static inline void PrintBT() {
        unw_cursor_t cursor;
        unw_context_t uc;
        unw_word_t ip, sp;
        unw_getcontext(&uc);
        unw_init_local(&cursor, &uc);
        printf("\n --------------- \n");
        
        while(unw_step(&cursor) > 0) {
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            printf(" %lx", ip);
            //        std::stringstream command;
            //                command << "/usr/bin/addr2line -C -f -e " << " /global/homes/m/mc29/nwchem-6.3_opt/bin/LINUX64/nwchem " << " " << std::hex << ip;
            //                      system(command.str().c_str());
        }
        
        printf("\n ---------------\n");
    }
    
    
#else
#define BUMP_BT
#define BT_SIZE (1000)
    static void* array[BT_SIZE];
    static inline uint64_t GetContextHash() {
        size_t size;
        size_t i;
        size = backtrace(array, BT_SIZE);
        uint64_t hash = 0;
        
        for(i = 0; i < size; i++)
            hash += (uint64_t) array[i];
        
        return hash;
    }
    
    static void PrintBT() {
        size_t size;
        size_t i;
        size = backtrace(array, BT_SIZE);
        backtrace_symbols_fd(array, size, 1);
    }
    
#endif
    
    
#ifdef ENABLE_LOGGING
    static FILE* logFilePtr;
    static  void LogStackTrace(uint64_t key) {
        //assert(backtraceMap.find(key) != backtraceMap.end());
        assert(logFilePtr);
        vector <void*>& stack = backtraceMap[key];
        
        for(int i = 0; i < stack.size(); i++)
            fprintf(logFilePtr, "\t %lx", stack[i]);
    }
    
    
    static void CloseLogFile() {
        if(logFilePtr)
            fclose(logFilePtr);
    }
    
    static void CreateLogFile(int rank) {
        assert(logFilePtr == NULL);
        std::stringstream ss;
        char hostname[200];
        pid_t pid = getpid();
        ss << "LOG_" << pid << ".log";
        logFilePtr = fopen(ss.str().c_str(), "w");
    }
    
    static void ComexLog(comex_group_t comex_group, uint64_t key, string s, uint64_t barInst, int status) {
        int size;
        //MPI_Comm_size(comm, &size);
        int rank;
        //MPI_Comm_rank(comm, &rank);
        comex_group_size(comex_group, &size);
        comex_group_rank(comex_group, &rank);
        
	string str_log;
        std::stringstream sstm;
        sstm << barInst << " " << key << " "  << s;
        log_vector.push_back(sstm.str());
	//fprintf(logFilePtr, " SIZE= %d : RANK = %d: BARINST = %lu, KEY = %lu, STATUS = %d, %s at \n", size, rank, barInst, key, status, s.c_str());
	if(log_vector.size()>10000) {
          for (int i = 0; i < log_vector.size(); i++)
            fprintf(logFilePtr, "%s\n", log_vector[i].c_str());
	  log_vector.clear();
	}
        //LogStackTrace(key);
    }
    static void XinLog(MPI_Comm comm, uint64_t key, string s, uint64_t barInst, int status) {
        int size;
        MPI_Comm_size(comm, &size);
        int rank;
        MPI_Comm_rank(comm, &rank);
        fprintf(logFilePtr, " SIZE= %d : RANK = %d: BARINST = %lu, KEY = %lu, STATUS = %d, %s at \n", size, rank, barInst, key, status, s.c_str());
        //LogStackTrace(key);
    }
#else
    static void DumpStack2(uint64_t key) {}
    static void CloseLogFile() {}
    static void CreateLogFile(int rank) {}
    static void Log(MPI_Comm comm, uint64_t key, string s, uint64_t barInst, int status) {}
#endif
   
    extern int REAL_FUNCTION(main)(int argc, char ** argv); 
    extern int REAL_FUNCTION(comex_barrier)(comex_group_t comex_group);
    extern int REAL_FUNCTION(MPI_Barrier)(MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Init)(int* argc, char** *argv);   
    /*extern int REAL_FUNCTION(MPI_Bcast)(void* buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Allgather)(const void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Allreduce)(const void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
    */
    extern int REAL_FUNCTION(MPI_Finalize)(void);  
  
    //static clock_t t1, t2;
    //static clock_t mpiInitTime, mpiFinalizeTime; 
    static struct timeval t1, t2;
    static struct timeval p_start, mpiInitTime, mpiFinalizeTime;
    static int freq_arr[68323]; 
    static int FREQ[] = {800000, 1100000, 1400000, 1800000, 2200000};
    //int retVal = MPI_SUCCESS;
    // increment the barrrier instance
    //uint64_t curbarrierinstance = global_state.incrementbarrierinstance();
        
    // not enabled, simple do the barrier and return
    // enabled, hence perform tracking / optimization
unsigned int freq=1;
 int WRAPPED_FUNCTION(main)(int argc, char ** argv) {
        gettimeofday(&p_start, NULL);
        if(argc == 3)
          freq = atoi(argv[2]);
        else 
          printf("the frequency is not set!!!\n");
        return REAL_FUNCTION(main)(argc, argv);
        /* 
        clock_t p_start, p_end; 
        double time_spent;
        p_start = clock();

        p_end = clock();
        time_spent = (double) (p_end - p_start) / CLOCKS_PER_SEC;
        printf("\nclocks per second: %ld\n", CLOCKS_PER_SEC);
        printf("\n the main execution, start is: %ld,  end is: %ld,  total execution is: %f !\n", (p_start), (p_end), time_spent );
        */ 
    }

 int WRAPPED_FUNCTION(comex_barrier)(comex_group_t comex_group) {
        
        uint64_t key;
        uint64_t cur_barrier;
        std::stringstream sstm;
        int res;
        uint64_t slack, ts1, ts2;
        
	MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
        cur_barrier = barrierInstance;
        gettimeofday(&t1, NULL);
        //t1 = clock();    
        
        // is this barrier previously seen?
        //global_state.barrierskipcacheiterator = global_state.barrierskipcache.find(key);
       

        //GLOBAL_STATE.lastBarrierSeenKey = key;
        res = REAL_FUNCTION(comex_barrier)(comex_group);
        
        /* 
	if (myRank == 0){
          string str = "sudo /home/xin/micro/tool/setfreq 48 ";
	  //char command_str[str.length() + 9];          
	  stringstream command_str;
	  int f_id = freq_arr[cur_barrier+1];
          command_str << str << FREQ[f_id];
	  int myres;
          //sprintf(command_str, "%s%d", str, freq_arr[cur_barrier+1]);
	  printf("%d:%s\n", cur_barrier, command_str.str().c_str() ); 
          myres = system( command_str.str().c_str() );
        }
        */
	gettimeofday(&t2, NULL);
        //t2 = clock();

        key = GetContextHash();
        ts1 = (t1.tv_sec - p_start.tv_sec)*1000000 + (t1.tv_usec - p_start.tv_usec);
        ts2 = (t2.tv_sec - p_start.tv_sec)*1000000 + (t2.tv_usec - p_start.tv_usec);
        slack = ts2 - ts1;
        //sstm  << t1.tv_sec << " " << t1.tv_usec << " " << t2.tv_sec << " " << t2.tv_usec ;
        sstm  << ts1 << " " << ts2 ;
        if (slack > 0)
          ComexLog(comex_group, key, sstm.str(), cur_barrier, -1);
        barrierInstance++;
        return res;
    }    

    int WRAPPED_FUNCTION(MPI_Barrier)(MPI_Comm comm) {
        
        uint64_t key;
        key = GetContextHash();
        //gettimeofday(&t1, NULL);
        std::stringstream sstm;
        //sstm << t1.tv_sec << ":" << t1.tv_usec;
        //XinLog(comm, key, sstm.str(), -1, -1);     
       // is this barrier previously seen?
        //global_state.barrierskipcacheiterator = global_state.barrierskipcache.find(key);
       
        //GLOBAL_STATE.lastBarrierSeenKey = key;
        return REAL_FUNCTION(MPI_Barrier)(comm);
    }


/*
    int WRAPPED_FUNCTION(MPI_Allgather)(const void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
        uint64_t key;
        key = GetContextHash();
        gettimeofday(&t1, NULL);
        std::stringstream sstm;
        sstm << t1.tv_sec << ":" << t1.tv_usec;
        XinLog(comm, key, "mpi_all_together", -1, -1); 
         int retVal = REAL_FUNCTION(MPI_Allgather)(sendbuf,  sendcount,  sendtype, recvbuf,  recvcount,  recvtype, comm);
        
        return retVal;
    }

    int WRAPPED_FUNCTION(MPI_Bcast)(void* buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
        uint64_t key;
        key = GetContextHash();
        gettimeofday(&t1, NULL);
        std::stringstream sstm;
        sstm << t1.tv_sec << ":" << t1.tv_usec;
        XinLog(comm, key, "mpi_bcast", -1, -1); 
        int retVal = REAL_FUNCTION(MPI_Bcast)(buffer, count, datatype, root, comm);
        
        return retVal;
    }
    int WRAPPED_FUNCTION(MPI_Allreduce)(const void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
        uint64_t key;
        key = GetContextHash();
        gettimeofday(&t1, NULL);
        std::stringstream sstm;
        sstm << t1.tv_sec << ":" << t1.tv_usec;
        XinLog(comm, key, "mpi_all_reduce", -1, -1); 
        int retVal = REAL_FUNCTION(MPI_Allreduce)(sendbuf, recvbuf, count, datatype,  op,  comm);
        
        return retVal;
    }
 */   
 void setAllFreq (int core_num, unsigned int freq) {
  char fname[256];
  FILE *freqFd;
  int i;
  for (i=0; i < core_num; i++) {
  snprintf (fname, sizeof (fname), SCALING_SETFREQ_FILE_PATTERN, i);
  freqFd = fopen(fname, "w");
  if (freqFd == NULL) {
       perror("Failed to open frequency setting file");
  }
  if (fprintf(freqFd, "%u", freq) < 0) {
       perror("Failed to set frequency");
       return;
  }
  fflush(freqFd);
  fclose(freqFd);
  }
}  
    static inline void InitBarrierDeleter(){
        // make sure dense_hash_map is initialized with empty key
        GLOBAL_STATE.barrierSkipCache.set_empty_key(0);

        // Record your rank

        //HACK HACK ... delete me
        // enable_barrier_optimization_();

        // Get time after statring MPI
        MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
        if(myRank == 0) {
          /*
          ifstream freqfile ("./freq.txt");
          if (freqfile.is_open())
          {
            int id = 0;
            string line;
            while ( getline (freqfile,line) )
            {
              freq_arr[id++] = atoi(line.c_str());
            }
            freqfile.close();
          } else {
            printf("no freq file found!!!\n");
	    return;
          }
          */
                      gettimeofday(&mpiInitTime, NULL);
            //mpiInitTime = clock();
        }
        if (freq != 1){
          if (myRank == 0)
            setAllFreq(4, freq);
          else if (myRank == 4)
            setAllFreq(4, freq);
          else if (myRank == 8)
            setAllFreq(4, freq);
          else if (myRank == 12)
            setAllFreq(4, freq);
        }

#ifdef ENABLE_LOGGING
        CreateLogFile(myRank);
        fprintf(logFilePtr, "%lu %lu\n", p_start.tv_sec, p_start.tv_usec);

        //atexit(CloseLogFile);
#endif

    }


    int WRAPPED_FUNCTION(MPI_Init)(int* argc, char** *argv) {
        int retVal = REAL_FUNCTION(MPI_Init)(argc, argv);

        InitBarrierDeleter();

        return retVal;
    } 
    
    int WRAPPED_FUNCTION(MPI_Finalize)() {
        // Print execution time
        if(myRank == 0) {
            gettimeofday(&mpiFinalizeTime, NULL);
            //mpiFinalizeTime = clock();
            uint64_t span = TIME_SPENT(mpiInitTime, mpiFinalizeTime);
            //double time_spent = (double) (mpiFinalizeTime - mpiInitTime) / CLOCKS_PER_SEC;
            uint64_t ts1, ts2;
            ts1 = (mpiInitTime.tv_sec)*1000000 + (mpiInitTime.tv_usec);
            ts2 = (mpiFinalizeTime.tv_sec)*1000000 + (mpiFinalizeTime.tv_usec);

            printf("\n the MPI End-to_end execution, start is: %lu,  end is: %lu,  total execution is: %lu !\n", ts1, ts2, span);

            //printf("\n End-to-end execution time %lu \n", span);
        }
	for (int i = 0; i < log_vector.size(); i++)
            fprintf(logFilePtr, "%s\n", log_vector[i].c_str());
	log_vector.clear();

        int retVal = REAL_FUNCTION(MPI_Finalize)();
        CloseLogFile();
        return retVal;
    }    
}


