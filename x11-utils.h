#ifndef _X11_UTILS_H_
#define _X11_UTILS_H_
#include <stdbool.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>

bool x11_set_input_device_enabled(Display* dpy, const XIDeviceInfo devinfo, const bool enable);

bool x11_set_all_input_devices_enabled(Display* dpy, const bool enable);

void x11_force_dpms_display_level(const bool on);

bool x11_set_dpms_display_level(Display* dpy, const bool on);

bool x11_set_dpms_enabled(Display* dpy, const bool enable);


#endif //_X11_UTILS_H_
