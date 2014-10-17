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

#define UNW_LOCAL_ONLY
#include <libunwind.h>


using namespace std;
using namespace std::tr1;
using google::dense_hash_map;
using google::dense_hash_set;

extern "C" {
    
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
    
    extern int REAL_FUNCTION(main)(int argc, char ** argv);
    int WRAPPED_FUNCTION(main)(int argc, char ** argv) {
        return REAL_FUNCTION(main)(argc, argv);
    }
    
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
        assert(backtraceMap.find(key) != backtraceMap.end());
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
    
    static void Log(MPI_Comm comm, uint64_t key, string s, uint64_t barInst, int status) {
        int size;
        MPI_Comm_size(comm, &size);
        int rank;
        MPI_Comm_rank(comm, &rank);
        fprintf(logFilePtr, "\n SIZE= %d : RANK = %d: BARINST = %lu, KEY = %lu, STATUS = %d, %s at ", size, rank, barInst, key, status, s.c_str());
        LogStackTrace(key);
    }
#else
    static void DumpStack2(uint64_t key) {}
    static void CloseLogFile() {}
    static void CreateLogFile(int rank) {}
    static void Log(MPI_Comm comm, uint64_t key, string s, uint64_t barInst, int status) {}
#endif
    
    extern int REAL_FUNCTION(MPI_Barrier)(MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Bcast)(void* buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Allgather)(const void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Allreduce)(const void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);
    extern int REAL_FUNCTION(MPI_Init)(int* argc, char** *argv);
    extern int REAL_FUNCTION(MPI_Finalize)(void);
    
    static inline void ParticipateInBarrier(MPI_Comm comm, uint64_t key, uint64_t curBarrierInstance, uint64_t val, int& retVal) {
        Log(comm, key, "Participating:", curBarrierInstance, AccessedRemoteData());
        // Force a dmapp sync here
        //xin************* no dmapp
        //dmapp_return_t t = dmapp_gsync_wait();
        //assert(t == DMAPP_RC_SUCCESS);
        // We have already decided to participate for this barrier
        GLOBAL_STATE.SetLastParticipatedBarrier(key);
        
#ifdef GUIDED_OPTIMIZATION
        RecordInParticipateBarriersMap(key);
#endif
        
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
        uint64_t recvBuf[2];
        uint64_t sendBuf[2];
        uint64_t sendStatus = PARTICIPATE;
        ALL_REDUCE_BUFFER(sendBuf, sendStatus, key);
        retVal = REAL_FUNCTION(MPI_Allreduce)(sendBuf, recvBuf, 2, MPI_UNSIGNED_LONG, myMPIOp, comm);
        assert((ALL_REDUCE_GET_CONTEXT(recvBuf)  == key) && "Context mismatch");
#else
        uint64_t recvBuf;
        uint64_t sendBuf = PARTICIPATE;
        sendBuf = ALL_REDUCE_BUFFER(sendBuf);
        retVal = REAL_FUNCTION(MPI_Allreduce)(&sendBuf, &recvBuf, 1, MPI_UNSIGNED_LONG, myMPIOp, comm);
#endif
        // Bad state! Somebody decided to skip! Should never happen.
        if(ALL_REDUCE_GET_INSTANCE(recvBuf)  != GLOBAL_STATE.GetBarrierInstance()) {
            printf("\n sendBuf = %lx, recvBuf = %lx, ALL_REDUCE_GET_INSTANCE(recvBuf) = %lx, GLOBAL_STATE.GetBarrierInstance() = %lx", sendBuf, recvBuf, ALL_REDUCE_GET_INSTANCE(recvBuf), GLOBAL_STATE.GetBarrierInstance());
        }
        
        assert(ALL_REDUCE_GET_INSTANCE(recvBuf)  == GLOBAL_STATE.GetBarrierInstance());
        //assert(recvBuf == curBarrierInstance);
        // Exit
        ResetAnalysisStatus();
    }
    
    
    static inline void SkipTheBarrier(MPI_Comm comm, uint64_t key, uint64_t curBarrierInstance, uint64_t val, int& retVal) {
        // We had decided to skip this barrier
        if(!AccessedRemoteData()) {
            // The decision still holds good.
            Log(comm, key, "Skipping:", curBarrierInstance, AccessedRemoteData());
            GLOBAL_STATE.IncrementSkippable();
#ifdef GUIDED_OPTIMIZATION
            RecordKeyInRedundancyMap(key);
#endif
            
//#define BARRIER_DEBUG
#ifdef BARRIER_DEBUG
            
            uint64_t receiveStatus;
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
            uint64_t recvBuf[2];
            //uint64_t sendBuf = curBarrierInstance;
            uint64_t sendBuf[2];
            ALL_REDUCE_BUFFER(sendBuf, SKIP, key);
            // Ensure the assumption is not violated by any process
            retVal = REAL_FUNCTION(MPI_Allreduce)(sendBuf, recvBuf, 2, MPI_UNSIGNED_LONG, myMPIOp, comm);
            assert((ALL_REDUCE_GET_CONTEXT(recvBuf)  == key) && "Context mismatch");
#else
            uint64_t recvBuf;
            //uint64_t sendBuf = curBarrierInstance;
            uint64_t sendBuf = SKIP;
            sendBuf = ALL_REDUCE_BUFFER(sendBuf);
            // Ensure the assumption is not violated by any process
            retVal = REAL_FUNCTION(MPI_Allreduce)(&sendBuf, &recvBuf, 1, MPI_UNSIGNED_LONG, myMPIOp, comm);
#endif
            
            assert(ALL_REDUCE_GET_INSTANCE(recvBuf)  == GLOBAL_STATE.GetBarrierInstance());
            
            
            int size;
            MPI_Comm_size(comm, &size);
            
            if(ALL_REDUCE_GET_STATUS(recvBuf) == SKIP) {
                //GLOBAL_STATE.skippable++;
            } else {
                GLOBAL_STATE.DecrementSkippable();
                GLOBAL_STATE.IncrementBadDecision();
                
                if(myRank == 0) {
                    printf("\n ALL_REDUCE_GET_STATUS(recvBuf) = %lu", ALL_REDUCE_GET_STATUS(recvBuf)) ;

                    printf("\n Bad decision at") ;
                    PrintBT();
                }
            }
            
#endif
        } else {
            Log(comm, key, "Breaking:", curBarrierInstance, AccessedRemoteData());
            // I am breaking my consensus... so what if all are breaking we are still fine.
            
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
            uint64_t recvBuf[2];
            uint64_t sendBuf[2];
            uint64_t sendStatus = PARTICIPATE;
            ALL_REDUCE_BUFFER(sendBuf, sendStatus, key);
            retVal = REAL_FUNCTION(MPI_Allreduce)(sendBuf, recvBuf, 2, MPI_UNSIGNED_LONG, myMPIOp, comm);
            assert((ALL_REDUCE_GET_CONTEXT(recvBuf)  == key) && "Context mismatch");
#else
            uint64_t recvBuf;
            uint64_t sendBuf = PARTICIPATE;
            sendBuf = ALL_REDUCE_BUFFER(sendBuf);
            retVal = REAL_FUNCTION(MPI_Allreduce)(&sendBuf, &recvBuf, 1, MPI_UNSIGNED_LONG, myMPIOp, comm);
#endif
            
            if(ALL_REDUCE_GET_INSTANCE(recvBuf)  != GLOBAL_STATE.GetBarrierInstance()) {
                printf("\n sendBuf = %lx, recvBuf = %lx, ALL_REDUCE_GET_INSTANCE(recvBuf) = %lx, GLOBAL_STATE.GetBarrierInstance() = %lx", sendBuf, recvBuf, ALL_REDUCE_GET_INSTANCE(recvBuf), GLOBAL_STATE.GetBarrierInstance());
            }
            
            assert(ALL_REDUCE_GET_INSTANCE(recvBuf)  == GLOBAL_STATE.GetBarrierInstance());
            
            if(ALL_REDUCE_GET_STATUS(recvBuf) != PARTICIPATE) {
                // This is the worst place to be in. We can't handle this as yet.
                if(myRank == 0) {
                    printf("\n Bad decision at") ;
                    PrintBT();
                }
                
                GLOBAL_STATE.IncrementBadDecision();
            } else {
                // All processes broke the code, so we are back in sync.
                GLOBAL_STATE.IncrementReSync();
            }
            
            // Exit
            ResetAnalysisStatus();
            //assert(recvBuf == curBarrierInstance);
        }
    }
    
    static inline void ContinueDecisionProcess(MPI_Comm comm, uint64_t key, uint64_t curBarrierInstance, uint64_t val, int& retVal) {
        Log(comm, key, "Deciding:", curBarrierInstance, AccessedRemoteData());
        // in decison process ... do all reduce
        
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
        uint64_t recvBuf[2], sendBuf[2];
        uint64_t newVal = val + 1;
        uint64_t sendStatus = AccessedRemoteData() ? PARTICIPATE : newVal;
        ALL_REDUCE_BUFFER(sendBuf, sendStatus, key);
        retVal = REAL_FUNCTION(MPI_Allreduce)(sendBuf, recvBuf, 2, MPI_UNSIGNED_LONG, myMPIOp, comm);
        assert((ALL_REDUCE_GET_CONTEXT(recvBuf) == key) && "Context mismatch");
#else
        uint64_t recvBuf;
        uint64_t newVal = val + 1;
        uint64_t sendBuf = AccessedRemoteData() ? PARTICIPATE : newVal;
        sendBuf = ALL_REDUCE_BUFFER(sendBuf);
        retVal = REAL_FUNCTION(MPI_Allreduce)(&sendBuf, &recvBuf, 1, MPI_UNSIGNED_LONG, myMPIOp, comm);
#endif
        assert(ALL_REDUCE_GET_INSTANCE(recvBuf)  == GLOBAL_STATE.GetBarrierInstance());
        
        if(ALL_REDUCE_GET_STATUS(recvBuf) == newVal) {
            GLOBAL_STATE.barrierSkipCache[key] = newVal;
        } else {
            GLOBAL_STATE.barrierSkipCache[key] = PARTICIPATE;
            Log(comm, key, "VetoInDecison:", curBarrierInstance, AccessedRemoteData());
        }

        // if a get seen, force PARTICIPATE on the previous barrier
        if (RemoteGetSeen() && GLOBAL_STATE.lastBarrierSeenKey) {
            GLOBAL_STATE.barrierSkipCache[GLOBAL_STATE.lastBarrierSeenKey] = PARTICIPATE;
        }

        // reset flags claiming we accessed remote data since we just did a barrier.
        ResetAnalysisStatus();
    }
    
    
    static inline void HandleFirstVisit(MPI_Comm comm, uint64_t key, uint64_t curBarrierInstance, int& retVal) {
        Log(comm, key, "Firsttime:", curBarrierInstance, AccessedRemoteData());
        // first visit ... do all reduce
#ifdef USE_CONTEXT_IN_MPI_REDUCTION
        uint64_t recvBuf[2];
        uint64_t sendBuf[2];
        uint64_t sendStatus= AccessedRemoteData() ? PARTICIPATE : 1;
        ALL_REDUCE_BUFFER(sendBuf, sendStatus, key);
        retVal = REAL_FUNCTION(MPI_Allreduce)(sendBuf, recvBuf, 2, MPI_UNSIGNED_LONG, myMPIOp, comm);
        assert((ALL_REDUCE_GET_CONTEXT(recvBuf) == key) && "Context mismatch");
#else
        uint64_t recvBuf;
        uint64_t sendBuf = AccessedRemoteData() ? PARTICIPATE : 1;
        sendBuf = ALL_REDUCE_BUFFER(sendBuf);
        retVal = REAL_FUNCTION(MPI_Allreduce)(&sendBuf, &recvBuf, 1, MPI_UNSIGNED_LONG, myMPIOp, comm);
#endif
        if(ALL_REDUCE_GET_INSTANCE(recvBuf)  != GLOBAL_STATE.GetBarrierInstance()) {
            printf("\n sendBuf = %lx, recvBuf = %lx, ALL_REDUCE_GET_INSTANCE(recvBuf) = %lx, GLOBAL_STATE.GetBarrierInstance() = %lx", sendBuf, recvBuf, ALL_REDUCE_GET_INSTANCE(recvBuf), GLOBAL_STATE.GetBarrierInstance());
        }
        
        assert(ALL_REDUCE_GET_INSTANCE(recvBuf)  == GLOBAL_STATE.GetBarrierInstance());
        GLOBAL_STATE.barrierSkipCache[key] = ALL_REDUCE_GET_STATUS(recvBuf);
        
        if(ALL_REDUCE_GET_STATUS(recvBuf) == PARTICIPATE) {
            Log(comm, key, "VetoOnFirstRound:", curBarrierInstance, AccessedRemoteData());
        }

        // if a get seen, force PARTICIPATE on the previous barrier
        if (RemoteGetSeen() && GLOBAL_STATE.lastBarrierSeenKey) {
            GLOBAL_STATE.barrierSkipCache[GLOBAL_STATE.lastBarrierSeenKey] = PARTICIPATE;
        }

        // we either participated (and need to reset), or the flags were not set, so reset is fine
        ResetAnalysisStatus();
    }
    
    static struct timeval t1, t2;
    static struct timeval mpiInitTime, mpiFinalizeTime;
    
    
    static void EnableBarrierOptimization() {
        if(myRank == 0) {
            gettimeofday(&t1, NULL);
            printf("\n Enabled BO\n");
        }
        
        GLOBAL_STATE.Enable();
    }
    // fortran interface
    void enable_barrier_optimization_() {
        EnableBarrierOptimization();
    }
    
    static void DisableAndCleanupBarrierOptimization() {
        if(myRank == 0) {
            printf("\n Disabled BO\n");
        }
        
        GLOBAL_STATE.Disable();
        // Clear the history in the hash table
        GLOBAL_STATE.barrierSkipCache.clear();
    }
    // fortran interface
    void disable_and_cleanup_barrier_optimization_() {
        DisableAndCleanupBarrierOptimization();
    }
    
    static void DisableBarrierOptimization() {
        if(myRank == 0) {
            gettimeofday(&t2, NULL);
            uint64_t span = TIME_SPENT(t1, t2);
            printf("\n Disabled BO %lu \n", span);
        }
        
        GLOBAL_STATE.Disable();
    }
    
    // fortran interface
    void disable_barrier_optimization_() {
        DisableBarrierOptimization();
    }
    
#ifdef ENABLE_REPLAY
    vector<char> replayLog;
    uint64_t replayIndex;
    void WriteToReplayLog(char replayVal){
        replayLog.push_back(replayVal);
    }
    
    void WriteReplayLogToFile(){
        FILE * replayFP = fopen(REPLAY_FILE_NAME, "w");
        assert(replayFP);
        for( int i = 0; i < replayLog.size(); i++) {
            char val = replayLog[i];
            fprintf(replayFP, "%c", val);
        }
        fclose(replayFP);
    }
    
    void ReadReplayLogToFile(){
        FILE * replayFP = fopen(REPLAY_FILE_NAME, "r");
        assert(replayFP);
        char val;
        while((val = fgetc(replayFP)) != EOF) {
            replayLog.push_back(val);
        }
        fclose(replayFP);
    }
    
    
    char GetReplayLogAtIndex(uint64_t index){
        if (index >= replayLog.size()) {
            printf("\n Replay log size is %d \n",replayLog.size());
        }
        assert(index < replayLog.size());
        return replayLog[index];
    }
#endif
    
    
#ifdef GUIDED_OPTIMIZATION
    static inline void ReadGuidedOptimizationLogFile(){
        FILE * fp = fopen(GUIDED_OPTIMIZATION_CTXT_HASHES_FILE_NAME, "r");
        assert(fp);
        uint64_t hashToSkip;
        while((fscanf(fp,"%lu", &hashToSkip) == 1)) {
            GLOBAL_STATE.guidedOptimizationSkipCtxtHashSet.insert(hashToSkip);
        }
        fclose(fp);
    }
#endif
    
#ifdef ENABLE_REPLAY
    // Reply the decision made in the last run.
    static inline void PerformBarrierReplay(MPI_Comm comm){
        if (GLOBAL_STATE.GetReplayMode() == REPLAY_MODE_REPLAY){
            // If we are in reply mode, we will return from here.
            
            // Replay the result
            char replayVal = GetReplayLogAtIndex(replayIndex);
            replayIndex++;
            
            switch(replayVal) {
                case REPLAY_SKIP: {
                    // skip barrier
                    return MPI_SUCCESS;
                }
                default: {
                    // do barrier
                    return REAL_FUNCTION(MPI_Barrier)(comm);
                }
            }
        }
    }
#endif
    
#ifdef GUIDED_OPTIMIZATION
    
    void UnprotectAllPages(){
        GLOBAL_STATE.PageProtectAllSharedData(PROT_READ | PROT_WRITE | PROT_EXEC);
    }
    
    static inline int HandleGuidedAnalyze(MPI_Comm comm, uint64_t key) {
        
        uint64_t recvBuf;
        const int POTENTIAL_SKIP = 1;
        uint64_t sendBuf = AccessedRemoteData() ? PARTICIPATE : POTENTIAL_SKIP;
        sendBuf = ALL_REDUCE_BUFFER(sendBuf);
        int retVal = REAL_FUNCTION(MPI_Allreduce)(&sendBuf, &recvBuf, 1, MPI_UNSIGNED_LONG, myMPIOp, comm);
        assert(ALL_REDUCE_GET_INSTANCE(recvBuf)  == GLOBAL_STATE.GetBarrierInstance());
        
        RedundancyKey rKey = {/* last barrier = */ 0, /*cur barrier =*/ key};
        
        if(ALL_REDUCE_GET_STATUS(recvBuf) == POTENTIAL_SKIP) {
            // if it is not in participate map, record in redundancy map.
            if (participatedBarriersMap.find(key) == participatedBarriersMap.end())
                RecordInRedundancyMap(rKey);
            // else this is non skippable barrier.
        } else {
            RecordInParticipateBarriersMap(key);
            // remove this key from redundancy map if recorded there
            DeleteFromRedundancyMap(rKey);
        }
#if 0 
        // Disable page protection
        GLOBAL_STATE.PageProtectAllSharedData(PROT_NONE);
#endif
        // reset flags claiming we accessed remote data since we just did a barrier.
        ResetAnalysisStatus();
        
        return retVal;
        
    }
    
    static inline int HandleGuidedOptimization(MPI_Comm comm, uint64_t key) {
        GLOBAL_STATE.guidedOptimizationSkipCtxtHashSetIterator = GLOBAL_STATE.guidedOptimizationSkipCtxtHashSet.find(key);
        // found
        if(GLOBAL_STATE.guidedOptimizationSkipCtxtHashSetIterator != GLOBAL_STATE.guidedOptimizationSkipCtxtHashSet.end()) {
            // Skip this barrier
            return  MPI_SUCCESS;
        } else {
            // Perform this barrier
            // TODO: Should we force a dmapp sync here? May be not since the user is supposed to have provided a sanitized set of skippables.
            // dmapp_return_t t = dmapp_gsync_wait();
            // assert(t == DMAPP_RC_SUCCESS);
            return REAL_FUNCTION(MPI_Barrier)(comm);
        }
    }
    
    static inline int PerformGuidedOptimization(MPI_Comm comm){
        GUIDED_OPTIMIZATION_MODE mode = GLOBAL_STATE.GetGuidedOptimizationMode();
        bool getBackTrace = (mode == GUIDED_OPTIMIZATION_MODE_ANALYZE);
        uint64_t key = GetContextHashWithBackTrace(getBackTrace);
        
        switch (GLOBAL_STATE.GetGuidedOptimizationMode()) {
            case GUIDED_OPTIMIZATION_MODE_ANALYZE:
                return HandleGuidedAnalyze(comm, key);
                break;
            case GUIDED_OPTIMIZATION_MODE_OPTIMIZE:
                return HandleGuidedOptimization(comm, key);
                break;
            default:
                assert(0 && "Should never reach here");
                break;
        }
    }
#endif
    int WRAPPED_FUNCTION(MPI_Barrier)(MPI_Comm comm) {
        int retVal = MPI_SUCCESS;
        // increment the barrrier instance
        uint64_t curBarrierInstance = GLOBAL_STATE.IncrementBarrierInstance();
        
        // Not enabled, simple do the barrier and return
        if(!GLOBAL_STATE.IsEnabled()) {
            return REAL_FUNCTION(MPI_Barrier)(comm);
        }
        
        // Enabled, hence perform tracking / optimization
        
        
#ifdef ENABLE_REPLAY
        // In replay mode, simply reply the barrier and return.
        PerformBarrierReplay(comm);
        return;
#endif
        
        uint64_t key;
        
#ifdef GUIDED_OPTIMIZATION
        return PerformGuidedOptimization(comm);
#else
        key = GetContextHash();
#endif
        // Is this barrier previously seen?
        GLOBAL_STATE.barrierSkipCacheIterator = GLOBAL_STATE.barrierSkipCache.find(key);
        
#ifdef ENABLE_REPLAY
        if(GLOBAL_STATE.GetReplayMode() == REPLAY_MODE_RECORD) {
            // Write to replay log
            // Anything other than "SKIP" is recorded as "PARTICIPATE"
            char replayVal;
            if(GLOBAL_STATE.barrierSkipCacheIterator != GLOBAL_STATE.barrierSkipCache.end()) {
                uint64_t val = GLOBAL_STATE.barrierSkipCacheIterator->second;
                switch(val) {
                    case SKIP: replayVal = REPLAY_SKIP; break;
                    default: replayVal = REPLAY_PARTICIPATE; break;
                }
            } else {
                replayVal = REPLAY_PARTICIPATE;
            }
            WriteToReplayLog(replayVal);
        }
#endif
        
        if(GLOBAL_STATE.barrierSkipCacheIterator != GLOBAL_STATE.barrierSkipCache.end()) {
            uint64_t val = GLOBAL_STATE.barrierSkipCacheIterator->second;
            
            switch(val) {
                case PARTICIPATE: {
                    ParticipateInBarrier(comm, key, curBarrierInstance, val, retVal);
                    break;
                }
                    
                case SKIP: {
                    SkipTheBarrier(comm, key, curBarrierInstance, val, retVal);
                    break;
                }
                    
                default: {
                    ContinueDecisionProcess(comm, key, curBarrierInstance, val, retVal);
                    break;
                }
            }
        } else {
            HandleFirstVisit(comm, key, curBarrierInstance, retVal);
        }

        GLOBAL_STATE.lastBarrierSeenKey = key;
        return retVal;
    }
    
    
    int WRAPPED_FUNCTION(MPI_Allgather)(const void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
        int retVal = REAL_FUNCTION(MPI_Allgather)(sendbuf,  sendcount,  sendtype, recvbuf,  recvcount,  recvtype, comm);
        /*
         #ifdef GUIDED_OPTIMIZATION
         uint64_t key = GetContextHashWithBackTrace();
         GLOBAL_STATE.SetLastParticipatedBarrier(key);
         Log(comm, key, "MPI_Allgather:");
         #endif
         
         // Exit
         gAccessedRemoteData = false; */
        return retVal;
    }
    
    int WRAPPED_FUNCTION(MPI_Bcast)(void* buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
        int retVal = REAL_FUNCTION(MPI_Bcast)(buffer, count, datatype, root, comm);
        /*
         #ifdef GUIDED_OPTIMIZATION
         uint64_t key = GetContextHashWithBackTrace();
         GLOBAL_STATE.SetLastParticipatedBarrier(key);
         Log(comm, key, "MPI_Bcast:");
         #endif
         // Exit
         gAccessedRemoteData = false; */
        return retVal;
    }
    
    
    int WRAPPED_FUNCTION(MPI_Allreduce)(const void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
        int retVal = REAL_FUNCTION(MPI_Allreduce)(sendbuf, recvbuf, count, datatype,  op,  comm);
        /*
         #ifdef GUIDED_OPTIMIZATION
         uint64_t key = GetContextHashWithBackTrace();
         GLOBAL_STATE.SetLastParticipatedBarrier(key);
         Log(comm, key, "MPI_Allreduce:");
         #endif
         // Exit
         gAccessedRemoteData = false; */
        return retVal;
    }
    
    static inline void ReadEnvironemtalVariables(){
#ifdef ENABLE_REPLAY
        char * val = getenv("NWCHEM_REPLAYMODE");
        if(val) {
            if (strcmp(val,"record") == 0 ) {
                GLOBAL_STATE.SetReplayMode(REPLAY_MODE_RECORD);
                // write the replay log to a file
                if(myRank == 0)
                    atexit(WriteReplayLogToFile);
            } else if (strcmp(val,"replay") == 0) {
                ReadReplayLogToFile();
                GLOBAL_STATE.SetReplayMode(REPLAY_MODE_REPLAY);
            } else {
                printf("\n Invalid NWCHEM_REPLAYMODE value %s", val);
                exit(-1);
            }
        }
#endif
        
#ifdef GUIDED_OPTIMIZATION
        char * val = getenv("NWCHEM_GUIDED_OPTIMIZATION_MODE");
        if(val) {
            if (strcmp(val,"analyze") == 0 ) {
                GLOBAL_STATE.SetGuidedOptimizationMode(GUIDED_OPTIMIZATION_MODE_ANALYZE);
                // write the replay log to a file
                if(myRank == 0)
                    atexit(DumpMaps);
            } else if (strcmp(val,"optimize") == 0) {
                ReadGuidedOptimizationLogFile();
                GLOBAL_STATE.SetGuidedOptimizationMode(GUIDED_OPTIMIZATION_MODE_OPTIMIZE);
            } else {
                printf("\n Invalid NWCHEM_GUIDED_OPTIMIZATION_MODE value %s", val);
                exit(-1);
            }
        }
#endif
    }
    
#ifdef GUIDED_OPTIMIZATION
    
#if 0 
    // Disable page protection
    extern dmapp_return_t REAL_FUNCTION(dmapp_mem_register) ( void *addr, uint64_t length, dmapp_seg_desc_t *seg_desc) ;
    dmapp_return_t WRAPPED_FUNCTION(dmapp_mem_register) ( void *addr, uint64_t length, dmapp_seg_desc_t *seg_desc) {
        dmapp_return_t retVal = REAL_FUNCTION(dmapp_mem_register)(addr, length, seg_desc);
        gAccessedRemoteData = true;
        
        //Record the address range in the map.
        GLOBAL_STATE.InsertInSharedDataRange(seg_desc->addr, seg_desc->len);
        
        // protect the page
        int rc = mprotect (seg_desc->addr, seg_desc->len, PROT_NONE);
        if(rc) {
            perror("mprotect failed in dmapp_mem_register");
        }
        return retVal;
    }
    
    extern dmapp_return_t REAL_FUNCTION(dmapp_mem_unregister) ( dmapp_seg_desc_t *seg_desc) ;
    dmapp_return_t WRAPPED_FUNCTION(dmapp_mem_unregister) ( dmapp_seg_desc_t *seg_desc) {
        
        // unprotect the page
        int rc = mprotect (seg_desc->addr, seg_desc->len, PROT_READ | PROT_WRITE | PROT_EXEC );
        if(rc) {
            perror("mprotect failed in dmapp_mem_register");
        }
        // Remove the address range in the map.
        GLOBAL_STATE.DeleteFromSharedDataRange(seg_desc->addr, seg_desc->len);
        
        dmapp_return_t retVal =  REAL_FUNCTION(dmapp_mem_unregister)(seg_desc);
        gAccessedRemoteData = true;
        
        return retVal;
    }
#endif
    
    void PageProtectionHandler(int sig, siginfo_t *si, void *unused){
        void * faultAddress;
        size_t len;
        GLOBAL_STATE.FindAddrInSharedDataRange(si->si_addr, faultAddress, len);
        
        // This indicate that the remote data is accessed.
        gAccessedRemoteData = true;
        
        // Faulting page
        // Change protection to R+W
        int rc = mprotect (faultAddress, len, PROT_READ | PROT_WRITE);
        if(rc) {
            perror("mprotect failed in PageProtectionHandler");
        }
    }
    
    static inline void InitializeGuidedOptimization(){
        // make sure dense_hash_map is initialized with empty key
        //GLOBAL_STATE.guidedOptimizationSkipCtxtHashSet.set_empty_key(0);
        
#if 0
        // Disable page protection
        //Setup SIGSEGV handler for detecting touches to shared memory pages
        // signal handler
        struct sigaction sa;
        /* Install segv_handler as the handler for SIGSEGV. */
        memset (&sa, 0, sizeof (sa));
        sa.sa_sigaction = &PageProtectionHandler;
        sa.sa_flags = SA_SIGINFO;
        sigaction (SIGSEGV, &sa, NULL);
#endif
        
    }
#endif
    
    static inline void InitBarrierDeleter(){
        // make sure dense_hash_map is initialized with empty key
        GLOBAL_STATE.barrierSkipCache.set_empty_key(0);
        
        // Register my reduction op
        MPI_Op_create(MyMPIReductionOp, 1 /*commute*/, &myMPIOp);
        
        // At enxit print some stats
        atexit(PrintStats);
        
        // Record your rank
        MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
        
        //HACK HACK ... delete me
        // enable_barrier_optimization_();
        
        
        // Get time after statring MPI
        if(myRank == 0) {
            gettimeofday(&mpiInitTime, NULL);
        }
        
        ReadEnvironemtalVariables();
        
#ifdef GUIDED_OPTIMIZATION
        InitializeGuidedOptimization();
#endif
        
        
#ifdef ENABLE_LOGGING
        CreateLogFile(myRank);
#ifdef VERBOSE
        
        // Register on exit function
        if(myRank == 0) {
            atexit(DumpMaps);
        }
        
#endif
        atexit(CloseLogFile);
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
            uint64_t span = TIME_SPENT(mpiInitTime, mpiFinalizeTime);
            printf("\n End-to-end execution time %lu \n", span);
        }
        int retVal = REAL_FUNCTION(MPI_Finalize)();
        return retVal;
    }
}


