#ifndef _EVDEV_FF_H_
#define _EVDEV_FF_H
#include <stdbool.h>
#include <linux/input.h>
#include <stdint.h>

typedef struct fffeatures {
	bool constant:1;	/* can render constant force effects */
	bool periodic:1;	/* can render periodic effects with the following waveforms: */
	bool square:1;		/* square waveform */
	bool triangle:1;	/* triangle waveform */
	bool sine:1;		/* sine waveform */
	bool saw_up:1;		/* sawtooth up waveform */
	bool saw_down:1;	/* sawtooth down waveform */
	bool custom:1;		/* custom waveform (not implemented) */
	bool ramp:1;		/* can render ramp effects */
	bool spring:1;		/* can simulate the presence of a spring */
	bool friction:1;	/* can simulate friction */
	bool damper:1;		/* can simulate damper effects */
	bool rumble:1;		/* rumble effects */
	bool inertia:1;		/* can simulate inertia */
	bool gain:1;		/* gain is adjustable */
	bool autocenter:1;	/* autocenter is adjustable */
} fffeatures;

bool ff_features_get(const int fd, struct fffeatures *features);

bool ff_gain_set(const int fd, const int gain);

bool ff_device_run(const int fd, const int lengthMs, const int delayMs, const int count, 
				   const uint8_t strength, const short attackLengthMs, const short fadeLengthMs);

inline bool ff_device_stop(const int fd)
{
	return ff_device_run(fd, 1, 0, 1, 0, 0, 0);
}

int ff_device_open(const char *const deviceName);

#endif				/* _EVDEV_FF_H_ */
