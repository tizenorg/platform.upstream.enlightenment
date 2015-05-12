#ifndef ESMGR_CLIENT_PROTOCOL_H
#define ESMGR_CLIENT_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

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

#define ES_BUFFER_QUEUE_MANAGER_CREATE_CONSUMER	0
#define ES_BUFFER_QUEUE_MANAGER_CREATE_PROVIDER	1

static inline void
es_buffer_queue_manager_set_user_data(struct es_buffer_queue_manager *es_buffer_queue_manager, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) es_buffer_queue_manager, user_data);
}

static inline void *
es_buffer_queue_manager_get_user_data(struct es_buffer_queue_manager *es_buffer_queue_manager)
{
	return wl_proxy_get_user_data((struct wl_proxy *) es_buffer_queue_manager);
}

static inline void
es_buffer_queue_manager_destroy(struct es_buffer_queue_manager *es_buffer_queue_manager)
{
	wl_proxy_destroy((struct wl_proxy *) es_buffer_queue_manager);
}

static inline struct es_consumer *
es_buffer_queue_manager_create_consumer(struct es_buffer_queue_manager *es_buffer_queue_manager, const char *name, int32_t queue_size, int32_t width, int32_t height)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_constructor((struct wl_proxy *) es_buffer_queue_manager,
			 ES_BUFFER_QUEUE_MANAGER_CREATE_CONSUMER, &es_consumer_interface, NULL, name, queue_size, width, height);

	return (struct es_consumer *) id;
}

static inline struct es_provider *
es_buffer_queue_manager_create_provider(struct es_buffer_queue_manager *es_buffer_queue_manager, const char *name)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_constructor((struct wl_proxy *) es_buffer_queue_manager,
			 ES_BUFFER_QUEUE_MANAGER_CREATE_PROVIDER, &es_provider_interface, NULL, name);

	return (struct es_provider *) id;
}

struct es_consumer_listener {
	/**
	 * connected - (none)
	 */
	void (*connected)(void *data,
			  struct es_consumer *es_consumer);
	/**
	 * disconnected - (none)
	 */
	void (*disconnected)(void *data,
			     struct es_consumer *es_consumer);
	/**
	 * buffer_attached - (none)
	 * @buffer: (none)
	 * @engine: (none)
	 * @width: (none)
	 * @height: (none)
	 * @format: (none)
	 * @flags: (none)
	 */
	void (*buffer_attached)(void *data,
				struct es_consumer *es_consumer,
				struct es_buffer *buffer,
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
	void (*set_buffer_id)(void *data,
			      struct es_consumer *es_consumer,
			      struct es_buffer *buffer,
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
	void (*set_buffer_fd)(void *data,
			      struct es_consumer *es_consumer,
			      struct es_buffer *buffer,
			      int32_t fd,
			      int32_t offset0,
			      int32_t stride0,
			      int32_t offset1,
			      int32_t stride1,
			      int32_t offset2,
			      int32_t stride2);
	/**
	 * buffer_detached - (none)
	 * @buffer: (none)
	 */
	void (*buffer_detached)(void *data,
				struct es_consumer *es_consumer,
				struct es_buffer *buffer);
	/**
	 * add_buffer - (none)
	 * @buffer: (none)
	 * @serial: (none)
	 */
	void (*add_buffer)(void *data,
			   struct es_consumer *es_consumer,
			   struct es_buffer *buffer,
			   uint32_t serial);
};

static inline int
es_consumer_add_listener(struct es_consumer *es_consumer,
			 const struct es_consumer_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) es_consumer,
				     (void (**)(void)) listener, data);
}

#define ES_CONSUMER_RELEASE_BUFFER	0

static inline void
es_consumer_set_user_data(struct es_consumer *es_consumer, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) es_consumer, user_data);
}

static inline void *
es_consumer_get_user_data(struct es_consumer *es_consumer)
{
	return wl_proxy_get_user_data((struct wl_proxy *) es_consumer);
}

static inline void
es_consumer_destroy(struct es_consumer *es_consumer)
{
	wl_proxy_destroy((struct wl_proxy *) es_consumer);
}

static inline void
es_consumer_release_buffer(struct es_consumer *es_consumer, struct es_buffer *buffer)
{
	wl_proxy_marshal((struct wl_proxy *) es_consumer,
			 ES_CONSUMER_RELEASE_BUFFER, buffer);
}

