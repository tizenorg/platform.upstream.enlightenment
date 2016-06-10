#ifndef TIZEN_SURFACE_SERVER_PROTOCOL_H
#define TIZEN_SURFACE_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-util.h"

struct wl_client;
struct wl_resource;

struct tizen_surface_shm;
struct tizen_surface_shm_flusher;

extern const struct wl_interface tizen_surface_shm_interface;
extern const struct wl_interface tizen_surface_shm_flusher_interface;

struct tizen_surface_shm_interface {
	/**
	 * get_flusher - (none)
	 * @id: 
	 * @surface: surface object
	 */
	void (*get_flusher)(struct wl_client *client,
			    struct wl_resource *resource,
			    uint32_t id,
			    struct wl_resource *surface);
};

struct tizen_surface_shm_flusher_interface {
	/**
	 * destroy - (none)
	 */
	void (*destroy)(struct wl_client *client,
			struct wl_resource *resource);
};

#define TIZEN_SURFACE_SHM_FLUSHER_FLUSH	0

static inline void
tizen_surface_shm_flusher_send_flush(struct wl_resource *resource_)
{
	wl_resource_post_event(resource_, TIZEN_SURFACE_SHM_FLUSHER_FLUSH);
}

#ifdef  __cplusplus
}
#endif

#endif
