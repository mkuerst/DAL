
#include "DSM.h"
#include "Directory.h"
#include "HugePageAlloc.h"

#include "DSMKeeper.h"

#include <algorithm>

#include <iostream>
using namespace std;

thread_local int DSM::thread_id = -1;
thread_local ThreadConnection *DSM::iCon = nullptr;
thread_local char *DSM::rdma_buffer = nullptr;
thread_local LocalAllocator DSM::local_allocator;
thread_local RdmaBuffer DSM::rbuf[define::kMaxCoro];
thread_local uint64_t DSM::thread_tag = 0;
thread_local uint64_t *DSM::spin_loc;
thread_local uint64_t *DSM::next_loc;
thread_local int DSM::next_loc_curr;
thread_local GlobalAddress DSM::spin_gaddr;
thread_local GlobalAddress DSM::next_gaddr;
thread_local GlobalAddress DSM::next_gaddr_base;

DSM *DSM::getInstance(const DSMConfig &conf) {
  static DSM *dsm = nullptr;
  static WRLock lock;

  lock.wLock();
  if (!dsm) {
    dsm = new DSM(conf);
  } else {
  }
  lock.wUnlock();

  return dsm;
}

DSM::DSM(const DSMConfig &conf)
    : conf(conf), appID(0), cache(conf.cacheConfig) {
      
  baseAddr = (uint64_t)hugePageAlloc(conf.dsmSize * define::GB + 64 * define::MB);
  #ifdef ON_CHIP
  rlockAddr = define::kLockStartAddr;
  #else
  rlockAddr = (uint64_t) malloc(conf.chipSize * 1024);
  memset((char *)rlockAddr, 0, conf.chipSize * 1024);
  #endif

  lockMetaAddr = (uint64_t) malloc(conf.lockMetaSize * 1024);
  memset((char *)lockMetaAddr, 0, conf.lockMetaSize * 1024);
  
  // Debug::notifyInfo("shared memory size: %dGB, 0x%lx", conf.dsmSize, baseAddr);
  // Debug::notifyInfo("rdma cache size: %dGB", conf.cacheConfig.cacheSize);
  // std::cerr << "cache_addr: " << cache.data << std::endl;
  
  // warmup
  memset((char *)cache.data, 0, cache.size * define::GB);
  memset((char *)baseAddr, 0, conf.dsmSize * define::GB + 64 * define::MB);
  
  initRDMAConnection();
  if (myNodeID < conf.mnNR) {
    for (int i = 0; i < NR_DIRECTORY; ++i) {
      dirAgent[i] =
          new Directory(dirCon[i], remoteInfo, conf.mnNR, i, myNodeID);
    }
    // Debug::notifyInfo("Memory server %d start up", myNodeID);
  }
  keeper->barrier("DSM-init");
}


DSM::~DSM() {}

