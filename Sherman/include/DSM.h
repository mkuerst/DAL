#ifndef __DSM_H__
#define __DSM_H__

#define CPU_PAUSE() asm volatile("pause\n" : : : "memory")
#define CPU_FENCE() __asm__ volatile("mfence" ::: "memory");
#include <atomic>

#include "Cache.h"
#include "Config.h"
#include "Connection.h"
#include "DSMKeeper.h"
#include "GlobalAddress.h"
#include "GLockAddress.h"
#include "LocalAllocator.h"
#include "RdmaBuffer.h"
#include "Directory.h"

#include <emmintrin.h>
#include <x86intrin.h>

class DSMKeeper;
class Directory;

class DSM {

public:
  // obtain netowrk resources for a thread
  void registerThread(int page_size = 1024);

  // clear the network resources for all threads
  void resetThread() { appID.store(0); }

  static DSM *getInstance(const DSMConfig &conf);

  uint16_t getMyNodeID() { return myNodeID; }
  uint16_t getMyThreadID() { return thread_id; }
  uint16_t getClusterSize() { return conf.machineNR; }
  uint64_t getThreadTag() { return thread_tag; }
  uint64_t getDsmSize() { return conf.dsmSize * define::GB; }
  uint64_t getLmSize() { return conf.lockMetaSize * 1024; }
  uint64_t getBaseAddr() { return baseAddr; }
  uint64_t getPeerAddr() { return peerAddr; }

  GlobalAddress getNextGaddr() { 
    // next_loc_curr = (next_loc_curr + 1) % kNextLocCnt;
    // next_gaddr.offset = next_gaddr_base.offset + next_loc_curr * sizeof(uint64_t);
    // next_loc += next_loc_curr;
    return next_gaddr;
  }

  char* spin_on(GlobalAddress lock_addr);

  void getNextGLaddr(GLockAddress *gaddr, GlobalAddress lock_addr) {
    version = (version + 1) % kMaxVersion;
    gaddr->nodeID = myNodeID;
    gaddr->threadID = thread_id;
    gaddr->version = version;
    gaddr->offset = lock_addr.offset + 8;
  }

  uint64_t *getNextLoc(GlobalAddress lock_gaddr) { return (uint64_t *) ((uintptr_t)lockMetaAddr + lock_gaddr.offset + 8); }
  // void reset_nextloc() { next_gaddr.version = (next_gaddr.version + 1) % 16; *next_loc = next_gaddr.val; }
  void set_nextloc(uint64_t val) {
    *next_loc = val;
    _mm_clflushopt(next_loc);
    _mm_mfence();
  }

  // RDMA operations
  // buffer is registered memory
  void read(char *buffer, GlobalAddress gaddr, size_t size, bool signal = true,
            CoroContext *ctx = nullptr);
  void read_sync(char *buffer, GlobalAddress gaddr, size_t size,
                 CoroContext *ctx = nullptr);

