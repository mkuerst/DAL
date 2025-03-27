#if !defined(_TREE_H_)
#define _TREE_H_

#include "DSM.h"
#include <atomic>
#include <city.h>
#include <functional>
#include <iostream>
#include "mb_utils.h"
#include <pthread.h>
#include <signal.h>

class IndexCache;

// struct alignas(CACHELINE_SIZE) LitlLock {
//     pthread_mutex_t mutex;
//     uint64_t safe1, safe2;
//     char disa = 'y';
// };
// struct alignas(CACHELINE_SIZE) LocalLockNode {
//     pthread_mutex_t mutex;
//     uint64_t safe1, safe2;
//     char disa = 'y';
//     std::atomic<uint64_t> ticket_lock;
//     bool hand_over;
//     uint8_t hand_time;
// };
struct LitlLock {
    pthread_mutex_t mutex;
    uint64_t safe1, safe2;
    char disa = 'y';
};

struct LocalLockNode {
    std::atomic<uint64_t> ticket_lock;
    bool hand_over;
    int level;
    uint8_t hand_time;
    char *page_buffer = nullptr;
    GlobalAddress page_addr;
    uint64_t size = 0;
    bool write_back = false;
    bool unlock_addr = false;
    bool safe = false;
    LitlLock litl_lock;

    void debug() const {
        cerr << "curr_lock_node:" << endl;
        cerr << "page_addr :" << page_addr << endl;
        cerr << "page_buffer :" << (uintptr_t) page_buffer << endl;
        cerr << "level: " << (int)level << endl;
    }
};


struct Measurements {
    uint16_t *lock_hold;
    uint16_t *lwait_acq;
    uint16_t *lwait_rel;
    uint16_t *gwait_acq;
    uint16_t *gwait_rel;
    uint16_t *data_read;
    uint16_t *data_write;
    uint16_t *end_to_end;
    uint32_t *lock_acqs;
    uint64_t loop_in_cs[MAX_APP_THREAD];
    uint64_t tp[MAX_APP_THREAD];
    uint64_t la[MAX_APP_THREAD];
    uint64_t glock_tries[MAX_APP_THREAD];
    uint64_t handovers[MAX_APP_THREAD];
    uint64_t handovers_data[MAX_APP_THREAD];
    uint64_t cache_misses[MAX_APP_THREAD];
    uint64_t c_ho[MAX_APP_THREAD];
    uint64_t c_hod[MAX_APP_THREAD];
    uint64_t duration;
};


struct Request {
  bool is_search;
  Key k;
  Value v;
};

class RequstGen {
public:
  RequstGen() = default;
  virtual Request next() { return Request{}; }
};

using CoroFunc = std::function<RequstGen *(int, DSM *, int)>;

struct SearchResult {
  bool is_leaf;
  uint8_t level;
  GlobalAddress slibing;
  GlobalAddress next_level;
  Value val;
};

class InternalPage;
class LeafPage;
class Tree {

public:
    Tree(DSM *dsm, uint16_t tree_id = 0, uint32_t lockNR = define::kNumOfLock, bool MB = false);

    void insert(const Key &k, const Value &v, CoroContext *cxt = nullptr,
                int coro_id = 0);
    bool search(const Key &k, Value &v, CoroContext *cxt = nullptr,
                int coro_id = 0);
    void del(const Key &k, CoroContext *cxt = nullptr, int coro_id = 0);

    uint64_t range_query(const Key &from, const Key &to, Value *buffer,
                        CoroContext *cxt = nullptr, int coro_id = 0);

    void print_and_check_tree(CoroContext *cxt = nullptr, int coro_id = 0);

    void run_coroutine(CoroFunc func, int id, int coro_cnt);

    void lock_bench(const Key &k, CoroContext *cxt = nullptr, int coro_id = 0);

    void index_cache_statistics();
    void clear_statistics();

    /*ADDED*/
    void mb_lock(GlobalAddress base_addr, GlobalAddress lock_addr, int data_size);
    void mb_unlock(GlobalAddress base_addr, int data_size);
    char *getCurrPB() { return curr_page_buffer; }
    GlobalAddress getCurrLockAddr() { return curr_lock_addr; }
    void setKPageSize(int page_size); 
    void set_IDs(int nid, int tid);
    uint32_t getLockNR() { return lockNR; }

