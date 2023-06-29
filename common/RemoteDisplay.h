#ifndef __REMOTE_DISPLAY_H__
#define __REMOTE_DISPLAY_H__

#include <hardware/hwcomposer2.h>

#include <vector>

#include "IRemoteDevice.h"
#include "display_protocol.h"

class RemoteDisplay {
 public:
  RemoteDisplay(int fd);
  RemoteDisplay(const RemoteDisplay& r){
    mSocketFd = r.mSocketFd;
  }

  RemoteDisplay& operator=(const RemoteDisplay& r){
    mSocketFd = r.mSocketFd;
    return *this;
  }
  virtual ~RemoteDisplay();

  int port() const { return mPort; }
  int width() const { return mWidth; }
  int height() const { return mHeight; }
  int fps() const { return mFramerate; }
  int xdpi() const { return mXDpi; }
  int ydpi() const { return mYDpi; }
  uint32_t flags() const { return mDisplayFlags.value; }

  uint64_t getDisplayId() const { return mDisplayId; }
  void setDisplayId(uint64_t id) { mDisplayId = id; }
  int setDisplayStatusListener(DisplayStatusListener* listener) {
    mStatusListener = listener;
    return 0;
  }

  int setDisplayEventListener(DisplayEventListener* listener) {
    mEventListener = listener;
    return 0;
  }

  // requests sent to remote
  int getConfigs();
  int sendDisplayPortReq();
  int createBuffer(buffer_handle_t buffer);
  int removeBuffer(buffer_handle_t buffer);
  int displayBuffer(buffer_handle_t buffer, const display_control_t* ctrl);
  int setRotation(int rotation);
  int createLayer(uint64_t id);
  int removeLayer(uint64_t id);
  int updateLayers(std::vector<layer_info_t>& layerInfo);
  int presentLayers(std::vector<layer_buffer_info_t>& layerBuffer);

  // events from remote
  int onDisplayEvent();

 private:
  int _send(const void* buf, size_t n);
  int _recv(void* buf, size_t n);
  int _sendFds(int* pfd, size_t fdlen);
  int onDisplayInfoAck(const display_event_t& ev);
  int onDisplayPortAck(const display_event_t& ev);
  int onDisplayBufferAck(const display_event_t& ev);
  int onPresentLayersAck(const display_event_t& ev);
  int onSetMode(const display_event_t& ev);
  int onSetVideoAlpha(const display_event_t& ev);

 private:
  bool mDisconnected = false;
  uint64_t mDisplayId = 0;
  int mSocketFd = -1;
  DisplayStatusListener* mStatusListener = nullptr;
  DisplayEventListener* mEventListener = nullptr;

  uint32_t mPort = 0;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  uint32_t mFramerate = 0;
  uint32_t mXDpi = 0;
  uint32_t mYDpi = 0;

  display_flags mDisplayFlags = {.value = 0};
};

#endif  // __REMOTE_DISPLAY_H__
