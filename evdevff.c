#include "evdevff.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static bool first_run = true;

static bool bit_in_array(unsigned char *array, size_t bit)
{
	return array[bit / 8] & (1 << bit % 8);
}

bool ff_gain_set(const int fd, const int gain)
{
	struct input_event event;
	if (fd < 0)
		return false;
	event.type = EV_FF;
	event.code = FF_GAIN;
	event.value = 0xFFFFUL * gain / 100;
	return write(fd, &event, sizeof(event)) >= 0;
}

bool ff_features_get(const int fd, struct fffeatures *features)
{
	unsigned char featuresBytes[1 + FF_MAX / 8];
	if (fd < 0)
		return false;
	memset(featuresBytes, 0, sizeof(featuresBytes));
	int ret =
	    ioctl(fd, EVIOCGBIT(EV_FF, sizeof(featuresBytes)), featuresBytes);
	if (ret < 0)
		return false;

	if (ret * sizeof(unsigned long) * 8 >= 16) {
		features->constant = bit_in_array(featuresBytes, FF_CONSTANT);
		features->periodic = bit_in_array(featuresBytes, FF_PERIODIC);
		features->square = bit_in_array(featuresBytes, FF_SQUARE);
		features->triangle = bit_in_array(featuresBytes, FF_TRIANGLE);
		features->sine = bit_in_array(featuresBytes, FF_SINE);
		features->saw_up = bit_in_array(featuresBytes, FF_SAW_UP);
		features->saw_down = bit_in_array(featuresBytes, FF_SAW_DOWN);
		features->custom = bit_in_array(featuresBytes, FF_CUSTOM);
		features->ramp = bit_in_array(featuresBytes, FF_RAMP);
		features->spring = bit_in_array(featuresBytes, FF_SPRING);
		features->friction = bit_in_array(featuresBytes, FF_FRICTION);
		features->damper = bit_in_array(featuresBytes, FF_DAMPER);
		features->rumble = bit_in_array(featuresBytes, FF_RUMBLE);
		features->inertia = bit_in_array(featuresBytes, FF_INERTIA);
		features->gain = bit_in_array(featuresBytes, FF_GAIN);
		features->autocenter =
		    bit_in_array(featuresBytes, FF_AUTOCENTER);
	} else
		return false;

	return true;
}

bool ff_device_run(const int fd, const int lengthMs, const int delayMs,
		   const int count, const uint8_t strength,
		   const short attackLengthMs, const short fadeLengthMs)
{
	static struct ff_effect effect;
	
	if (fd < 0)
		return false;

	if (first_run) {
		memset(&effect, 0, sizeof(struct ff_effect));
		effect.type = FF_PERIODIC;
		effect.id = -1;
		effect.u.periodic.waveform = FF_SINE;
		effect.u.periodic.period = 100;
		first_run = false;
	}

	effect.u.periodic.magnitude = (0x7fff * strength) / 255;
	effect.u.periodic.envelope.attack_length = attackLengthMs;
	effect.u.periodic.envelope.fade_length = fadeLengthMs;
	effect.replay.delay = delayMs;
	effect.replay.length = lengthMs;

	if (ioctl(fd, EVIOCSFF, &effect) == -1) {
		perror("Error at ioctl() in ff_device_run");
		return false;
	}

	struct input_event run_event;
	memset(&run_event, 0, sizeof(struct input_event));
	run_event.type = EV_FF;
	run_event.code = effect.id;
	run_event.value = count;

	if (write(fd, (const void *)&run_event, sizeof(run_event)) == -1) {
		return false;
	} else
		return true;
}

int ff_device_open(const char *const deviceName)
{
	int inputDevice = open(deviceName, O_RDWR);
	if (inputDevice < 0)
		return -4;
	if (!ff_gain_set(inputDevice, 100))
		return -1;
	fffeatures features;
	if (!ff_features_get(inputDevice, &features))
		return -2;
	if (!features.periodic || !features.sine || !features.gain)
		return -3;
	return inputDevice;
}
