#ifdef E_TYPEDEFS

#else
# ifndef E_COMP_WL_H
#  define E_COMP_WL_H

/* NB: Turn off shadow warnings for Wayland includes */
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#  define WL_HIDE_DEPRECATED
#  include <wayland-server.h>
#  include <tizen-extension-server-protocol.h>
#  pragma GCC diagnostic pop

#  include <xkbcommon/xkbcommon.h>

/* #  ifdef HAVE_WAYLAND_EGL */
/* #   include <EGL/egl.h> */
/* #   define GL_GLEXT_PROTOTYPES */
/* #  endif */

#  ifdef __linux__
#   include <linux/input.h>
#  else
#   define BTN_LEFT 0x110
#   define BTN_RIGHT 0x111
#   define BTN_MIDDLE 0x112
#   define BTN_SIDE 0x113
#   define BTN_EXTRA 0x114
#   define BTN_FORWARD 0x115
#   define BTN_BACK 0x116
#  endif

#  define container_of(ptr, type, member) \
   ({ \
      const __typeof__( ((type *)0)->member ) *__mptr = (ptr); \
      (type *)( (char *)__mptr - offsetof(type,member) ); \
   })

#include <Evas_GL.h>

typedef struct _E_Comp_Wl_Aux_Hint  E_Comp_Wl_Aux_Hint;
typedef struct _E_Comp_Wl_Buffer E_Comp_Wl_Buffer;
typedef struct _E_Comp_Wl_Buffer_Ref E_Comp_Wl_Buffer_Ref;
typedef struct _E_Comp_Wl_Buffer_Viewport E_Comp_Wl_Buffer_Viewport;
typedef struct _E_Comp_Wl_Subsurf_Data E_Comp_Wl_Subsurf_Data;
typedef struct _E_Comp_Wl_Surface_State E_Comp_Wl_Surface_State;
typedef struct _E_Comp_Wl_Client_Data E_Comp_Wl_Client_Data;
typedef struct _E_Comp_Wl_Data E_Comp_Wl_Data;
typedef struct _E_Comp_Wl_Output E_Comp_Wl_Output;
typedef struct _E_Comp_Wl_Input_Device E_Comp_Wl_Input_Device;

typedef enum _E_Comp_Wl_Buffer_Type
{
   E_COMP_WL_BUFFER_TYPE_NONE = 0,
   E_COMP_WL_BUFFER_TYPE_SHM = 1,
   E_COMP_WL_BUFFER_TYPE_NATIVE = 2,
   E_COMP_WL_BUFFER_TYPE_VIDEO = 3,
} E_Comp_Wl_Buffer_Type;

struct _E_Comp_Wl_Aux_Hint
{
   unsigned int id;
   const char *hint;
   const char *val;
};

struct _E_Comp_Wl_Buffer
{
   E_Comp_Wl_Buffer_Type type;
   struct wl_resource *resource;
   struct wl_signal destroy_signal;
   struct wl_listener destroy_listener;
   struct wl_shm_buffer *shm_buffer;
   int32_t w, h;
   uint32_t busy;
};

struct _E_Comp_Wl_Buffer_Ref
{
   E_Comp_Wl_Buffer *buffer;
   struct wl_listener destroy_listener;
};

struct _E_Comp_Wl_Buffer_Viewport {
   struct
     {
        uint32_t transform;   /* wl_surface.set_buffer_transform */
        int32_t scale;        /* wl_surface.set_scaling_factor */

        /* If src_width != wl_fixed_from_int(-1), then and only then src_* are used. */
        wl_fixed_t src_x, src_y;
        wl_fixed_t src_width, src_height;
     } buffer;

   struct
     {
        /* If width == -1, the size is inferred from the buffer. */
        int32_t width, height;
     } surface;

   int changed;
};

struct _E_Comp_Wl_Surface_State
{
   int sx, sy;
   int bw, bh;
   E_Comp_Wl_Buffer *buffer;
   struct wl_listener buffer_destroy_listener;
   Eina_List *damages, *frames;
   Eina_Tiler *input, *opaque;
   E_Comp_Wl_Buffer_Viewport buffer_viewport;
   Eina_Bool new_attach : 1;
   Eina_Bool has_data : 1;
};

struct _E_Comp_Wl_Subsurf_Data
{
   struct wl_resource *resource;

   E_Client *parent;

