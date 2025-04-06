#include "Tree.h"
#include "IndexCache.h"
#include "RdmaBuffer.h"
#include "Timer.h"

#include <algorithm>
#include <city.h>
#include <iostream>
#include <queue>
#include <utility>
#include <vector>
#include <bitset>

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace std;

bool enter_debug = false;

uint64_t cache_miss[MAX_APP_THREAD][8];
uint64_t cache_hit[MAX_APP_THREAD][8];
// uint64_t latency[MAX_APP_THREAD][LATENCY_WINDOWS];

thread_local CoroCall Tree::worker[define::kMaxCoro];
thread_local CoroCall Tree::master;
thread_local GlobalAddress path_stack[define::kMaxCoro]
                                     [define::kMaxLevelOfTree];

thread_local Timer timer;
thread_local std::queue<uint16_t> hot_wait_queue;
thread_local int Tree::threadID;
thread_local uint64_t Tree::nodeID;

thread_local char* Tree::curr_page_buffer = nullptr;
thread_local uint64_t* Tree::curr_cas_buffer = nullptr;
thread_local GlobalAddress Tree::curr_lock_addr;
thread_local LocalLockNode *Tree::ln;
thread_local GLockAddress Tree::next_gaddr;
thread_local GLockAddress Tree::version_addr;
thread_local GLockAddress Tree::expected_addr = GLockAddress::Null();
thread_local uint64_t Tree::lockMeta = 0;
thread_local bool Tree::from_peer = false;


Measurements measurements;


