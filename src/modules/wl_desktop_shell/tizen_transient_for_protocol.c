#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"


static const struct wl_interface *types[] = {
	NULL,
	NULL,
};

static const struct wl_message tizen_transient_for_requests[] = {
	{ "set", "uu", types + 0 },
};

static const struct wl_message tizen_transient_for_events[] = {
	{ "done", "u", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_transient_for_interface = {
	"tizen_transient_for", 1,
	1, tizen_transient_for_requests,
	1, tizen_transient_for_events,
};

