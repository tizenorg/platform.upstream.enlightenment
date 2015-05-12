#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <wayland-server.h>

#define E_COMP_WL
#include "es_share_server_protocol.h"
#include "e.h"
#include "e_mod_main.h"

#define ES_MGR_TYPE        (int)0xE0b91001
#define ES_QUEUE_TYPE      (int)0xE0b91002
#define ES_CONSUMER_TYPE   (int)0xE0b91003
#define ES_PROVIDER_TYPE   (int)0xE0b91004
#define ES_BUFFER_TYPE     (int)0xE0b91005

#define ARRAY_LENGTH(a)       (sizeof (a) / sizeof (a)[0])

#define ES_LOG(f, x ...)      printf("[ES|%30.30s|%04d] " f "\n", __func__, __LINE__, ##x)
#define ES_DEBUG(f, x ...)    if (debug) \
    printf("[ES|%30.30s|%04d] " f "\n", __func__, __LINE__, ##x)

typedef struct _Es_Mgr             Es_Mgr;
typedef struct _Es_Buffer_Queue    Es_Buffer_Queue;
typedef struct _Es_Buffer_Consumer Es_Buffer_Consumer;
typedef struct _Es_Buffer_Provider Es_Buffer_Provider;
typedef struct _Es_Buffer          Es_Buffer;
typedef enum _Es_Buffer_Type       Es_Buffer_Type;

struct _Es_Mgr
{
   E_Object               e_obj_inherit;

   int self_dpy;
   struct wl_display      *wdpy;
   struct wl_event_source *signals[3];
   struct wl_event_loop   *loop;

   /*BufferQueue manager*/
   struct wl_global       *es_buffer_queue_manager;
   Eina_Hash              *buffer_queues;

   Ecore_Fd_Handler       *fdHandler;
   Ecore_Idle_Enterer     *idler;
};

struct _Es_Buffer_Queue
{
   E_Object           e_obj_inherit;
   Eina_Hash          *link;

   char               *name;
   struct wl_signal    connect;

   Es_Buffer_Consumer *consumer;
   Es_Buffer_Provider *provider;
   Eina_Inlist        *buffers;
};

struct _Es_Buffer_Consumer
{
   E_Object         e_obj_inherit;
   Es_Buffer_Queue *bufferQueue;

   int32_t          queue_size;
   int32_t          width;
   int32_t          height;
};

struct _Es_Buffer_Provider
{
   E_Object         e_obj_inherit;
   Es_Buffer_Queue *bufferQueue;
};

enum _Es_Buffer_Type
{
   ES_BUFFER_TYPE_ID,
   ES_BUFFER_TYPE_FD,
};

struct _Es_Buffer
{
   E_Object            e_obj_inherit; /*Dont use wl_resource in e_obj*/
   EINA_INLIST;

   struct wl_resource *consumer;
   struct wl_resource *provider;
   uint32_t            serial;

   char               *engine;
   Es_Buffer_Type      type;
   int32_t             width;
   int32_t             height;
   int32_t             format;
   uint32_t            flags;

   int32_t             id;
   int32_t             offset0;
   int32_t             stride0;
   int32_t             offset1;
   int32_t             stride1;
   int32_t             offset2;
   int32_t             stride2;
};

Eina_Bool   es_mgr_share_init(Es_Mgr *esMgr);
Eina_Bool   es_mgr_buffer_queue_manager_init(Es_Mgr *esMgr);
static void __es_buffer_create_consumer_side(Es_Buffer_Consumer *bqCon, Es_Buffer *esBuf);
static void __es_buffer_set_consumer_side(Es_Buffer_Consumer *bqCon, Es_Buffer *esBuf);

static Eina_Bool debug = EINA_FALSE;

static Eina_Bool
_es_mgr_wl_cb_read(void *data, Ecore_Fd_Handler *hdl EINA_UNUSED)
{
   Es_Mgr *esMgr = (Es_Mgr *)data;

   if (!esMgr) return ECORE_CALLBACK_RENEW;
   if (!esMgr->wdpy) return ECORE_CALLBACK_RENEW;

   /* flush any pending client events */
   wl_display_flush_clients(esMgr->wdpy);

   /* dispatch any pending main loop events */
   wl_event_loop_dispatch(esMgr->loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_es_mgr_wl_cb_idle(void *data)
{
   Es_Mgr *esMgr = (Es_Mgr *)data;

   if (!esMgr) return ECORE_CALLBACK_RENEW;
   if (!esMgr->wdpy) return ECORE_CALLBACK_RENEW;

   /* flush any pending client events */
   wl_display_flush_clients(esMgr->wdpy);

   /* dispatch any pending main loop events */
   wl_event_loop_dispatch(esMgr->loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static int
es_mgr_on_term_signal(int signal_number, void *data)
{
   Es_Mgr *esMgr = (Es_Mgr *)data;

   ES_LOG("caught signal %d\n", signal_number);
   wl_display_terminate(esMgr->wdpy);

   return 1;
}

static void
es_mgr_free(Es_Mgr *esMgr)
{
   int i;

   if (!esMgr->self_dpy) return;

   for (i = ARRAY_LENGTH(esMgr->signals) - 1; i >= 0; i--)
     if (esMgr->signals[i])
       wl_event_source_remove(esMgr->signals[i]);

   if (esMgr->wdpy)
     wl_display_destroy(esMgr->wdpy);
}

static Es_Mgr *
es_mgr_new(char *sock_name)
{
   E_Comp *comp;

   static char *default_sock_name = "es_mgr_daemon";
   Es_Mgr *esMgr = E_OBJECT_ALLOC(Es_Mgr, ES_MGR_TYPE, es_mgr_free);

   EINA_SAFETY_ON_NULL_RETURN_VAL(esMgr, NULL);

   /* try to get the current compositor */
   if (!(comp = e_comp)) return NULL;

   if (comp->comp_type == E_PIXMAP_TYPE_X ||
      sock_name != NULL)
     {
        int fd;

        esMgr->wdpy = wl_display_create();
        esMgr->loop = wl_display_get_event_loop(esMgr->wdpy);
        EINA_SAFETY_ON_NULL_GOTO(esMgr->loop, on_err);

        esMgr->signals[0] = wl_event_loop_add_signal(esMgr->loop, SIGTERM, es_mgr_on_term_signal, esMgr);
        esMgr->signals[1] = wl_event_loop_add_signal(esMgr->loop, SIGINT, es_mgr_on_term_signal, esMgr);
        esMgr->signals[2] = wl_event_loop_add_signal(esMgr->loop, SIGQUIT, es_mgr_on_term_signal, esMgr);

        if (!sock_name)
          sock_name = default_sock_name;
        wl_display_add_socket(esMgr->wdpy, sock_name);

        fd = wl_event_loop_get_fd(esMgr->loop);
        esMgr->fdHandler = ecore_main_fd_handler_add(fd, ECORE_FD_READ | ECORE_FD_WRITE,
                                                     _es_mgr_wl_cb_read, esMgr, NULL, NULL);

        esMgr->idler = ecore_idle_enterer_add(_es_mgr_wl_cb_idle, esMgr);

        esMgr->self_dpy = 1;
     }
   else
     {
        E_Comp_Data *cdata;

        /* try to get the compositor data */
        if (!(cdata = comp->wl_comp_data)) goto on_err;
        esMgr->wdpy = cdata->wl.disp;
        esMgr->loop = wl_display_get_event_loop(esMgr->wdpy);

        esMgr->self_dpy = 0;
     }

   return esMgr;

on_err:
   free(esMgr);
   return NULL;
}

static void
__es_mgr_buffer_queue_free(Es_Buffer_Queue *bq)
{
   Es_Buffer *buf;
   EINA_SAFETY_ON_NULL_RETURN(bq);

   ES_DEBUG("destroy buffer queue : %s\n", bq->name);

   if (bq->consumer)
     {
        wl_resource_destroy(e_object_data_get(E_OBJECT(bq->consumer)));
        bq->consumer = NULL;
     }

   if (bq->provider)
     {
        wl_resource_destroy(e_object_data_get(E_OBJECT(bq->provider)));
        bq->provider = NULL;
     }

   while (bq->buffers)
     {
        buf = EINA_INLIST_CONTAINER_GET(bq->buffers, Es_Buffer);
        bq->buffers = eina_inlist_remove(bq->buffers, bq->buffers);
        if (buf->consumer)
          {
             wl_resource_destroy(buf->consumer);
             buf->consumer = NULL;
          }

        if (buf->provider)
          {
             wl_resource_destroy(buf->provider);
             buf->provider = NULL;
          }
     }

   if (bq->link)
     {
        eina_hash_del(bq->link, bq->name, bq);
        bq->link = NULL;
     }
   if (bq->name)
     {
        free(bq->name);
        bq->name = NULL;
     }
}

static Es_Buffer_Queue *
__es_mgr_buffer_queue_new(Es_Mgr *esMgr, const char *name)
{
   Es_Buffer_Queue *bq;

   bq = eina_hash_find(esMgr->buffer_queues, name);
   if (bq)
     {
        e_object_ref(E_OBJECT(bq));
        return bq;
     }

   bq = E_OBJECT_ALLOC(Es_Buffer_Queue, ES_QUEUE_TYPE, __es_mgr_buffer_queue_free);
   EINA_SAFETY_ON_NULL_RETURN_VAL(bq, NULL);

   bq->link = esMgr->buffer_queues;
   bq->name = strdup(name);
   if (!eina_hash_add(bq->link, bq->name, bq))
     {
        free(bq->name);
        free(bq);
        return NULL;
     }

   wl_signal_init(&bq->connect);
   bq->buffers = NULL;
   return bq;
}

static void
_es_mgr_buffer_consumer_release_buffer(struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *buffer)
{
   Es_Buffer_Queue *bq;
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Provider *bqPro;
   Es_Buffer *esBuf;

   bqCon = (Es_Buffer_Consumer *)wl_resource_get_user_data(resource);
   esBuf = (Es_Buffer *)wl_resource_get_user_data(buffer);
   bq = bqCon->bufferQueue;
   bqPro = bq->provider;

   if (bqPro && esBuf->provider)
     {
        es_provider_send_add_buffer(e_object_data_get(E_OBJECT(bqPro)),
                                    esBuf->provider, esBuf->serial);
     }
}

static const struct es_consumer_interface _es_consumer_interface = {
   _es_mgr_buffer_consumer_release_buffer
};

static void
_es_mgr_buffer_consumer_destroy(struct wl_resource *resource)
{
   Es_Buffer_Consumer *bqCon = wl_resource_get_user_data(resource);
   Es_Buffer_Provider *bqPro;
   Es_Buffer_Queue *bq;
   Es_Buffer *buf;

   EINA_SAFETY_ON_NULL_RETURN(bqCon);
   ES_DEBUG("destroy buffer consumer : %s\n", bqCon->bufferQueue->name);
   bq = bqCon->bufferQueue;
   bqPro = bq->provider;

   bq->consumer = NULL;
   e_object_data_set(E_OBJECT(bqCon), NULL);

   if (bqPro)
     es_provider_send_disconnected(e_object_data_get(E_OBJECT(bqPro)));

   while (bq->buffers)
     {
        buf = EINA_INLIST_CONTAINER_GET(bq->buffers, Es_Buffer);
        bq->buffers = eina_inlist_remove(bq->buffers, bq->buffers);

        ES_DEBUG("destroy BUFFER : %d\n", buf->type);
        if (buf->consumer)
          {
             wl_resource_destroy(buf->consumer);
             buf->consumer = NULL;
             e_object_unref(E_OBJECT(buf));
          }

        if (buf->provider)
          {
             wl_resource_destroy(buf->provider);
             buf->provider = NULL;
             e_object_del(E_OBJECT(buf));
          }
     }

   e_object_unref(E_OBJECT(bq));
   e_object_del(E_OBJECT(bqCon));
}

static void
_es_mgr_buffer_consumer_free(Es_Buffer_Consumer *bqCon)
{
   free(bqCon);
}

static void
_es_mgr_buffer_queue_create_consumer(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     const char *name,
                                     int32_t queue_size,
                                     int32_t width,
                                     int32_t height)
{
   Es_Mgr *esMgr = (Es_Mgr *)wl_resource_get_user_data(resource);
   Es_Buffer_Queue *bq;
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Provider *bqPro;
   Es_Buffer *buf;
   struct wl_resource *new_resource;

   EINA_SAFETY_ON_NULL_RETURN(esMgr);

   bq = __es_mgr_buffer_queue_new(esMgr, name);
   EINA_SAFETY_ON_NULL_RETURN(bq);

   if (bq->consumer)
     {
        e_object_unref(E_OBJECT(bq));
        wl_resource_post_error(resource,
                               ES_BUFFER_QUEUE_MANAGER_ERROR_ALREADY_USED,
                               "%s consumer already used", name);
        return;
     }

   bqCon = E_OBJECT_ALLOC(Es_Buffer_Consumer, ES_CONSUMER_TYPE, _es_mgr_buffer_consumer_free);
   EINA_SAFETY_ON_NULL_RETURN(bqCon);

   new_resource = wl_resource_create(client,
                                 &es_consumer_interface,
                                 1, id);
   if (!new_resource)
     {
        free(bqCon);
        wl_client_post_no_memory(client);
        return;
     }

   e_object_data_set(E_OBJECT(bqCon), new_resource);
   wl_resource_set_implementation(new_resource,
                                  &_es_consumer_interface,
                                  bqCon,
                                  _es_mgr_buffer_consumer_destroy);

   bqCon->bufferQueue = bq;
   bqCon->queue_size = queue_size;
   bqCon->width = width;
   bqCon->height = height;

   bqPro = bq->provider;
   bq->consumer = bqCon;
   if (bqPro)
     {
        es_provider_send_connected(e_object_data_get(E_OBJECT(bqPro)),
                                   queue_size, width, height);
        es_consumer_send_connected(new_resource);
     }

   EINA_INLIST_FOREACH(bq->buffers, buf)
     {
        __es_buffer_create_consumer_side(bqCon, buf);
        __es_buffer_set_consumer_side(bqCon, buf);
     }
}

static void
_es_mgr_buffer_destroy(struct wl_resource *resource)
{
   Es_Buffer *buf = wl_resource_get_user_data(resource);

   if (resource == buf->consumer)
     {
        ES_DEBUG("destroy buffer : consumer\n");
     }
   else if (resource == buf->provider)
     {
        ES_DEBUG("destroy buffer : provider\n");
     }
}

static void
__es_buffer_create_consumer_side(Es_Buffer_Consumer *bqCon, Es_Buffer *esBuf)
{
   struct wl_resource *resource;
   if (!bqCon) return;

   resource = e_object_data_get(E_OBJECT(bqCon));
   esBuf->consumer = wl_resource_create(wl_resource_get_client(resource),
                                        &es_buffer_interface, 1, 0);
   wl_resource_set_implementation(esBuf->consumer, NULL, esBuf, _es_mgr_buffer_destroy);
   e_object_ref(E_OBJECT(esBuf));

   es_consumer_send_buffer_attached(resource,
                                    esBuf->consumer,
                                    esBuf->engine,
                                    esBuf->width,
                                    esBuf->height,
                                    esBuf->format,
                                    esBuf->flags);
}

static void
__es_buffer_set_consumer_side(Es_Buffer_Consumer *bqCon, Es_Buffer *esBuf)
{
   struct wl_resource *resource;

   if (!bqCon) return;

   EINA_SAFETY_ON_NULL_RETURN(esBuf);
   EINA_SAFETY_ON_NULL_RETURN(esBuf->consumer);

   resource = e_object_data_get(E_OBJECT(bqCon));

   if (esBuf->type == ES_BUFFER_TYPE_ID)
     es_consumer_send_set_buffer_id(resource,
                                    esBuf->consumer,
                                    esBuf->id,
                                    esBuf->offset0,
                                    esBuf->stride0,
                                    esBuf->offset1,
                                    esBuf->stride1,
                                    esBuf->offset2,
                                    esBuf->stride2);
   else
     {
#if 0
        //Change smack label
        // TODO : Change proper smack label
        if (fsetxattr(esBuf->id, "security.SMACK64", "*", 2, 0))
          {
             ES_LOG("Error setxattr\n");
             close(esBuf->id);
             return;
          }
#endif
        es_consumer_send_set_buffer_fd(resource,
                                       esBuf->consumer,
                                       esBuf->id,
                                       esBuf->offset0,
                                       esBuf->stride0,
                                       esBuf->offset1,
                                       esBuf->stride1,
                                       esBuf->offset2,
                                       esBuf->stride2);
        close(esBuf->id);
     }
}

static void
_es_buffer_free(Es_Buffer *buf)
{
   if (buf->engine)
     free(buf->engine);
}

static void
_es_mgr_buffer_provider_attatch_buffer(struct wl_client *client,
                                       struct wl_resource *resource,
                                       uint32_t buffer,
                                       const char *engine,
                                       int32_t width,
                                       int32_t height,
                                       int32_t format,
                                       uint32_t flags)
{
   Es_Buffer_Provider *bqPro = wl_resource_get_user_data(resource);
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Queue *bq;
   Es_Buffer *esBuf;

   EINA_SAFETY_ON_NULL_RETURN(bqPro);
   bq = bqPro->bufferQueue;
   bqCon = bq->consumer;

   esBuf = E_OBJECT_ALLOC(Es_Buffer, ES_BUFFER_TYPE, _es_buffer_free);
   esBuf->provider = wl_resource_create(client, &es_buffer_interface, 1, buffer);
   if (!esBuf->provider)
     {
        free(esBuf);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(esBuf->provider, NULL, esBuf, _es_mgr_buffer_destroy);

   esBuf->engine = strdup(engine);
   esBuf->width = width;
   esBuf->height = height;
   esBuf->format = format;
   esBuf->flags = flags;

   bq->buffers = eina_inlist_append(bq->buffers, EINA_INLIST_GET(esBuf));
   ES_DEBUG("add BUFFER : %d\n", esBuf->type);

   __es_buffer_create_consumer_side(bqCon, esBuf);
}

static void
_es_mgr_buffer_provider_set_buffer_id(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *buffer,
                                      int32_t id,
                                      int32_t offset0,
                                      int32_t stride0,
                                      int32_t offset1,
                                      int32_t stride1,
                                      int32_t offset2,
                                      int32_t stride2)
{
   Es_Buffer_Provider *bqPro = wl_resource_get_user_data(resource);
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Queue *bq;
   Es_Buffer *esBuf;

   EINA_SAFETY_ON_NULL_RETURN(bqPro);
   bq = bqPro->bufferQueue;
   bqCon = bq->consumer;
   esBuf = wl_resource_get_user_data(buffer);
   EINA_SAFETY_ON_NULL_RETURN(esBuf);

   esBuf->type = ES_BUFFER_TYPE_ID;
   esBuf->id = id;
   esBuf->offset0 = offset0;
   esBuf->stride0 = stride0;
   esBuf->offset1 = offset1;
   esBuf->stride1 = stride1;
   esBuf->offset2 = offset2;
   esBuf->stride2 = stride2;

   __es_buffer_set_consumer_side(bqCon, esBuf);
}

static void
_es_mgr_buffer_provider_set_buffer_fd(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *buffer,
                                      int32_t fd,
                                      int32_t offset0,
                                      int32_t stride0,
                                      int32_t offset1,
                                      int32_t stride1,
                                      int32_t offset2,
                                      int32_t stride2)
{
   Es_Buffer_Provider *bqPro = wl_resource_get_user_data(resource);
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Queue *bq;
   Es_Buffer *esBuf;

   EINA_SAFETY_ON_NULL_RETURN(bqPro);
   bq = bqPro->bufferQueue;
   bqCon = bq->consumer;
   esBuf = wl_resource_get_user_data(buffer);
   EINA_SAFETY_ON_NULL_RETURN(esBuf);

   esBuf->type = ES_BUFFER_TYPE_FD;
   esBuf->id = fd;
   esBuf->offset0 = offset0;
   esBuf->stride0 = stride0;
   esBuf->offset1 = offset1;
   esBuf->stride1 = stride1;
   esBuf->offset2 = offset2;
   esBuf->stride2 = stride2;

   __es_buffer_set_consumer_side(bqCon, esBuf);
}

static void
_es_mgr_buffer_provider_detach_buffer(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *buffer)
{
   Es_Buffer_Provider *bqPro = wl_resource_get_user_data(resource);
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Queue *bq;
   Es_Buffer *esBuf;

   EINA_SAFETY_ON_NULL_RETURN(bqPro);
   bq = bqPro->bufferQueue;
   bqCon = bq->consumer;
   esBuf = wl_resource_get_user_data(buffer);

   if (bqCon)
     {
        es_consumer_send_buffer_detached(e_object_data_get(E_OBJECT(bqCon)),
                                         esBuf->consumer);
        wl_resource_destroy(esBuf->consumer);
        e_object_unref(E_OBJECT(esBuf));
     }

   wl_resource_destroy(esBuf->provider);
   bq->buffers = eina_inlist_remove(bq->buffers, EINA_INLIST_GET(esBuf));
   e_object_del(E_OBJECT(esBuf));
}

static void
_es_mgr_buffer_provider_enqueue_buffer(struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *buffer,
                                       uint32_t serial)
{
   Es_Buffer_Provider *bqPro = wl_resource_get_user_data(resource);
   Es_Buffer_Consumer *bqCon;
   Es_Buffer_Queue *bq;
   Es_Buffer *esBuf;

   EINA_SAFETY_ON_NULL_RETURN(bqPro);
   bq = bqPro->bufferQueue;
   bqCon = bq->consumer;
   if (!bqCon)
     {
        /* (!bqCon)
        wl_resource_post_error(ES_OBJECT_RESOURCE(bqCon),
                               ES_PROVIDER_ERROR_CONNECTION,
                               "Not connected:%s", bq->name);
         */
        return;
     }

   esBuf = wl_resource_get_user_data(buffer);
   EINA_SAFETY_ON_NULL_RETURN(esBuf);
   esBuf->serial = serial;

   es_consumer_send_add_buffer(e_object_data_get(E_OBJECT(bqCon)),
                               esBuf->consumer,
                               esBuf->serial);
}

static const struct es_provider_interface _es_provider_interface = {
   _es_mgr_buffer_provider_attatch_buffer,
   _es_mgr_buffer_provider_set_buffer_id,
   _es_mgr_buffer_provider_set_buffer_fd,
   _es_mgr_buffer_provider_detach_buffer,
   _es_mgr_buffer_provider_enqueue_buffer
};

static void
_es_mgr_buffer_provider_destroy(struct wl_resource *resource)
{
   Es_Buffer_Queue *bq;
   Es_Buffer_Provider *bqPro = wl_resource_get_user_data(resource);
   Es_Buffer_Consumer *bqCon;
   Es_Buffer *buf;

   EINA_SAFETY_ON_NULL_RETURN(bqPro);
   ES_DEBUG("destroy buffer provider : %s\n", bqPro->bufferQueue->name);
   bq = bqPro->bufferQueue;
   bqCon = bq->consumer;

   e_object_data_set(E_OBJECT(bqPro), NULL);
   bq->provider = NULL;

   while (bq->buffers)
     {
        buf = EINA_INLIST_CONTAINER_GET(bq->buffers, Es_Buffer);
        bq->buffers = eina_inlist_remove(bq->buffers, bq->buffers);

        if (buf->consumer)
          {
             es_consumer_send_buffer_detached(e_object_data_get(E_OBJECT(bqCon)),
                                              buf->consumer);
             wl_resource_destroy(buf->consumer);
             buf->consumer = NULL;
             e_object_unref(E_OBJECT(buf));
          }

        if (buf->provider)
          {
             wl_resource_destroy(buf->provider);
             buf->provider = NULL;
             e_object_del(E_OBJECT(buf));
          }
     }

   if (bqCon)
     {
        struct wl_resource *resource = e_object_data_get(E_OBJECT(bqCon));

        if (resource)
          es_consumer_send_disconnected(resource);
     }

   e_object_del(E_OBJECT(bqPro));
   e_object_unref(E_OBJECT(bq));
}

static void
_es_mgr_buffer_provider_free(Es_Buffer_Provider *bqPro)
{
   free(bqPro);
}

static void
_es_mgr_buffer_queue_create_provider(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     const char *name)
{
   Es_Mgr *esMgr = (Es_Mgr *)wl_resource_get_user_data(resource);
   Es_Buffer_Queue *bq;
   Es_Buffer_Provider *bqPro;
   Es_Buffer_Consumer *bqCon;
   struct wl_resource *new_resource;

   EINA_SAFETY_ON_NULL_RETURN(esMgr);

   bq = __es_mgr_buffer_queue_new(esMgr, name);
   EINA_SAFETY_ON_NULL_RETURN(bq);

   if (bq->provider)
     {
        e_object_unref(E_OBJECT(bq));
        wl_resource_post_error(resource,
                               ES_BUFFER_QUEUE_MANAGER_ERROR_ALREADY_USED,
                               "%s rpovider already used", name);
        return;
     }

   bqPro = E_OBJECT_ALLOC(Es_Buffer_Provider, ES_PROVIDER_TYPE, _es_mgr_buffer_provider_free);
   EINA_SAFETY_ON_NULL_GOTO(bqPro, on_error);

   new_resource = wl_resource_create(client,
                                 &es_provider_interface,
                                 1, id);
   EINA_SAFETY_ON_NULL_GOTO(new_resource, on_error);

   e_object_data_set(E_OBJECT(bqPro), new_resource);

   wl_resource_set_implementation(new_resource,
                                  &_es_provider_interface,
                                  bqPro,
                                  _es_mgr_buffer_provider_destroy);

   bqPro->bufferQueue = bq;
   bq->provider = bqPro;
   bqCon = bq->consumer;
   if (bqCon)
     {
        /*Send connect*/
        es_consumer_send_connected(e_object_data_get(E_OBJECT(bqCon)));
        es_provider_send_connected(new_resource,
                                   bqCon->queue_size,
                                   bqCon->width, bqCon->height);
     }

   return;

on_error:
   if (bq) e_object_unref(E_OBJECT(bq));
   if (bqPro) free(bqPro);
   wl_client_post_no_memory(client);
}

static const struct es_buffer_queue_manager_interface _es_buffer_queue_manager_interface =
{
   _es_mgr_buffer_queue_create_consumer,
   _es_mgr_buffer_queue_create_provider
};

static void
_es_mgr_buffer_queue_bind(struct wl_client *client, void *data,
                          uint32_t version, uint32_t id)
{
   Es_Mgr *esMgr = (Es_Mgr *)data;
   struct wl_resource *resource;

   resource = wl_resource_create(client, &es_buffer_queue_manager_interface,
                                 1, id);
   if (resource == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource,
                                  &_es_buffer_queue_manager_interface,
                                  esMgr, NULL);
}

Eina_Bool
es_mgr_buffer_queue_manager_init(Es_Mgr *esMgr)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(esMgr, EINA_FALSE);
   esMgr->es_buffer_queue_manager = wl_global_create(esMgr->wdpy
                                                     , &es_buffer_queue_manager_interface, 1
                                                     , esMgr
                                                     , _es_mgr_buffer_queue_bind);

   EINA_SAFETY_ON_NULL_RETURN_VAL(esMgr->es_buffer_queue_manager, EINA_FALSE);

   esMgr->buffer_queues = eina_hash_string_superfast_new(NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(esMgr->buffer_queues, EINA_FALSE);

   return EINA_TRUE;
}

/* this is needed to advertise a label for the module IN the code (not just
 * the .desktop file) but more specifically the api version it was compiled
 * for so E can skip modules that are compiled for an incorrect API version
 * safely) */
EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION, "Buffer queue manager"
};

EAPI void *
e_modapi_init(E_Module *m)
{
   Es_Mgr *esMgr = NULL;

   esMgr = es_mgr_new(NULL);
   EINA_SAFETY_ON_NULL_GOTO(esMgr, finish);
   EINA_SAFETY_ON_FALSE_GOTO(es_mgr_buffer_queue_manager_init(esMgr), finish);
   m->data = esMgr;
   return m;
finish:
   if (esMgr)
      e_object_del(E_OBJECT(esMgr));

   return NULL;
}

EAPI int
e_modapi_shutdown(E_Module *m)
{
   Es_Mgr *esMgr = (Es_Mgr *)m->data;
   e_object_del(E_OBJECT(esMgr));
   return 1;
}

EAPI int
e_modapi_save(E_Module *m)
{
   /* Do Something */
   return 1;
}

