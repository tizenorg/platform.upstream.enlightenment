#ifndef TIZEN_EXTENSION_SERVER_PROTOCOL_H
#define TIZEN_EXTENSION_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-util.h"

struct wl_client;
struct wl_resource;

struct tizen_resource;
struct tizen_surface_extension;
struct tizen_transient_for;

extern const struct wl_interface tizen_resource_interface;
extern const struct wl_interface tizen_surface_extension_interface;
extern const struct wl_interface tizen_transient_for_interface;

struct tizen_resource_interface {
	/**
	 * destroy - (none)
	 */
	void (*destroy)(struct wl_client *client,
			struct wl_resource *resource);
};

#define TIZEN_RESOURCE_RESOURCE_ID	0

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
};

struct tizen_transient_for_interface {
	/**
	 * set - (none)
	 * @child_id: (none)
	 * @parent_id: (none)
	 */
	void (*set)(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t child_id,
		    uint32_t parent_id);
};

#define TIZEN_TRANSIENT_FOR_DONE	0

static inline void
tizen_transient_for_send_done(struct wl_resource *resource_, uint32_t child_id)
{
	wl_resource_post_event(resource_, TIZEN_TRANSIENT_FOR_DONE, child_id);
}

#ifdef  __cplusplus
}
#endif

#endif