void DSM::free_dsm() {
  munmap((void*)baseAddr, conf.dsmSize * define::GB);
  munmap((void*)cache.data, cache.size * define::GB);

  
  if (myNodeID < conf.mnNR) {
    stopDirThread();
    RawMessage m;
    m.type = RpcType::END;
    this->rpc_call_dir(m, myNodeID, 0);
  }

  if (myNodeID  == 0) {

    if (ibv_dereg_mr(dirCon[myNodeID]->dsmMR)) {
      perror("ibv_dereg_mr dmsMR failed");
    }
    if (ibv_dereg_mr(dirCon[myNodeID]->lockMR)) {
      perror("ibv_dereg_mr failed");
    }
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      for (size_t k = 0; k < conf.machineNR; ++k) {
        if (ibv_destroy_qp(dirCon[myNodeID]->data2app[i][k])) {
          Debug::notifyError("ibv_destroy_qp dirCon[%d] failed\n", myNodeID);
        }
      }
    }
    // if (dirCon[myNodeID]->cq) {
    //   if (ibv_destroy_cq(dirCon[myNodeID]->cq)) {
    //     Debug::notifyError("ibv_destroy_cq dirCon[%d] failed\n", myNodeID);
    //   }
    // }
    // if (dirCon[myNodeID]->ctx.pd) {
    //   if (ibv_dealloc_pd(dirCon[myNodeID]->ctx.pd)) {
    //     Debug::notifyError("Failed to deallocate PD dirCon[%d]", myNodeID);
    //   }
    // }
    // if (dirCon[myNodeID]->ctx.ctx) {
    //   if (ibv_close_device(dirCon[myNodeID]->ctx.ctx)) {
    //     Debug::notifyError("failed to close device context dirCon[%d]", myNodeID);
    //   }
    // }
  }

    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      if (ibv_dereg_mr(thCon[i]->cacheMR)) {
        Debug::notifyError("ibv_dereg_mr failed thCon[%d]->cacheMR", i);
      }
      for (int j = 0; j < NR_DIRECTORY; ++j) {
        for (size_t k = 0; k < conf.machineNR; ++k) {
          if (thCon[i]->data[j][k]) {
            if (ibv_destroy_qp(thCon[i]->data[j][k])) {
              Debug::notifyError("ibv_destroy_qp thCon[%d] failed\n", i);
            }
          }
        }
      }
      if (thCon[i]->cq) {
        if (ibv_destroy_cq(thCon[i]->cq)) {
          Debug::notifyError("ibv_destroy_cq thCon[%d] failed\n", i);
        }
      }
      // if (thCon[i]->rpc_cq) {
      //   if (ibv_destroy_cq(thCon[i]->rpc_cq)) {
      //     Debug::notifyError("ibv_destroy_cq rpc_cq thCon[%d] failed\n", i);
      //   }
      // }
      // if (thCon[i]->ctx.pd) {
      //   if (ibv_dealloc_pd(thCon[i]->ctx.pd)) {
      //     Debug::notifyError("Failed to deallocate PD for thCon %d", i);
      //   }
      // }
      // if (thCon[i]->ctx.ctx) {
      //   if (ibv_close_device(thCon[i]->ctx.ctx)) {
      //     Debug::notifyError("failed to close device context for thCon %d", i);
      //   }
      // }
    }
}

void DSM::registerThread(int page_size) {

  static bool has_init[MAX_APP_THREAD];

  if (thread_id != -1)
    return;

  thread_id = appID.fetch_add(1);
  thread_tag = thread_id + (((uint64_t)this->getMyNodeID()) << 32) + 1;

  iCon = thCon[thread_id];

  if (!has_init[thread_id]) {
    iCon->message->initRecv();
    iCon->message->initSend();

    has_init[thread_id] = true;
  }

  rdma_buffer = (char *)cache.data + thread_id * 12 * define::MB;

  spin_gaddr.nodeID = this->getMyNodeID();
  spin_gaddr.offset = conf.dsmSize * define::GB + thread_id * 16 * sizeof(uint64_t);
  next_gaddr.nodeID = this->getMyNodeID();
  next_gaddr.offset = spin_gaddr.offset + 2*sizeof(uint64_t);
  next_gaddr_base = next_gaddr;
  next_gaddr.version = thread_id;
  spin_loc = (uint64_t *) ((char *)baseAddr + spin_gaddr.offset);
  *spin_loc = 0;
  next_loc = (uint64_t *) ((char *)baseAddr + next_gaddr.offset);
  *next_loc = 0;
  next_loc_curr = 0;

  for (int i = 0; i < define::kMaxCoro; ++i) {
    rbuf[i].set_buffer(rdma_buffer + i * define::kPerCoroRdmaBuf, page_size);
  }
}

void DSM::initRDMAConnection() {
  remoteInfo = new RemoteConnection[conf.machineNR];

  for (int i = 0; i < MAX_APP_THREAD; ++i) {
    thCon[i] =
        new ThreadConnection(i, (void *)cache.data, cache.size * define::GB,
                             conf.machineNR, remoteInfo);
  }

  for (int i = 0; i < NR_DIRECTORY; ++i) {
    dirCon[i] =
        new DirectoryConnection(i, (void *)baseAddr, conf.dsmSize * define::GB,
                                (void *) rlockAddr, (void *) lockMetaAddr, conf.lockMetaSize*1024,
                                 conf.machineNR, conf.chipSize*1024,
                                remoteInfo);
  }

  keeper = new DSMKeeper(thCon, dirCon, remoteInfo, conf.machineNR);

  myNodeID = keeper->getMyNodeID();
}