Tree::Tree(DSM *dsm, uint16_t tree_id, uint32_t lockNR, bool MB, uint16_t mHo) : dsm(dsm), tree_id(tree_id), lockNR(lockNR), maxHandover(mHo)  {

  measurements.lock_hold = (uint16_t *) malloc(MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  memset(measurements.lock_hold, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));

  measurements.end_to_end = (uint16_t *) malloc(MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  memset(measurements.end_to_end, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  
  measurements.lwait_acq = (uint16_t *) malloc(MAX_APP_THREAD * LWAIT_WINDOWS * sizeof(uint16_t));
  memset(measurements.lwait_acq, 0, MAX_APP_THREAD * LWAIT_WINDOWS * sizeof(uint16_t));
  
  measurements.lwait_rel = (uint16_t *) malloc(MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  memset(measurements.lwait_rel, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  
  measurements.gwait_acq = (uint16_t *) malloc(MAX_APP_THREAD * LWAIT_WINDOWS * sizeof(uint16_t));
  memset(measurements.gwait_acq, 0, MAX_APP_THREAD * LWAIT_WINDOWS * sizeof(uint16_t));
  
  measurements.gwait_rel = (uint16_t *) malloc(MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  memset(measurements.gwait_rel, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  
  measurements.data_read = (uint16_t *) malloc(MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  memset(measurements.data_read, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  
  measurements.data_write = (uint16_t *) malloc(MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
  memset(measurements.data_write, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));

  measurements.lock_acqs = (uint32_t *) malloc(MAX_MACHINE * lockNR * sizeof(uint32_t));
  memset(measurements.lock_acqs, 0, MAX_MACHINE * lockNR * sizeof(uint32_t));

  // DEB("allocated measurements object: nodeID %d\n", dsm->getMyNodeID());

    for (int i = 0; i < dsm->getClusterSize(); ++i) {
        local_locks[i] = new LocalLockNode[lockNR];
        for (size_t k = 0; k < lockNR; ++k) {
            auto &n = local_locks[i][k];
            n.ticket_lock.store(0);
            n.hand_over = false;
            n.hand_time = 0;
        }
    }
    rlockAddr = dsm->get_rlockAddr();

    if (!MB) {
        assert(dsm->is_register());
        // print_verbose();

        index_cache = new IndexCache(define::kIndexCacheSize);

        root_ptr_ptr = get_root_ptr_ptr();

        // try to init tree and install root pointer
        auto page_buffer = (dsm->get_rbuf(0)).get_page_buffer();
        auto root_addr = dsm->alloc(kLeafPageSize);
        auto root_page = new (page_buffer) LeafPage;

        root_page->set_consistent();
        dsm->write_sync(page_buffer, root_addr, kLeafPageSize, nullptr, false);

        auto cas_buffer = (dsm->get_rbuf(0)).get_cas_buffer();
        bool res = dsm->cas_sync(root_ptr_ptr, 0, root_addr.val, cas_buffer);
        if (res) {
            std::cout << "Tree root pointer value " << root_addr << std::endl;
            treeNodeNR++;
        } else {
            std::cout << "fail\n";
        }
    }
    print_verbose();
}

void Tree::print_verbose() {

  int kLeafHdrOffset = STRUCT_OFFSET(LeafPage, hdr);
  int kInternalHdrOffset = STRUCT_OFFSET(InternalPage, hdr);
  if (kLeafHdrOffset != kInternalHdrOffset) {
    std::cerr << "format error" << std::endl;
  }

  if (dsm->getMyNodeID() == 0) {
    std::cout << "Header size: " << sizeof(Header) << std::endl;
    std::cout << "Internal Page size: " << sizeof(InternalPage) << " ["
              << kInternalPageSize << "]" << std::endl;
    std::cout << "Internal per Page: " << kInternalCardinality << std::endl;
    std::cout << "Leaf Page size: " << sizeof(LeafPage) << " [" << kLeafPageSize
              << "]" << std::endl;
    std::cout << "Leaf per Page: " << kLeafCardinality << std::endl;
    std::cout << "LeafEntry size: " << sizeof(LeafEntry) << std::endl;
    std::cout << "InternalEntry size: " << sizeof(InternalEntry) << std::endl;
  }
}

inline void Tree::before_operation(CoroContext *cxt, int coro_id) {
  for (size_t i = 0; i < define::kMaxLevelOfTree; ++i) {
    path_stack[coro_id][i] = GlobalAddress::Null();
  }
}

GlobalAddress Tree::get_root_ptr_ptr() {
  GlobalAddress addr;
  addr.nodeID = 0;
  addr.offset =
      define::kRootPointerStoreOffest + sizeof(GlobalAddress) * tree_id;

  return addr;
}

extern GlobalAddress g_root_ptr;
extern int g_root_level;
extern bool enable_cache;
GlobalAddress Tree::get_root_ptr(CoroContext *cxt, int coro_id) {

  if (g_root_ptr == GlobalAddress::Null()) {
    auto page_buffer = (dsm->get_rbuf(coro_id)).get_page_buffer();
    dsm->read_sync(page_buffer, root_ptr_ptr, sizeof(GlobalAddress), cxt);
    GlobalAddress root_ptr = *(GlobalAddress *)page_buffer;
    return root_ptr;
  } else {
    return g_root_ptr;
  }

  // std::cout << "root ptr " << root_ptr << std::endl;
}


void Tree::broadcast_new_root(GlobalAddress new_root_addr, int root_level) {
  RawMessage m;
  m.type = RpcType::NEW_ROOT;
  m.addr = new_root_addr;
  m.level = root_level;
  for (int i = 0; i < dsm->getClusterSize(); ++i) {
    dsm->rpc_call_dir(m, i);
  }
}

bool Tree::update_new_root(GlobalAddress left, const Key &k,
                           GlobalAddress right, int level,
                           GlobalAddress old_root, CoroContext *cxt,
                           int coro_id) {

  auto page_buffer = dsm->get_rbuf(coro_id).get_page_buffer();
  auto cas_buffer = dsm->get_rbuf(coro_id).get_cas_buffer();
  auto new_root = new (page_buffer) InternalPage(left, k, right, level);

  auto new_root_addr = dsm->alloc(kInternalPageSize);

  new_root->set_consistent();
  dsm->write_sync(page_buffer, new_root_addr, kInternalPageSize, cxt, false);
  if (dsm->cas_sync(root_ptr_ptr, old_root, new_root_addr, cas_buffer, cxt)) {
    broadcast_new_root(new_root_addr, level);
    std::cout << "new root level " << level << " " << new_root_addr
              << std::endl;
    return true;
  } else {
    std::cout << "cas root fail " << std::endl;
  }
  treeNodeNR++;

  return false;
}

void Tree::generate_graphviz() {
  auto root = get_root_ptr(nullptr, 0);
  GlobalAddress p = root;
  int level_cnt = 0;
  auto page_buffer = (dsm->get_rbuf(0)).get_page_buffer();

  ofstream file("/nfs/DAL/microbench/bplustree.dot");
  file << "digraph BPlusTree {\n";
  file << "  node [shape=box];\n";
  file << "  rankdir=TB;\n"; // Vertical layout

  map<uint64_t, vector<uint64_t>> parentEdges; // Parent -> Children
  map<uint64_t, uint64_t> siblingEdges; // Node -> Sibling
  map<uint64_t, vector<uint64_t>> nodeKeys; // Node Address -> Keys
  map<uint64_t, vector<uint64_t>> nodeValues; // Node Address -> Values

  map<uint64_t, uint64_t> nodeIDs; // Node Address -> Node IDs
  map<int, vector<uint64_t>> levelNodes; // Levels -> Node Addr
  map<uint64_t, GlobalAddress> leftmostPtrs; // Levels -> leftmost Addr

  dsm->read_sync(page_buffer, p, kLeafPageSize);
  auto header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));
  uint64_t id = 0;

next_level:
  dsm->read_sync(page_buffer, p, kLeafPageSize);
  vector<uint64_t> nodes_on_lvl;
  GlobalAddress level_root = p;
  int lvl = header->level;
  leftmostPtrs[level_root] = header->leftmost_ptr;
  cerr << "scanning level: " << lvl << endl;
  
next_sibling:
  auto page = (InternalPage *) page_buffer;
  header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));
  if (header->level > 0) {
    vector<uint64_t> children;
    vector<uint64_t> keys;
    nodeIDs[p] = id;
    id++;
    nodes_on_lvl.push_back(p);
  
    for (int i = 0; i < kInternalCardinality; i++) {
      if (page->records[i].key != kValueNull) {
        children.push_back(page->records[i].ptr);
        keys.push_back(page->records[i].key);
      }
    }
    parentEdges[p] = children;
    nodeKeys[p] = keys; 
  
    if (header->sibling_ptr != GlobalAddress::Null()) {
      siblingEdges[p] = header->sibling_ptr;
      p = header->sibling_ptr;
      dsm->read_sync(page_buffer, p, kInternalPageSize);
      goto next_sibling;
    } else {
      p = leftmostPtrs[level_root];
      levelNodes[lvl] = nodes_on_lvl;
      goto next_level;
    }
  }

  vector<uint64_t> nodes_on_leaflvl;
leaf_nodes:
  vector<uint64_t> values;
  vector<uint64_t> keys;
  nodeIDs[p] = id;
  id++;
  nodes_on_leaflvl.push_back(p);
  auto lpage = (LeafPage *) page_buffer;
  header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));

  for (int i = 0; i < kLeafCardinality; i++) {
    if (lpage->records[i].key != kValueNull) {
      values.push_back(lpage->records[i].value);
      keys.push_back(lpage->records[i].key);
    }
  }

  nodeValues[p] = values;
  nodeKeys[p] = keys;
  if (header->sibling_ptr != GlobalAddress::Null()) {
    siblingEdges[p] = header->sibling_ptr;
    p = header->sibling_ptr;
    dsm->read_sync(page_buffer, p, kLeafPageSize);
    goto leaf_nodes;
  }
  levelNodes[0] = nodes_on_leaflvl;



  for (auto &[node, keys] : nodeKeys) {
    file << "  Node_" << nodeIDs[node] << " [label=<";
    file << "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\"><TR>";

    for (size_t i = 0; i < keys.size(); ++i) {
        file << "<TD>" << keys[i] << "</TD>";
    }

    file << "</TR></TABLE>";
    file << ">];\n";
  }

  // Ensure nodes on the same level are aligned correctly
  for (const auto& [level, nodes] : levelNodes) {
      file << "  { rank=same; ";
      for (auto node : nodes) {
          file << "Node_" << nodeIDs[node] << "; ";
      }
      file << "}\n";
  }

  // Connect parent nodes to child nodes
  for (auto &[parent, children] : parentEdges) {
      for (auto &child : children) {
          file << "  Node_" << nodeIDs[parent] << " -> Node_" << nodeIDs[child] << ";\n";
      }
  }

  // Connect sibling nodes with dashed blue edges
  for (auto &[node, sibling] : siblingEdges) {
      file << "  Node_" << nodeIDs[node] << " -> Node_" << nodeIDs[sibling] << " [style=dashed, color=blue];\n";
  }

  file << "}\n";
  file.close();

  std::cout << "Graphviz file 'bplustree.dot' generated. Run 'dot -Tpng /nfs/DAL/microbench/bplustree.dot -o bplustree.png' to visualize.\n";
}

void Tree::print_and_check_tree(CoroContext *cxt, int coro_id) {
  assert(dsm->is_register());

  auto root = get_root_ptr(cxt, coro_id);
  // SearchResult result;

  GlobalAddress p = root;
  GlobalAddress levels[define::kMaxLevelOfTree];
  int level_cnt = 0;
  auto page_buffer = (dsm->get_rbuf(coro_id)).get_page_buffer();
  GlobalAddress leaf_head;

next_level:

  dsm->read_sync(page_buffer, p, kLeafPageSize);
  auto header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));
  levels[level_cnt++] = p;
  if (header->level != 0) {
    p = header->leftmost_ptr;
    goto next_level;
  } else {
    leaf_head = p;
  }

next:
  dsm->read_sync(page_buffer, leaf_head, kLeafPageSize);
  auto page = (LeafPage *)page_buffer;
  for (int i = 0; i < kLeafCardinality; ++i) {
    if (page->records[i].value != kValueNull) {
    }
  }
  while (page->hdr.sibling_ptr != GlobalAddress::Null()) {
    leaf_head = page->hdr.sibling_ptr;
    goto next;
  }

  for (int i = 0; i < level_cnt; ++i) {
    dsm->read_sync(page_buffer, levels[i], kLeafPageSize);
    auto header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));
    std::cout << "addr: " << levels[i] << " ";
    header->debug();
    std::cout << " | ";
    while (header->sibling_ptr != GlobalAddress::Null()) {
      dsm->read_sync(page_buffer, header->sibling_ptr, kLeafPageSize);
      header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));
      std::cout << "addr: " << header->sibling_ptr << " ";
      header->debug();
      std::cout << " | ";
    }
    std::cout << "\n------------------------------------" << std::endl;
    std::cout << "------------------------------------" << std::endl;
  }
}

  
inline bool Tree::try_lock_addr(GlobalAddress lock_addr, uint64_t tag,
                                uint64_t *buf, CoroContext *cxt, int coro_id) {


  timer.begin();
  bool hand_over = acquire_local_lock(lock_addr, cxt, coro_id);
  save_measurement(threadID, measurements.lwait_acq, 1, true);
  #ifdef HANDOVER
  if (hand_over) {
    // DEB("[%d.%d] was handed over the global lock: %lu\n", dsm->getMyNodeID(), dsm->getMyThreadID(), lock_addr.offset);
    measurements.handovers[threadID]++;
    measurements.lock_acqs[lock_addr.nodeID * lockNR + lock_addr.offset / 8]++;
    measurements.la[threadID]++;
    return true;
  }
  #endif

  timer.begin();
  #ifdef RAND_FAA
  from_peer = false;
  uint64_t add = 1ULL << nodeID;

  // bitset<64> bits(add);
  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "FAA DM (LOCK), lock_addr: " << lock_addr << endl <<
  // "add: " << bits << "\n\n";

  dsm->faa_dm_sync(lock_addr, add, buf, nullptr);
  measurements.glock_tries[threadID]++;
  lockMeta = *buf;
  bitset<64> lm_bits(lockMeta);

  if (lockMeta == 0) {
  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "LOCK FROM MN" << endl <<
  // "lock_addr: " << lock_addr << endl <<
  // "lockMeta: " << lm_bits << "\n\n";

    save_measurement(threadID, measurements.gwait_acq, 1, true);
    measurements.lock_acqs[lock_addr.nodeID * lockNR + lock_addr.offset / 8]++;
    measurements.la[threadID]++;
    return false;
  }

  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "SPIN" << endl <<
  // "lock_addr: " << lock_addr << endl <<
  // "lockMeta: " << lm_bits << "\n\n";

  char* c_ho_buf = dsm->spin_on(lock_addr);
  measurements.lock_acqs[lock_addr.nodeID * lockNR + lock_addr.offset / 8]++;
  measurements.la[threadID]++;
  
  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "WOKE UP" << endl <<
  // "lock_addr: " << lock_addr << endl <<
  // "lockMeta: " << lm_bits << "\n\n";
  
  #ifdef RAND_FAAD
  ln->page_buffer = dsm->get_rbuf(coro_id).get_page_buffer();
  memcpy(ln->page_buffer, c_ho_buf, kLeafPageSize);
  // ln->page_buffer = c_ho_buf;
  from_peer = true;
  save_measurement(threadID, measurements.gwait_acq, 1, true);
  return true;
  #endif
  save_measurement(threadID, measurements.gwait_acq, 1, true);
  return false;
  #endif

  // #ifdef CN_AWARE
  // GLockAddress next_holder_addr;
  // GLockAddress next_holder_version;
  // GLockAddress old_holder_addr = GLockAddress::Null();
  // GLockAddress old_version_addr = GLockAddress::Null();
  // next_holder_addr.val = lock_addr.val;
  
  // dsm->getNextGLaddr(&next_gaddr, lock_addr);
  // version_addr.nodeID = dsm->getMyNodeID();
  // version_addr.threadID = dsm->getMyThreadID();
  // version_addr.state = 1;
  // version_addr.version = next_gaddr.version;

  // if (!dsm->cas_peer_sync(next_gaddr, expected_addr.val, version_addr.val, buf, nullptr)) {
  //   Debug::notifyError("FAILED TO CAS INITIAL WANT LOCK\n");
  //   exit(1);
  // }

  // // char* pbuffer = dsm->get_rbuf(coro_id).get_page_buffer();
  // // // *(uint64_t *) pbuffer = next_gaddr.val;
  // // *(uint64_t *) pbuffer = 1;
  // // dsm->write_sync(pbuffer, next_gaddr, sizeof(uint64_t), cxt);

  // cerr << "NODE @try_lock_addr " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  // "*next_loc @ start: " << *(GLockAddress*) dsm->getNextLoc(lock_addr) << "\n\n";

  // uint64_t mn_retry = 0;
  // retry_from_mn:
  //   if (!dsm->cas_dm_sync(lock_addr, 0, next_gaddr.val, buf, cxt)) {
  //     uint64_t peer_retry = 0;
  //     GLockAddress ga = *(GLockAddress*) buf;
  //     next_holder_addr = ga;
  //     next_holder_version.version = next_holder_addr.version;
  //     next_holder_version.threadID = next_holder_addr.threadID;
  //     cerr << "NODE " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  //     "CAS MN FAILED, lock_addr: " << lock_addr << "\n" <<
  //     "next_holder: " << next_holder_addr << "\n\n";
  //     assert(next_holder_addr.nodeID != next_gaddr.nodeID);

  //     // old_version_addr = version_addr;
  //     // version_addr.state = 2;
  //     // if (!dsm->cas_peer_sync(next_gaddr, old_version_addr.val, version_addr.val, buf, nullptr)) {
  //     //   Debug::notifyError("FAILED CAS TO STATE 2: MN CAS FAIL\n");
  //     //   exit(1);
  //     // }

  //     next_holder_version.state = 4;
  //     while (!dsm->cas_peer_sync(next_holder_addr, next_holder_version.val, next_gaddr.val, buf, nullptr)) {
  //       peer_retry++;
  //       old_holder_addr = next_holder_addr;
  //       GLockAddress ga = *(GLockAddress*) buf;

  //       cerr << "NODE " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  //       "CAS NEXT PEER FAILED, lock_addr: " << lock_addr << "\n" <<
  //       "next_holder: " << ga << "\n" <<
  //       "*next_loc: " << *(GLockAddress*) dsm->getNextLoc(lock_addr) << "\n\n";
  //       assert(next_holder_addr != next_gaddr);



  //       if (ga.val == 0 || ga.state == 1) {
  //         mn_retry++;
  //         cerr << "NODE " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  //         "RETRY FROM MN, lock_addr: " << lock_addr << "\n" <<
  //         "old_holder: " << old_holder_addr << "\n" <<
  //         "next_holder: " << next_holder_addr << endl <<
  //         "*next_loc: " << *(GLockAddress*) dsm->getNextLoc(lock_addr) << "\n\n";

  //         next_holder_addr.val = lock_addr.val;
  //         old_holder_addr = GLockAddress::Null();
  //         if (mn_retry > 10000) {
  //           Debug::notifyError("MN RETRY DEADLOCK");
  //           assert(false);
  //           exit(1);
  //         }
  //         // old_version_addr = version_addr;
  //         // version_addr.state = 1;
  //         // if (!dsm->cas_peer_sync(next_gaddr, old_version_addr.val, version_addr.val, buf, nullptr)) {
  //         //   Debug::notifyError("FAILED CAS TO STATE 1: WANT LOCK RETRY\n");
  //         //   exit(1);
  //         // }
  //         goto retry_from_mn;
  //       } 

  //       next_holder_addr = ga;
  //       next_holder_version.version = next_holder_addr.version;
  //       next_holder_version.threadID = next_holder_addr.threadID;

  //       if (peer_retry > 10) {
  //         Debug::notifyError("PEER RETRY DEADLOCK");
  //         assert(false);
  //         exit(1);
  //      }
  //       next_holder_version.state = 4;
  //     }

  //     // old_version_addr = version_addr;
  //     // version_addr.state = 3;
  //     // if (!dsm->cas_peer_sync(next_gaddr, old_version_addr.val, version_addr.val, buf, nullptr)) {
  //     //   Debug::notifyError("FAILED CAS TO STATE 2: PEER CAS FAIL\n");
  //     //   exit(1);
  //     // }

  //   } else {
  //     cerr << "NODE " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  //     "LOCK FROM MN, lock_addr: " << lock_addr << "\n" <<
  //     "next_gaddr (written to lock location): " << next_gaddr << "\n" <<
  //     "*next_loc: " << *(GLockAddress*) dsm->getNextLoc(lock_addr) << "\n\n";

  //     old_version_addr = version_addr;
  //     version_addr.state = 4;
  //     if (!dsm->cas_peer_sync(next_gaddr, old_version_addr.val, version_addr.val, buf, nullptr)) {
  //       Debug::notifyError("FAILED CAS TO STATE 4: ACQUIRED LOCK FROM MN\n");
  //       exit(1);
  //     }
  //     return false;
  //   }

  // cerr << "NODE " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  // "SPIN FOR, lock_addr: " << lock_addr << "\n" <<
  // "current_next_loc: " << *((GLockAddress*) dsm->getNextLoc(lock_addr)) << "\n" <<
  // "(current)_holder: " << next_holder_addr << "\n\n";
  // assert(next_holder_addr.nodeID != next_gaddr.nodeID);
  // // char* pbuf = dsm->get_rbuf(coro_id).get_page_buffer();
  // // *(uint64_t *) pbuf = 0;
  // // dsm->spin_on(pbuf, next_holder_addr);
  // old_version_addr = version_addr;
  // version_addr.state = 4;
  // if (!dsm->cas_peer_sync(next_gaddr, old_version_addr.val, version_addr.val, buf, nullptr)) {
  //   Debug::notifyError("FAILED CAS TO STATE 4: WAITING FOR PEER\n");
  //   exit(1);
  // }
  // dsm->wait_for_peer(next_holder_addr, dsm->getMyThreadID());
  // return false;


  // #else
  {
    uint64_t retry_cnt = 0;
    uint64_t pre_tag = 0;
    uint64_t conflict_tag = 0;
    uint64_t ttag = 0;
  retry:
    retry_cnt++;
    if (retry_cnt > 1000000) {
      std::cout << "Deadlock " << lock_addr << std::endl;

      std::cout << dsm->getMyNodeID() << ", " << dsm->getMyThreadID()
                << " locked by " << (conflict_tag >> 32) << ", "
                << (conflict_tag << 32 >> 32) << std::endl
                << "ttag " << (ttag >> 32) << ", "
                << (ttag << 32 >> 32) << std::endl;
      // assert(false);
      sleep(1);
      measurements.glock_tries[threadID] += retry_cnt; 
      retry_cnt = 0;
    }

    assert(tag >> 32 < MAX_MACHINE);
    assert(tag << 32 >> 32 < MAX_APP_THREAD);
    bool res = dsm->cas_dm_sync(lock_addr, 0, tag, buf, nullptr);

    if (!res) {
      conflict_tag = *buf - 1;
      ttag = *buf;
      if (conflict_tag != pre_tag) {
        measurements.glock_tries[threadID] += retry_cnt;
        retry_cnt = 0;
        pre_tag = conflict_tag;
      }
      goto retry;
    }
    measurements.glock_tries[threadID] += retry_cnt;
  }
  // #endif
  save_measurement(threadID, measurements.gwait_acq, 1, true);
  // DEB("[%d.%d] got the global lock via rdma: %lu\n", dsm->getMyNodeID(), dsm->getMyThreadID(), lock_addr.offset);
  measurements.lock_acqs[lock_addr.nodeID * lockNR + lock_addr.offset / 8]++;
  measurements.la[threadID]++;

  return false;
}

