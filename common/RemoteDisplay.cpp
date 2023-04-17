//#define LOG_NDEBUG 0

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/properties.h>
#include <cutils/log.h>

#include <sys/socket.h>

#include "RemoteDisplay.h"

#include "BufferMapper.h"

//#define DEBUG_LAYER
#ifdef DEBUG_LAYER
#define LAYER_TRACE(...) ALOGD(__VA_ARGS__)
#else
#define LAYER_TRACE(...)
#endif

RemoteDisplay::RemoteDisplay(int fd) : mSocketFd(fd) {}
RemoteDisplay::~RemoteDisplay() {
  if (mSocketFd >= 0) {
    ALOGD("Close socket %d", mSocketFd);
    close(mSocketFd);
  }
}

int RemoteDisplay::_send(const void* buf, size_t n) {
  ALOGV("RemoteDisplay(%d)::%s size=%zd", mSocketFd, __func__, n);

  if (mDisconnected)
    return -1;

  if (!buf || n <= 0)
    return 0;

  ssize_t len;
  len = send(mSocketFd, buf, n, 0);
  if (len <= 0) {
    mDisconnected = true;
    if (mStatusListener) {
      mStatusListener->onDisconnect(mSocketFd);
    }
    return -1;
  }
  return 0;
}
int RemoteDisplay::_recv(void* buf, size_t n) {
  ALOGV("RemoteDisplay(%d)::%s size=%zd", mSocketFd, __func__, n);

  if (mDisconnected)
    return -1;

  if (!buf || n <= 0)
    return 0;

  ssize_t len;
  len = recv(mSocketFd, buf, n, 0);
  if (len <= 0) {
    mDisconnected = true;
    if (mStatusListener) {
      mStatusListener->onDisconnect(mSocketFd);
    }
    return -1;
  }
  return 0;
}

int RemoteDisplay::_sendFds(int* pfd, size_t fdlen) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  if (mDisconnected)
    return -1;

  int count = 0;
  int i = 0;
  struct msghdr msg;
  struct cmsghdr* p_cmsg = NULL;
  struct iovec vec;
  char cmsgbuf[CMSG_SPACE(fdlen * sizeof(int))];
  int* p_fds = NULL;
  int sdata[4] = {
      0x88,
  };

  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);
  p_cmsg = CMSG_FIRSTHDR(&msg);
  if (p_cmsg == NULL) {
    return -1;
  }
  p_cmsg->cmsg_level = SOL_SOCKET;
  p_cmsg->cmsg_type = SCM_RIGHTS;
  p_cmsg->cmsg_len = CMSG_LEN(fdlen * sizeof(int));
  p_fds = (int*)CMSG_DATA(p_cmsg);

  for (i = 0; i < (int)fdlen; i++) {
    p_fds[i] = pfd[i];
  }

  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = &vec;
  msg.msg_iovlen = 1;
  msg.msg_flags = 0;

  vec.iov_base = sdata;
  vec.iov_len = 16;
  count = sendmsg(mSocketFd, &msg, 0);
  if (count <= 0) {
    mDisconnected = true;
    if (mStatusListener) {
      mStatusListener->onDisconnect(mSocketFd);
    }
    return -1;
  }

  return 0;
}

int RemoteDisplay::getConfigs() {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  char value[PROPERTY_VALUE_MAX];
  display_event_t req;

  memset(&req, 0, sizeof(req));
  req.type = DD_EVENT_DISPINFO_REQ;
  req.size = sizeof(req);
  property_get("ro.container.id", value, "0");
  req.id = atoi(value);
  memset(value, 0, sizeof(value));
  property_get("ro.acg.rnode", value, "0");
  req.pad = atoi(value);
  if (_send(&req, sizeof(req)) < 0) {
    ALOGE("%s:%d: Can't send display info request\n", __func__, __LINE__);
    return -1;
  }
  return 0;
}

int RemoteDisplay::sendDisplayPortReq() {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);
  display_event_t req;

  memset(&req, 0, sizeof(req));
  req.type = DD_EVENT_DISPPORT_REQ;
  req.size = sizeof(req);
  if (_send(&req, sizeof(req)) < 0) {
    ALOGE("%s:%d: Can't send display port request\n", __func__, __LINE__);
    return -1;
  }
  return 0;
}