#include <immintrin.h>
void DSM::spin_on(char *buf, GlobalAddress curr_holder_addr) {
  // uint64_t x = 0;
  // while (*spin_loc == 0) {
  while (*spin_loc == 0) {
    this->read_sync(buf, spin_gaddr, sizeof(uint64_t), NULL);
    // x++;
    // if (x > 1e9) {
    //   cerr << "DEADLOCK SPIN" << endl;
    //   exit(1);
    // }
    // _mm_clflush(spin_loc);  // Flush cache to see the latest value
    // _mm_pause();
  }
  GlobalAddress *ga = (GlobalAddress *) spin_loc ;

  cerr << "NODE " << myNodeID << endl;
  cerr << "WOKEN UP: " << endl <<
  "curr_holder_addr: " << curr_holder_addr << endl <<
  // "own spin_gaddr: " << spin_gaddr << "\n" <<
  "*spin_loc: " << *spin_loc << "\n" <<
  "*spin_loc as gaddr: " << *ga << "\n\n";
  *spin_loc = 0;
}

void DSM::wait_for_peer(GlobalAddress gaddr) {
  struct ibv_recv_wr wr, *bad_wr;
  ibv_wc wc;
  memset(&wr, 0, sizeof(wr));

  cerr << "NODE " << myNodeID << endl;
  cerr << "START POLLING FOR WAKEUP CALL FROM: " << gaddr.nodeID << ", " << gaddr.version << endl;
  pollWithCQ(iCon->rpc_cq, 1, &wc);

  switch (int(wc.opcode)) {
    case IBV_WC_RECV: {

      auto *m = (RawMessage *)iCon->message->getMessage();

      switch (m->type) {
        case RpcType::WAKEUP: {
          cerr << "RECEIVED WAKEUP CALL FROM: " << m->node_id  << ", " << m->app_id << endl;
          break;
        }
        default: {
          cerr << "NON-WAKEUP CALL!" << endl;
          break;
        }
      }
      break;
    }
    default: {
      cerr << "NON-RCV EVENT!" << endl;
      break;
    }
  }
  // while (ibv_poll_cq(iCon->cq, 1, &wc) > 0);
  // ibv_req_notify_cq(iCon->cq, 0);
  // cerr << "NODE " << myNodeID << endl;
  // cerr << "AWAITING WAKEUP" << "\n\n";
  
  // ibv_post_recv(iCon->data[0][myNodeID], &wr, &bad_wr);

  // struct ibv_cq* ev_cq;
  // void* ev_ctx;
  // ibv_get_cq_event(iCon->cc, &iCon->cq, &ev_ctx);
  // cerr << "NODE " << myNodeID << endl;
  // cerr << "GOT AWOKEN: " << "\n\n";
  // ibv_ack_cq_events(ev_cq, 1);

  // pollWithCQ(iCon->cq, 1, &wc, 1);

  // poll:
  //   while (ibv_poll_cq(iCon->cq, 1, &wc) == 0);
  //   if (wc.opcode == IBV_WC_RECV) {
  //     cerr << "WOKEN UP" << std::endl;
  //   } else {
  //     cerr << "NOT A RCV" << std::endl;
  //     goto poll;
  //   }
}

void DSM::wakeup_peer(GlobalAddress gaddr) {
    auto buffer = (RawMessage *)iCon->message->getSendPool();
    RawMessage m;
    m.type = RpcType::WAKEUP;

    memcpy(buffer, &m, sizeof(RawMessage));
    buffer->node_id = myNodeID;
    buffer->app_id = thread_id;
    cerr << "NODE " << myNodeID << endl;
    cerr << "ABOUT TO SEND MSG TO PEER" << endl <<
    buffer->node_id << ", " << buffer->app_id << endl <<
    "gaddr: " << gaddr << endl << "\n\n";
    iCon->sendMessage2App(buffer, gaddr.nodeID, gaddr.version);
    cerr << "SENT WAKEUP CALL TO PEER" << "\n\n";
}

void DSM::read(char *buffer, GlobalAddress gaddr, size_t size, bool signal,
               CoroContext *ctx) {
  if (ctx == nullptr) {
    rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
             remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
             iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], signal);
  } else {
    rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
             remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
             iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], true,
             ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::read_sync(char *buffer, GlobalAddress gaddr, size_t size,
                    CoroContext *ctx) {
  read(buffer, gaddr, size, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr.offset, size);
  }
}

