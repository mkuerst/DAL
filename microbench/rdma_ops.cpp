#include "rdma_ops.h"
#include "IndexCache.h"
#include "RdmaBuffer.h"
#include "Timer.h"

#include <algorithm>
#include <city.h>
#include <iostream>
#include <queue>
#include <utility>
#include <vector>

inline bool try_lock_addr(GlobalAddress lock_addr, uint64_t tag,
                                uint64_t *buf, CoroContext *cxt, int coro_id) {

  bool hand_over = acquire_local_lock(lock_addr, cxt, coro_id);
  if (hand_over) {
    return true;
  }

  {

    uint64_t retry_cnt = 0;
    uint64_t pre_tag = 0;
    uint64_t conflict_tag = 0;
  retry:
    retry_cnt++;
    if (retry_cnt > 1000000) {
      std::cout << "Deadlock " << lock_addr << std::endl;

      std::cout << dsm->getMyNodeID() << ", " << dsm->getMyThreadID()
                << " locked by " << (conflict_tag >> 32) << ", "
                << (conflict_tag << 32 >> 32) << std::endl;
      assert(false);
    }

    bool res = dsm->cas_dm_sync(lock_addr, 0, tag, buf, cxt);

    if (!res) {
      conflict_tag = *buf - 1;
      if (conflict_tag != pre_tag) {
        retry_cnt = 0;
        pre_tag = conflict_tag;
      }
      goto retry;
    }
  }

  return true;
}

bool Tree::leaf_page_store(GlobalAddress page_addr, const Key &k,
                           const Value &v, GlobalAddress root, int level,
                           CoroContext *cxt, int coro_id, bool from_cache) {

  uint64_t lock_index =
      CityHash64((char *)&page_addr, sizeof(page_addr)) % define::kNumOfLock;

  GlobalAddress lock_addr;

#ifdef CONFIG_ENABLE_EMBEDDING_LOCK
  lock_addr = page_addr;
#else
  lock_addr.nodeID = page_addr.nodeID;
  lock_addr.offset = lock_index * sizeof(uint64_t);
#endif

  auto &rbuf = dsm->get_rbuf(coro_id);
  uint64_t *cas_buffer = rbuf.get_cas_buffer();
  auto page_buffer = rbuf.get_page_buffer();

  auto tag = dsm->getThreadTag();
  assert(tag != 0);

  lock_and_read_page(page_buffer, page_addr, kLeafPageSize, cas_buffer,
                     lock_addr, tag, cxt, coro_id);

  auto page = (LeafPage *)page_buffer;

  assert(page->hdr.level == level);
  assert(page->check_consistent());

  if (from_cache &&
      (k < page->hdr.lowest || k >= page->hdr.highest)) { // cache is stale
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true);
    return false;
  }

  if (k >= page->hdr.highest) {

    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true);
    assert(page->hdr.sibling_ptr != GlobalAddress::Null());
    this->leaf_page_store(page->hdr.sibling_ptr, k, v, root, level, cxt,
                          coro_id);
    return true;
  }
  assert(k >= page->hdr.lowest);

  int cnt = 0;
  int empty_index = -1;
  char *update_addr = nullptr;
  for (int i = 0; i < kLeafCardinality; ++i) {

    auto &r = page->records[i];
    if (r.value != kValueNull) {
      cnt++;
      if (r.key == k) {
        r.value = v;
        r.f_version++;
        r.r_version = r.f_version;
        update_addr = (char *)&r;
        break;
      }
    } else if (empty_index == -1) {
      empty_index = i;
    }
  }

  assert(cnt != kLeafCardinality);

  if (update_addr == nullptr) { // insert new item
    if (empty_index == -1) {
      printf("%d cnt\n", cnt);
      assert(false);
    }

    auto &r = page->records[empty_index];
    r.key = k;
    r.value = v;
    r.f_version++;
    r.r_version = r.f_version;

    update_addr = (char *)&r;

    cnt++;
  }

  bool need_split = cnt == kLeafCardinality;
  if (!need_split) {
    assert(update_addr);
    write_page_and_unlock(
        update_addr, GADD(page_addr, (update_addr - (char *)page)),
        sizeof(LeafEntry), cas_buffer, lock_addr, tag, cxt, coro_id, false);

    return true;
  } else {
    std::sort(
        page->records, page->records + kLeafCardinality,
        [](const LeafEntry &a, const LeafEntry &b) { return a.key < b.key; });
  }

  Key split_key;
  GlobalAddress sibling_addr;
  if (need_split) { // need split
    sibling_addr = dsm->alloc(kLeafPageSize);
    auto sibling_buf = rbuf.get_sibling_buffer();

    auto sibling = new (sibling_buf) LeafPage(page->hdr.level);

    // std::cout << "addr " <<  sibling_addr << " | level " <<
    // (int)(page->hdr.level) << std::endl;

    int m = cnt / 2;
    split_key = page->records[m].key;
    assert(split_key > page->hdr.lowest);
    assert(split_key < page->hdr.highest);

    for (int i = m; i < cnt; ++i) { // move
      sibling->records[i - m].key = page->records[i].key;
      sibling->records[i - m].value = page->records[i].value;
      page->records[i].key = 0;
      page->records[i].value = kValueNull;
    }
    page->hdr.last_index -= (cnt - m);
    sibling->hdr.last_index += (cnt - m);

    sibling->hdr.lowest = split_key;
    sibling->hdr.highest = page->hdr.highest;
    page->hdr.highest = split_key;

    // link
    sibling->hdr.sibling_ptr = page->hdr.sibling_ptr;
    page->hdr.sibling_ptr = sibling_addr;

    sibling->set_consistent();
    dsm->write_sync(sibling_buf, sibling_addr, kLeafPageSize, cxt);
  }

  page->set_consistent();

  write_page_and_unlock(page_buffer, page_addr, kLeafPageSize, cas_buffer,
                        lock_addr, tag, cxt, coro_id, need_split);

  if (!need_split)
    return true;

  if (root == page_addr) { // update root
    if (update_new_root(page_addr, split_key, sibling_addr, level + 1, root,
                        cxt, coro_id)) {
      return true;
    }
  }

  auto up_level = path_stack[coro_id][level + 1];

  if (up_level != GlobalAddress::Null()) {
    internal_page_store(up_level, split_key, sibling_addr, root, level + 1, cxt,
                        coro_id);
  } else {
    assert(from_cache);
    insert_internal(split_key, sibling_addr, cxt, coro_id, level + 1);
  }

  return true;
}