#include "DirectoryConnection.h"

#include "Connection.h"

DirectoryConnection::DirectoryConnection(uint16_t dirID, void *dsmPool,
                                         uint64_t dsmSize, void* rlockPool, 
                                         void* lockMetaPool, uint64_t lockMetaSize,
                                         void* peerPool, uint64_t peerSize,
                                         uint32_t machineNR, uint64_t chipSize,
                                         ThreadConnection thCon,
                                         RemoteConnection *remoteInfo)
    : dirID(dirID), remoteInfo(remoteInfo) {

  createContext(&ctx);
  cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);
  message = new RawMessageConnection(ctx, cq, DIR_MESSAGE_NR);

  message->initRecv();
  message->initSend();

  // dsm memory
  this->dsmPool = dsmPool;
  this->dsmSize = dsmSize;
  this->dsmMR = createMemoryRegion((uint64_t)dsmPool, dsmSize, &ctx);
  this->dsmLKey = dsmMR->lkey;

  this->lockMetaPool = lockMetaPool;
  lockMetaMR = createMemoryRegion((uint64_t)lockMetaPool, lockMetaSize, &ctx);
  lockMetaLKey = lockMetaMR->lkey;
  lockMetaRKey = lockMetaMR->rkey;

  this->peerPool = peerPool;
  peerMR = createMemoryRegion((uint64_t)peerPool, peerSize, &ctx);
  peerLKey = peerMR->lkey;
  peerRKey = peerMR->rkey;

  // on-chip lock memory
  if (dirID == 0) {
    this->lockPool = rlockPool;
    this->lockSize = chipSize;
    #ifdef ON_CHIP
    this->lockMR = createMemoryRegionOnChip((uint64_t)this->lockPool,
                                            this->lockSize, &ctx);
    #else
    this->lockMR = createMemoryRegion((uint64_t)this->lockPool,
                                            this->lockSize, &ctx);
    #endif
    this->lockLKey = lockMR->lkey;
  }
  // DEB("Allocated %lu Bytes for lock\n", this->lockSize);

  // app, RC
  for (int i = 0; i < MAX_APP_THREAD; ++i) {
    data2app[i] = new ibv_qp *[machineNR];
    lock2app[i] = new ibv_qp *[machineNR];
    // peer2app[i] = new ibv_qp *[machineNR];
    for (size_t k = 0; k < machineNR; ++k) {
      createQueuePair(&data2app[i][k], IBV_QPT_RC, cq, &ctx);
      createQueuePair(&lock2app[i][k], IBV_QPT_RC, cq, &ctx);
      // createQueuePair(&peer2app[i][k], IBV_QPT_RC, cq, &ctx);
    }
  }
}

void DirectoryConnection::sendMessage2App(RawMessage *m, uint16_t node_id,
                                          uint16_t th_id) {
  message->sendRawMessage(m, remoteInfo[node_id].appMessageQPN[th_id],
                          remoteInfo[node_id].dirToAppAh[dirID][th_id]);
  ;
}