void DSM::write(const char *buffer, GlobalAddress gaddr, size_t size,
                bool signal, CoroContext *ctx) {

  if (ctx == nullptr) {
    rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
              remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
              iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], -1, signal);
  } else {
    rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
              remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
              iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], -1, true,
              ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::write_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                     CoroContext *ctx) {
  write(buffer, gaddr, size, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr.offset, size);
  }
}

void DSM::write_peer_cache(const char *buffer, GlobalAddress gaddr, size_t size,
                bool signal, CoroContext *ctx) {

  if (ctx == nullptr) {
    rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
              remoteInfo[gaddr.nodeID].cacheBase + gaddr.offset, size,
              iCon->cacheLKey, remoteInfo[gaddr.nodeID].appRKey[0], -1, signal);
  } else {
    rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
              remoteInfo[gaddr.nodeID].cacheBase + gaddr.offset, size,
              iCon->cacheLKey, remoteInfo[gaddr.nodeID].appRKey[0], -1, true,
              ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::write_peer_cache_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                     CoroContext *ctx) {
  write_peer_cache(buffer, gaddr, size, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr.offset, size);
  }
}

void DSM::fill_keys_dest(RdmaOpRegion &ror, GlobalAddress gaddr, bool is_chip) {
  ror.lkey = iCon->cacheLKey;
  if (is_chip) {
    ror.dest = remoteInfo[gaddr.nodeID].lockBase + gaddr.offset;
    ror.remoteRKey = remoteInfo[gaddr.nodeID].lockRKey[0];
  } 
   else {
    ror.dest = remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset;
    ror.remoteRKey = remoteInfo[gaddr.nodeID].dsmRKey[0];
  }
}

