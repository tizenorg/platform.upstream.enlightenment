#ifndef TIZEN_EXTENSION_SERVER_PROTOCOL_H
#define TIZEN_EXTENSION_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct tizen_resource;
struct tizen_surface_extension;

extern const struct wl_interface tizen_resource_interface;
extern const struct wl_interface tizen_surface_extension_interface;

struct tizen_resource_interface {
	/**
	 * destroy - (none)
	 */
	void (*destroy)(struct wl_client *client,
			struct wl_resource *resource);
};

#define TIZEN_RESOURCE_RESOURCE_ID	0

#define TIZEN_RESOURCE_RESOURCE_ID_SINCE_VERSION	1

static inline void
tizen_resource_send_resource_id(struct wl_resource *resource_, uint32_t id)
{
	wl_resource_post_event(resource_, TIZEN_RESOURCE_RESOURCE_ID, id);
}

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
	/**
	 * set_transient_for - (none)
	 * @child_id: (none)
	 * @parent_id: (none)
	 */
	void (*set_transient_for)(struct wl_client *client,
				  struct wl_resource *resource,
				  uint32_t child_id,
				  uint32_t parent_id);
};


#ifdef  __cplusplus
}
#endif

#endif