int RemoteDisplay::createBuffer(buffer_handle_t b) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  BufferMapper& mapper = BufferMapper::getMapper();

  buffer_info_event_t ev;
  int64_t bufferId = (int64_t)b;
  buffer_handle_t buffer = b;
  bool isGralloc1Buffer = mapper.isGralloc1(b);
  cros_gralloc_handle* gh = nullptr;

  if (isGralloc1Buffer) {
    buffer = b;
  } else {
    gh = (cros_gralloc_handle*)malloc(sizeof(cros_gralloc_handle));
    mapper.gralloc4ToGralloc1(b, gh);
    buffer = (buffer_handle_t)gh;
  }

  memset(&ev, 0, sizeof(ev));
  ev.event.type = DD_EVENT_CREATE_BUFFER;
  ev.info.bufferId = (int64_t)bufferId;
  ev.event.size = sizeof(ev) + sizeof(native_handle_t) +
                  (buffer->numFds + buffer->numInts) * 4;

  ALOGI("createBuffer size=%d, sizeof(cros_gralloc_handle)=%zd, numFd=%d numInts=%d native_handle=%zd size(ev)=%zd",
     ev.event.size, sizeof(cros_gralloc_handle), buffer->numFds, buffer->numInts, sizeof(native_handle_t), sizeof(ev));

  if (_send(&(ev.event), sizeof(ev.event)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send create buffer event", mSocketFd);
    return -1;
  }
  if (_send(&(ev.info), sizeof(ev.info)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send create buffer event info",
          mSocketFd);
    return -1;
  }
  if (_send(buffer, sizeof(native_handle_t) +
                        (buffer->numFds + buffer->numInts) * 4) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send create buffer event", mSocketFd);
    return -1;
  }
  if (buffer->numFds > 0) {
    if (_sendFds((int*)(buffer->data), buffer->numFds) < 0) {
      ALOGE("RemoteDisplay(%d) failed to send create buffer event", mSocketFd);
      return -1;
    }
  }
  if (gh)
    free(gh);
  return 0;
}

int RemoteDisplay::removeBuffer(buffer_handle_t buffer) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  buffer_info_event_t ev;

  memset(&ev, 0, sizeof(ev));
  ev.event.type = DD_EVENT_REMOVE_BUFFER;
  ev.info.bufferId = (int64_t)buffer;
  ev.event.size = sizeof(ev);

  if (_send(&(ev.event), sizeof(ev.event)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send remove buffer event", mSocketFd);
    return -1;
  }
  if (_send(&(ev.info), sizeof(ev.info)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send remove buffer event info",
          mSocketFd);
    return -1;
  }
  return 0;
}

int RemoteDisplay::displayBuffer(buffer_handle_t buffer, const display_control_t* ctrl) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  buffer_info_event_t ev;

  memset(&ev, 0, sizeof(ev));
  ev.event.type = DD_EVENT_DISPLAY_REQ;
  ev.event.size = ctrl ? sizeof(ev) + sizeof(display_control_t) : sizeof(ev);
  ev.info.bufferId = (int64_t)buffer;

  if (_send(&ev, sizeof(ev)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send display buffer request", mSocketFd);
    return -1;
  }
  if (ctrl) {
    if (_send(ctrl, sizeof(display_control_t)) < 0) {
      ALOGE("RemoteDisplay(%d) failed to send display control", mSocketFd);
      return -1;
    }
  }
  return 0;
}

int RemoteDisplay::setRotation(int rotation) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  rotation_event_t ev;

  memset(&ev, 0, sizeof(ev));
  ev.event.type = DD_EVENT_SET_ROTATION;
  ev.event.size = sizeof(ev);
  ev.rotation = rotation;

  if (_send(&ev, sizeof(ev)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send display rotation request", mSocketFd);
    return -1;
  }
  return 0;
}

int RemoteDisplay::createLayer(uint64_t id) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  create_layer_event_t ev;

  memset(&ev, 0, sizeof(ev));
  ev.event.type = DD_EVENT_CREATE_LAYER;
  ev.event.size = sizeof(ev);
  ev.layerId = id;

  if (_send(&(ev), sizeof(ev)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send create layer event", mSocketFd);
    return -1;
  }

  return 0;
}