    /*TESTING FUNCTIONS*/
    void wait();
    void contact();
    uint64_t test_self_cas(GLockAddress gaddr, bool with_read);
    uint64_t node0(uint64_t cnt);
    uint64_t node1(uint64_t cnt);
    void test_write_peer();
    void test_spin();

    int randNodeID(uint64_t value);
    /**/

private:
    DSM *dsm;
    uint64_t tree_id;
    GlobalAddress root_ptr_ptr; // the address which stores root pointer;
    /*ADDED*/
    uint64_t rlockAddr;
    /**/

    // static thread_local int coro_id;
    static thread_local CoroCall worker[define::kMaxCoro];
    static thread_local CoroCall master;

    LocalLockNode *local_locks[MAX_MACHINE];
    LitlLock *litl_locks;

    IndexCache *index_cache;

    /*ADDED*/
    uint32_t lockNR;
    static thread_local uint64_t *curr_cas_buffer;
    static thread_local char *curr_page_buffer;
    static thread_local GlobalAddress curr_lock_addr;
    static thread_local LocalLockNode *curr_lock_node;
    static thread_local GLockAddress next_gaddr;
    static thread_local GLockAddress version_addr;
    static thread_local GLockAddress expected_addr;
    static thread_local int threadID;
    static thread_local uint64_t nodeID;
    static thread_local uint64_t lockMeta;
    static thread_local bool from_peer;
    /**/

    void print_verbose();

    void before_operation(CoroContext *cxt, int coro_id);

    GlobalAddress get_root_ptr_ptr();
    GlobalAddress get_root_ptr(CoroContext *cxt, int coro_id);

    void coro_worker(CoroYield &yield, RequstGen *gen, int coro_id);
    void coro_master(CoroYield &yield, int coro_cnt);

    void broadcast_new_root(GlobalAddress new_root_addr, int root_level);
    bool update_new_root(GlobalAddress left, const Key &k, GlobalAddress right,
                        int level, GlobalAddress old_root, CoroContext *cxt,
                        int coro_id);

    void insert_internal(const Key &k, GlobalAddress v, CoroContext *cxt,
                        int coro_id, int level);

    bool try_lock_addr(GlobalAddress lock_addr, uint64_t tag, uint64_t *buf,
                        CoroContext *cxt, int coro_id);
    void unlock_addr(GlobalAddress lock_addr, uint64_t tag, uint64_t *buf,
                    CoroContext *cxt, int coro_id, bool async, char *page_buf=nullptr, GlobalAddress page_addr=GlobalAddress::Null(),
                    int level = 0);
    void write_page_and_unlock(char *page_buffer, GlobalAddress page_addr,
                                int page_size, uint64_t *cas_buffer,
                                GlobalAddress lock_addr, uint64_t tag,
                                CoroContext *cxt, int coro_id, bool async, int level,
                                char* orig_pbuf, GlobalAddress orig_paddr);
    bool lock_and_read_page(char **page_buffer, GlobalAddress page_addr,
                            int page_size, uint64_t *cas_buffer,
                            GlobalAddress lock_addr, uint64_t tag,
                            CoroContext *cxt, int coro_id, int level = 0);

    bool page_search(GlobalAddress page_addr, const Key &k, SearchResult &result,
                    CoroContext *cxt, int coro_id, bool from_cache = false);
    void internal_page_search(InternalPage *page, const Key &k,
                            SearchResult &result);
    void leaf_page_search(LeafPage *page, const Key &k, SearchResult &result);

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
    /*ADDED*/
    GlobalAddress get_lock_addr(GlobalAddress base_addr);
    void get_bufs();
    /**/
};

class Header {
private:
    GlobalAddress leftmost_ptr;
    GlobalAddress sibling_ptr;
    uint8_t level;
    int16_t last_index;
    Key lowest;
    Key highest;

    friend class InternalPage;
    friend class LeafPage;
    friend class Tree;
    friend class IndexCache;

public:
    Header() {
        leftmost_ptr = GlobalAddress::Null();
        sibling_ptr = GlobalAddress::Null();
        last_index = -1;
        lowest = kKeyMin;
        highest = kKeyMax;
    }

    void debug() const {
        cerr << "page: " << endl;
        std::cerr << "leftmost=" << leftmost_ptr << ", "
                << "sibling=" << sibling_ptr << ", "
                << "level=" << (int)level << ","
                << "cnt=" << last_index + 1 << ","
                << "range=[" << lowest << " - " << highest << "]" << std::endl;
    }
} __attribute__((packed));


