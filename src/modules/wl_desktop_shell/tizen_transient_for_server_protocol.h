#ifndef TIZEN_TRANSIENT_FOR_SERVER_PROTOCOL_H
#define TIZEN_TRANSIENT_FOR_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct tizen_transient_for;

extern const struct wl_interface tizen_transient_for_interface;

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
	/**
	 * unset - (none)
	 * @child_id: (none)
	 */
	void (*unset)(struct wl_client *client,
		      struct wl_resource *resource,
		      uint32_t child_id);
};

#define TIZEN_TRANSIENT_FOR_DONE	0

#define TIZEN_TRANSIENT_FOR_DONE_SINCE_VERSION	1

static inline void
tizen_transient_for_send_done(struct wl_resource *resource_, uint32_t child_id)
{
	wl_resource_post_event(resource_, TIZEN_TRANSIENT_FOR_DONE, child_id);
}

#ifdef  __cplusplus
}
#endif

#endif