int RemoteDisplay::removeLayer(uint64_t id) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  remove_layer_event_t ev;

  memset(&ev, 0, sizeof(ev));
  ev.event.type = DD_EVENT_REMOVE_LAYER;
  ev.event.size = sizeof(ev);
  ev.layerId = id;

  if (_send(&(ev), sizeof(ev)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send remove layer event", mSocketFd);
    return -1;
  }

  return 0;
}

int RemoteDisplay::updateLayers(std::vector<layer_info_t>& layerInfo) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  update_layers_event_t ev;
  uint32_t numLayers = 0;
  layer_info_t* layers = nullptr;

  numLayers = layerInfo.size();
  if (numLayers) {
    layers = (layer_info_t*)malloc(sizeof(layer_info_t) * numLayers);
    if (!layers) {
      ALOGE("Failed to alloc layer info, out of memory");
      return -1;
    }
    LAYER_TRACE("%s layer count %d", __func__, numLayers);
    for (uint32_t i = 0; i < numLayers; i++) {
      layers[i] = layerInfo[i];
      LAYER_TRACE("  %d layer %" PRIx64 " stack %d task %d", i,
                  layers[i].layerId, layers[i].stackId, layers[i].taskId);
    }
  }

  ev.event.type = DD_EVENT_UPDATE_LAYERS;
  ev.event.size = sizeof(ev) + sizeof(layer_info_t) * numLayers;
  ev.numLayers = numLayers;

  if (_send(&(ev), sizeof(ev)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send update layers event", mSocketFd);
    if (layers) {
      free(layers);
    }
    return -1;
  }

  if (_send(layers, sizeof(layer_info_t) * numLayers) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send update layers info", mSocketFd);
    if (layers) {
      free(layers);
    }
    return -1;
  }

  if (layers) {
    free(layers);
  }
  return 0;
}

int RemoteDisplay::presentLayers(
    std::vector<layer_buffer_info_t>& layerBuffer) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  present_layers_req_event_t ev;
  uint32_t numLayers = 0;
  layer_buffer_info_t* layers = nullptr;

  numLayers = layerBuffer.size();
  if (numLayers) {
    layers =
        (layer_buffer_info_t*)malloc(sizeof(layer_buffer_info_t) * numLayers);
    if (!layers) {
      ALOGE("Failed to alloc present req layer buffer info, out of memory");
      return -1;
    }
    for (uint32_t i = 0; i < numLayers; i++) {
      layers[i] = layerBuffer[i];
    }
  }

  ev.event.type = DD_EVENT_PRESENT_LAYERS_REQ;
  ev.event.size = sizeof(ev) + sizeof(layer_buffer_info_t) * numLayers;
  ev.numLayers = numLayers;

  if (_send(&(ev), sizeof(ev)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send present layers req event",
          mSocketFd);
    if (layers) {
      free(layers);
    }
    return -1;
  }

  if (_send(layers, sizeof(layer_buffer_info_t) * numLayers) < 0) {
    ALOGE("RemoteDisplay(%d) failed to send present layers info", mSocketFd);
    if (layers) {
      free(layers);
    }
    return -1;
  }
  // TODO: send layers' acqureFences
  if (layers) {
    free(layers);
  }

  return 0;
}

int RemoteDisplay::onDisplayInfoAck(const display_event_t& ev) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  display_info_t info;
  int ret = _recv(&info, sizeof(info));
  if (ret < 0) {
    ALOGE("%s:%d: Can't send display info request\n", __func__, __LINE__);
    return -1;
  }
  mWidth = info.width;
  mHeight = info.height;
  mFramerate = info.fps;
  mXDpi = info.xdpi;
  mYDpi = info.ydpi;
  mDisplayFlags.value = info.flags;


  if (property_get_bool("ro.fw.concurrent.user", false)) {
    sendDisplayPortReq();
  } else {
    mPort = mSocketFd;
    if (mStatusListener) {
      mStatusListener->onConnect(mSocketFd);
    }
  }
  return 0;
}

int RemoteDisplay::onDisplayPortAck(const display_event_t& ev) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  display_port_t info;
  int ret = _recv(&info, sizeof(info));
  if (ret < 0) {
    ALOGE("%s:%d: Can't receive display port ack\n", __func__, __LINE__);
    return -1;
  }
  mPort = info.port;
  ALOGD("Received display port: %d in %s", mPort, __func__);

  if (mStatusListener) {
    mStatusListener->onConnect(mSocketFd);
  }
  return 0;
}

