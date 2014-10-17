#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/resource.h>
//#include<dmapp.h>

extern "C" {
    extern __thread uint64_t gAnalysisStatus;
    extern void enable_barrier_optimization_();
    extern void disable_barrier_optimization_();
    extern void disable_and_cleanup_barrier_optimization_();
    extern void UnprotectAllPages();
}

// available analysis flags (for gAnalysisStatus)
namespace BD {

    static const uint64_t STATUS_NONE          = 0x0;
    static const uint64_t ACCESSED_REMOTE_DATA = 0x1;
    static const uint64_t REMOTE_GET_SEEN      = 0x2;

}

static inline bool AccessedRemoteData() {
    return gAnalysisStatus & BD::ACCESSED_REMOTE_DATA;
}

static inline void SetAccessedRemoteData() {
    gAnalysisStatus |= BD::ACCESSED_REMOTE_DATA;
}

static inline bool RemoteGetSeen() {
    return gAnalysisStatus & BD::REMOTE_GET_SEEN;
}

static inline void SetRemoteGetSeen() {
    gAnalysisStatus |= BD::REMOTE_GET_SEEN;
}

static inline void ResetAnalysisStatus() {
    gAnalysisStatus &= BD::STATUS_NONE;
}