class InternalEntry {
public:
  Key key;
  GlobalAddress ptr;

  InternalEntry() {
    ptr = GlobalAddress::Null();
    key = 0;
  }
} __attribute__((packed));

class LeafEntry {
public:
  uint8_t f_version : 4;
  Key key;
  Value value;
  uint8_t r_version : 4;

  LeafEntry() {
    f_version = 0;
    r_version = 0;
    value = kValueNull;
    key = 0;
  }
} __attribute__((packed));

constexpr int kInternalCardinality = (kInternalPageSize - sizeof(Header) -
                                      sizeof(uint8_t) * 2 - sizeof(uint64_t)) /
                                     sizeof(InternalEntry);

constexpr int kLeafCardinality =
    (kLeafPageSize - sizeof(Header) - sizeof(uint8_t) * 2 - sizeof(uint64_t)) /
    sizeof(LeafEntry);

class InternalPage {
private:
  union {
    uint32_t crc;
    uint64_t embedding_lock;
    uint64_t index_cache_freq;
  };

  uint8_t front_version;
  Header hdr;
  InternalEntry records[kInternalCardinality];

  // uint8_t padding[3];
  uint8_t rear_version;

  friend class Tree;
  friend class IndexCache;

public:
  // this is called when tree grows
  InternalPage(GlobalAddress left, const Key &key, GlobalAddress right,
               uint32_t level = 0) {
    hdr.leftmost_ptr = left;
    hdr.level = level;
    records[0].key = key;
    records[0].ptr = right;
    records[1].ptr = GlobalAddress::Null();

    hdr.last_index = 0;

    front_version = 0;
    rear_version = 0;
  }

  InternalPage(uint32_t level = 0) {
    hdr.level = level;
    records[0].ptr = GlobalAddress::Null();

    front_version = 0;
    rear_version = 0;

    embedding_lock = 0;
  }

  void set_consistent() {
    front_version++;
    rear_version = front_version;
#ifdef CONFIG_ENABLE_CRC
    this->crc =
        CityHash32((char *)&front_version, (&rear_version) - (&front_version));
#endif
  }

  bool check_consistent() const {

    bool succ = true;
    #ifdef CONFIG_ENABLE_CRC
        auto cal_crc =
            CityHash32((char *)&front_version, (&rear_version) - (&front_version));
        succ = cal_crc == this->crc;
    #endif
        succ = succ && (rear_version == front_version);

        return succ;
    }

    void debug() const {
        std::cout << "InternalPage@ ";
        hdr.debug();
        std::cout << "version: [" << (int)front_version << ", " << (int)rear_version
                << "]" << std::endl;
    }

    void verbose_debug() const {
        this->debug();
        for (int i = 0; i < this->hdr.last_index + 1; ++i) {
        printf("[%lu %lu] ", this->records[i].key, this->records[i].ptr.val);
        }
        printf("\n");
    }

} __attribute__((packed));

class LeafPage {
private:
    union {
        uint32_t crc;
        uint64_t embedding_lock;
    };
    uint8_t front_version;
    Header hdr;
    LeafEntry records[kLeafCardinality];

    // uint8_t padding[1];
    uint8_t rear_version;

    friend class Tree;

public:
    LeafPage(uint32_t level = 0) {
        hdr.level = level;
        records[0].value = kValueNull;

        front_version = 0;
        rear_version = 0;

        embedding_lock = 0;
    }

    void set_consistent() {
        front_version++;
        rear_version = front_version;
#ifdef CONFIG_ENABLE_CRC
        this->crc =
            CityHash32((char *)&front_version, (&rear_version) - (&front_version));
#endif
    }

    bool check_consistent() const {

        bool succ = true;
#ifdef CONFIG_ENABLE_CRC
        auto cal_crc =
            CityHash32((char *)&front_version, (&rear_version) - (&front_version));
        succ = cal_crc == this->crc;
#endif

        succ = succ && (rear_version == front_version);

        return succ;
    }

    void debug() const {
        std::cout << "LeafPage@ ";
        hdr.debug();
        std::cout << "version: [" << (int)front_version << ", " << (int)rear_version
                << "]" << std::endl;
    }

} __attribute__((packed));

#endif // _TREE_H_
