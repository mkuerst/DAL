#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "Common.h"

class CacheConfig {
public:
  uint32_t cacheSize;

  CacheConfig(uint32_t cacheSize = 1) : cacheSize(cacheSize) {}
};

class DSMConfig {
public:
  CacheConfig cacheConfig;
  uint32_t machineNR;
  uint32_t threadNR;
  uint64_t dsmSize; // G
  uint32_t mnNR;
  uint64_t chipSize;
  uint64_t lockMetaSize;
  uint64_t lockNR;

  DSMConfig(const CacheConfig &cacheConfig = CacheConfig(),
            uint32_t machineNR = 2, uint32_t threadNR = 1, uint64_t dsmSize = 8,
            uint32_t mnNR = 1, uint64_t chipSize = 128, uint64_t lockMetaSize = 128,
            uint64_t lockNR = 0)
      : cacheConfig(cacheConfig), machineNR(machineNR), threadNR(threadNR), 
      dsmSize(dsmSize), mnNR(mnNR), chipSize(chipSize), lockMetaSize(lockMetaSize),
      lockNR(lockNR) {}
};

#endif /* __CONFIG_H__ */
