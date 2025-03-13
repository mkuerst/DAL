#ifndef __THREADCONNECTION_H__
#define __THREADCONNECTION_H__

#include "Common.h"
#include "RawMessageConnection.h"

struct RemoteConnection;

// app thread
struct ThreadConnection {

  uint16_t threadID;

  RdmaContext ctx;
  ibv_cq *cq; // for one-side verbs
  ibv_cq *rpc_cq;
  ibv_cq *lock_cq;

  RawMessageConnection *message;

  ibv_qp **data[NR_DIRECTORY];
  ibv_qp **lock[NR_DIRECTORY];

  ibv_mr *cacheMR;
  void *cachePool;
  uint32_t cacheLKey;
  RemoteConnection *remoteInfo;

  ibv_mr *lockMetaMR;
  void *lockMetaPool;
  uint32_t lockMetaLKey;
  uint32_t lockMetaRKey;


  ThreadConnection(uint16_t threadID, void *cachePool, uint64_t cacheSize,
                   void *lockeMetaPool, uint64_t lockMetaSize,
                   uint32_t machineNR, RemoteConnection *remoteInfo);

  void sendMessage2Dir(RawMessage *m, uint16_t node_id, uint16_t dir_id = 0);
  void sendMessage2App(RawMessage *m, uint16_t node_id, uint16_t th_id = 0);
};

#endif /* __THREADCONNECTION_H__ */
