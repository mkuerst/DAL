#ifndef __GLOCKADDRESS_H__
#define __GLOCKADDRESS_H__

#include "Common.h"


class GLockAddress {
public:

union {
  struct {
  uint64_t nodeID: 8;
  uint64_t threadID: 8;
  uint64_t version: 16;
  uint64_t offset : 32;
  };
  uint64_t val;
};

 operator uint64_t() {
  return val;
}

  static GLockAddress Null() {
    static GLockAddress zero{0, 0, 0, 0};
    return zero;
  };
} __attribute__((packed));

static_assert(sizeof(GLockAddress) == sizeof(uint64_t), "XXX");

inline GLockAddress GADD(const GLockAddress &addr, int off) {
  auto ret = addr;
  ret.offset += off;
  return ret;
}

inline bool operator==(const GLockAddress &lhs, const GLockAddress &rhs) {
  return (lhs.nodeID == rhs.nodeID) && (lhs.offset == rhs.offset);
}

inline bool operator!=(const GLockAddress &lhs, const GLockAddress &rhs) {
  return !(lhs == rhs);
}

inline std::ostream &operator<<(std::ostream &os, const GLockAddress &obj) {
  os << "[" << (int)obj.nodeID << ", " << obj.threadID << ", " << ", " << obj.version << obj.offset << "]";
  return os;
}

#endif /* __GLockAddress_H__ */