inline void Tree::unlock_addr(GlobalAddress lock_addr, uint64_t tag,
                              uint64_t *buf, CoroContext *cxt, int coro_id,
                              bool async, char *page_buf, GlobalAddress page_addr, int level, bool stale_cache) {

  #ifdef HANDOVER
  bool hand_over_other = can_hand_over(lock_addr);
  if (hand_over_other) {

    #ifdef HANDOVER_DATA
    ln->safe = !stale_cache;
    // if (ln->wb && !stale_cache) {
    //   timer.begin();
    //   dsm->write_sync(ln->page_buffer, ln->page_addr, kLeafPageSize);
    //   save_measurement(threadID, measurements.data_write);
    //   ln->wb = 0;
    //   ln->safe = false;
    // } else if (ln->wb > 0 && stale_cache) {
    //   Debug::notifyError("losing %d inserts", ln->wb);
    //   ln->wb = 0;
    //   ln->safe = false;
    // }

    #endif
    ln->page_buffer = page_buf;
    ln->page_addr = page_addr;
    ln->level = level;
    ln->written = false;
    ln->ua_hod = true;

    releases_local_lock(lock_addr);
    // DEB("[%d.%d] unlocked the global lock for handover: %lu\n", dsm->getMyNodeID(), dsm->getMyThreadID(), curr_lock_addr.offset);
    return;
  }
  #endif


  timer.begin();
  auto cas_buf = dsm->get_rbuf(coro_id).get_cas_buffer();
  *cas_buf = 0;

  #ifdef RAND_FAA
  uint64_t add = -(1ULL << nodeID);

  // bitset<64> bits(add);
  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "FAA DM (REL), lock_addr: " << lock_addr << endl <<
  // "add: " << bits << "\n\n";
  #ifdef HANDOVER_DATA
  if (ln->wb) {
    timer.begin();
    dsm->write_sync(ln->page_buffer, ln->page_addr, kLeafPageSize);
    save_measurement(threadID, measurements.data_write);
    ln->written = true;
  }
  ln->wb = 0;
  ln->safe = false;
  #endif

  timer.begin();
  dsm->faa_dm_sync(lock_addr, add, cas_buf, nullptr);
  lockMeta = *cas_buf;
  bitset<64> lm_bits(lockMeta);
  if (lockMeta == 1ULL << nodeID) {
    save_measurement(threadID, measurements.gwait_rel);
    // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
    // "REL LOCK TO MN" << endl <<
    // "lock_addr: " << lock_addr << endl <<
    // "lockMeta: " << lm_bits << "\n\n";

    releases_local_lock(lock_addr);
    return;
  }

  int peerNodeID = randNodeID(lockMeta);
  char *lmbuf = dsm->get_rbuf(coro_id).get_page_buffer();
  *(uint64_t *) lmbuf = 1;
  GlobalAddress peerSpinLoc = GlobalAddress::Null();
  peerSpinLoc.nodeID = peerNodeID;
  // peerSpinLoc.offset = (lock_addr.nodeID * dsm->getLmSize()) + lock_addr.offset;
  peerSpinLoc.offset = (lock_addr.nodeID * dsm->getLmSize()) + lock_addr.offset;

  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "REL LOCK TO PEER" << endl <<
  // "lock_addr: " << lock_addr << endl <<
  // "lockMeta: " << lm_bits << endl <<
  // "peerSpinLoc: " << peerSpinLoc << "\n\n";
  assert(peerSpinLoc.nodeID != nodeID);

  #ifdef RAND_FAAD
  GlobalAddress peerDataLoc = GlobalAddress::Null();
  peerDataLoc.nodeID = peerNodeID;
  peerDataLoc.offset = dsm->getDsmSize() + (lock_addr.nodeID * lockNR * kLeafPageSize) + (lock_addr.offset / 8 )*kLeafPageSize;

  // dsm->write_sync(page_buf, peerDataLoc, kLeafPageSize, nullptr, from_peer);
  // save_measurement(threadID, measurements.data_write);
  dsm->write_peer(page_buf, peerDataLoc, kLeafPageSize, false, nullptr, from_peer);
  // timer.begin();

  #endif


  if (async) {
    dsm->write_lm(lmbuf, peerSpinLoc, sizeof(uint64_t), false, nullptr);
  } else {
    timer.begin();
    dsm->write_lm_sync(lmbuf, peerSpinLoc, sizeof(uint64_t), nullptr);
  }
  save_measurement(threadID, measurements.gwait_rel);
  measurements.c_ho[threadID]++;

  releases_local_lock(lock_addr);
  return;
  #endif

  #ifdef HANDOVER_DATA
  if (ln->wb) {
    timer.begin();
    dsm->write_sync(ln->page_buffer, ln->page_addr, kLeafPageSize);
    save_measurement(threadID, measurements.data_write);
    ln->written = true;
  }
  ln->wb = 0;
  ln->safe = false;
  #endif

  if (async) {
    dsm->write_dm((char *)cas_buf, lock_addr, sizeof(uint64_t), false);
  } else {
    timer.begin();
    dsm->write_dm_sync((char *)cas_buf, lock_addr, sizeof(uint64_t), cxt);
    save_measurement(threadID, measurements.gwait_rel);
  }

  releases_local_lock(lock_addr);
}

  int Tree::randNodeID(uint64_t value) {
    int nodeNR = MAX_MACHINE;
    int indices[nodeNR]; // Fixed-size array to store set bit positions
    int count = 0;

    value &= ~(1ULL << nodeID);
    while (value && count < nodeNR) {
        int index = __builtin_ctzll(value); // Get the lowest set bit position
        indices[count++] = index;
        value &= (value - 1); // Clear the lowest set bit
    }
    assert(count > 0);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, count - 1);

    return indices[dist(gen)];
  }


