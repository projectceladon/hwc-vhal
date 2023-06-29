//#define LOG_NDEBUG 0

#include <cutils/log.h>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <cutils/properties.h>

#include "RemoteDisplayMgr.h"

RemoteDisplayMgr::RemoteDisplayMgr() {}

RemoteDisplayMgr::~RemoteDisplayMgr() {
  if (mServerFd >= 0) {
    close(mServerFd);
  }
  if (mWorkerEventReadPipeFd >= 0) {
    close(mWorkerEventReadPipeFd);
  }
  if (mWorkerEventWritePipeFd >= 0) {
    close(mWorkerEventWritePipeFd);
  }
}

int RemoteDisplayMgr::init(IRemoteDevice* dev) {
  mEpollFd = epoll_create(kMaxEvents);
  if (mEpollFd == -1) {
    ALOGE("epoll_create:%s", strerror(errno));
    return -1;
  }

  mHwcDevice = std::unique_ptr<IRemoteDevice>(dev);
  mMaxConnections = mHwcDevice->getMaxRemoteDisplayCount();

  // start worker event for device remove
  int workerEventFds[2];
  int ret = pipe(workerEventFds);
  if (ret < 0) {
    ALOGE("Failed to create worker pipe");
    return -1;
  }
  mWorkerEventReadPipeFd = workerEventFds[0];
  mWorkerEventWritePipeFd = workerEventFds[1];
  setNonblocking(mWorkerEventReadPipeFd);
  addEpollFd(mWorkerEventReadPipeFd);

  mSocketThread = std::unique_ptr<std::thread>(
      new std::thread(&RemoteDisplayMgr::socketThreadProc, this));

  return 0;
}

int RemoteDisplayMgr::addRemoteDisplay(int fd) {
  ALOGV("%s(%d)", __func__, fd);

  setNonblocking(fd);
  addEpollFd(fd);

  mRemoteDisplays.emplace(fd, fd);
  auto& remote = mRemoteDisplays.at(fd);
  remote.setDisplayStatusListener(this);
  if (remote.getConfigs() < 0) {
    ALOGE("Failed to init remote display!");
    return -1;
  }
  return 0;
}

int RemoteDisplayMgr::removeRemoteDisplay(int fd) {
  ALOGV("%s(%d)", __func__, fd);

  delEpollFd(fd);

  if (mRemoteDisplays.find(fd) != mRemoteDisplays.end()) {
    mHwcDevice->removeRemoteDisplay(&mRemoteDisplays.at(fd));
    mRemoteDisplays.erase(fd);
  }

  return 0;
}

int RemoteDisplayMgr::connectToRemote() {
  ALOGV("%s", __func__);

  struct sockaddr_un addr;
  std::unique_lock<std::mutex> lck(mConnectionMutex);

  mClientFd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (mClientFd < 0) {
    ALOGD("Can't create socket, it will run as server mode");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (strlen(kClientSock) > sizeof(addr.sun_path)) {
    return -1;
  }
  strncpy(&addr.sun_path[0], kClientSock, strlen(kClientSock));
  if (connect(mClientFd, (struct sockaddr*)&addr,
              sizeof(sa_family_t) + strlen(kClientSock) + 1) < 0) {
    ALOGD("Can't connect to remote, it will run as server mode");
    close(mClientFd);
    mClientFd = -1;
    return -1;
  }
  if (mClientFd >= 0) {
    addRemoteDisplay(mClientFd);
  }

  // wait the display config ready
  mClientConnected.wait(lck);
  return 0;
}

int RemoteDisplayMgr::disconnectToRemote() {
  ALOGV("%s", __func__);

  removeRemoteDisplay(mClientFd);
  close(mClientFd);
  mClientFd = -1;
  return 0;
}

int RemoteDisplayMgr::onConnect(int fd) {
  std::unique_lock<std::mutex> lck(mConnectionMutex);

  if (mRemoteDisplays.find(fd) != mRemoteDisplays.end()) {
    ALOGI("Remote Display %d connected", fd);
    mHwcDevice->addRemoteDisplay(&mRemoteDisplays.at(fd));
  }
  mClientConnected.notify_all();
  return 0;
}

int RemoteDisplayMgr::onSetMode(int fd) {
  std::unique_lock<std::mutex> lck(mConnectionMutex);

  if (mRemoteDisplays.find(fd) != mRemoteDisplays.end()) {
    ALOGI("Remote Display %d setMode", fd);
    mHwcDevice->setMode(&mRemoteDisplays.at(fd));
  }
  mClientConnected.notify_all();
  return 0;
}

int RemoteDisplayMgr::onDisconnect(int fd) {
  ALOGI("Remote Display %d disconnected", fd);

  std::unique_lock<std::mutex> lk(mWorkerMutex);

  mPendingRemoveDisplays.push_back(fd);
  // notify the epoll thread
  write(mWorkerEventWritePipeFd, "D", 1);
  return 0;
}

int RemoteDisplayMgr::setNonblocking(int fd) {
  int flag = 1;
  if (ioctl(fd, FIONBIO, &flag) < 0) {
    ALOGE("set client socket to FIONBIO failed");
    return -1;
  }
  return 0;
}

int RemoteDisplayMgr::addEpollFd(int fd) {
  struct epoll_event ev;

  ev.events = EPOLLIN;
  ev.data.fd = fd;
  if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    ALOGE("epoll_ctl add fd %d:%s", fd, strerror(errno));
    exit(EXIT_FAILURE);
  }
  return 0;
}