int RemoteDisplay::onSetMode(const display_event_t& ev) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  display_info_t info;
  int ret = _recv(&info, sizeof(info));
  if (ret < 0) {
    ALOGE("%s:%d: Can't receive setMode\n", __func__, __LINE__);
    return -1;
  }

  uint32_t old_Width = mWidth;
  uint32_t old_Height = mHeight;

  mWidth = info.width;
  mHeight = info.height;

  if ((mStatusListener) && ((mWidth != old_Width)||(mHeight != old_Height))) {
    mStatusListener->onSetMode(mSocketFd);
  }
  return 0;
}

 int RemoteDisplay::onSetVideoAlpha(const display_event_t& ev) {
  ALOGD("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  set_video_alpha_t video_alpha;
  int ret = _recv(&video_alpha, sizeof(video_alpha));
  if (ret < 0) {
    ALOGE("RemoteDisplay(%d) failed to set video alpha req", mSocketFd);
    return -1;
  }
  if (mEventListener) {
    ALOGD("RemoteDisplay(%d)::%s alpha=%d", mSocketFd, __func__, video_alpha.enable);
    mEventListener->onSetVideoAlpha(video_alpha.enable);
  }
  return 0;
 }

int RemoteDisplay::onDisplayBufferAck(const display_event_t& ev) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  buffer_info_t info;
  int ret = _recv(&info, sizeof(info));
  if (ret < 0) {
    ALOGE("RemoteDisplay(%d) failed to receive present ack", mSocketFd);
    return -1;
  }
  if (mEventListener) {
    mEventListener->onBufferDisplayed(info);
  }
  return 0;
}

int RemoteDisplay::onPresentLayersAck(const display_event_t& ev) {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);
  present_layers_ack_event_t ack{};

  if (_recv(&ack.flags, sizeof(ack.flags)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to receive present layers ack event",
          mSocketFd);
    return -1;
  }
  if (_recv(&ack.releaseFence, sizeof(ack.releaseFence)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to receive present layers ack event",
          mSocketFd);
    return -1;
  }
  if (_recv(&ack.numLayers, sizeof(ack.numLayers)) < 0) {
    ALOGE("RemoteDisplay(%d) failed to receive present layers ack event",
          mSocketFd);
    return -1;
  }

  mDisplayFlags.value = ack.flags;

  std::vector<layer_buffer_info_t> layerBuffers;
  layerBuffers.resize(ack.numLayers);
  for (size_t i = 0; i < ack.numLayers; i++) {
    if (_recv(&layerBuffers.at(i), sizeof(layer_buffer_info_t)) < 0) {
      ALOGE("Failed to recv presemt layer(%zd) ack", i);
      return -1;
    }
  }
  if (mEventListener) {
    mEventListener->onPresented(layerBuffers, ack.releaseFence);
  }

  return 0;
}

int RemoteDisplay::onDisplayEvent() {
  ALOGV("RemoteDisplay(%d)::%s", mSocketFd, __func__);

  display_event_t ev;
  int ret = _recv(&ev, sizeof(ev));
  if (ret < 0) {
    return -1;
  }

  switch (ev.type) {
    case DD_EVENT_DISPINFO_ACK:
      onDisplayInfoAck(ev);
      break;
    case DD_EVENT_DISPPORT_ACK:
      onDisplayPortAck(ev);
      break;
    case DD_EVENT_DISPLAY_ACK:
      onDisplayBufferAck(ev);
      break;
    case DD_EVENT_PRESENT_LAYERS_ACK:
      onPresentLayersAck(ev);
      break;
    case DD_EVENT_SET_MODE:
      onSetMode(ev);
      break;
    case DD_EVENT_SET_VIDEO_ALPHA_REQ:
      ALOGD("RemoteDisplay(%d)::%s DD_EVENT_SET_VIDEO_ALPHA_REQ", mSocketFd, __func__);
      onSetVideoAlpha(ev);
      break;
    default: {
      char buf[1024];
      ret = _recv(buf, 1024);
      printf("Unknown command type %d expect %d recv=%d\n", ev.type,
             (int)(ev.size - sizeof(ev)), ret);
      break;
    }
  }

  return 0;
}