void Tree::write_page_and_unlock(char *page_buffer, GlobalAddress page_addr,
                                 int page_size, uint64_t *cas_buffer,
                                 GlobalAddress lock_addr, uint64_t tag,
                                 CoroContext *cxt, int coro_id, bool async, int level,
                                 char* orig_pbuf, GlobalAddress orig_paddr) {


  ln->page_buffer = orig_pbuf;
  ln->page_addr = orig_paddr;
  ln->level = level;
  ln->safe = !async;
  ln->wb++;

  #ifdef HANDOVER
  bool hand_over_other = can_hand_over(lock_addr);
  if (hand_over_other) {
    timer.begin();
    #ifndef HANDOVER_DATA
    dsm->write_sync(page_buffer, page_addr, page_size, cxt, from_peer);
    save_measurement(threadID, measurements.data_write);
    #endif

    // #ifdef HANDOVER_DATA
    // if (!ln->safe) {
    //   dsm->write_sync(page_buffer, page_addr, page_size, cxt, from_peer);
    //   save_measurement(threadID, measurements.data_write);
    //   ln->wb = 0;
    // }
    // #endif
    
    releases_local_lock(lock_addr);
    // DEB("[%d.%d] unlocked global lock for handover: %lu\n", dsm->getMyNodeID(), dsm->getMyThreadID(), curr_lock_addr.offset);
    return;
  }
  #endif

  timer.begin();
  RdmaOpRegion rs[2];
  uint64_t *cas_buf = dsm->get_rbuf(coro_id).get_cas_buffer();

  #ifdef RAND_FAA
  uint64_t add_ = -(1ULL << nodeID);

    // #ifdef BATCHED_WRITEBACK

    // rs[0].source = (uint64_t)page_buffer;
    // rs[0].dest = page_addr;
    // rs[0].size = page_size;
    // rs[0].is_on_chip = false;

    // // rs[1].source = (uint64_t)dsm->get_rbuf(coro_id).get_cas_buffer();
    // rs[1].source = (uint64_t)cas_buf;
    // rs[1].dest = lock_addr;
    // rs[1].size = sizeof(uint64_t);
    // rs[1].is_on_chip = true;
    // // *(uint64_t *)rs[1].source = 0;

    // if (async) {
    //   dsm->write_faa(rs[0], rs[1], add_, false);
    // } else {
    //   dsm->write_faa_sync(rs[0], rs[1], add_, cxt);
    // }
    // save_measurement(threadID, measurements.data_write);

    // #else

  bitset<64> bits(add_);
  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "FAA DM (REL), lock_addr: " << lock_addr << endl <<
  // "add: " << bits << "\n\n";

  if (false) {
    dsm->write(page_buffer, page_addr, page_size, false, cxt, from_peer);
  } else {
    dsm->write_sync(page_buffer, page_addr, page_size, cxt, from_peer);
    save_measurement(threadID, measurements.data_write);
  }
  ln->wb = 0;
  ln->safe = false;

  // uint64_t *long_data = (uint64_t*) page_buffer;
  // cerr << "WRITTEN TO: " << page_addr << endl;
  // for (size_t i = 0; i < page_size / sizeof(uint64_t); i++) {
  //   cerr << long_data[i] << ", ";
  // }
    // dsm->write_sync(page_buffer, page_addr, page_size, cxt);
  timer.begin();
  dsm->faa_dm_sync(lock_addr, add_, cas_buf, nullptr);



    // #endif

  lockMeta = *cas_buf;
  bitset<64> lm_bits(lockMeta);
  if (lockMeta == 1ULL << nodeID) {
    save_measurement(threadID, measurements.gwait_rel);
    // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
    // "REL LOCK TO MN" << endl <<
    // "lock_addr: " << lock_addr << endl <<
    // "lockMeta: " << lm_bits << "\n\n";

    releases_local_lock(lock_addr);
    return;
  }

  int peerNodeID = randNodeID(lockMeta);
  char *lmbuf = dsm->get_rbuf(coro_id).get_page_buffer();
  *(uint64_t *) lmbuf = 1;
  GlobalAddress peerSpinLoc = GlobalAddress::Null();
  peerSpinLoc.nodeID = peerNodeID;
  // peerSpinLoc.offset = (lock_addr.nodeID * dsm->getLmSize()) + lock_addr.offset;
  peerSpinLoc.offset = (lock_addr.nodeID * dsm->getLmSize()) + lock_addr.offset;

  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "REL LOCK TO PEER" << endl <<
  // "lock_addr: " << lock_addr << endl <<
  // "lockMeta: " << lm_bits << endl <<
  // "peerSpinLoc: " << peerSpinLoc << "\n\n";
  assert(peerSpinLoc.nodeID != nodeID);

  #ifdef RAND_FAAD
  GlobalAddress peerDataLoc = GlobalAddress::Null();
  peerDataLoc.nodeID = peerNodeID;
  peerDataLoc.offset = dsm->getDsmSize() + (lock_addr.nodeID * lockNR * kLeafPageSize) + (lock_addr.offset / 8 ) * kLeafPageSize;

  // rs[0].source = (uint64_t)page_buffer;
  // rs[0].dest = page_addr;
  // rs[0].size = page_size;
  // rs[0].is_on_chip = false;

  // // rs[1].source = (uint64_t)dsm->get_rbuf(coro_id).get_cas_buffer();
  // rs[1].source = (uint64_t)lmbuf;
  // rs[1].dest = lock_addr;
  // rs[1].size = sizeof(uint64_t);
  // rs[1].is_on_chip = false;
  // rs[1].is_lockMeta = true;
  // *(uint64_t *)rs[1].source = 1;

  // if (async) {
  //   dsm->write_batch(rs, 2, false);
  // } else {
  //   dsm->write_batch_sync(rs, 2, nullptr);
  // }
  // // dsm->write_batch_sync(rs, 2, nullptr);

  // save_measurement(threadID, measurements.data_write);
  // cerr << "HANDING OVER DATA: " << endl;
  // cerr << "peerDataLoc: " << peerDataLoc << endl;
  // uint64_t * long_data = (uint64_t *) page_buffer;
  // for (int i = 0; i < page_size/sizeof(uint64_t); i++) {
  //   cerr << long_data[i] << ", ";
  // }
  // cerr << endl;

  // dsm->write_peer_sync(page_buffer, peerDataLoc, page_size, nullptr, from_peer);
  // save_measurement(threadID, measurements.data_write);
  dsm->write_peer(page_buffer, peerDataLoc, page_size, false, nullptr, from_peer);
  timer.begin();

  #endif

  // TODO: ?
  *lmbuf = 1;
  if (async) {
    dsm->write_lm(lmbuf, peerSpinLoc, sizeof(uint64_t), false, nullptr);
  } else {
    dsm->write_lm_sync(lmbuf, peerSpinLoc, sizeof(uint64_t), nullptr);
  }
  save_measurement(threadID, measurements.gwait_rel);
  // async release counts as we first faad the mn
  // cerr << "[" << nodeID << ", " << threadID << "]" << endl <<
  // "WOKE UP PEER" << endl <<
  // "lock_addr: " << lock_addr << endl <<
  // "peerSpinLoc: " << peerSpinLoc << endl <<
  // "lockMeta: " << lm_bits << "\n\n";
  // dsm->write_lm_sync(lmbuf, peerSpinLoc, sizeof(uint64_t), nullptr);
  measurements.c_ho[threadID]++;

  releases_local_lock(lock_addr);
  return;

  #endif

  // #ifdef CN_AWARE
  // // *curr_cas_buffer = 0;
  // cerr << "NODE " << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << endl <<
  // "*nextloc = " << *(GLockAddress *) dsm->getNextLoc(lock_addr) << "\n" <<
  // "version_addr = " << version_addr << "\n\n";

  // if (!dsm->cas_peer_sync(next_gaddr, version_addr.val, 0, curr_cas_buffer, cxt)) {
    
  //   GLockAddress next_addr = *(GLockAddress *) curr_cas_buffer;

  //   expected_addr = next_addr;
  //   cerr << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << ": OTHER THREAD WAITING, lock addr: " << lock_addr << "\n" << 
  //   "next_addr: " << next_addr << "\n\n";
  //   assert(next_gaddr.nodeID !=  next_addr.nodeID);

  //   rs[0].source = (uint64_t)page_buffer;
  //   rs[0].dest = page_addr;
  //   rs[0].size = page_size;
  //   rs[0].is_on_chip = false;
  //   rs[0].is_peer = from_peer;

  //   rs[1].source = (uint64_t)dsm->get_rbuf(coro_id).get_cas_buffer();
  //   rs[1].dest = lock_addr;
  //   rs[1].size = sizeof(uint64_t);
  //   rs[1].is_on_chip = true;
  //   *(uint64_t *)rs[1].source = next_addr.val;

  //   // rs[2].source = (uint64_t)dsm->get_rbuf(coro_id).get_page_buffer();
  //   // rs[2].dest = next_spinloc;
  //   // rs[2].size = sizeof(uint64_t);
  //   // rs[2].is_on_chip = false;
  //   // *(uint64_t *)rs[2].source = 1;

  //   if (async) {
  //     dsm->write_batch(rs, 2, false);
  //   } else {
  //     dsm->write_batch_sync(rs, 1, cxt);
  //   }

  //   if (!dsm->cas_dm_sync(lock_addr, next_gaddr.val, next_addr.val, cas_buffer, cxt)) {
  //     Debug::notifyError("FAILED TO CAS MN FOR CN HO");
  //     cerr << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << " | lock addr: " << lock_addr << "\n" << 
  //     "next_addr: " << next_addr << endl <<
  //     "next_gaddr: " << next_gaddr << endl <<
  //     "*cas_buffer: " << *(GLockAddress*) cas_buffer << "\n\n";
  //     exit(1);
  //   }

  //   // if (!dsm->cas_peer_sync(next_gaddr, next_addr.val, 0, cas_buffer, cxt)) {
  //   //   Debug::notifyError("FAILED TO CAS OWN STATE TO 0\n");
  //   //   cerr << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << " | lock addr: " << lock_addr << "\n" << 
  //   //   "next_addr: " << next_addr << endl <<
  //   //   "next_gaddr: " << next_gaddr << endl <<
  //   //   "*cas_buffer: " << *(GLockAddress*) cas_buffer << "\n\n";
  //   //   exit(1);
  //   // }
    
  //   // char* pbuffer = dsm->get_rbuf(coro_id).get_page_buffer();
  //   // // *(uint64_t *) pbuffer = next_gaddr.val;
  //   // *(uint64_t *) pbuffer = 0;

  //   cerr << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << ": OTHER THREAD ENQ\n" <<
  //   // "pbuffer (gaddr) : " << *(GLockAddress *) pbuffer << endl <<
  //   "nextloc gaddr = " << *(GLockAddress *) dsm->getNextLoc(lock_addr) << "\n\n";
  //   // dsm->set_nextloc(GLockAddress::Null());
  //   // dsm->write_sync(pbuffer, next_gaddr, sizeof(uint64_t), cxt);

  //   dsm->wakeup_peer(next_addr, dsm->getMyThreadID());

  //   releases_local_lock(lock_addr);
  //   return;
  // }
  // // last_next_gaddr = GLockAddress::Null();
  // // cerr << "page_addr: " << page_addr << "\n\n";
  // // dsm->write_sync(page_buffer, page_addr, page_size, cxt);

  // // *cas_buf = 0;
  // // dsm->write_dm_sync((char *)cas_buf, lock_addr, sizeof(uint64_t), cxt);
  // if (!dsm->cas_dm_sync(lock_addr, next_gaddr.val, 0, cas_buffer, cxt)) {
  //   Debug::notifyError("FAILED TO CAS MN SIMPLE RELEASE\n");
  //   cerr << dsm->getMyNodeID() << ", lock addr: " << lock_addr << "\n" << 
  //   "next_gaddr: " << next_gaddr << endl <<
  //   "*cas_buffer: " << *(GLockAddress*) cas_buffer << "\n\n";
  //   exit(1);
  // }
  // expected_addr = GLockAddress::Null();
  //   cerr << dsm->getMyNodeID() << ", " << dsm->getMyThreadID() << ": REL LOCK TO MN\n" <<
  // "lock_addr: " << lock_addr << "\n\n"; 

  // releases_local_lock(lock_addr);
  // return;
  // #endif

  timer.begin();
  
  #ifdef BATCHED_WRITEBACK
  rs[0].source = (uint64_t)page_buffer;
  rs[0].dest = page_addr;
  rs[0].size = page_size;
  rs[0].is_on_chip = false;
  rs[0].is_peer = from_peer;

  rs[1].source = (uint64_t)dsm->get_rbuf(coro_id).get_cas_buffer();
  // rs[1].source = (uint64_t)cas_buffer;
  rs[1].dest = lock_addr;
  rs[1].size = sizeof(uint64_t);
  rs[1].is_on_chip = true;
  *(uint64_t *)rs[1].source = 0;

  if (async) {
    dsm->write_batch(rs, 2, false);
  } else {
    dsm->write_batch_sync(rs, 2, cxt);
    save_measurement(threadID, measurements.data_write);
  }
  save_measurement(threadID, measurements.gwait_rel);

  #else

  dsm->write_sync(page_buffer, page_addr, page_size, cxt, from_peer);
  save_measurement(threadID, measurements.data_write);
  timer.begin();
  *cas_buf = 0;
  dsm->write_dm_sync((char *)cas_buf, lock_addr, sizeof(uint64_t), cxt);
  save_measurement(threadID, measurements.gwait_rel);
  

  // if (!dsm->cas_dm_sync(lock_addr, next_gaddr.val, 0, cas_buffer, cxt)) {
  //   Debug::notifyError("FAILED TO CAS MN FOR CN HO\n");
  //   cerr << dsm->getMyNodeID() << ", lock addr: " << lock_addr << "\n" << 
  //   "next_gaddr: " << next_gaddr << endl <<
  //   "*cas_buffer: " << *(GLockAddress*) cas_buffer << "\n\n";
  //   exit(1);
  // }

  #endif
  // cout << "********************************************" << endl;
  // cout << "WRITTEN BACK (NO HOD): " << "[" + to_string(dsm->getMyNodeID()) + "." + to_string(dsm->getMyThreadID()) + "]" << endl;
  // cout << "lock_addr: " << lock_addr << endl; 
  // cout << "page_addr: " << page_addr << endl;
  // cout << "curr_page_buffer: " << (uintptr_t) curr_page_buffer << " = " << (uint64_t) *curr_page_buffer << endl;
  // cout << "********************************************" << endl;


  // cerr << "REL LOCK TO MN" << endl <<
  // "lock_addr: " << lock_addr << "\n\n"; 
  #ifdef HANDOVER_DATA
  ln->safe = false;
  ln->wb = 0;
  ln->written = true;
  #endif

  releases_local_lock(lock_addr);
  // DEB("[%d.%d] unlocked global lock remotely: %lu\n", dsm->getMyNodeID(), dsm->getMyThreadID(), lock_addr.offset);
}