#ifndef ES_PROVIDER_ERROR_ENUM
#define ES_PROVIDER_ERROR_ENUM
enum es_provider_error {
	ES_PROVIDER_ERROR_OVERFLOW_QUEUE_SIZE = 0,
	ES_PROVIDER_ERROR_CONNECTION = 0,
};
#endif /* ES_PROVIDER_ERROR_ENUM */

struct es_provider_listener {
	/**
	 * connected - (none)
	 * @queue_size: (none)
	 * @width: (none)
	 * @height: (none)
	 */
	void (*connected)(void *data,
			  struct es_provider *es_provider,
			  int32_t queue_size,
			  int32_t width,
			  int32_t height);
	/**
	 * disconnected - (none)
	 */
	void (*disconnected)(void *data,
			     struct es_provider *es_provider);
	/**
	 * add_buffer - (none)
	 * @buffer: (none)
	 * @serial: (none)
	 */
	void (*add_buffer)(void *data,
			   struct es_provider *es_provider,
			   struct es_buffer *buffer,
			   uint32_t serial);
};

static inline int
es_provider_add_listener(struct es_provider *es_provider,
			 const struct es_provider_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) es_provider,
				     (void (**)(void)) listener, data);
}

#define ES_PROVIDER_ATTACH_BUFFER	0
#define ES_PROVIDER_SET_BUFFER_ID	1
#define ES_PROVIDER_SET_BUFFER_FD	2
#define ES_PROVIDER_DETACH_BUFFER	3
#define ES_PROVIDER_ENQUEUE_BUFFER	4

static inline void
es_provider_set_user_data(struct es_provider *es_provider, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) es_provider, user_data);
}

static inline void *
es_provider_get_user_data(struct es_provider *es_provider)
{
	return wl_proxy_get_user_data((struct wl_proxy *) es_provider);
}

static inline void
es_provider_destroy(struct es_provider *es_provider)
{
	wl_proxy_destroy((struct wl_proxy *) es_provider);
}

static inline struct es_buffer *
es_provider_attach_buffer(struct es_provider *es_provider, const char *engine, int32_t width, int32_t height, int32_t format, uint32_t flags)
{
	struct wl_proxy *buffer;

	buffer = wl_proxy_marshal_constructor((struct wl_proxy *) es_provider,
			 ES_PROVIDER_ATTACH_BUFFER, &es_buffer_interface, NULL, engine, width, height, format, flags);

	return (struct es_buffer *) buffer;
}

static inline void
es_provider_set_buffer_id(struct es_provider *es_provider, struct es_buffer *buffer, int32_t id, int32_t offset0, int32_t stride0, int32_t offset1, int32_t stride1, int32_t offset2, int32_t stride2)
{
	wl_proxy_marshal((struct wl_proxy *) es_provider,
			 ES_PROVIDER_SET_BUFFER_ID, buffer, id, offset0, stride0, offset1, stride1, offset2, stride2);
}

static inline void
es_provider_set_buffer_fd(struct es_provider *es_provider, struct es_buffer *buffer, int32_t fd, int32_t offset0, int32_t stride0, int32_t offset1, int32_t stride1, int32_t offset2, int32_t stride2)
{
	wl_proxy_marshal((struct wl_proxy *) es_provider,
			 ES_PROVIDER_SET_BUFFER_FD, buffer, fd, offset0, stride0, offset1, stride1, offset2, stride2);
}

static inline void
es_provider_detach_buffer(struct es_provider *es_provider, struct es_buffer *buffer)
{
	wl_proxy_marshal((struct wl_proxy *) es_provider,
			 ES_PROVIDER_DETACH_BUFFER, buffer);
}

static inline void
es_provider_enqueue_buffer(struct es_provider *es_provider, struct es_buffer *buffer, uint32_t serial)
{
	wl_proxy_marshal((struct wl_proxy *) es_provider,
			 ES_PROVIDER_ENQUEUE_BUFFER, buffer, serial);
}

static inline void
es_buffer_set_user_data(struct es_buffer *es_buffer, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) es_buffer, user_data);
}

static inline void *
es_buffer_get_user_data(struct es_buffer *es_buffer)
{
	return wl_proxy_get_user_data((struct wl_proxy *) es_buffer);
}

static inline void
es_buffer_destroy(struct es_buffer *es_buffer)
{
	wl_proxy_destroy((struct wl_proxy *) es_buffer);
}

#ifdef  __cplusplus
}
#endif

#endif