   struct
     {
        int x, y;
        Eina_Bool set;
     } position;

   E_Comp_Wl_Surface_State cached;
   E_Comp_Wl_Buffer_Ref cached_buffer_ref;

   Eina_Bool synchronized;
};

struct _E_Comp_Wl_Input_Device
{
   Eina_List *resources;
   const char *name;
   const char *identifier;
   unsigned int capability;
};

struct _E_Comp_Wl_Data
{
   struct
     {
        struct wl_display *disp;
        struct wl_event_loop *loop;
     } wl;

   struct
     {
        struct
          {
             struct wl_signal create;
             struct wl_signal activate;
             struct wl_signal kill;
          } surface;
        /* NB: At the moment, we don't need these */
        /*      struct wl_signal destroy; */
        /*      struct wl_signal activate; */
        /*      struct wl_signal transform; */
        /*      struct wl_signal kill; */
        /*      struct wl_signal idle; */
        /*      struct wl_signal wake; */
        /*      struct wl_signal session; */
        /*      struct  */
        /*        { */
        /*           struct wl_signal created; */
        /*           struct wl_signal destroyed; */
        /*           struct wl_signal moved; */
        /*        } seat, output; */
     } signals;

   struct
     {
        struct wl_resource *shell;
        struct wl_resource *xdg_shell;
     } shell_interface;

   struct
     {
        struct wl_global *global;
        Eina_List *resources;
        Eina_List *device_list;
        const char *curr_device_name;
        unsigned int curr_device_cap;
        struct
          {
             double radius_x;
             double radius_y;
             double pressure;
             double angle;
          } multi;
     } input_device_mgr;

   struct
     {
        Eina_List *resources;
        Eina_List *focused;
        Eina_Bool enabled : 1;
        xkb_mod_index_t mod_shift, mod_caps;
        xkb_mod_index_t mod_ctrl, mod_alt;
        xkb_mod_index_t mod_super;
        xkb_mod_mask_t mod_depressed, mod_latched, mod_locked;
        xkb_layout_index_t mod_group;
        struct wl_array keys;
        struct wl_resource *focus;
        int mod_changed;
        int repeat_delay;
        int repeat_rate;
        unsigned int num_devices;
     } kbd;

   struct
     {
        Eina_List *resources;
        Eina_Bool enabled : 1;
        wl_fixed_t x, y;
        wl_fixed_t grab_x, grab_y;
        uint32_t button;
        Ecore_Timer *hide_tmr;
        Eina_Bool hidden :1;
        unsigned int num_devices;
     } ptr;

   struct
     {
        Eina_List *resources;
        Eina_Bool enabled : 1;
        unsigned int num_devices;
     } touch;

   struct
     {
        struct wl_global *global;
        Eina_List *resources;
        uint32_t version;
        char *name;
     } seat;

   struct
     {
        struct wl_global *global;
        Eina_List *data_resources;
     } mgr;

   struct
     {
        void *data_source;
        uint32_t serial;
        struct wl_signal signal;
        struct wl_listener data_source_listener;
     } selection;

   struct
     {
        void *source;
        struct wl_listener listener;
     } clipboard;

   struct
     {
        void *data_source;
        E_Client *icon;
        uint32_t serial;
        struct wl_signal signal;
        struct wl_listener data_source_listener;
        struct wl_client *client;
        struct wl_resource *focus;
        Eina_Bool enabled : 1;
     } dnd;

   struct
     {
        struct wl_resource *resource;
        uint32_t edges;
     } resize;

   struct
     {
        struct xkb_keymap *keymap;
        struct xkb_context *context;
        struct xkb_state *state;
        int fd;
        size_t size;
        char *area;
     } xkb;

   struct
     {
        Evas_GL *evasgl;
        Evas_GL_API *api;
        Evas_GL_Surface *sfc;
        Evas_GL_Context *ctx;
     } gl;

   struct
     {
        Eina_Bool underlay;
        Eina_Bool scaler;
     } available_hw_accel;

   struct
     {
        void *server;
     } tbm;

   Eina_List *outputs;

   Ecore_Fd_Handler *fd_hdlr;
   Ecore_Idler *idler;

   /* Eina_List *retry_clients; */
   /* Ecore_Timer *retry_timer; */
   Eina_Bool restack : 1;
};

