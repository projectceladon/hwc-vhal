#include <stdio.h>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "LocalDisplay.h"

int getResFromFb(int& w, int& h) {
  struct fb_var_screeninfo fbVar;
  int fd = open("/dev/fb0", O_RDWR);
  if (fd >= 0) {
    if (ioctl(fd, FBIOGET_VSCREENINFO, &fbVar)) {
      close(fd);
      return -1;
    }
    w = fbVar.xres;
    h = fbVar.yres;
    close(fd);
  }

  return -1;
}

int getResFromDebugFs(int& w, int& h) {
  const char* path = "/sys/kernel/debug/dri/0/i915_display_info";
  char lineBuf[256];
  int ret = -1;
  int crtc;

  FILE* fp = fopen(path, "rb");
  if (!fp)
    return -1;

  while (!feof(fp)) {
    fgets(lineBuf, 256, fp);
    if (sscanf(lineBuf, "CRTC %d: pipe: A, active=yes, (size=%dx%d)", &crtc, &w,
               &h) == 3) {
      ret = 0;
      break;
    }
  }
  fclose(fp);
  return ret;
}

int getResFromKms(int& w, int& h) {
  return -1;
}