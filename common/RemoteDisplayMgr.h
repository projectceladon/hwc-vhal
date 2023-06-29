#ifndef __REMOTE_DISPLAY_MGR_H__
#define __REMOTE_DISPLAY_MGR_H__

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "IRemoteDevice.h"
#include "RemoteDisplay.h"

class RemoteDisplayMgr : public DisplayStatusListener {
 public:
  RemoteDisplayMgr();

  RemoteDisplayMgr(const RemoteDisplayMgr& r){
      mServerFd = r.mServerFd;
      mWorkerEventReadPipeFd = r.mWorkerEventReadPipeFd;
      mWorkerEventWritePipeFd = r.mWorkerEventWritePipeFd;
  }

  RemoteDisplayMgr& operator=(const RemoteDisplayMgr& r){
      mServerFd = r.mServerFd;
      mWorkerEventReadPipeFd = r.mWorkerEventReadPipeFd;
      mWorkerEventWritePipeFd = r.mWorkerEventWritePipeFd;
      return *this;
  }

  ~RemoteDisplayMgr();

  int init(IRemoteDevice* dev);
  // hwc as client, legacy to compatible mdc
  int connectToRemote();
  int disconnectToRemote();

  // DisplayStatusListener
  int onConnect(int fd) override;
  int onDisconnect(int fd) override;
  int onSetMode(int fd) override;

 private:
  int addRemoteDisplay(int fd);
  int removeRemoteDisplay(int fd);
  void socketThreadProc();
  void workerThreadProc();

  int setNonblocking(int fd);
  int addEpollFd(int fd);
  int delEpollFd(int fd);

 private:
  const char* kClientSock = "/ipc/display-sock";

  std::unique_ptr<IRemoteDevice> mHwcDevice;
  int mClientFd = -1;
  std::mutex mConnectionMutex;
  std::condition_variable mClientConnected;

  std::unique_ptr<std::thread> mSocketThread;
  int mServerFd = -1;
  int mMaxConnections = 2;

  std::vector<int> mPendingRemoveDisplays;
  std::mutex mWorkerMutex;
  int mWorkerEventReadPipeFd = -1;
  int mWorkerEventWritePipeFd = -1;

  std::map<int, RemoteDisplay> mRemoteDisplays;
  static const int kMaxEvents = 10;
  int mEpollFd = -1;
};
#endif  //__REMOTE_DISPLAY_MGR_H__
