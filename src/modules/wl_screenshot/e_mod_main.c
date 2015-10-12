#include "e.h"
#include <wayland-client.h>
#include <Ecore_Wayland.h>
#include <sys/mman.h>
#include <screenshooter-client-protocol.h>
#include "e_screenshooter_server.h"

typedef struct _Instance Instance;
struct _Instance
{
   E_Gadcon_Client *gcc;
   Evas_Object *o_btn;
};

static E_Gadcon_Client *_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style);
static void _gc_shutdown(E_Gadcon_Client *gcc);
static void _gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient);
static const char *_gc_label(const E_Gadcon_Client_Class *cc);
static Evas_Object *_gc_icon(const E_Gadcon_Client_Class *cc, Evas *evas);
static const char *_gc_id_new(const E_Gadcon_Client_Class *cc);
static void _cb_btn_down(void *data, Evas *evas __UNUSED__, Evas_Object *obj __UNUSED__, void *event);
static void _cb_handle_global(void *data, struct wl_registry *registry, unsigned int name, const char *interface, unsigned int version);
static void _cb_handle_global_remove(void *data __UNUSED__, struct wl_registry *registry __UNUSED__, unsigned int name __UNUSED__);
static struct wl_buffer *_create_shm_buffer(struct wl_shm *_shm, int width, int height, void **data_out);
static void _cb_handle_geometry(void *data __UNUSED__, struct wl_output *wl_output __UNUSED__, int x __UNUSED__, int y __UNUSED__, int w __UNUSED__, int h __UNUSED__, int subpixel __UNUSED__, const char *make __UNUSED__, const char *model __UNUSED__, int transform __UNUSED__);
static void _cb_handle_mode(void *data, struct wl_output *wl_output, unsigned int flags, int w, int h, int refresh);
static void _save_png(int w, int h, void *data);
static Eina_Bool _cb_timer(void *data __UNUSED__);

static const E_Gadcon_Client_Class _gc = 
{
   GADCON_CLIENT_CLASS_VERSION, "wl_screenshot", 
   {
      _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL, 
      e_gadcon_site_is_not_toolbar
   },
   E_GADCON_CLIENT_STYLE_PLAIN
};

static const struct wl_output_listener _output_listener = 
{
   _cb_handle_geometry,
   _cb_handle_mode
};

static const struct wl_registry_listener _registry_listener = 
{
   _cb_handle_global,
   _cb_handle_global_remove
};

static E_Module *_mod = NULL;
static struct screenshooter *_shooter = NULL;
static struct wl_output *_output;
static int ow = 0, oh = 0;
static Ecore_Timer *_timer = NULL;

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Screenshooter" };

EAPI void *
e_modapi_init(E_Module *m)
{
   struct wl_display *disp;
   struct wl_registry *reg;

   const char *engine_name;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   engine_name = ecore_evas_engine_name_get(e_comp->ee);
   if (!strncmp(engine_name, "drm", 3) || !strncmp(engine_name, "gl_drm", 2))
     return e_screenshooter_server_init(m);

   if (!ecore_wl_init(NULL)) return NULL;

   if (!(disp = ecore_wl_display_get()))
     {
        ecore_wl_shutdown();
        return NULL;
     }

   if (!(reg = ecore_wl_registry_get()))
     {
        ecore_wl_shutdown();
        return NULL;
     }

   _mod = m;
   e_gadcon_provider_register(&_gc);

   wl_registry_add_listener(reg, &_registry_listener, NULL);

   return m;
}

EAPI int 
e_modapi_shutdown(E_Module *m __UNUSED__)
{
   _mod = NULL;
   e_gadcon_provider_unregister(&_gc);
   ecore_wl_shutdown();
   return 1;
}

EAPI int 
e_modapi_save(E_Module *m __UNUSED__)
{
   return 1;
}

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Evas_Object *o;
   E_Gadcon_Client *gcc;
   Instance *inst;
   char buff[PATH_MAX];

   inst = E_NEW(Instance, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(inst, NULL);

   snprintf(buff, sizeof(buff), "%s/e-module-wl_screenshot.edj", _mod->dir);

   o = edje_object_add(gc->evas);
   if (!e_theme_edje_object_set(o, "base/theme/modules/wl_screenshot", 
                                "modules/wl_screenshot/main"))
     edje_object_file_set(o, buff, "modules/wl_screenshot/main");

   gcc = e_gadcon_client_new(gc, name, id, style, o);
   if (!gcc)
     {
        evas_object_del(o);
        E_FREE(inst);
        return NULL;
     }

   gcc->data = inst;

   inst->gcc = gcc;
   inst->o_btn = o;

   evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN, 
                                  _cb_btn_down, inst);
   return gcc;
}

static void 
_gc_shutdown(E_Gadcon_Client *gcc)
{
   Instance *inst;

   if (!(inst = gcc->data)) return;
   evas_object_del(inst->o_btn);
   free(inst);
}

static void 
_gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient __UNUSED__)
{
   Instance *inst;
   Evas_Coord mw, mh;

   inst = gcc->data;
   mw = 0, mh = 0;
   edje_object_size_min_get(inst->o_btn, &mw, &mh);
   if ((mw < 1) || (mh < 1))
     edje_object_size_min_calc(inst->o_btn, &mw, &mh);
   if (mw < 4) mw = 4;
   if (mh < 4) mh = 4;
   e_gadcon_client_aspect_set(gcc, mw, mh);
   e_gadcon_client_min_size_set(gcc, mw, mh);
}

