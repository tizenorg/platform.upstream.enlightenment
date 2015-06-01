#ifndef TIZEN_BUFFER_POOL_SERVER_PROTOCOL_H
#define TIZEN_BUFFER_POOL_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct tizen_buffer_pool;

extern const struct wl_interface tizen_buffer_pool_interface;

#ifndef TIZEN_BUFFER_POOL_ERROR_ENUM
#define TIZEN_BUFFER_POOL_ERROR_ENUM
enum tizen_buffer_pool_error {
	TIZEN_BUFFER_POOL_ERROR_INVALID_FORMAT = 0,
	TIZEN_BUFFER_POOL_ERROR_INVALID_NAME = 1,
};
#endif /* TIZEN_BUFFER_POOL_ERROR_ENUM */

#ifndef TIZEN_BUFFER_POOL_CAPABILITY_ENUM
#define TIZEN_BUFFER_POOL_CAPABILITY_ENUM
enum tizen_buffer_pool_capability {
	TIZEN_BUFFER_POOL_CAPABILITY_DEFAULT = 0,
	TIZEN_BUFFER_POOL_CAPABILITY_VIDEO = 0x1,
	TIZEN_BUFFER_POOL_CAPABILITY_SCREENMIRROR = 0x2,
};
#endif /* TIZEN_BUFFER_POOL_CAPABILITY_ENUM */

#ifndef TIZEN_BUFFER_POOL_FORMAT_ENUM
#define TIZEN_BUFFER_POOL_FORMAT_ENUM
enum tizen_buffer_pool_format {
	TIZEN_BUFFER_POOL_FORMAT_C8 = 0x20203843,
	TIZEN_BUFFER_POOL_FORMAT_RGB332 = 0x38424752,
	TIZEN_BUFFER_POOL_FORMAT_BGR233 = 0x38524742,
	TIZEN_BUFFER_POOL_FORMAT_XRGB4444 = 0x32315258,
	TIZEN_BUFFER_POOL_FORMAT_XBGR4444 = 0x32314258,
	TIZEN_BUFFER_POOL_FORMAT_RGBX4444 = 0x32315852,
	TIZEN_BUFFER_POOL_FORMAT_BGRX4444 = 0x32315842,
	TIZEN_BUFFER_POOL_FORMAT_ARGB4444 = 0x32315241,
	TIZEN_BUFFER_POOL_FORMAT_ABGR4444 = 0x32314241,
	TIZEN_BUFFER_POOL_FORMAT_RGBA4444 = 0x32314152,
	TIZEN_BUFFER_POOL_FORMAT_BGRA4444 = 0x32314142,
	TIZEN_BUFFER_POOL_FORMAT_XRGB1555 = 0x35315258,
	TIZEN_BUFFER_POOL_FORMAT_XBGR1555 = 0x35314258,
	TIZEN_BUFFER_POOL_FORMAT_RGBX5551 = 0x35315852,
	TIZEN_BUFFER_POOL_FORMAT_BGRX5551 = 0x35315842,
	TIZEN_BUFFER_POOL_FORMAT_ARGB1555 = 0x35315241,
	TIZEN_BUFFER_POOL_FORMAT_ABGR1555 = 0x35314241,
	TIZEN_BUFFER_POOL_FORMAT_RGBA5551 = 0x35314152,
	TIZEN_BUFFER_POOL_FORMAT_BGRA5551 = 0x35314142,
	TIZEN_BUFFER_POOL_FORMAT_RGB565 = 0x36314752,
	TIZEN_BUFFER_POOL_FORMAT_BGR565 = 0x36314742,
	TIZEN_BUFFER_POOL_FORMAT_RGB888 = 0x34324752,
	TIZEN_BUFFER_POOL_FORMAT_BGR888 = 0x34324742,
	TIZEN_BUFFER_POOL_FORMAT_XRGB8888 = 0x34325258,
	TIZEN_BUFFER_POOL_FORMAT_XBGR8888 = 0x34324258,
	TIZEN_BUFFER_POOL_FORMAT_RGBX8888 = 0x34325852,
	TIZEN_BUFFER_POOL_FORMAT_BGRX8888 = 0x34325842,
	TIZEN_BUFFER_POOL_FORMAT_ARGB8888 = 0x34325241,
	TIZEN_BUFFER_POOL_FORMAT_ABGR8888 = 0x34324241,
	TIZEN_BUFFER_POOL_FORMAT_RGBA8888 = 0x34324152,
	TIZEN_BUFFER_POOL_FORMAT_BGRA8888 = 0x34324142,
	TIZEN_BUFFER_POOL_FORMAT_XRGB2101010 = 0x30335258,
	TIZEN_BUFFER_POOL_FORMAT_XBGR2101010 = 0x30334258,
	TIZEN_BUFFER_POOL_FORMAT_RGBX1010102 = 0x30335852,
	TIZEN_BUFFER_POOL_FORMAT_BGRX1010102 = 0x30335842,
	TIZEN_BUFFER_POOL_FORMAT_ARGB2101010 = 0x30335241,
	TIZEN_BUFFER_POOL_FORMAT_ABGR2101010 = 0x30334241,
	TIZEN_BUFFER_POOL_FORMAT_RGBA1010102 = 0x30334152,
	TIZEN_BUFFER_POOL_FORMAT_BGRA1010102 = 0x30334142,
	TIZEN_BUFFER_POOL_FORMAT_YUYV = 0x56595559,
	TIZEN_BUFFER_POOL_FORMAT_YVYU = 0x55595659,
	TIZEN_BUFFER_POOL_FORMAT_UYVY = 0x59565955,
	TIZEN_BUFFER_POOL_FORMAT_VYUY = 0x59555956,
	TIZEN_BUFFER_POOL_FORMAT_AYUV = 0x56555941,
	TIZEN_BUFFER_POOL_FORMAT_NV12 = 0x3231564e,
	TIZEN_BUFFER_POOL_FORMAT_NV21 = 0x3132564e,
	TIZEN_BUFFER_POOL_FORMAT_NV16 = 0x3631564e,
	TIZEN_BUFFER_POOL_FORMAT_NV61 = 0x3136564e,
	TIZEN_BUFFER_POOL_FORMAT_YUV410 = 0x39565559,
	TIZEN_BUFFER_POOL_FORMAT_YVU410 = 0x39555659,
	TIZEN_BUFFER_POOL_FORMAT_YUV411 = 0x31315559,
	TIZEN_BUFFER_POOL_FORMAT_YVU411 = 0x31315659,
	TIZEN_BUFFER_POOL_FORMAT_YUV420 = 0x32315559,
	TIZEN_BUFFER_POOL_FORMAT_YVU420 = 0x32315659,
	TIZEN_BUFFER_POOL_FORMAT_YUV422 = 0x36315559,
	TIZEN_BUFFER_POOL_FORMAT_YVU422 = 0x36315659,
	TIZEN_BUFFER_POOL_FORMAT_YUV444 = 0x34325559,
	TIZEN_BUFFER_POOL_FORMAT_YVU444 = 0x34325659,
	TIZEN_BUFFER_POOL_FORMAT_SN12 = 0x32314e53,
	TIZEN_BUFFER_POOL_FORMAT_ST12 = 0x32315453,
};
#endif /* TIZEN_BUFFER_POOL_FORMAT_ENUM */