struct _E_Comp_Wl_Client_Data
{
   struct wl_resource *wl_surface;

   Ecore_Timer *on_focus_timer;

   struct
     {
        E_Comp_Wl_Subsurf_Data *data;

        Eina_List *list;
        Eina_List *list_pending;
        Eina_Bool list_changed : 1;

        Eina_List *below_list;
        Eina_List *below_list_pending;
        Evas_Object *below_obj;

        Eina_Bool restacking : 1;
     } sub;

   /* regular surface resource (wl_compositor_create_surface) */
   struct wl_resource *surface;
   struct wl_signal destroy_signal;

   struct
     {
        /* shell surface resource */
        struct wl_resource *surface;

        void (*configure_send)(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height);
        void (*configure)(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h);
        void (*ping)(struct wl_resource *resource);
        void (*map)(struct wl_resource *resource);
        void (*unmap)(struct wl_resource *resource);
        Eina_Rectangle window;
     } shell;

   E_Comp_Wl_Buffer_Ref buffer_ref;
   E_Comp_Wl_Surface_State pending;

   Eina_List *frames;

   struct
     {
        int32_t x, y;
     } popup;

   struct
     {
        struct wl_resource *viewport;
        E_Comp_Wl_Buffer_Viewport buffer_viewport;
     } scaler;

   struct
     {
        Eina_Bool enabled : 1;
        Eina_Bool start : 1;

        unsigned int scount, stime;
        int sx, sy, dx, dy;
        int prev_degree, cur_degree;
     } transform;

   struct
     {
        Eina_Bool  changed : 1;
        Eina_List *hints;
     } aux_hint;

   /* before applying viewport */
   int width_from_buffer;
   int height_from_buffer;

   /* after applying viewport */
   int width_from_viewport;
   int height_from_viewport;

   Eina_Bool keep_buffer : 1;
   Eina_Bool mapped : 1;
   Eina_Bool change_icon : 1;
   Eina_Bool need_reparent : 1;
   Eina_Bool reparented : 1;
   Eina_Bool evas_init : 1;
   Eina_Bool first_damage : 1;
   Eina_Bool set_win_type : 1;
   Eina_Bool frame_update : 1;
   Eina_Bool focus_update : 1;
   Eina_Bool opaque_state : 1;
   Eina_Bool video_client : 1;
   unsigned char accepts_focus : 1;
   unsigned char conformant : 1;
   E_Window_Type win_type;
   E_Layer layer;

   struct
   {
      unsigned char win_type : 1;
      unsigned char layer : 1;
   } fetch;
};

struct _E_Comp_Wl_Output
{
   struct wl_global *global;
   Eina_List *resources;
   const char *id, *make, *model;
   int x, y, w, h;
   int phys_width, phys_height;
   unsigned int refresh;
   unsigned int subpixel;
   unsigned int transform;
   double scale;
};

EAPI Eina_Bool e_comp_wl_init(void);
EINTERN void e_comp_wl_shutdown(void);

EAPI void e_comp_wl_deferred_job(void);

EINTERN struct wl_resource *e_comp_wl_surface_create(struct wl_client *client, int version, uint32_t id);
EINTERN void e_comp_wl_surface_destroy(struct wl_resource *resource);
EINTERN void e_comp_wl_surface_attach(E_Client *ec, E_Comp_Wl_Buffer *buffer);
EINTERN Eina_Bool e_comp_wl_surface_commit(E_Client *ec);
EINTERN Eina_Bool e_comp_wl_subsurface_commit(E_Client *ec);
EAPI void e_comp_wl_buffer_reference(E_Comp_Wl_Buffer_Ref *ref, E_Comp_Wl_Buffer *buffer);
EAPI E_Comp_Wl_Buffer *e_comp_wl_buffer_get(struct wl_resource *resource, struct wl_resource *surface);
EAPI Eina_Bool e_comp_wl_subsurface_create(E_Client *ec, E_Client *epc, uint32_t id, struct wl_resource *surface_resource);

EAPI struct wl_signal e_comp_wl_surface_create_signal_get(E_Comp *comp);
EAPI double e_comp_wl_idle_time_get(void);
EAPI void e_comp_wl_output_init(const char *id, const char *make, const char *model, int x, int y, int w, int h, int pw, int ph, unsigned int refresh, unsigned int subpixel, unsigned int transform);

# endif
#endif
