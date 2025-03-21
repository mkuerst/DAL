#include "ThreadConnection.h"

#include "Connection.h"
#include <iostream>

ThreadConnection::ThreadConnection(uint16_t threadID, void *cachePool, uint64_t cacheSize,
                                   void *lockMetaPool, uint64_t lockMetaSize, uint32_t machineNR,
                                   RemoteConnection *remoteInfo)
    : threadID(threadID), remoteInfo(remoteInfo) {
  createContext(&ctx);

  cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);
  // rpc_cq = cq;
  rpc_cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);
  lock_cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);
  // peer_cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);

  message = new RawMessageConnection(ctx, rpc_cq, APP_MESSAGE_NR);

  this->cachePool = cachePool;
  cacheMR = createMemoryRegion((uint64_t)cachePool, cacheSize, &ctx);
  cacheLKey = cacheMR->lkey;

  // this->lockMetaPool = lockMetaPool;
  // lockMetaMR = createMemoryRegion((uint64_t)lockMetaPool, lockMetaSize, &ctx);
  // lockMetaLKey = lockMetaMR->lkey;
  // lockMetaRKey = lockMetaMR->rkey;

  // dir, RC
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    data[i] = new ibv_qp *[machineNR];
    lock[i] = new ibv_qp *[machineNR];
    // peer[i] = new ibv_qp *[machineNR];
    for (size_t k = 0; k < machineNR; ++k) {
      createQueuePair(&data[i][k], IBV_QPT_RC, cq, &ctx);
      createQueuePair(&lock[i][k], IBV_QPT_RC, lock_cq, &ctx);
      // createQueuePair(&peer[i][k], IBV_QPT_RC, peer_cq, &ctx);
    }
  }
}

void ThreadConnection::sendMessage2Dir(RawMessage *m, uint16_t node_id,
                                       uint16_t dir_id) {
  
  message->sendRawMessage(m, remoteInfo[node_id].dirMessageQPN[dir_id],
                          remoteInfo[node_id].appToDirAh[threadID][dir_id]);

}
void ThreadConnection::sendMessage2App(RawMessage *m, uint16_t node_id,
                                       uint16_t th_id) {
  
  message->sendRawMessage(m, remoteInfo[node_id].appMessageQPN[th_id],
                          remoteInfo[node_id].dirToAppAh[0][th_id]);

}
