#ifndef TIZEN_EXT_SERVER_PROTOCOL_H
#define TIZEN_EXT_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-util.h"

struct wl_client;
struct wl_resource;

struct tizen_ext;
struct tizen_gid;

extern const struct wl_interface tizen_ext_interface;
extern const struct wl_interface tizen_gid_interface;

struct tizen_ext_interface {
	/**
	 * get_tizen_gid - (none)
	 * @id: (none)
	 * @surface: (none)
	 */
	void (*get_tizen_gid)(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t id,
			      struct wl_resource *surface);
	/**
	 * set_transient_for - (none)
	 * @surface_gid: (none)
	 * @parent_gid: (none)
	 */
	void (*set_transient_for)(struct wl_client *client,
				  struct wl_resource *resource,
				  uint32_t surface_gid,
				  uint32_t parent_gid);
};

struct tizen_gid_interface {
	/**
	 * destroy - (none)
	 */
	void (*destroy)(struct wl_client *client,
			struct wl_resource *resource);
};

#define TIZEN_GID_NOTIFY	0

static inline void
tizen_gid_send_notify(struct wl_resource *resource_, uint32_t gid)
{
	wl_resource_post_event(resource_, TIZEN_GID_NOTIFY, gid);
}

#ifdef  __cplusplus
}
#endif

#endif