int RemoteDisplayMgr::delEpollFd(int fd) {
  struct epoll_event ev;

  ev.events = EPOLLIN;
  ev.data.fd = fd;
  if (epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, &ev) == -1) {
    ALOGE("epoll_ctl del fd %d:%s", fd, strerror(errno));
    exit(EXIT_FAILURE);
  }
  return 0;
}

void RemoteDisplayMgr::socketThreadProc() {
  mServerFd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (mServerFd < 0) {
    ALOGE("Failed to create server socket");
    return;
  }

  char value[PROPERTY_VALUE_MAX];
  property_get("ro.boot.container.id", value, "0");
  char kServerSockId[50];
  char* p_env = NULL;
  p_env = getenv("K8S_ENV");
  if ((p_env != NULL) && strcmp(p_env, "true") == 0)
    sprintf(kServerSockId,"%s","/conn/hwc-sock");
  else
    sprintf(kServerSockId,"%s%s","/ipc/hwc-sock",value);

  setNonblocking(mServerFd);
  addEpollFd(mServerFd);

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(&addr.sun_path[0], kServerSockId, strlen(kServerSockId));

  unlink(kServerSockId);
  if (bind(mServerFd, (struct sockaddr*)&addr,
           sizeof(sa_family_t) + strlen(kServerSockId) + 1) < 0) {
    ALOGE("Failed to bind server socket address");
    return;
  }

  // TODO: use group access only for security
  struct stat st;
  __mode_t mod = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  if (fstat(mServerFd, &st) == 0) {
    mod |= st.st_mode;
  }
  if (chmod(kServerSockId, mod)) {
    return;
  }

  if (listen(mServerFd, 1) < 0) {
    ALOGE("Failed to listen on server socket");
    return;
  }

  while (true) {
    if (NULL == mHwcDevice) {
      ALOGE("RemoteDisplayMgr::socketThreadProc is exiting!");
      break;
    }
    struct epoll_event events[kMaxEvents];
    for (int n = 0; n < kMaxEvents; ++n) {
      memset(&events[n], 0, sizeof(events[n]));
    }

    int nfds = epoll_wait(mEpollFd, events, kMaxEvents, -1);
    if (nfds < 0) {
      nfds = 0;
      if (errno != EINTR) {
        ALOGE("epoll_wait:%s", strerror(errno));
      }
    }

    for (int n = 0; n < nfds; ++n) {
      if (events[n].data.fd == mServerFd) {
        struct sockaddr_un addr;
        socklen_t sockLen = 0;
        int clientFd = -1;

        clientFd = accept(mServerFd, (struct sockaddr*)&addr, &sockLen);
        if (clientFd < 0) {
          ALOGE("Failed to accept client connection");
          break;
        }
        if (mHwcDevice->getRemoteDisplayCount() < mMaxConnections) {
          addRemoteDisplay(clientFd);
        } else {
          ALOGD("Can't accept more than %d remote displays!", mMaxConnections);
          close(clientFd);
        }
      } else if (events[n].data.fd == mWorkerEventReadPipeFd) {
        char buf[16];
        if (read(mWorkerEventReadPipeFd, buf, sizeof(buf)) > 0){
          std::unique_lock<std::mutex> lk(mWorkerMutex);
          for (auto fd : mPendingRemoveDisplays) {
            removeRemoteDisplay(fd);
          }
          mPendingRemoveDisplays.clear();
        }
      } else {
        int fd = events[n].data.fd;
        if (mRemoteDisplays.find(fd) != mRemoteDisplays.end()) {
          mRemoteDisplays.at(fd).onDisplayEvent();
        } else {
          // This shouldn't happen, something is wrong if go here
          ALOGE("No remote display for %d", events[n].data.fd);
          delEpollFd(events[n].data.fd);
          close(events[n].data.fd);
        }
      }
    }
  }
}
