/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) Linaro Ltd 2020
 *  Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 */

struct thermal_genl_cpu_caps {
	int cpu;
	int performance;
	int efficiency;
};

struct thermal_zone_device;
struct thermal_trip;

/* Netlink notification function */
#ifdef CONFIG_THERMAL_NETLINK
int __init thermal_netlink_init(void);
void __init thermal_netlink_exit(void);
int thermal_notify_tz_create(const struct thermal_zone_device *tz);
int thermal_notify_tz_delete(const struct thermal_zone_device *tz);
int thermal_notify_tz_enable(const struct thermal_zone_device *tz);
int thermal_notify_tz_disable(const struct thermal_zone_device *tz);
int thermal_notify_tz_trip_down(const struct thermal_zone_device *tz,
				const struct thermal_trip *trip);
int thermal_notify_tz_trip_up(const struct thermal_zone_device *tz,
			      const struct thermal_trip *trip);
int thermal_notify_tz_trip_delete(int tz_id, int id);
int thermal_notify_tz_trip_add(int tz_id, int id, int type,
			       int temp, int hyst);
int thermal_notify_tz_trip_change(const struct thermal_zone_device *tz,
				  const struct thermal_trip *trip);
int thermal_notify_cdev_state_update(int cdev_id, int state);
int thermal_notify_cdev_add(int cdev_id, const char *name, int max_state);
int thermal_notify_cdev_delete(int cdev_id);
int thermal_notify_tz_gov_change(const struct thermal_zone_device *tz,
				 const char *name);
int thermal_genl_sampling_temp(int id, int temp);
int thermal_genl_cpu_capability_event(int count,
				      struct thermal_genl_cpu_caps *caps);
#else
static inline int thermal_netlink_init(void)
{
	return 0;
}

static inline int thermal_notify_tz_create(const struct thermal_zone_device *tz)
{
	return 0;
}

static inline int thermal_notify_tz_delete(const struct thermal_zone_device *tz)
{
	return 0;
}

static inline int thermal_notify_tz_enable(const struct thermal_zone_device *tz)
{
	return 0;
}

static inline int thermal_notify_tz_disable(const struct thermal_zone_device *tz)
{
	return 0;
}

static inline int thermal_notify_tz_trip_down(const struct thermal_zone_device *tz,
					      const struct thermal_trip *trip)
{
	return 0;
}

static inline int thermal_notify_tz_trip_up(const struct thermal_zone_device *tz,
					    const struct thermal_trip *trip)
{
	return 0;
}

static inline int thermal_notify_tz_trip_delete(int tz_id, int id)
{
	return 0;
}

static inline int thermal_notify_tz_trip_add(int tz_id, int id, int type,
					     int temp, int hyst)
{
	return 0;
}

static inline int thermal_notify_tz_trip_change(const struct thermal_zone_device *tz,
						const struct thermal_trip *trip)
{
	return 0;
}

static inline int thermal_notify_cdev_state_update(int cdev_id, int state)
{
	return 0;
}

static inline int thermal_notify_cdev_add(int cdev_id, const char *name,
					  int max_state)
{
	return 0;
}

static inline int thermal_notify_cdev_delete(int cdev_id)
{
	return 0;
}

static inline int thermal_notify_tz_gov_change(const struct thermal_zone_device *tz,
					       const char *name)
{
	return 0;
}

static inline int thermal_genl_sampling_temp(int id, int temp)
{
	return 0;
}

static inline int thermal_genl_cpu_capability_event(int count, struct thermal_genl_cpu_caps *caps)
{
	return 0;
}

static inline void __init thermal_netlink_exit(void) {}

#endif /* CONFIG_THERMAL_NETLINK */