void DSM::write_batch(RdmaOpRegion *rs, int k, bool signal, CoroContext *ctx) {

  int node_id = -1;
  for (int i = 0; i < k; ++i) {

    GlobalAddress gaddr;
    gaddr.val = rs[i].dest;
    node_id = gaddr.nodeID;
    // cerr << "filling batched write " << i << ": " << endl <<
    // "gaddr: " << gaddr << "\n\n";
    fill_keys_dest(rs[i], gaddr, rs[i].is_on_chip);
  }

  if (ctx == nullptr) {
    rdmaWriteBatch(iCon->data[0][node_id], rs, k, signal);
  } else {
    rdmaWriteBatch(iCon->data[0][node_id], rs, k, true, ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::write_batch_sync(RdmaOpRegion *rs, int k, CoroContext *ctx) {
  write_batch(rs, k, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, rs[2].dest, rs[2].size, rs[2].source);
  }
}

void DSM::write_faa(RdmaOpRegion &write_ror, RdmaOpRegion &faa_ror,
                    uint64_t add_val, bool signal, CoroContext *ctx) {
  int node_id;
  {
    GlobalAddress gaddr;
    gaddr.val = write_ror.dest;
    node_id = gaddr.nodeID;

    fill_keys_dest(write_ror, gaddr, write_ror.is_on_chip);
  }
  {
    GlobalAddress gaddr;
    gaddr.val = faa_ror.dest;

    fill_keys_dest(faa_ror, gaddr, faa_ror.is_on_chip);
  }
  if (ctx == nullptr) {
    rdmaWriteFaa(iCon->data[0][node_id], write_ror, faa_ror, add_val, signal);
  } else {
    rdmaWriteFaa(iCon->data[0][node_id], write_ror, faa_ror, add_val, true,
                 ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}
void DSM::write_faa_sync(RdmaOpRegion &write_ror, RdmaOpRegion &faa_ror,
                         uint64_t add_val, CoroContext *ctx) {
  write_faa(write_ror, faa_ror, add_val, true, ctx);
  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }
}

void DSM::write_cas(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                    uint64_t equal, uint64_t val, bool signal,
                    CoroContext *ctx) {
  int node_id;
  {
    GlobalAddress gaddr;
    gaddr.val = write_ror.dest;
    node_id = gaddr.nodeID;

    fill_keys_dest(write_ror, gaddr, write_ror.is_on_chip);
  }
  {
    GlobalAddress gaddr;
    gaddr.val = cas_ror.dest;

    fill_keys_dest(cas_ror, gaddr, cas_ror.is_on_chip);
  }
  if (ctx == nullptr) {
    rdmaWriteCas(iCon->data[0][node_id], write_ror, cas_ror, equal, val,
                 signal);
  } else {
    rdmaWriteCas(iCon->data[0][node_id], write_ror, cas_ror, equal, val, true,
                 ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}
void DSM::write_cas_sync(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                         uint64_t equal, uint64_t val, CoroContext *ctx) {
  write_cas(write_ror, cas_ror, equal, val, true, ctx);
  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }
}

void DSM::cas_read(RdmaOpRegion &cas_ror, RdmaOpRegion &read_ror,
                   uint64_t equal, uint64_t val, bool signal,
                   CoroContext *ctx) {

  int node_id;
  {
    GlobalAddress gaddr;
    gaddr.val = cas_ror.dest;
    node_id = gaddr.nodeID;
    fill_keys_dest(cas_ror, gaddr, cas_ror.is_on_chip);
  }
  {
    GlobalAddress gaddr;
    gaddr.val = read_ror.dest;
    fill_keys_dest(read_ror, gaddr, read_ror.is_on_chip);
  }

  if (ctx == nullptr) {
    rdmaCasRead(iCon->data[0][node_id], cas_ror, read_ror, equal, val, signal);
  } else {
    rdmaCasRead(iCon->data[0][node_id], cas_ror, read_ror, equal, val, true,
                ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

bool DSM::cas_read_sync(RdmaOpRegion &cas_ror, RdmaOpRegion &read_ror,
                        uint64_t equal, uint64_t val, CoroContext *ctx) {
  cas_read(cas_ror, read_ror, equal, val, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }

  return equal == *(uint64_t *)cas_ror.source;
}

void DSM::cas(GlobalAddress gaddr, uint64_t equal, uint64_t val,
              uint64_t *rdma_buffer, bool signal, CoroContext *ctx) {

  if (ctx == nullptr) {
    rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                       remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                       val, iCon->cacheLKey,
                       remoteInfo[gaddr.nodeID].dsmRKey[0], signal);
  } else {
    rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                       remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                       val, iCon->cacheLKey,
                       remoteInfo[gaddr.nodeID].dsmRKey[0], true, ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

bool DSM::cas_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, CoroContext *ctx) {
  cas(gaddr, equal, val, rdma_buffer, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }

  return equal == *rdma_buffer;
}

void DSM::cas_peer(GlobalAddress gaddr, uint64_t equal, uint64_t val,
              uint64_t *rdma_buffer, bool signal, CoroContext *ctx) {

  if (ctx == nullptr) {
    rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                       remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                       val, iCon->cacheLKey,
                       remoteInfo[gaddr.nodeID].dsmRKey[0], signal);
  } else {
    rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                       remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                       val, iCon->cacheLKey,
                       remoteInfo[gaddr.nodeID].dsmRKey[0], true, ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
  std::cerr << "cas_peer" << std::endl;
  std::cerr << "gaddr :" << gaddr << std::endl;
  GlobalAddress *v = (GlobalAddress *) &val;
  std::cerr << "val :" << *v << "\n";
}

bool DSM::cas_peer_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, CoroContext *ctx) {
  cas_peer(gaddr, equal, val, rdma_buffer, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr, 8, val);
  }
  std::cerr << "equal == *rdma_buffer" << "\n" <<
  *((GlobalAddress*) &equal) << " = " << *((GlobalAddress*) rdma_buffer) << "\n\n";

  return equal == *rdma_buffer;
}

void DSM::cas_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, uint64_t mask, bool signal) {
  rdmaCompareAndSwapMask(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                         remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                         val, iCon->cacheLKey,
                         remoteInfo[gaddr.nodeID].dsmRKey[0], mask, signal);
}

bool DSM::cas_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                        uint64_t *rdma_buffer, uint64_t mask) {
  cas_mask(gaddr, equal, val, rdma_buffer, mask);
  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);

  return (equal & mask) == (*rdma_buffer & mask);
}

void DSM::faa_boundary(GlobalAddress gaddr, uint64_t add_val,
                       uint64_t *rdma_buffer, uint64_t mask, bool signal,
                       CoroContext *ctx) {
  if (ctx == nullptr) {
    rdmaFetchAndAddBoundary(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                            remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset,
                            add_val, iCon->cacheLKey,
                            remoteInfo[gaddr.nodeID].dsmRKey[0], mask, signal);
  } else {
    rdmaFetchAndAddBoundary(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                            remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset,
                            add_val, iCon->cacheLKey,
                            remoteInfo[gaddr.nodeID].dsmRKey[0], mask, true,
                            ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::faa_boundary_sync(GlobalAddress gaddr, uint64_t add_val,
                            uint64_t *rdma_buffer, uint64_t mask,
                            CoroContext *ctx) {
  faa_boundary(gaddr, add_val, rdma_buffer, mask, true, ctx);
  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }
}

void DSM::read_dm(char *buffer, GlobalAddress gaddr, size_t size, bool signal,
                  CoroContext *ctx) {

  if (ctx == nullptr) {
    rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
             remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
             iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], signal);
  } else {
    rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
             remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
             iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], true,
             ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::read_dm_sync(char *buffer, GlobalAddress gaddr, size_t size,
                       CoroContext *ctx) {
  read_dm(buffer, gaddr, size, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr.offset, size);
  }
}

void DSM::write_dm(const char *buffer, GlobalAddress gaddr, size_t size,
                   bool signal, CoroContext *ctx) {
  if (ctx == nullptr) {
    rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
              remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
              iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], -1,
              signal);
  } else {
    rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
              remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
              iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], -1, true,
              ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::write_dm_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                        CoroContext *ctx) {
  write_dm(buffer, gaddr, size, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr.offset, size);
  }
}

