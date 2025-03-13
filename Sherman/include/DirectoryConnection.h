#ifndef __DIRECTORYCONNECTION_H__
#define __DIRECTORYCONNECTION_H__

#include "Common.h"
#include "RawMessageConnection.h"

struct RemoteConnection;

// directory thread
struct DirectoryConnection {
  uint16_t dirID;

  RdmaContext ctx;
  ibv_cq *cq;

  RawMessageConnection *message;

  ibv_qp **data2app[MAX_APP_THREAD];
  ibv_qp **lock2app[MAX_APP_THREAD];

  ibv_mr *dsmMR;
  void *dsmPool;
  uint64_t dsmSize;
  uint32_t dsmLKey;

  ibv_mr *lockMR;
  void *lockPool; // address on-chip
  uint64_t lockSize;
  uint32_t lockLKey;

  ibv_mr *lockMetaMR;
  void *lockMetaPool;
  uint64_t lockMetaSize;
  uint32_t lockMetaLKey;
  uint32_t lockMetaRKey;

  RemoteConnection *remoteInfo;

  DirectoryConnection(uint16_t dirID, void *dsmPool, uint64_t dsmSize,
                      void *rlockPool, void *lockMetaPool, uint64_t lockMetaSize,
                      uint32_t machineNR, uint64_t chipSize,
                      RemoteConnection *remoteInfo);

  void sendMessage2App(RawMessage *m, uint16_t node_id, uint16_t th_id);
};

#endif /* __DIRECTORYCONNECTION_H__ */