  void write(const char *buffer, GlobalAddress gaddr, size_t size,
             bool signal = true, CoroContext *ctx = nullptr, bool from_peer = false);
  void write_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                  CoroContext *ctx = nullptr, bool from_peer = false);
  void write_lm(const char *buffer, GlobalAddress gaddr, size_t size,
             bool signal = true, CoroContext *ctx = nullptr);
  void write_lm_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                      CoroContext *ctx = nullptr);

  void write_batch(RdmaOpRegion *rs, int k, bool signal = true,
                   CoroContext *ctx = nullptr);
  void write_batch_sync(RdmaOpRegion *rs, int k, CoroContext *ctx = nullptr);

  void write_faa(RdmaOpRegion &write_ror, RdmaOpRegion &faa_ror,
                 uint64_t add_val, bool signal = true,
                 CoroContext *ctx = nullptr);
  void write_faa_sync(RdmaOpRegion &write_ror, RdmaOpRegion &faa_ror,
                      uint64_t add_val, CoroContext *ctx = nullptr);

  void write_cas(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror, uint64_t equal,
                 uint64_t val, bool signal = true, CoroContext *ctx = nullptr);
  void write_cas_sync(RdmaOpRegion &write_ror, RdmaOpRegion &cas_ror,
                      uint64_t equal, uint64_t val, CoroContext *ctx = nullptr);

  void write_peer(const char *buffer, GlobalAddress gaddr, size_t size,
                  bool signal, CoroContext *ctx, bool from_peer = false);

  void write_peer_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                      CoroContext *ctx, bool from_peer = false);

  void cas(GlobalAddress gaddr, uint64_t equal, uint64_t val,
           uint64_t *rdma_buffer, bool signal = true,
           CoroContext *ctx = nullptr);
  bool cas_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer, CoroContext *ctx = nullptr);

  void cas_peer(GLockAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer, bool signal, CoroContext *ctx);

  bool cas_peer_sync(GLockAddress gaddr, uint64_t equal, uint64_t val,
                    uint64_t *rdma_buffer, CoroContext *ctx);
  void cas_read(RdmaOpRegion &cas_ror, RdmaOpRegion &read_ror, uint64_t equal,
                uint64_t val, bool signal = true, CoroContext *ctx = nullptr);
  bool cas_read_sync(RdmaOpRegion &cas_ror, RdmaOpRegion &read_ror,
                     uint64_t equal, uint64_t val, CoroContext *ctx = nullptr);

  void cas_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer, uint64_t mask = ~(0ull),
                bool signal = true);
  bool cas_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                     uint64_t *rdma_buffer, uint64_t mask = ~(0ull));

  void faa_boundary(GlobalAddress gaddr, uint64_t add_val,
                    uint64_t *rdma_buffer, uint64_t mask = 63,
                    bool signal = true, CoroContext *ctx = nullptr);
  void faa_boundary_sync(GlobalAddress gaddr, uint64_t add_val,
                         uint64_t *rdma_buffer, uint64_t mask = 63,
                         CoroContext *ctx = nullptr);

  // for on-chip device memory
  void read_dm(char *buffer, GlobalAddress gaddr, size_t size,
               bool signal = true, CoroContext *ctx = nullptr);
  void read_dm_sync(char *buffer, GlobalAddress gaddr, size_t size,
                    CoroContext *ctx = nullptr);

  void write_dm(const char *buffer, GlobalAddress gaddr, size_t size,
                bool signal = true, CoroContext *ctx = nullptr);
  void write_dm_sync(const char *buffer, GlobalAddress gaddr, size_t size,
                     CoroContext *ctx = nullptr);

  void cas_dm(GlobalAddress gaddr, uint64_t equal, uint64_t val,
              uint64_t *rdma_buffer, bool signal = true,
              CoroContext *ctx = nullptr);
  bool cas_dm_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, CoroContext *ctx = nullptr);

  void cas_dm_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, uint64_t mask = ~(0ull),
                   bool signal = true);
  bool cas_dm_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                        uint64_t *rdma_buffer, uint64_t mask = ~(0ull));

  void faa_dm_boundary(GlobalAddress gaddr, uint64_t add_val,
                       uint64_t *rdma_buffer, uint64_t mask = 63,
                       bool signal = true, CoroContext *ctx = nullptr);
  void faa_dm_boundary_sync(GlobalAddress gaddr, uint64_t add_val,
                            uint64_t *rdma_buffer, uint64_t mask = 63,
                            CoroContext *ctx = nullptr);

void faa_dm(GlobalAddress gaddr, uint64_t add_val,
                          uint64_t *rdma_buffer, bool signal,
                          CoroContext *ctx);

void faa_dm_sync(GlobalAddress gaddr, uint64_t add_val,
                               uint64_t *rdma_buffer, CoroContext *ctx);

  uint64_t poll_rdma_cq(int count = 1);
  bool poll_rdma_cq_once(uint64_t &wr_id);

  void wait_for_peer(GLockAddress gaddr, int tid);
  void wakeup_peer(GLockAddress gaddr, int tid);

  uint64_t sum(uint64_t value) {
    static uint64_t count = 0;
    return keeper->sum(std::string("sum-") + std::to_string(count++), value);
  }

  // Memcached operations for sync
  size_t Put(uint64_t key, const void *value, size_t count) {

    std::string k = std::string("gam-") + std::to_string(key);
    keeper->memSet(k.c_str(), k.size(), (char *)value, count);
    return count;
  }

  size_t Get(uint64_t key, void *value) {

    std::string k = std::string("gam-") + std::to_string(key);
    size_t size;
    char *ret = keeper->memGet(k.c_str(), k.size(), &size);
    memcpy(value, ret, size);

    return size;
  }

