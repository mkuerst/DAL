#ifndef __DIRECTORY_H__
#define __DIRECTORY_H__

#include <thread>
#include <pthread.h>

#include <unordered_map>

#include "Common.h"

#include "Connection.h"
#include "GlobalAllocator.h"


class Directory {
public:
  Directory(DirectoryConnection *dCon, RemoteConnection *remoteInfo,
            uint32_t machineNR, uint16_t dirID, uint16_t nodeID);

  ~Directory();
  std::atomic_bool stopDirThread{false};
  DirectoryConnection *getDCon() { return dCon; }

  static void* dirThreadWrapper(void* arg) {
    static_cast<Directory*>(arg)->dirThread();
    return nullptr;
}

private:
  DirectoryConnection *dCon;
  RemoteConnection *remoteInfo;

  uint32_t machineNR;
  uint16_t dirID;
  uint16_t nodeID;

  // std::thread *dirTh;
  pthread_t dirTh;

  GlobalAllocator *chunckAlloc;

  void dirThread();

  void sendData2App(const RawMessage *m);

  void process_message(const RawMessage *m);

};

#endif /* __DIRECTORY_H__ */