bool Tree::lock_and_read_page(char **page_buffer, GlobalAddress page_addr,
                              int page_size, uint64_t *cas_buffer,
                              GlobalAddress lock_addr, uint64_t tag,
                              CoroContext *cxt, int coro_id, int level, bool internal_page) {

  bool handover = try_lock_addr(lock_addr, tag, cas_buffer, cxt, coro_id);

  timer.begin();
  #ifndef HANDOVER_DATA
  dsm->read_sync(*page_buffer, page_addr, page_size, cxt);
  save_measurement(threadID, measurements.data_read);
  return false;
  #endif



  // *page_buffer = dsm->get_rbuf(0).get_page_buffer();
  if (handover) {
    auto lpage = (LeafPage *) ln->page_buffer;
    auto ipage = (InternalPage *) ln->page_buffer;
    bool same_address = 
      ln->page_addr.val == page_addr.val && 
      ln->level == level &&
      lpage->hdr.level == level &&
      ln->safe;

    // cerr << "********************************************" << endl;
    // cerr << "DATA HO: " << "[" + to_string(dsm->getMyNodeID()) + "." + to_string(dsm->getMyThreadID()) + "]" << endl;
    // cerr << "lock_addr: " << lock_addr << endl; 
    // cerr << "page_addr: " << page_addr << endl;
    // cerr << "page_buffer: " << (uintptr_t) *page_buffer << " = " << (uint64_t) **page_buffer << endl;
    // cerr << "********************************************" << endl;
    // ln->debug();



    timer.begin();
    #ifdef HANDOVER_DATA
    memcpy(*page_buffer, ln->page_buffer, kLeafPageSize);
    ln->ua_hod = false;
    #endif

    if (!same_address) {
      if (ln->wb > 0) {
        dsm->write_sync(*page_buffer, ln->page_addr, kLeafPageSize, nullptr);
        save_measurement(threadID, measurements.data_write);
        ln->wb = 0;
        ln->written = true;
      }
      ln->safe = false;

      timer.begin();
      *page_buffer = dsm->get_rbuf(0).get_page_buffer();
      dsm->read_sync(*page_buffer, page_addr, page_size, cxt);
      save_measurement(threadID, measurements.data_read);
      return false;
    }
    else {
      if (!lpage->check_consistent() && !ipage->check_consistent()) {
        cerr << "level " << level << endl;
        lpage->debug();
        ipage->debug();
        ln->debug();
        assert(false);
      }
      assert(ln->level == level);
      ln->written = false;
      measurements.handovers_data[threadID]++;

      // #ifdef RAND_FAAD
      // if (from_peer) {
      //   measurements.c_hod[threadID]++;
      // }
      // #endif

      return true;
    }
  } else {
    // cerr << "********************************************" << endl;
    // cerr << "NO DATA HO: " << "[" + to_string(dsm->getMyNodeID()) + "." + to_string(dsm->getMyThreadID()) + "]" << endl;
    // cerr << "lock_addr: " << lock_addr << endl; 
    // cerr << "page_addr: " << page_addr << endl;
    // cerr << "page_buffer: " << (uintptr_t) *page_buffer << " = " << (uint64_t) **page_buffer << endl;
    // cerr << "********************************************" << endl;
    *page_buffer = dsm->get_rbuf(0).get_page_buffer();
    timer.begin();
    dsm->read_sync(*page_buffer, page_addr, page_size, cxt);
    save_measurement(threadID, measurements.data_read);
    return false;
  }
}

void Tree::lock_bench(const Key &k, CoroContext *cxt, int coro_id) {
  uint64_t lock_index = CityHash64((char *)&k, sizeof(k)) % lockNR;

  GlobalAddress lock_addr;
  lock_addr.nodeID = 0;
  lock_addr.offset = lock_index * sizeof(uint64_t);
  auto cas_buffer = dsm->get_rbuf(coro_id).get_cas_buffer();

  // bool res = dsm->cas_sync(lock_addr, 0, 1, cas_buffer, cxt);
  try_lock_addr(lock_addr, 1, cas_buffer, cxt, coro_id);
  unlock_addr(lock_addr, 1, cas_buffer, cxt, coro_id, true);
}

void Tree::insert_internal(const Key &k, GlobalAddress v, CoroContext *cxt,
                           int coro_id, int level) {
  auto root = get_root_ptr(cxt, coro_id);
  SearchResult result;

  GlobalAddress p = root;

next:

  if (!page_search(p, k, result, cxt, coro_id)) {
    std::cout << "SEARCH WARNING insert" << std::endl;
    p = get_root_ptr(cxt, coro_id);
    // sleep(1);
    goto next;
  }

  assert(result.level != 0);
  if (result.slibing != GlobalAddress::Null()) {
    p = result.slibing;
    goto next;
  }

  p = result.next_level;
  if (result.level != level + 1) {
    goto next;
  }

  internal_page_store(p, k, v, root, level, cxt, coro_id);
}

void Tree::insert(const Key &k, const Value &v, CoroContext *cxt, int coro_id) {
  assert(dsm->is_register());

  before_operation(cxt, coro_id);

  if (enable_cache) {
    GlobalAddress cache_addr;
    auto entry = index_cache->search_from_cache(k, &cache_addr,
                                                dsm->getMyThreadID() == 0);
    if (entry) { // cache hit
      auto root = get_root_ptr(cxt, coro_id);
      if (leaf_page_store(cache_addr, k, v, root, 0, cxt, coro_id, true)) {

        cache_hit[dsm->getMyThreadID()][0]++;
        return;
      }
      // cache stale, from root,
      index_cache->invalidate(entry);
    }
    cache_miss[dsm->getMyThreadID()][0]++;
  }

  auto root = get_root_ptr(cxt, coro_id);
  SearchResult result;

  GlobalAddress p = root;

next:

  if (!page_search(p, k, result, cxt, coro_id)) {
    std::cout << "SEARCH WARNING insert" << std::endl;
    p = get_root_ptr(cxt, coro_id);
    // sleep(1);
    goto next;
  }

  if (!result.is_leaf) {
    assert(result.level != 0);
    if (result.slibing != GlobalAddress::Null()) {
      p = result.slibing;
      goto next;
    }

    p = result.next_level;
    if (result.level != 1) {
      goto next;
    }
  }

  leaf_page_store(p, k, v, root, 0, cxt, coro_id);
}

bool Tree::search(const Key &k, Value &v, CoroContext *cxt, int coro_id) {
  assert(dsm->is_register());

  auto root = get_root_ptr(cxt, coro_id);
  SearchResult result;

  GlobalAddress p = root;

  bool from_cache = false;
  const CacheEntry *entry = nullptr;
  if (enable_cache) {
    GlobalAddress cache_addr;
    entry = index_cache->search_from_cache(k, &cache_addr,
                                           dsm->getMyThreadID() == 0);
    if (entry) { // cache hit
      cache_hit[dsm->getMyThreadID()][0]++;
      from_cache = true;
      p = cache_addr;

    } else {
      cache_miss[dsm->getMyThreadID()][0]++;
    }
  }

next:
  if (!page_search(p, k, result, cxt, coro_id, from_cache)) {
    if (from_cache) { // cache stale
      index_cache->invalidate(entry);
      cache_hit[dsm->getMyThreadID()][0]--;
      cache_miss[dsm->getMyThreadID()][0]++;
      from_cache = false;

      p = root;
    } else {
      std::cout << "SEARCH WARNING search" << std::endl;
      // sleep(1);
    }
    goto next;
  }
  if (result.is_leaf) {
    if (result.val != kValueNull) { // find
      v = result.val;
      return true;
    }
    if (result.slibing != GlobalAddress::Null()) { // turn right
      p = result.slibing;
      goto next;
    }
    return false; // not found
  } else {        // internal
    p = result.slibing != GlobalAddress::Null() ? result.slibing
                                                : result.next_level;
    goto next;
  }
}

