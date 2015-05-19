#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface wl_buffer_interface;

static const struct wl_interface *types[] = {
	NULL,
	&wl_buffer_interface,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&wl_buffer_interface,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static const struct wl_message tizen_buffer_pool_requests[] = {
	{ "authenticate", "u", types + 0 },
	{ "create_buffer", "nuiiuu", types + 1 },
	{ "create_planar_buffer", "niiuuiiuiiuii", types + 7 },
};

static const struct wl_message tizen_buffer_pool_events[] = {
	{ "device", "s", types + 0 },
	{ "authenticated", "", types + 0 },
	{ "capabilities", "u", types + 0 },
	{ "format", "u", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_buffer_pool_interface = {
	"tizen_buffer_pool", 1,
	3, tizen_buffer_pool_requests,
	4, tizen_buffer_pool_events,
};
