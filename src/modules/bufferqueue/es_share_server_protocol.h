#ifndef ESMGR_SERVER_PROTOCOL_H
#define ESMGR_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct es_buffer_queue_manager;
struct es_consumer;
struct es_provider;
struct es_buffer;

extern const struct wl_interface es_buffer_queue_manager_interface;
extern const struct wl_interface es_consumer_interface;
extern const struct wl_interface es_provider_interface;
extern const struct wl_interface es_buffer_interface;

#ifndef ES_BUFFER_QUEUE_MANAGER_ERROR_ENUM
#define ES_BUFFER_QUEUE_MANAGER_ERROR_ENUM
enum es_buffer_queue_manager_error {
	ES_BUFFER_QUEUE_MANAGER_ERROR_INVALID_PERMISSION = 0,
	ES_BUFFER_QUEUE_MANAGER_ERROR_INVALID_NAME = 1,
	ES_BUFFER_QUEUE_MANAGER_ERROR_ALREADY_USED = 2,
};
#endif /* ES_BUFFER_QUEUE_MANAGER_ERROR_ENUM */

struct es_buffer_queue_manager_interface {
	/**
	 * create_consumer - (none)
	 * @id: (none)
	 * @name: (none)
	 * @queue_size: (none)
	 * @width: (none)
	 * @height: (none)
	 */
	void (*create_consumer)(struct wl_client *client,
				struct wl_resource *resource,
				uint32_t id,
				const char *name,
				int32_t queue_size,
				int32_t width,
				int32_t height);
	/**
	 * create_provider - (none)
	 * @id: (none)
	 * @name: (none)
	 */
	void (*create_provider)(struct wl_client *client,
				struct wl_resource *resource,
				uint32_t id,
				const char *name);
};


struct es_consumer_interface {
	/**
	 * release_buffer - (none)
	 * @buffer: (none)
	 */
	void (*release_buffer)(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *buffer);
};

#define ES_CONSUMER_CONNECTED	0
#define ES_CONSUMER_DISCONNECTED	1
#define ES_CONSUMER_BUFFER_ATTACHED	2
#define ES_CONSUMER_SET_BUFFER_ID	3
#define ES_CONSUMER_SET_BUFFER_FD	4
#define ES_CONSUMER_BUFFER_DETACHED	5
#define ES_CONSUMER_ADD_BUFFER	6

#define ES_CONSUMER_CONNECTED_SINCE_VERSION	1
#define ES_CONSUMER_DISCONNECTED_SINCE_VERSION	1
#define ES_CONSUMER_BUFFER_ATTACHED_SINCE_VERSION	1
#define ES_CONSUMER_SET_BUFFER_ID_SINCE_VERSION	1
#define ES_CONSUMER_SET_BUFFER_FD_SINCE_VERSION	1
#define ES_CONSUMER_BUFFER_DETACHED_SINCE_VERSION	1
#define ES_CONSUMER_ADD_BUFFER_SINCE_VERSION	1

static inline void
es_consumer_send_connected(struct wl_resource *resource_)
{
	wl_resource_post_event(resource_, ES_CONSUMER_CONNECTED);
}

static inline void
es_consumer_send_disconnected(struct wl_resource *resource_)
{
	wl_resource_post_event(resource_, ES_CONSUMER_DISCONNECTED);
}

static inline void
es_consumer_send_buffer_attached(struct wl_resource *resource_, struct wl_resource *buffer, const char *engine, int32_t width, int32_t height, int32_t format, uint32_t flags)
{
	wl_resource_post_event(resource_, ES_CONSUMER_BUFFER_ATTACHED, buffer, engine, width, height, format, flags);
}

static inline void
es_consumer_send_set_buffer_id(struct wl_resource *resource_, struct wl_resource *buffer, int32_t id, int32_t offset0, int32_t stride0, int32_t offset1, int32_t stride1, int32_t offset2, int32_t stride2)
{
	wl_resource_post_event(resource_, ES_CONSUMER_SET_BUFFER_ID, buffer, id, offset0, stride0, offset1, stride1, offset2, stride2);
}