uint64_t Tree::range_query(const Key &from, const Key &to, Value *value_buffer,
                           CoroContext *cxt, int coro_id) {

  const int kParaFetch = 32;
  thread_local std::vector<InternalPage *> result;
  thread_local std::vector<GlobalAddress> leaves;

  result.clear();
  leaves.clear();
  index_cache->search_range_from_cache(from, to, result);
  
  // FIXME: here, we assume all innernal nodes are cached in compute node
  if (result.empty()) {
    return 0;
  }

  uint64_t counter = 0;
  for (auto page : result) {
    auto cnt = page->hdr.last_index + 1;
    auto addr = page->hdr.leftmost_ptr;

    // [from, to]
    // [lowest, page->records[0].key);
    bool no_fetch = from > page->records[0].key || to < page->hdr.lowest;
    if (!no_fetch) {
      leaves.push_back(addr);
    }
    for (int i = 1; i < cnt; ++i) {
      no_fetch = from > page->records[i].key || to < page->records[i - 1].key;
      if (!no_fetch) {
        leaves.push_back(page->records[i - 1].ptr);
      }
    }

    no_fetch = from > page->hdr.highest || to < page->records[cnt - 1].key;
    if (!no_fetch) {
      leaves.push_back(page->records[cnt - 1].ptr);
    }
  }

  int cq_cnt = 0;
  char *range_buffer = (dsm->get_rbuf(coro_id)).get_range_buffer();
  for (size_t i = 0; i < leaves.size(); ++i) {
    if (i > 0 && i % kParaFetch == 0) {
      dsm->poll_rdma_cq(kParaFetch);
      cq_cnt -= kParaFetch;
      for (int k = 0; k < kParaFetch; ++k) {
        auto page = (LeafPage *)(range_buffer + k * kLeafPageSize);
        for (int i = 0; i < kLeafCardinality; ++i) {
          auto &r = page->records[i];
          if (r.value != kValueNull && r.f_version == r.r_version) {
            if (r.key >= from && r.key <= to) {
              value_buffer[counter++] = r.value;
            }
          }
        }
      }
    }
    dsm->read(range_buffer + kLeafPageSize * (i % kParaFetch), leaves[i],
              kLeafPageSize, true);
    cq_cnt++;
  }

  if (cq_cnt != 0) {
    dsm->poll_rdma_cq(cq_cnt);
    for (int k = 0; k < cq_cnt; ++k) {
      auto page = (LeafPage *)(range_buffer + k * kLeafPageSize);
      for (int i = 0; i < kLeafCardinality; ++i) {
        auto &r = page->records[i];
        if (r.value != kValueNull && r.f_version == r.r_version) {
          if (r.key >= from && r.key <= to) {
            value_buffer[counter++] = r.value;
          }
        }
      }
    }
  }

  return counter;
}

void Tree::del(const Key &k, CoroContext *cxt, int coro_id) {
  assert(dsm->is_register());

  before_operation(cxt, coro_id);

  if (enable_cache) {
    GlobalAddress cache_addr;
    auto entry = index_cache->search_from_cache(k, &cache_addr,
                                                dsm->getMyThreadID() == 0);
    if (entry) { // cache hit
      if (leaf_page_del(cache_addr, k, 0, cxt, coro_id, true)) {

        cache_hit[dsm->getMyThreadID()][0]++;
        return;
      }
      // cache stale, from root,
      index_cache->invalidate(entry);
    }
    cache_miss[dsm->getMyThreadID()][0]++;
  }

  auto root = get_root_ptr(cxt, coro_id);
  SearchResult result;

  GlobalAddress p = root;

next:

  if (!page_search(p, k, result, cxt, coro_id)) {
    std::cout << "SEARCH WARNING del" << std::endl;
    p = get_root_ptr(cxt, coro_id);
    // sleep(1);
    goto next;
  }

  if (!result.is_leaf) {
    assert(result.level != 0);
    if (result.slibing != GlobalAddress::Null()) {
      p = result.slibing;
      goto next;
    }

    p = result.next_level;
    if (result.level != 1) {
      goto next;
    }
  }

  leaf_page_del(p, k, 0, cxt, coro_id);
}

bool Tree::page_search(GlobalAddress page_addr, const Key &k,
                       SearchResult &result, CoroContext *cxt, int coro_id,
                       bool from_cache) {
  auto page_buffer = (dsm->get_rbuf(coro_id)).get_page_buffer();
  auto header = (Header *)(page_buffer + (STRUCT_OFFSET(LeafPage, hdr)));

  int counter = 0;
re_read:
  if (++counter > 100) {
    printf("re read too many times\n");
    sleep(1);
  }
  dsm->read_sync(page_buffer, page_addr, kLeafPageSize, cxt);

  memset(&result, 0, sizeof(result));
  result.is_leaf = header->leftmost_ptr == GlobalAddress::Null();
  result.level = header->level;
  path_stack[coro_id][result.level] = page_addr;
  // std::cout << "level " << (int)result.level << " " << page_addr <<
  // std::endl;

  if (result.is_leaf) {
    auto page = (LeafPage *)page_buffer;
    if (!page->check_consistent()) {
      goto re_read;
    }

    if (from_cache &&
        (k < page->hdr.lowest || k >= page->hdr.highest)) { // cache is stale
      return false;
    }

    assert(result.level == 0);
    if (k >= page->hdr.highest) { // should turn right
      result.slibing = page->hdr.sibling_ptr;
      return true;
    }
    if (k < page->hdr.lowest) {
      assert(false);
      page->debug();
      return false;
    }
    leaf_page_search(page, k, result);
  } else {
    assert(result.level != 0);
    assert(!from_cache);
    auto page = (InternalPage *)page_buffer;

    if (!page->check_consistent()) {
      goto re_read;
    }

    if (result.level == 1 && enable_cache) {
      index_cache->add_to_cache(page);
    }

    if (k >= page->hdr.highest) { // should turn right
      result.slibing = page->hdr.sibling_ptr;
      return true;
    }
    if (k < page->hdr.lowest) {
      Debug::notifyError("key %ld error in level %d\n", k, page->hdr.level);
      Debug::notifyError("Tree:793:page_search: k < page->hdr.lowest");
      std::cout << "page_addr: " << page_addr << std::endl;
      std::cout << "key: " << k << std::endl;
      page->debug();
      // sleep(10);
      // print_and_check_tree();
      // assert(false);
      return false;
    }
    internal_page_search(page, k, result);
  }

  return true;
}

void Tree::internal_page_search(InternalPage *page, const Key &k,
                                SearchResult &result) {

  assert(k >= page->hdr.lowest);
  assert(k < page->hdr.highest);

  auto cnt = page->hdr.last_index + 1;
  // page->debug();
  if (k < page->records[0].key) {
    result.next_level = page->hdr.leftmost_ptr;
    return;
  }

  for (int i = 1; i < cnt; ++i) {
    if (k < page->records[i].key) {
      result.next_level = page->records[i - 1].ptr;
      return;
    }
  }
  result.next_level = page->records[cnt - 1].ptr;
}

void Tree::leaf_page_search(LeafPage *page, const Key &k,
                            SearchResult &result) {

  for (int i = 0; i < kLeafCardinality; ++i) {
    auto &r = page->records[i];
    if (r.key == k && r.value != kValueNull && r.f_version == r.r_version) {
      result.val = r.value;
      break;
    }
  }
}

void Tree::internal_page_store(GlobalAddress page_addr, const Key &k,
                               GlobalAddress v, GlobalAddress root, int level,
                               CoroContext *cxt, int coro_id) {
  uint64_t lock_index =
      CityHash64((char *)&page_addr, sizeof(page_addr)) % lockNR;

  GlobalAddress lock_addr;
  lock_addr.nodeID = page_addr.nodeID;
  // lock_addr.offset = align_to_64((page_addr.offset - 33554432) / kLeafPageSize * sizeof(uint64_t));
  lock_addr.offset = lock_index * sizeof(uint64_t);


  auto &rbuf = dsm->get_rbuf(coro_id);
  uint64_t *cas_buffer = rbuf.get_cas_buffer();
  auto page_buffer = rbuf.get_page_buffer();

  auto tag = dsm->getThreadTag();
  assert(tag != 0);

  bool hod = lock_and_read_page(&page_buffer, page_addr, kInternalPageSize, cas_buffer,
                     lock_addr, tag, cxt, coro_id, level, true);

  auto page = (InternalPage *)page_buffer;
  auto lpage = (LeafPage *)page_buffer;

  if (page->hdr.level != level) {
    Debug::notifyError("Tree:913:internal_page_store: page->hdr.level != level");
    cerr << "root: " << root << endl;
    cerr << "page_addr: " << page_addr << endl;
    cerr << "key: " << k << endl;
    cerr << "level: " << (int)level  << endl;
    cerr << "hod: " << hod << endl;
    ln->debug();
    page->hdr.debug();
    lpage->debug();
    assert(page->hdr.level == level);
  }
  assert(page->check_consistent());
  if (k >= page->hdr.highest) {

    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr, level);

    assert(page->hdr.sibling_ptr != GlobalAddress::Null());

    this->internal_page_store(page->hdr.sibling_ptr, k, v, root, level, cxt,
                              coro_id);

    return;
  }
  assert(k >= page->hdr.lowest);
  // if (k < page->hdr.lowest) {
  //   Debug::notifyError("Tree:875:internal_page_store: k < page->hdr.lowest");
  //   std::cout << "root: " << root << std::endl;
  //   std::cout << "page_addr: " << page_addr << std::endl;
  //   std::cout << "key: " << k << std::endl;
  //   page->hdr.debug();
  //   return;
  // }

  auto cnt = page->hdr.last_index + 1;

  bool is_update = false;
  uint16_t insert_index = 0;
  for (int i = cnt - 1; i >= 0; --i) {
    if (page->records[i].key == k) { // find and update
      page->records[i].ptr = v;
      // assert(false);
      is_update = true;
      break;
    }
    if (page->records[i].key < k) {
      insert_index = i + 1;
      break;
    }
  }

  assert(cnt != kInternalCardinality);

  if (!is_update) { // insert and shift
    for (int i = cnt; i > insert_index; --i) {
      page->records[i].key = page->records[i - 1].key;
      page->records[i].ptr = page->records[i - 1].ptr;
    }
    page->records[insert_index].key = k;
    page->records[insert_index].ptr = v;

    page->hdr.last_index++;
  }

  cnt = page->hdr.last_index + 1;
  bool need_split = cnt == kInternalCardinality;
  Key split_key;
  GlobalAddress sibling_addr;
  if (need_split) { // need split
    sibling_addr = dsm->alloc(kInternalPageSize);
    auto sibling_buf = rbuf.get_sibling_buffer();

    auto sibling = new (sibling_buf) InternalPage(page->hdr.level);

    //    std::cout << "addr " <<  sibling_addr << " | level " <<
    //    (int)(page->hdr.level) << std::endl;

    int m = cnt / 2;
    split_key = page->records[m].key;
    assert(split_key > page->hdr.lowest);
    assert(split_key < page->hdr.highest);
    for (int i = m + 1; i < cnt; ++i) { // move
      sibling->records[i - m - 1].key = page->records[i].key;
      sibling->records[i - m - 1].ptr = page->records[i].ptr;
    }
    page->hdr.last_index -= (cnt - m);
    sibling->hdr.last_index += (cnt - m - 1);

    sibling->hdr.leftmost_ptr = page->records[m].ptr;
    sibling->hdr.lowest = page->records[m].key;
    sibling->hdr.highest = page->hdr.highest;
    page->hdr.highest = page->records[m].key;

    // link
    sibling->hdr.sibling_ptr = page->hdr.sibling_ptr;
    page->hdr.sibling_ptr = sibling_addr;

    sibling->set_consistent();
    dsm->write_sync(sibling_buf, sibling_addr, kInternalPageSize, cxt, false);
    treeNodeNR++;
  }

  page->set_consistent();
  write_page_and_unlock(page_buffer, page_addr, kInternalPageSize, cas_buffer,
                        lock_addr, tag, cxt, coro_id, need_split, level, page_buffer, page_addr);

  if (!need_split)
    return;

  if (root == page_addr) { // update root

    if (update_new_root(page_addr, split_key, sibling_addr, level + 1, root,
                        cxt, coro_id)) {
      return;
    }
  }

  auto up_level = path_stack[coro_id][level + 1];

  if (up_level != GlobalAddress::Null()) {
    internal_page_store(up_level, split_key, sibling_addr, root, level + 1, cxt,
                        coro_id);
  } else {
    assert(false);
  }
}

