#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/dpms.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include "x11-utils.h"
#include "mce-log.h"

static Atom x11_atom_touchscreen = None;
static Atom x11_atom_device_enabled = None;
static Atom x11_atom_device_enabled_type = None;
static int x11_atom_device_enabled_format = 0;

static Display *x11_get_display(void)
{
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		dpy = XOpenDisplay(":0.0");
	}

	if (dpy == NULL) {
		mce_log(LL_INFO, "%s: unable to open display", __func__);
	}
	return dpy;
}

bool x11_set_input_device_enabled(Display *dpy, const XIDeviceInfo devinfo, const bool enable)
{
	if (dpy == NULL) {
		mce_log(LL_WARN, "%s: XIDeviceInfo needs to be from same Display", __func__);
		return false;
	}
	if (x11_atom_device_enabled == None)
		x11_atom_device_enabled = XInternAtom(dpy, "Device Enabled", False);
	if (x11_atom_device_enabled == None) {
		mce_log(LL_WARN, "%s: unable to obtain X11 Atoms", __func__);
		return false;
	}

	if ((x11_atom_device_enabled_type == None) || (x11_atom_device_enabled_format == 0)) {
		unsigned long ignore_bytes_after, ignore_nitems;
		unsigned char *ignore_data = NULL;

		if (XIGetProperty
		    (dpy, devinfo.deviceid, x11_atom_device_enabled, 0, 0, False, AnyPropertyType,
		     &x11_atom_device_enabled_type, &x11_atom_device_enabled_format, &ignore_nitems,
		     &ignore_bytes_after, &ignore_data)) {
			mce_log(LL_WARN, "%s: unable to obtain X11 Device Enabled property atom type", __func__);
			return false;
		} else {
			XFree(ignore_data);
		}
	}

	XIChangeProperty(dpy, devinfo.deviceid, x11_atom_device_enabled,
			 x11_atom_device_enabled_type,
			 x11_atom_device_enabled_format, PropModeReplace, (unsigned char *)&enable, 1);
	return true;
}

bool x11_set_all_input_devices_enabled(Display *dpy, const bool enable)
{
	bool ret = false;
	XIDeviceInfo *devinfo = NULL;
	int ndev = 0;

	static XIDeviceInfo *disabledDevices = NULL;
	static unsigned int disabledDevicesCount = 0;

	bool ownsDisplay = false;
	if (dpy == NULL) {
		dpy = x11_get_display();
		if (dpy == NULL)
			return false;
		ownsDisplay = true;
	}

	if (x11_atom_touchscreen == None)
		x11_atom_touchscreen = XInternAtom(dpy, XI_TOUCHSCREEN, True);

	if (x11_atom_touchscreen == None) {
		mce_log(LL_WARN, "%s: unable to obtain X11 Atoms", __func__);
		goto done;
	}

	devinfo = XIQueryDevice(dpy, XIAllDevices, &ndev);
	if (devinfo == NULL)
		goto done;

	if (enable && disabledDevices == NULL) {
		mce_log(LL_WARN, "%s: this function only enables devices previously disabled by it", __func__);
		goto doneXiFree;
	} else if (!enable && disabledDevices != NULL) {
		mce_log(LL_WARN, "%s: this function can only disable devices once before renableing them", __func__);
		goto doneXiFree;
	} else if (!enable && disabledDevices == NULL) {
		disabledDevices = malloc(sizeof(*disabledDevices) * ndev);
	}

	if (!enable) {
		for (int i = 0; i < ndev; ++i) {
			if (devinfo[i].use == XIMasterPointer || devinfo[i].use == XIMasterKeyboard
			    || !devinfo[i].enabled)
				continue;

			if (devinfo[i].name && strstr(devinfo[i].name, "XTEST") != NULL)
				continue;

			mce_log(LL_INFO, "%s: disableing %s", __func__, devinfo[i].name);

			if (x11_set_input_device_enabled(dpy, devinfo[i], enable)) {
				disabledDevices[disabledDevicesCount] = devinfo[i];
				++disabledDevicesCount;
			}
		}
	} else {
		for (int i = 0; i < ndev; ++i) {
			if (devinfo[i].use == XIMasterPointer || devinfo[i].use == XIMasterKeyboard)
				continue;

			if (devinfo[i].name && strstr(devinfo[i].name, "XTEST") != NULL)
				continue;

			for (unsigned int j = 0; j < disabledDevicesCount; ++j) {
				if (disabledDevices[j].deviceid == devinfo[i].deviceid) {
					mce_log(LL_INFO, "%s: enableing %s", __func__, devinfo[i].name);
					x11_set_input_device_enabled(dpy, devinfo[i], enable);
				}
			}
		}
		disabledDevicesCount = 0;
		free(disabledDevices);
		disabledDevices = NULL;
	}

	ret = true;

 doneXiFree:
	XIFreeDeviceInfo(devinfo);

 done:
	if (dpy != NULL && ownsDisplay)
		XCloseDisplay(dpy);

	return ret;
}

void x11_force_dpms_display_level(const bool on)
{
	Display *dpy = x11_get_display();

	if (dpy == NULL)
		return;

	if (!on) {
		x11_set_all_input_devices_enabled(dpy, false);
		XSync(dpy, false);
		x11_set_dpms_display_level(dpy, false);
	} else {
		x11_set_all_input_devices_enabled(dpy, true);
		x11_set_dpms_display_level(dpy, true);
	}

	XCloseDisplay(dpy);
}

bool x11_set_dpms_display_level(Display *dpy, const bool state)
{
	bool ownsDisplay = false;
	if (dpy == NULL) {
		dpy = x11_get_display();
		if (dpy == NULL)
			return false;
		ownsDisplay = true;
	}

	if (DPMSCapable(dpy)) {
		x11_set_dpms_enabled(dpy, true);
		if (state) {
			DPMSForceLevel(dpy, DPMSModeOn);
			XSync(dpy, false);
		} else {
			usleep(100000);
			DPMSForceLevel(dpy, DPMSModeOff);
			XSync(dpy, false);
		}
	} else {
		mce_log(LL_WARN, "%s: Dsiplay dose not support DPMS", __func__);
		if (ownsDisplay)
			XCloseDisplay(dpy);
		return false;
	}

	if (ownsDisplay)
		XCloseDisplay(dpy);
	return true;
}

bool x11_set_dpms_enabled(Display *dpy, const bool enable)
{
	int dummy;
	bool ownsDisplay = false;
	if (dpy == NULL) {
		dpy = x11_get_display();
		if (dpy == NULL)
			return false;
		ownsDisplay = true;
	}

	if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
		uint16_t level;
		bool enabled;
		DPMSInfo(dpy, &level, (unsigned char *)&enabled);
		if (enabled != enable)
			enable ? DPMSEnable(dpy) : DPMSDisable(dpy);
	} else {
		mce_log(LL_INFO, "%s: XServer dosent have dpms extension", __func__);
		if (ownsDisplay)
			XCloseDisplay(dpy);
		return false;
	}

	if (ownsDisplay)
		XCloseDisplay(dpy);
	return true;
}