static const char *
_gc_label(const E_Gadcon_Client_Class *cc __UNUSED__)
{
   return _("Screenshooter");
}

static Evas_Object *
_gc_icon(const E_Gadcon_Client_Class *cc __UNUSED__, Evas *evas)
{
   Evas_Object *o;
   char buf[PATH_MAX];

   o = edje_object_add(evas);
   snprintf(buf, sizeof(buf), "%s/e-module-wl_screenshot.edj", _mod->dir);
   edje_object_file_set(o, buf, "icon");
   return o;
}

static const char *
_gc_id_new(const E_Gadcon_Client_Class *cc __UNUSED__)
{
   return _gc.name;
}

static void 
_cb_btn_down(void *data __UNUSED__, Evas *evas __UNUSED__, Evas_Object *obj __UNUSED__, void *event)
{
   Evas_Event_Mouse_Down *ev;

   ev = event;
   if (ev->button == 1)
     {
        if (_timer) ecore_timer_del(_timer);
        _timer = ecore_timer_add(5.0, _cb_timer, NULL);
     }
}

static void 
_cb_handle_global(void *data __UNUSED__, struct wl_registry *registry, unsigned int name, const char *interface, unsigned int version __UNUSED__)
{
   if (!strcmp(interface, "screenshooter"))
     {
        _shooter = wl_registry_bind(registry, name, &screenshooter_interface, 1);
        /* FIXME: When we handle shots from multiple outputs, then we will 
         * need to setup the listener here */
        /* screenshooter_add_listener(_shooter, &, _shooter); */
     }
   else if (!strcmp(interface, "wl_output"))
     {
        _output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        wl_output_add_listener(_output, &_output_listener, NULL);
     }

}

static void 
_cb_handle_global_remove(void *data __UNUSED__, struct wl_registry *registry __UNUSED__, unsigned int name __UNUSED__)
{
   /* no-op */
}

static struct wl_buffer *
_create_shm_buffer(struct wl_shm *_shm, int width, int height, void **data_out)
{
   char filename[] = "wayland-shm-XXXXXX";
   Eina_Tmpstr *tmpfile = NULL;
   struct wl_shm_pool *pool;
   struct wl_buffer *buffer;
   int fd, size, stride;
   void *data;

   fd = eina_file_mkstemp(filename, &tmpfile);
   if (fd < 0) 
     {
        fprintf(stderr, "open %s failed: %m\n", tmpfile);
        return NULL;
     }

   stride = width * 4;
   size = stride * height;
   if (ftruncate(fd, size) < 0) 
     {
        fprintf(stderr, "ftruncate failed: %m\n");
        close(fd);
        return NULL;
     }

   data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   unlink(tmpfile);
   eina_tmpstr_del(tmpfile);

   if (data == MAP_FAILED) 
     {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return NULL;
     }

   pool = wl_shm_create_pool(_shm, fd, size);
   close(fd);
   if (!pool)
     return NULL;

   buffer = 
     wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                               WL_SHM_FORMAT_ARGB8888);
   wl_shm_pool_destroy(pool);

   *data_out = data;

   return buffer;
}

static void 
_cb_handle_geometry(void *data __UNUSED__, struct wl_output *wl_output __UNUSED__, int x __UNUSED__, int y __UNUSED__, int w __UNUSED__, int h __UNUSED__, int subpixel __UNUSED__, const char *make __UNUSED__, const char *model __UNUSED__, int transform __UNUSED__)
{
   /* no-op */
}

static void 
_cb_handle_mode(void *data __UNUSED__, struct wl_output *wl_output __UNUSED__, unsigned int flags __UNUSED__, int w, int h, int refresh __UNUSED__)
{
   if (ow == 0) ow = w;
   if (oh == 0) oh = h;
}

static void 
_save_png(int w, int h, void *data)
{
   Ecore_Evas *ee;
   Evas *evas;
   Evas_Object *img;
   char buff[1024];
   char fname[PATH_MAX];

   ee = ecore_evas_buffer_new(w, h);
   evas = ecore_evas_get(ee);

   img = evas_object_image_filled_add(evas);
   evas_object_image_fill_set(img, 0, 0, w, h);
   evas_object_image_size_set(img, w, h);
   evas_object_image_data_set(img, data);
   evas_object_show(img);

   snprintf(fname, sizeof(fname), "%s/screenshot.png", e_user_homedir_get());
   snprintf(buff, sizeof(buff), "quality=90 compress=9");
   if (!(evas_object_image_save(img, fname, NULL, buff)))
     {
        printf("Error saving shot\n");
     }

   if (img) evas_object_del(img);
   if (ee) ecore_evas_free(ee);
}

static Eina_Bool 
_cb_timer(void *data __UNUSED__)
{
   struct wl_buffer *buffer;
   void *d = NULL;

   if (!_shooter) return EINA_FALSE;

   /* FIXME: ow and oh should probably be the size of all outputs */
   buffer = _create_shm_buffer(ecore_wl_shm_get(), ow, oh, &d);
   if (!buffer) return EINA_FALSE;

   screenshooter_shoot(_shooter, _output, buffer);

   ecore_wl_sync();

   _save_png(ow, oh, d);

   wl_buffer_destroy(buffer);

   return EINA_FALSE;
}