void DSM::cas_dm(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                 uint64_t *rdma_buffer, bool signal, CoroContext *ctx) {

  if (ctx == nullptr) {
    rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                       remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, equal,
                       val, iCon->cacheLKey,
                       remoteInfo[gaddr.nodeID].lockRKey[0], signal);
  } else {
    rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                       remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, equal,
                       val, iCon->cacheLKey,
                       remoteInfo[gaddr.nodeID].lockRKey[0], true,
                       ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

bool DSM::cas_dm_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                      uint64_t *rdma_buffer, CoroContext *ctx) {
  cas_dm(gaddr, equal, val, rdma_buffer, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc, gaddr.offset, 64);
  }

  return equal == *rdma_buffer;
}

void DSM::cas_dm_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                      uint64_t *rdma_buffer, uint64_t mask, bool signal) {
  rdmaCompareAndSwapMask(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                         remoteInfo[gaddr.nodeID].lockBase + gaddr.offset,
                         equal, val, iCon->cacheLKey,
                         remoteInfo[gaddr.nodeID].lockRKey[0], mask, signal);
}

bool DSM::cas_dm_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                           uint64_t *rdma_buffer, uint64_t mask) {
  cas_dm_mask(gaddr, equal, val, rdma_buffer, mask);
  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);

  return (equal & mask) == (*rdma_buffer & mask);
}

void DSM::faa_dm_boundary(GlobalAddress gaddr, uint64_t add_val,
                          uint64_t *rdma_buffer, uint64_t mask, bool signal,
                          CoroContext *ctx) {
  if (ctx == nullptr) {

    rdmaFetchAndAddBoundary(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                            remoteInfo[gaddr.nodeID].lockBase + gaddr.offset,
                            add_val, iCon->cacheLKey,
                            remoteInfo[gaddr.nodeID].lockRKey[0], mask, signal);
  } else {
    rdmaFetchAndAddBoundary(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                            remoteInfo[gaddr.nodeID].lockBase + gaddr.offset,
                            add_val, iCon->cacheLKey,
                            remoteInfo[gaddr.nodeID].lockRKey[0], mask, true,
                            ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

void DSM::faa_dm_boundary_sync(GlobalAddress gaddr, uint64_t add_val,
                               uint64_t *rdma_buffer, uint64_t mask,
                               CoroContext *ctx) {
  faa_dm_boundary(gaddr, add_val, rdma_buffer, mask, true, ctx);
  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }
}

uint64_t DSM::poll_rdma_cq(int count) {
  ibv_wc wc;
  pollWithCQ(iCon->cq, count, &wc);

  return wc.wr_id;
}

bool DSM::poll_rdma_cq_once(uint64_t &wr_id) {
  ibv_wc wc;
  int res = pollOnce(iCon->cq, 1, &wc);

  wr_id = wc.wr_id;

  return res == 1;
}