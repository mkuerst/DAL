
#if !defined(_RDMA_OPS_H_)
#define _RDMA_OPS_H_

#include "DSM.h"
#include <atomic>
#include <city.h>
#include <functional>
#include <iostream>

class IndexCache;

struct LocalLockNode {
  std::atomic<uint64_t> ticket_lock;
  bool hand_over;
  uint8_t hand_time;
};


class RDMA_if {

public:
  RDMA_if(DSM *dsm, uint16_t tree_id = 0);


private:
  DSM *dsm;
  uint64_t tree_id;
  GlobalAddress root_ptr_ptr; // the address which stores root pointer;

  LocalLockNode *local_locks[MAX_MACHINE];

  IndexCache *index_cache;

  void print_verbose();

  void before_operation(CoroContext *cxt, int coro_id);

  GlobalAddress get_root_ptr_ptr();
  GlobalAddress get_root_ptr(CoroContext *cxt, int coro_id);

  bool try_lock_addr(GlobalAddress lock_addr, uint64_t tag, uint64_t *buf,
                     CoroContext *cxt, int coro_id);
  void unlock_addr(GlobalAddress lock_addr, uint64_t tag, uint64_t *buf,
                   CoroContext *cxt, int coro_id, bool async);
  void write_page_and_unlock(char *page_buffer, GlobalAddress page_addr,
                             int page_size, uint64_t *cas_buffer,
                             GlobalAddress lock_addr, uint64_t tag,
                             CoroContext *cxt, int coro_id, bool async);
  void lock_and_read_page(char *page_buffer, GlobalAddress page_addr,
                          int page_size, uint64_t *cas_buffer,
                          GlobalAddress lock_addr, uint64_t tag,
                          CoroContext *cxt, int coro_id);

  bool page_search(GlobalAddress page_addr, const Key &k, SearchResult &result,
                   CoroContext *cxt, int coro_id, bool from_cache = false);

  void internal_page_store(GlobalAddress page_addr, const Key &k,
                           GlobalAddress value, GlobalAddress root, int level,
                           CoroContext *cxt, int coro_id);
  bool leaf_page_store(GlobalAddress page_addr, const Key &k, const Value &v,
                       GlobalAddress root, int level, CoroContext *cxt,
                       int coro_id, bool from_cache = false);
  bool leaf_page_del(GlobalAddress page_addr, const Key &k, int level,
                     CoroContext *cxt, int coro_id, bool from_cache = false);

  bool acquire_local_lock(GlobalAddress lock_addr, CoroContext *cxt,
                          int coro_id);
  bool can_hand_over(GlobalAddress lock_addr);
  void releases_local_lock(GlobalAddress lock_addr);
};

#endif // _RMDA_OPS_H