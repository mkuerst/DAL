#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "Common.h"
#include "RawMessageConnection.h"

#include "ThreadConnection.h"
#include "DirectoryConnection.h"

struct RemoteConnection {
    // directory
    uint64_t dsmBase;

    uint32_t dsmRKey[NR_DIRECTORY];
    uint32_t dirMessageQPN[NR_DIRECTORY];
    ibv_ah *appToDirAh[MAX_APP_THREAD][NR_DIRECTORY];

    // cache
    uint64_t cacheBase;
    uint32_t cacheRKey[MAX_APP_THREAD];

    // lock memory
    uint64_t lockBase;
    uint32_t lockRKey[NR_DIRECTORY];

    // app thread
    uint32_t appRKey[MAX_APP_THREAD];
    uint32_t appMessageQPN[MAX_APP_THREAD];
    ibv_ah *dirToAppAh[NR_DIRECTORY][MAX_APP_THREAD];

    // lock meta
    uint64_t lockMetaBase;
    uint32_t lockMetaRKey[MAX_APP_THREAD];

    // peer
    uint64_t peerBase;
    uint32_t peerRKey[MAX_APP_THREAD];
};

#endif /* __CONNECTION_H__ */