private:
  DSM(const DSMConfig &conf);
  ~DSM();

  void initRDMAConnection();
  void fill_keys_dest(RdmaOpRegion &ror, GlobalAddress addr, bool is_chip, bool is_lockMeta = false, bool is_peer = false);

  DSMConfig conf;
  std::atomic_int appID;
  Cache cache;

  static thread_local int thread_id;
  static thread_local uint16_t version;
  static thread_local ThreadConnection *iCon;
  static thread_local char *rdma_buffer;
  static thread_local LocalAllocator local_allocator;
  static thread_local RdmaBuffer rbuf[define::kMaxCoro];
  static thread_local uint64_t thread_tag;
  static thread_local GlobalAddress next_gaddr;
  static thread_local uint64_t *next_loc;
  
  uint64_t baseAddr;
  uint64_t rlockAddr;
  uint64_t lockMetaAddr;
  uint64_t peerAddr;
  uint32_t myNodeID;
  int kNextLocCnt = 10;
  int kMaxVersion = 0xFFFF;

  RemoteConnection *remoteInfo;
  ThreadConnection *thCon[MAX_APP_THREAD];
  DirectoryConnection *dirCon[NR_DIRECTORY];
  DSMKeeper *keeper;

  Directory *dirAgent[NR_DIRECTORY];
  std::thread *lockTh;

public:
  bool is_register() { return thread_id != -1; }
  void barrier(const std::string &ss) { keeper->barrier(ss); }

  DSMKeeper *get_DSMKeeper() { return keeper; }
  char *get_rdma_buffer() { return rdma_buffer; }
  RdmaBuffer &get_rbuf(int coro_id) { return rbuf[coro_id]; }
  uint64_t get_baseAddr() { return baseAddr; }
  uint64_t get_rlockAddr() { return rlockAddr; }
  DSMConfig getConf() { return conf; }
  void stopDirThread() { dirAgent[0]->stopDirThread.store(true);
    struct ibv_send_wr wr = {};
    struct ibv_send_wr *bad_wr = nullptr;
    ibv_post_send(dirAgent[0]->getDCon()->data2app[0][0], &wr, &bad_wr);
  }
  void lockThread();

  GlobalAddress alloc(size_t size);
  void free_(GlobalAddress addr);
  void free_dsm();

  void rpc_call_dir(const RawMessage &m, uint16_t node_id,
                    uint16_t dir_id = 0) {

    auto buffer = (RawMessage *)iCon->message->getSendPool();

    memcpy(buffer, &m, sizeof(RawMessage));
    buffer->node_id = myNodeID;
    buffer->app_id = thread_id;

    iCon->sendMessage2Dir(buffer, node_id, dir_id);
  }

  RawMessage *rpc_wait() {
    ibv_wc wc;

    pollWithCQ(iCon->rpc_cq, 1, &wc);
    return (RawMessage *)iCon->message->getMessage();
  }
};

inline GlobalAddress DSM::alloc(size_t size) {

  thread_local int next_target_node =
      (getMyThreadID() + getMyNodeID()) % conf.mnNR;
  thread_local int next_target_dir_id =
      (getMyThreadID() + getMyNodeID()) % NR_DIRECTORY;

  bool need_chunk = false;
  auto addr = local_allocator.malloc(size, need_chunk);
  if (need_chunk) {
    RawMessage m;
    m.type = RpcType::MALLOC;

    this->rpc_call_dir(m, next_target_node, next_target_dir_id);
    local_allocator.set_chunck(rpc_wait()->addr);

    if (++next_target_dir_id == NR_DIRECTORY) {
      next_target_node = (next_target_node + 1) % conf.mnNR;
      next_target_dir_id = 0;
    }

    // retry
    addr = local_allocator.malloc(size, need_chunk);
  }

  return addr;
}

inline void DSM::free_(GlobalAddress addr) { local_allocator.free_(addr); }
#endif /* __DSM_H__ */
