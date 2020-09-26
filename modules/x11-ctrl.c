#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/dpms.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <gmodule.h>
#include <stdbool.h>

#include "mce-log.h"
#include "mce.h"
#include "datapipe.h"

/** Module name */
#define MODULE_NAME		"x11-ctrl"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 250
};

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
		mce_log(LL_INFO, "%s: unable to open display", MODULE_NAME);
	}
	return dpy;
}

static bool x11_set_input_device_enabled(Display *dpy, const XIDeviceInfo devinfo, const bool enable)
{
	if (dpy == NULL) {
		mce_log(LL_WARN, "%s: XIDeviceInfo needs to be from same Display", MODULE_NAME);
		return false;
	}
	if (x11_atom_device_enabled == None)
		x11_atom_device_enabled = XInternAtom(dpy, "Device Enabled", False);
	if (x11_atom_device_enabled == None) {
		mce_log(LL_WARN, "%s: unable to obtain X11 Atoms", MODULE_NAME);
		return false;
	}

	if ((x11_atom_device_enabled_type == None) || (x11_atom_device_enabled_format == 0)) {
		unsigned long ignore_bytes_after, ignore_nitems;
		unsigned char *ignore_data = NULL;

		if (XIGetProperty
		    (dpy, devinfo.deviceid, x11_atom_device_enabled, 0, 0, False, AnyPropertyType,
		     &x11_atom_device_enabled_type, &x11_atom_device_enabled_format, &ignore_nitems,
		     &ignore_bytes_after, &ignore_data)) {
			mce_log(LL_WARN, "%s: unable to obtain X11 Device Enabled property atom type", MODULE_NAME);
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

static bool x11_set_all_input_devices_enabled(Display *dpy, const bool enable)
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
		mce_log(LL_WARN, "%s: unable to obtain X11 Atoms", MODULE_NAME);
		goto done;
	}

	devinfo = XIQueryDevice(dpy, XIAllDevices, &ndev);
	if (devinfo == NULL)
		goto done;

	if (enable && disabledDevices == NULL) {
		goto doneXiFree;
	} else if (!enable && disabledDevices != NULL) {
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

			mce_log(LL_INFO, "%s: disableing %s", MODULE_NAME, devinfo[i].name);

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
					mce_log(LL_INFO, "%s: enableing %s", MODULE_NAME, devinfo[i].name);
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

static bool x11_set_dpms_enabled(Display *dpy, const bool enable)
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
		mce_log(LL_INFO, "%s: XServer dosent have dpms extension", MODULE_NAME);
		if (ownsDisplay)
			XCloseDisplay(dpy);
		return false;
	}

	if (ownsDisplay)
		XCloseDisplay(dpy);
	return true;
}

static bool x11_set_dpms_display_level(Display *dpy, const bool state)
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
		mce_log(LL_WARN, "%s: Dsiplay dose not support DPMS", MODULE_NAME);
		if (ownsDisplay)
			XCloseDisplay(dpy);
		return false;
	}

	if (ownsDisplay)
		XCloseDisplay(dpy);
	return true;
}


static void x11_force_dpms_display_level(const bool on)
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

static void display_state_trigger(gconstpointer data)
{
	static display_state_t old_state = MCE_DISPLAY_UNDEF;
	display_state_t new_state = (display_state_t)data;
	
	if (new_state != old_state) {
		if (new_state == MCE_DISPLAY_OFF)
			x11_force_dpms_display_level(false);
		else
			x11_force_dpms_display_level(true);
		old_state = new_state;
	}
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	(void)module;
	
	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);

	return NULL;
}

G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);

}

