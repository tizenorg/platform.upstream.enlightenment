#ifndef TIZEN_SUBSURFACE_SERVER_PROTOCOL_H
#define TIZEN_SUBSURFACE_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct tizen_subsurface;

extern const struct wl_interface tizen_subsurface_interface;

struct tizen_subsurface_interface {
	/**
	 * place_below_parent - (none)
	 * @subsurface: (none)
	 */
	void (*place_below_parent)(struct wl_client *client,
				   struct wl_resource *resource,
				   struct wl_resource *subsurface);
};


#ifdef  __cplusplus
}
#endif

#endif
