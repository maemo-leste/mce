#ifndef _FILTER_BRIGHTNESS_ALS_IIO_H_
#define _FILTER_BRIGHTNESS_ALS_IIO_H_

/** Path to the ALS enabled GConf setting */
#define MCE_ALS_ENABLED_KEY	"als_enabled"

typedef struct {
	/** Lower and upper bound for each brightness range */
	gint range[5][2];
	gint value[6];					/* brightness in % */
} als_profile_struct; 


typedef enum {
	ALS_PROFILE_MINIMUM = 0,		/**< Minimum profile */
	ALS_PROFILE_ECONOMY,			/**< Economy profile */
	ALS_PROFILE_NORMAL,			/**< Normal profile */
	ALS_PROFILE_BRIGHT,			/**< Bright profile */
	ALS_PROFILE_MAXIMUM,			/**< Maximum profile */
	ALS_PROFILE_COUNT
} als_profile_t;


/* Profiles based on physicly sensible values taken from https://en.wikipedia.org/wiki/Daylight*/

const als_profile_struct display_als_profiles_generic[ALS_PROFILE_COUNT] = {
	{
		{
			{ 25, 50000 },
			{ 150000, 300000 },
			{ 1750000, 8750000 },
			{ 15000000, 20000000 },
			{ 30000000, 75000000 },
		}, {20, 30, 50, 80, 80, 80}
	}, {
		{
			{ 25, 50000 },
			{ 150000, 300000 },
			{ 1750000, 8750000 },
			{ 15000000, 20000000 },
			{ 30000000, 75000000 },
		}, {30, 50, 70, 80, 100, 100}
	}, {
		{
			{ 25, 50000 },
			{ 150000, 300000 },
			{ 1750000, 8750000 },
			{ 15000000, 20000000 },
			{ 30000000, 75000000 },
		}, {50, 60, 80, 100, 100, 100}
	}, {
		{
			{ 25, 50000 },
			{ 150000, 300000 },
			{ 1750000, 8750000 },
			{ 15000000, 20000000 },
			{ 30000000, 75000000 },
		}, { 60, 70, 100, 100, 100, 100}
	}, {
		{
			{ 32, 64 },
			{ 160, 320},
			{ -1, -1 },
			{ -1, -1 },
			{ -1, -1 },
		}, { 100, 100, 100, 0, 0, 0 }
	}
};

#endif