static inline void
es_consumer_send_set_buffer_fd(struct wl_resource *resource_, struct wl_resource *buffer, int32_t fd, int32_t offset0, int32_t stride0, int32_t offset1, int32_t stride1, int32_t offset2, int32_t stride2)
{
	wl_resource_post_event(resource_, ES_CONSUMER_SET_BUFFER_FD, buffer, fd, offset0, stride0, offset1, stride1, offset2, stride2);
}

static inline void
es_consumer_send_buffer_detached(struct wl_resource *resource_, struct wl_resource *buffer)
{
	wl_resource_post_event(resource_, ES_CONSUMER_BUFFER_DETACHED, buffer);
}

static inline void
es_consumer_send_add_buffer(struct wl_resource *resource_, struct wl_resource *buffer, uint32_t serial)
{
	wl_resource_post_event(resource_, ES_CONSUMER_ADD_BUFFER, buffer, serial);
}

#ifndef ES_PROVIDER_ERROR_ENUM
#define ES_PROVIDER_ERROR_ENUM
enum es_provider_error {
	ES_PROVIDER_ERROR_OVERFLOW_QUEUE_SIZE = 0,
	ES_PROVIDER_ERROR_CONNECTION = 0,
};
#endif /* ES_PROVIDER_ERROR_ENUM */

struct es_provider_interface {
	/**
	 * attach_buffer - (none)
	 * @buffer: (none)
	 * @engine: (none)
	 * @width: (none)
	 * @height: (none)
	 * @format: (none)
	 * @flags: (none)
	 */
	void (*attach_buffer)(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t buffer,
			      const char *engine,
			      int32_t width,
			      int32_t height,
			      int32_t format,
			      uint32_t flags);
	/**
	 * set_buffer_id - (none)
	 * @buffer: (none)
	 * @id: (none)
	 * @offset0: (none)
	 * @stride0: (none)
	 * @offset1: (none)
	 * @stride1: (none)
	 * @offset2: (none)
	 * @stride2: (none)
	 */
	void (*set_buffer_id)(struct wl_client *client,
			      struct wl_resource *resource,
			      struct wl_resource *buffer,
			      int32_t id,
			      int32_t offset0,
			      int32_t stride0,
			      int32_t offset1,
			      int32_t stride1,
			      int32_t offset2,
			      int32_t stride2);
	/**
	 * set_buffer_fd - (none)
	 * @buffer: (none)
	 * @fd: (none)
	 * @offset0: (none)
	 * @stride0: (none)
	 * @offset1: (none)
	 * @stride1: (none)
	 * @offset2: (none)
	 * @stride2: (none)
	 */
	void (*set_buffer_fd)(struct wl_client *client,
			      struct wl_resource *resource,
			      struct wl_resource *buffer,
			      int32_t fd,
			      int32_t offset0,
			      int32_t stride0,
			      int32_t offset1,
			      int32_t stride1,
			      int32_t offset2,
			      int32_t stride2);
	/**
	 * detach_buffer - (none)
	 * @buffer: (none)
	 */
	void (*detach_buffer)(struct wl_client *client,
			      struct wl_resource *resource,
			      struct wl_resource *buffer);
	/**
	 * enqueue_buffer - (none)
	 * @buffer: (none)
	 * @serial: (none)
	 */
	void (*enqueue_buffer)(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *buffer,
			       uint32_t serial);
};

#define ES_PROVIDER_CONNECTED	0
#define ES_PROVIDER_DISCONNECTED	1
#define ES_PROVIDER_ADD_BUFFER	2

#define ES_PROVIDER_CONNECTED_SINCE_VERSION	1
#define ES_PROVIDER_DISCONNECTED_SINCE_VERSION	1
#define ES_PROVIDER_ADD_BUFFER_SINCE_VERSION	1

static inline void
es_provider_send_connected(struct wl_resource *resource_, int32_t queue_size, int32_t width, int32_t height)
{
	wl_resource_post_event(resource_, ES_PROVIDER_CONNECTED, queue_size, width, height);
}

static inline void
es_provider_send_disconnected(struct wl_resource *resource_)
{
	wl_resource_post_event(resource_, ES_PROVIDER_DISCONNECTED);
}

static inline void
es_provider_send_add_buffer(struct wl_resource *resource_, struct wl_resource *buffer, uint32_t serial)
{
	wl_resource_post_event(resource_, ES_PROVIDER_ADD_BUFFER, buffer, serial);
}


#ifdef  __cplusplus
}
#endif

#endif
