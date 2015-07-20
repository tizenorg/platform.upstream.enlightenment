#ifndef TIZEN_EXT_SERVER_PROTOCOL_H
#define TIZEN_EXT_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct tizen_surface_extension;

extern const struct wl_interface tizen_surface_extension_interface;

struct tizen_surface_extension_interface {
	/**
	 * get_tizen_resource - (none)
	 * @id: (none)
	 * @surface: (none)
	 */
	void (*get_tizen_resource)(struct wl_client *client,
				   struct wl_resource *resource,
				   uint32_t id,
				   struct wl_resource *surface);
};


#ifdef  __cplusplus
}
#endif

#endif