bool Tree::leaf_page_store(GlobalAddress page_addr, const Key &k,
                           const Value &v, GlobalAddress root, int level,
                           CoroContext *cxt, int coro_id, bool from_cache) {

  uint64_t lock_index =
      CityHash64((char *)&page_addr, sizeof(page_addr)) % lockNR;

  
  GlobalAddress lock_addr;
#ifdef CONFIG_ENABLE_EMBEDDING_LOCK
  lock_addr = page_addr;
#else
  lock_addr.nodeID = page_addr.nodeID;
  // lock_addr.offset = align_to_64((page_addr.offset - 33554432) / kLeafPageSize * sizeof(uint64_t)) ;
  lock_addr.offset = lock_index * sizeof(uint64_t);
#endif

  auto &rbuf = dsm->get_rbuf(coro_id);
  uint64_t *cas_buffer = rbuf.get_cas_buffer();
  auto page_buffer = rbuf.get_page_buffer();

  auto tag = dsm->getThreadTag();
  assert(tag != 0);

  bool hod = lock_and_read_page(&page_buffer, page_addr, kLeafPageSize, cas_buffer,
                     lock_addr, tag, cxt, coro_id, level);

  auto page = (LeafPage *)page_buffer;
  auto ln_page = (LeafPage *)ln->page_buffer;
  auto iln_page = (InternalPage *)ln->page_buffer;
  auto ipage = (InternalPage *)page_buffer;
  // if (hod) {
  //   level = page->hdr.level;
  // }

  if (page->hdr.level != level) {
    cerr << "*****************************************************" << endl;
    Debug::notifyError("Tree:leaf_page_store: page->hdr.level != level");
    cerr << "root: " << root << endl;
    cerr << "page_addr: " << page_addr << endl;
    cerr << "key: " << k << endl;
    cerr << "level: " << (int) level  << endl;
    cerr << "hod: " << hod << endl;
    cerr << "from cache: " << from_cache << endl;
    cerr << "lock_addr: " << lock_addr << endl;
    cerr << "*****************************************************" << endl;
    ln->debug();
    ln_page->debug();
    iln_page->debug();
    cerr << "*****************************************************" << endl;
    ipage->debug();
    page->debug();
    cerr << "*****************************************************" << endl;
    dsm->read_sync(page_buffer, page_addr, kLeafPageSize, nullptr);
    auto page = (LeafPage *)page_buffer;
    auto ipage = (InternalPage *)page_buffer;
    cerr << "*****************************************************" << endl;
    ipage->debug();
    page->debug();
    cerr << "*****************************************************" << endl;
    assert(page->hdr.level == level);
    // assert(from_cache);
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr, level);
    return true;
  }

  //TODO: WHY CONSISTENCY CHECK IF PAGE ALREADY LOCKED?
  assert(page->check_consistent());

  if (from_cache &&
      (k < page->hdr.lowest || k >= page->hdr.highest)) { // cache is stale
    // Debug::notifyInfo("stale cache");
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr, level, true);
    return false;
  }

  if (k >= page->hdr.highest) {
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr, level);
    assert(page->hdr.sibling_ptr != GlobalAddress::Null());
    this->leaf_page_store(page->hdr.sibling_ptr, k, v, root, level, cxt,
                          coro_id);
    return true;
  }

  if (k < page->hdr.lowest) {
    Debug::notifyError("Tree:leaf_page_store: k < page->hdr.lowest");
    std::cout << "root: " << root << std::endl;
    std::cout << "page_addr: " << page_addr << std::endl;
    std::cout << "key: " << k << std::endl;
    cerr << "hod: " << hod << endl;
    cerr << "from_cache: " << from_cache << endl;
    cerr << "*****************************************************" << endl;
    ln->debug();
    ln_page->debug();
    iln_page->debug();
    cerr << "*****************************************************" << endl;
    ipage->debug();
    page->debug();
    assert(k >= page->hdr.lowest);
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr, level);
    // insert(k, v);
    measurements.tp[threadID]--;
    return true;
  }

  int cnt = 0;
  int empty_index = -1;
  char *update_addr = nullptr;
  for (int i = 0; i < kLeafCardinality; ++i) {

    auto &r = page->records[i];
    if (r.value != kValueNull) {
      cnt++;
      if (r.key == k) {
        // KEY ALREADY PRESENT --> UPDATE V
        r.value = v;
        r.f_version++;
        r.r_version = r.f_version;
        update_addr = (char *)&r;
        break;
      }
    } else if (empty_index == -1) {
      // FOUND EMPTY RECORD ENTRY --> INSERT V HERE
      empty_index = i;
    }
  }

  // SHOULD NOT HAPPEN, NODE WILL SPLIT WHEN ITS FULL
  assert(cnt != kLeafCardinality);

  if (update_addr == nullptr) { // insert new item
    if (empty_index == -1) {
      // SHOULD NOT HAPPEN, KEY NOT PRESENT && NO FREE ENTRY
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
        sizeof(LeafEntry), cas_buffer, lock_addr, tag, cxt, coro_id, false, level, page_buffer, page_addr);

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

    // if (!(page->hdr.last_index > 0 && sibling->hdr.last_index > 0)) {
    //   cerr << "cnt: " << cnt << endl;
    //   cerr << "m: " << m << endl;
    //   cerr << "page->hdr.last_index: " << page->hdr.last_index << endl;
    //   cerr << "sibling->hdr.last_index: " << sibling->hdr.last_index << endl;
    //   page->debug();
    //   sibling->debug();
    //   assert(page->hdr.last_index > 0 && sibling->hdr.last_index > 0);
    // }

    dsm->write_sync(sibling_buf, sibling_addr, kLeafPageSize, cxt, false);
    treeNodeNR++;
  }

  page->set_consistent();

  //TODO: WHY CAN WE DO AN ASYNC WB ON SPLIT?
  write_page_and_unlock(page_buffer, page_addr, kLeafPageSize, cas_buffer,
                        lock_addr, tag, cxt, coro_id, need_split, level, page_buffer, page_addr);

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

bool Tree::leaf_page_del(GlobalAddress page_addr, const Key &k, int level,
                         CoroContext *cxt, int coro_id, bool from_cache) {
  uint64_t lock_index =
      CityHash64((char *)&page_addr, sizeof(page_addr)) % lockNR;

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

  lock_and_read_page(&page_buffer, page_addr, kLeafPageSize, cas_buffer,
                     lock_addr, tag, cxt, coro_id);

  auto page = (LeafPage *)page_buffer;

  assert(page->hdr.level == level);
  assert(page->check_consistent());

  if (from_cache &&
      (k < page->hdr.lowest || k >= page->hdr.highest)) { // cache is stale
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr, true);
    return false;
  }

  if (k >= page->hdr.highest) {
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, true, page_buffer, page_addr);
    assert(page->hdr.sibling_ptr != GlobalAddress::Null());
    this->leaf_page_del(page->hdr.sibling_ptr, k, level, cxt, coro_id);
    return true;
  }

  assert(k >= page->hdr.lowest);

  char *update_addr = nullptr;
  for (int i = 0; i < kLeafCardinality; ++i) {
    auto &r = page->records[i];
    if (r.key == k && r.value != kValueNull) {
      r.value = kValueNull;
      r.f_version++;
      r.r_version = r.f_version;
      update_addr = (char *)&r;
      break;
    }
  }

  if (update_addr) {
    write_page_and_unlock(
        update_addr, GADD(page_addr, (update_addr - (char *)page)),
        sizeof(LeafEntry), cas_buffer, lock_addr, tag, cxt, coro_id, false, 0, page_buffer, page_addr);
  } else {
    this->unlock_addr(lock_addr, tag, cas_buffer, cxt, coro_id, false);
  }
  return true;
}

void Tree::run_coroutine(CoroFunc func, int id, int coro_cnt) {

  using namespace std::placeholders;

  assert(coro_cnt <= define::kMaxCoro);
  for (int i = 0; i < coro_cnt; ++i) {
    auto gen = func(i, dsm, id);
    worker[i] = CoroCall(std::bind(&Tree::coro_worker, this, _1, gen, i));
  }

  master = CoroCall(std::bind(&Tree::coro_master, this, _1, coro_cnt));

  master();
}

void Tree::coro_worker(CoroYield &yield, RequstGen *gen, int coro_id) {
  CoroContext ctx;
  ctx.coro_id = coro_id;
  ctx.master = &master;
  ctx.yield = &yield;

  Timer coro_timer;
  auto thread_id = dsm->getMyThreadID();

  while (true) {

    auto r = gen->next();

    coro_timer.begin();
    if (r.is_search) {
      Value v;
      this->search(r.k, v, &ctx, coro_id);
    } else {
      this->insert(r.k, r.v, &ctx, coro_id);
    }
    auto us_10 = coro_timer.end() / 100;
    if (us_10 >= LATENCY_WINDOWS) {
      us_10 = LATENCY_WINDOWS - 1;
    }
    // latency[thread_id][us_10]++;
  }
}

void Tree::coro_master(CoroYield &yield, int coro_cnt) {

  for (int i = 0; i < coro_cnt; ++i) {
    yield(worker[i]);
  }

  while (true) {

    uint64_t next_coro_id;

    if (dsm->poll_rdma_cq_once(next_coro_id)) {
      yield(worker[next_coro_id]);
    }

    if (!hot_wait_queue.empty()) {
      next_coro_id = hot_wait_queue.front();
      hot_wait_queue.pop();
      yield(worker[next_coro_id]);
    }
  }
}

