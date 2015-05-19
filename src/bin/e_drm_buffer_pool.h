#ifdef E_TYPEDEFS

#else
#ifndef E_BUFFER_POOL_H
#define E_BUFFER_POOL_H

#include <wayland-server.h>

typedef struct _E_Drm_Buffer_Pool E_Drm_Buffer_Pool;

typedef struct _E_Drm_Buffer
{
   int   width, height;
   uint  format;
   uint  name[3];
   int   offset[3];
   int   stride[3];

   E_Drm_Buffer_Pool *pool;
   struct wl_resource *resource;
   void *driver_buffer;
} E_Drm_Buffer;

typedef struct _E_Drm_Buffer_Callbacks
{
   uint  (*get_capbilities)(void *user_data);
   uint* (*get_formats)(void *user_data, int *format_cnt);
   void  (*reference_buffer)(void *user_data, E_Drm_Buffer *buffer);
   void  (*release_buffer)(void *user_data, E_Drm_Buffer *buffer);
} E_Drm_Buffer_Callbacks;

EAPI int e_drm_buffer_pool_init(struct wl_display *display,
                                E_Drm_Buffer_Callbacks *callbacks,
                                void *user_data);

EAPI E_Drm_Buffer *e_drm_buffer_get(struct wl_resource *resource);

#endif
#endif