struct tizen_buffer_pool_interface {
	/**
	 * authenticate - (none)
	 * @id: (none)
	 */
	void (*authenticate)(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t id);
	/**
	 * create_buffer - (none)
	 * @id: (none)
	 * @name: (none)
	 * @width: (none)
	 * @height: (none)
	 * @stride: (none)
	 * @format: (none)
	 */
	void (*create_buffer)(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t id,
			      uint32_t name,
			      int32_t width,
			      int32_t height,
			      uint32_t stride,
			      uint32_t format);
	/**
	 * create_planar_buffer - (none)
	 * @id: (none)
	 * @width: (none)
	 * @height: (none)
	 * @format: (none)
	 * @name0: (none)
	 * @offset0: (none)
	 * @stride0: (none)
	 * @name1: (none)
	 * @offset1: (none)
	 * @stride1: (none)
	 * @name2: (none)
	 * @offset2: (none)
	 * @stride2: (none)
	 */
	void (*create_planar_buffer)(struct wl_client *client,
				     struct wl_resource *resource,
				     uint32_t id,
				     int32_t width,
				     int32_t height,
				     uint32_t format,
				     uint32_t name0,
				     int32_t offset0,
				     int32_t stride0,
				     uint32_t name1,
				     int32_t offset1,
				     int32_t stride1,
				     uint32_t name2,
				     int32_t offset2,
				     int32_t stride2);
};

#define TIZEN_BUFFER_POOL_DEVICE	0
#define TIZEN_BUFFER_POOL_AUTHENTICATED	1
#define TIZEN_BUFFER_POOL_CAPABILITIES	2
#define TIZEN_BUFFER_POOL_FORMAT	3

#define TIZEN_BUFFER_POOL_DEVICE_SINCE_VERSION	1
#define TIZEN_BUFFER_POOL_AUTHENTICATED_SINCE_VERSION	1
#define TIZEN_BUFFER_POOL_CAPABILITIES_SINCE_VERSION	1
#define TIZEN_BUFFER_POOL_FORMAT_SINCE_VERSION	1

static inline void
tizen_buffer_pool_send_device(struct wl_resource *resource_, const char *name)
{
	wl_resource_post_event(resource_, TIZEN_BUFFER_POOL_DEVICE, name);
}

static inline void
tizen_buffer_pool_send_authenticated(struct wl_resource *resource_)
{
	wl_resource_post_event(resource_, TIZEN_BUFFER_POOL_AUTHENTICATED);
}

static inline void
tizen_buffer_pool_send_capabilities(struct wl_resource *resource_, uint32_t value)
{
	wl_resource_post_event(resource_, TIZEN_BUFFER_POOL_CAPABILITIES, value);
}

static inline void
tizen_buffer_pool_send_format(struct wl_resource *resource_, uint32_t format)
{
	wl_resource_post_event(resource_, TIZEN_BUFFER_POOL_FORMAT, format);
}

#ifdef  __cplusplus
}
#endif

#endif