// Local Locks
inline bool Tree::acquire_local_lock(GlobalAddress lock_addr, CoroContext *cxt,
                                     int coro_id) {

  auto &node = local_locks[lock_addr.nodeID][lock_addr.offset / 8];
  uint64_t lock_val = node.ticket_lock.fetch_add(1);
  ln = &local_locks[lock_addr.nodeID][lock_addr.offset / 8];
  #ifdef SHERMAN_LOCK
  uint32_t ticket = lock_val << 32 >> 32;
  uint32_t current = lock_val >> 32;

  while (ticket != current) { // lock failed

    if (cxt != nullptr) {
      hot_wait_queue.push(coro_id);
      (*cxt->yield)(*cxt->master);
    }

    current = node.ticket_lock.load(std::memory_order_relaxed) >> 32;
  }

  node.hand_time++;

  #endif

  #ifdef LITL
  // node.ticket_lock.fetch_add(1, std::memory_order_acq_rel);
  pthread_mutex_lock((pthread_mutex_t *) &node.litl_lock);
  node.ticket_lock.fetch_add(-1);
  node.hand_time++;

  #endif
  // DEB("[%d.%d] acquired the local lock: %lu\n", dsm->getMyNodeID(), dsm->getMyThreadID(), lock_addr.offset);
  return node.hand_over;
}

inline bool Tree::can_hand_over(GlobalAddress lock_addr) {
  auto &node = local_locks[lock_addr.nodeID][lock_addr.offset / 8];
  #ifdef SHERMAN_LOCK

  uint64_t lock_val = node.ticket_lock.load(std::memory_order_relaxed);
  uint32_t ticket = lock_val << 32 >> 32;
  uint32_t current = lock_val >> 32;

  if (ticket <= current + 1) { // no pending locks
    node.hand_over = false;
  } else {
    node.hand_over = node.hand_time < maxHandover;
  }
  if (!node.hand_over) {
    node.hand_time = 0;
  }
  
  return node.hand_over;
  #endif

  node.hand_over = node.ticket_lock.load(std::memory_order_relaxed) > 0 && node.hand_time < maxHandover;
  if (!node.hand_over) {
    node.hand_time = 0;
  }
  return node.hand_over;
}

inline void Tree::releases_local_lock(GlobalAddress lock_addr) {
  timer.begin();
  auto &node = local_locks[lock_addr.nodeID][lock_addr.offset / 8];
  #ifdef SHERMAN_LOCK
  node.ticket_lock.fetch_add((1ull << 32));
  #endif

  #ifdef LITL
  pthread_mutex_unlock((pthread_mutex_t *) &node.litl_lock);
  #endif

  save_measurement(threadID, measurements.lwait_rel);
}

void Tree::index_cache_statistics() {
  index_cache->statistics();
  index_cache->bench();
}

void Tree::clear_statistics() {
  for (int i = 0; i < MAX_APP_THREAD; ++i) {
    cache_hit[i][0] = 0;
    cache_miss[i][0] = 0;
  }
}

GlobalAddress Tree::get_lock_addr(GlobalAddress base_addr) {
	uint64_t lock_index =
		CityHash64((char *)&base_addr, sizeof(base_addr)) % lockNR;

	GlobalAddress lock_addr;
	lock_addr.nodeID = base_addr.nodeID;
	lock_addr.offset = lock_index * sizeof(uint64_t);
	return lock_addr;
}

void Tree::get_bufs() {
	auto rbuf = dsm->get_rbuf(0);
	curr_cas_buffer = rbuf.get_cas_buffer();
	curr_page_buffer = rbuf.get_page_buffer();
}

void Tree::mb_lock(GlobalAddress base_addr, GlobalAddress lock_addr, int data_size) {
  curr_lock_addr = lock_addr;
  // ln = &local_locks[curr_lock_addr.nodeID][curr_lock_addr.offset / 8];
  // Debug::notifyError("data_addr: %lu\nsize %d", base_addr.offset, data_size);
  // Debug::notifyError("lock_addr: %lu\n", curr_lock_addr.offset);
  
	get_bufs();
	auto tag = dsm->getThreadTag();

	bool handover = try_lock_addr(curr_lock_addr, tag, curr_cas_buffer, NULL, 0);
	if (data_size > 0) {
    #ifndef HANDOVER_DATA
    timer.begin();
		dsm->read_sync(curr_page_buffer, base_addr, data_size, NULL);
    save_measurement(threadID, measurements.data_read);
    return;
    #endif

    bool same_address = ln->page_addr.val == base_addr.val;
    // cerr << ln->page_addr << " ?==? " << base_addr << " " << same_address << endl;
    if ((!handover || !same_address) && !from_peer) {
      timer.begin();
      curr_page_buffer = dsm->get_rbuf(0).get_page_buffer();
      dsm->read_sync(curr_page_buffer, base_addr, data_size, NULL);
      save_measurement(threadID, measurements.data_read);
      // cerr << "********************************************" << endl;
      // cerr << "NO DATA HO: " << "[" + to_string(dsm->getMyNodeID()) + "." + to_string(dsm->getMyThreadID()) + "]" << endl;
      // cerr << "lock_addr: " << lock_addr << endl; 
      // cerr << "base_addr: " << base_addr << endl;
      // cerr << "curr_page_buffer: " << (uintptr_t) curr_page_buffer << " = " << (uint64_t) *curr_page_buffer << endl;
      // cerr << "********************************************" << endl;
    } else {
      curr_page_buffer = ln->page_buffer;
      measurements.handovers_data[threadID]++;
      if (from_peer) {
        measurements.c_hod[threadID]++;
        // cerr << "RECEIVED DATA: " << endl;
        // uint64_t * long_data = (uint64_t *) curr_page_buffer;
        // for (size_t i = 0; i < data_size/sizeof(uint64_t); i++) {
        //   cerr << long_data[i] << ", ";
        // }
        // cerr << endl;

      }
      // cerr << "********************************************" << endl;
      // cerr << "DATA HO: " << "[" + to_string(dsm->getMyNodeID()) + "." + to_string(dsm->getMyThreadID()) + "]" << endl;
      // cerr << "lock_addr: " << lock_addr << endl; 
      // cerr << "base_addr: " << base_addr << endl;
      // cerr << "curr_page_buffer: " << (uintptr_t) curr_page_buffer << " = " << (uint64_t) *curr_page_buffer << endl;
      // cerr << "********************************************" << endl;
    }
	}
}

void Tree::mb_unlock(GlobalAddress base_addr, int data_size) {
	auto tag = dsm->getThreadTag();
	assert(tag != 0);
	if (data_size > 0) {
		write_page_and_unlock(curr_page_buffer, base_addr, data_size, curr_cas_buffer,
			curr_lock_addr, tag, NULL, 0, false, 0, curr_page_buffer, base_addr);
	}
	else {
		unlock_addr(curr_lock_addr, tag, curr_cas_buffer, NULL, 0, false);
	}
}

void Tree::set_IDs(int nid, int tid) {
    nodeID = nid;
    threadID = tid;
}


void Tree::wait() {
  GLockAddress ga;
  ga.nodeID = dsm->getMyNodeID();
  ga.threadID = dsm->getMyThreadID();
  dsm->wait_for_peer(ga, threadID);
}

void Tree::contact() {
  GLockAddress ga;
  ga.nodeID = dsm->getMyNodeID() == 1 ? 0 : 1;
  ga.threadID = 0;
  dsm->wakeup_peer(ga, threadID);
}


uint64_t Tree::test_self_cas(GLockAddress gaddr, bool with_read) {
  uint64_t *buf = dsm->get_rbuf(0).get_cas_buffer();
  char *pbuf = dsm->get_rbuf(0).get_page_buffer();
  uint64_t failed_cases = 0;
  while (!dsm->cas_peer_sync(gaddr, 0, 1, buf, nullptr)) {
    Debug::notifyError("INCONSISTENT CAS to 1");
    failed_cases++;
  }
  if (with_read) {
    GlobalAddress read_addr{0,256};
    dsm->read_sync(pbuf, read_addr, 1024, nullptr);
  }
  while (!dsm->cas_peer_sync(gaddr, 1, 0, buf, nullptr)) {
    Debug::notifyError("INCONSISTENT CAS to 0");
    failed_cases++;
  }
  return failed_cases;
}

uint64_t Tree::node0(uint64_t cnt) {
  GLockAddress next_gaddr = GLockAddress::Null();
  next_gaddr.nodeID = 0;
  next_gaddr.offset = 0;
  GLockAddress other_gaddr = GLockAddress::Null();
  uint64_t *buf = dsm->get_rbuf(0).get_cas_buffer();
  uint64_t failed_cases = 0;
  uint64_t fails = 0;
  uint64_t completed = 0;

  while (completed < cnt) {
    if (!dsm->cas_peer_sync(next_gaddr, 0, 1, buf, nullptr)) {
      other_gaddr = *(GLockAddress *) buf;
      if (dsm->cas_peer_sync(next_gaddr, other_gaddr.val, 0, buf, nullptr)) {
        dsm->wakeup_peer(other_gaddr, threadID);
        completed++;
        fails = 0;
      } else {
        while (!dsm->cas_peer_sync(next_gaddr, other_gaddr.val, 0, buf, nullptr)) {

          Debug::notifyError("FAILED TO RESET SELF FROM OTHER CAS\n");
          fails++;
          failed_cases++;
          if (fails > 10) {
            Debug::notifyError("TOO MANY FAILED CASES --> TERMINATING\n");
            return failed_cases;
          }
        }
      }
  } else {
      while (!dsm->cas_peer_sync(next_gaddr, 1, 0, buf, nullptr)) {
        Debug::notifyError("FAILED TO RESET SELF FROM SELF CAS\n");
        fails++;
        failed_cases++;
        if (fails > 10) {
          Debug::notifyError("TOO MANY FAILED CASES --> TERMINATING\n");
          return failed_cases;
        }
      }
    }
    fails = 0;
  }
  return failed_cases;
}

uint64_t Tree::node1(uint64_t cnt) {
  GLockAddress next_gaddr = GLockAddress::Null();
  next_gaddr.nodeID = 1;
  next_gaddr.offset = 8;
  GLockAddress other_gaddr = GLockAddress::Null();
  other_gaddr.nodeID = 0;
  other_gaddr.offset = 0;
  uint64_t *buf = dsm->get_rbuf(0).get_cas_buffer();
  uint64_t failed_cases = 0;
  uint64_t fails = 0;
  uint64_t completed = 0;

  while (completed < cnt) {
    while (!dsm->cas_peer_sync(other_gaddr, 0, next_gaddr.val, buf, nullptr)) {}
    dsm->wait_for_peer(other_gaddr, threadID);
    completed++;
  }
  return failed_cases;
}


void Tree::test_write_peer() {
  // char* pbuf = dsm->get_rbuf(0).get_page_buffer();
  char* pbuf = (char*)dsm->getBaseAddr();
  GlobalAddress gaddr = GlobalAddress::Null();
  gaddr.nodeID = 1;
  *(uint64_t*) pbuf = 1;
  dsm->write_peer_sync(pbuf, gaddr, 1024, nullptr, true);
  cerr << "WRITTEN TO PEER: " << *(uint64_t *) pbuf << endl;
}

void Tree::test_spin() {
  uint64_t *spinloc = (uint64_t *) dsm->getCacheAddr();
  *spinloc = 0;
  while(*spinloc == 0) {
    CPU_PAUSE();
  }
  cerr << "WOKE UP" << endl;
}