#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface wl_subsurface_interface;

static const struct wl_interface *types[] = {
	&wl_subsurface_interface,
};

static const struct wl_message tizen_subsurface_requests[] = {
	{ "place_below_parent", "o", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_subsurface_interface = {
	"tizen_subsurface", 1,
	1, tizen_subsurface_requests,
	0, NULL,
};

