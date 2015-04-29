#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface tizen_gid_interface;
extern const struct wl_interface wl_surface_interface;

static const struct wl_interface *types[] = {
	NULL,
	NULL,
	&tizen_gid_interface,
	&wl_surface_interface,
};

static const struct wl_message tizen_ext_requests[] = {
	{ "get_tizen_gid", "no", types + 2 },
	{ "set_transient_for", "uu", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_ext_interface = {
	"tizen_ext", 1,
	2, tizen_ext_requests,
	0, NULL,
};

static const struct wl_message tizen_gid_requests[] = {
	{ "destroy", "", types + 0 },
};

static const struct wl_message tizen_gid_events[] = {
	{ "notify", "u", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_gid_interface = {
	"tizen_gid", 1,
	1, tizen_gid_requests,
	1, tizen_gid_events,
};

