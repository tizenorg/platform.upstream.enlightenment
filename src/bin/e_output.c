#include "e.h"


/////////////////////////////////////////////////////////////////////////
static void                    _do_apply(void);
static void                    _info_free(E_Output *r);
static void                    _screen_config_eval(void);
static void                    _screen_config_maxsize(void);
static Eina_Bool               _hwc_set(E_Output_Screen * screen, E_Hwc_Mode mode, Eina_List* prepare_ec_list);


/////////////////////////////////////////////////////////////////////////

E_API E_Output        *e_output = NULL;

E_API int              E_EVENT_SCREEN_CHANGE = 0;

static Eina_List *all_screens = NULL; // e_screen list

/////////////////////////////////////////////////////////////////////////
EINTERN Eina_Bool
e_output_init(void)
{
   if (!E_EVENT_SCREEN_CHANGE) E_EVENT_SCREEN_CHANGE = ecore_event_type_new();
   if (!e_comp_drm_available()) return EINA_FALSE;

   _do_apply();

   ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);

   return EINA_TRUE;
}

EINTERN int
e_output_shutdown(void)
{
   // free up screen info
   _info_free(e_output);
   e_output = NULL;

   return 1;
}

/////////////////////////////////////////////////////////////////////////

static void
_do_apply(void)
{
   // take current screen config and apply it to the driver
   printf("OUTPUT: re-get info before applying..\n");
   _info_free(e_output);
   e_output = e_comp_drm_create();
   _screen_config_maxsize();
   printf("OUTPUT: eval config...\n");
   _screen_config_eval();
   printf("OUTPUT: really apply config...\n");
   e_comp_drm_apply();
   printf("OUTPUT: done config...\n");
}

static void
_info_free(E_Output *r)
{
   E_Output_Screen *s;
   E_Output_Mode *m;
   E_Plane *ep;

   if (!r) return;
   // free up our output screen data
   EINA_LIST_FREE(r->screens, s)
     {
        free(s->id);
        free(s->info.screen);
        free(s->info.name);
        free(s->info.edid);
        EINA_LIST_FREE(s->info.modes, m) free(m);
        EINA_LIST_FREE(s->planes, ep) e_plane_free(ep);
        free(s);
     }
   free(r);
}

static void
_screen_config_eval(void)
{
   Eina_List *l;
   E_Output_Screen *s;
   int minx, miny, maxx, maxy;

   minx = 65535;
   miny = 65535;
   maxx = -65536;
   maxy = -65536;

   EINA_LIST_FOREACH(e_output->screens, l, s)
     {
        if (!s->config.enabled) continue;
        if (s->config.geom.x < minx) minx = s->config.geom.x;
        if (s->config.geom.y < miny) miny = s->config.geom.y;
        if ((s->config.geom.x + s->config.geom.w) > maxx)
          maxx = s->config.geom.x + s->config.geom.w;
        if ((s->config.geom.y + s->config.geom.h) > maxy)
          maxy = s->config.geom.y + s->config.geom.h;
        printf("OUTPUT: s: '%s' @ %i %i - %ix%i\n",
               s->info.name,
               s->config.geom.x, s->config.geom.y,
               s->config.geom.w, s->config.geom.h);
     }
   printf("OUTPUT:--- %i %i -> %i %i\n", minx, miny, maxx, maxy);
   EINA_LIST_FOREACH(e_output->screens, l, s)
     {
        s->config.geom.x -= minx;
        s->config.geom.y -= miny;
     }
   e_output->w = maxx - minx;
   e_output->h = maxy - miny;
}

static void
_screen_config_maxsize(void)
{
   Eina_List *l;
   E_Output_Screen *s;
   int maxx, maxy;

   maxx = -65536;
   maxy = -65536;
   EINA_LIST_FOREACH(e_output->screens, l, s)
     {
        if (!s->config.enabled) continue;
        if ((s->config.geom.x + s->config.geom.w) > maxx)
          maxx = s->config.geom.x + s->config.geom.w;
        if ((s->config.geom.y + s->config.geom.h) > maxy)
          maxy = s->config.geom.y + s->config.geom.h;
        printf("OUTPUT: '%s': %i %i %ix%i\n",
               s->info.name,
               s->config.geom.x, s->config.geom.y,
               s->config.geom.w, s->config.geom.h);
     }
   printf("OUTPUT: result max: %ix%i\n", maxx, maxy);
   e_output->w = maxx;
   e_output->h = maxy;
}

static int
_screen_sort_cb(const void *data1, const void *data2)
{
   const E_Output_Screen *s1 = data1, *s2 = data2;
   int dif;

   dif = -(s1->config.priority - s2->config.priority);
   if (dif == 0)
     {
        dif = s1->config.geom.x - s2->config.geom.x;
        if (dif == 0)
          dif = s1->config.geom.y - s2->config.geom.y;
     }
   return dif;
}

static void
_escreen_free(E_Screen *scr)
{
   free(scr->id);
   free(scr);
}

void
_escreens_set(Eina_List *screens)
{
   E_FREE_LIST(all_screens, _escreen_free);
   all_screens = screens;
}

static Eina_Bool
_hwc_set(E_Output_Screen * screen, E_Hwc_Mode mode, Eina_List* prepare_ec_list)
{
   Eina_List *l_p, *l_ec;
   Eina_List *l, *ll;
   E_Plane *ep;
   int num_c, num_p;

   EINA_SAFETY_ON_NULL_RETURN_VAL(screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(screen->planes, EINA_FALSE);
   INF("HWC : mode(%d) (%d)overlays\n", mode, eina_list_count(prepare_ec_list));

   l_p = screen->planes; // Overlay sort by Z
   num_p = screen->plane_count;

   l_ec = prepare_ec_list; // Visible clients sort by Z
   num_c = eina_list_count(prepare_ec_list);

   if ((mode == E_HWC_MODE_COMPOSITE) ||
       (mode == E_HWC_MODE_HWC_COMPOSITE))
     {
        ep = eina_list_data_get(l_p);
        if (ep)
          {
             num_p--;
             e_client_redirected_set(ep->ec, 1);
             ep->ec = NULL; // 1st plane is assigned for e_comp->evas
          }
        l_p = eina_list_next(l_p);
     }

   if ((num_c < 1) || (num_p < 1))
     {
        INF("HWC : prepared (%d) overlays on (%d) planes are wrong\n", num_c, num_p);
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(l_p, EINA_FALSE);
   EINA_LIST_REVERSE_FOREACH_SAFE(l_p, l, ll, ep)
     {
        E_Client *ec = NULL;

        if (ep->ec) e_client_redirected_set(ep->ec, 1);

        if (num_p < 1) break;

        if (num_c < num_p)
          {
             num_p--; continue;
          }

        if (!l_ec) break;
        ec = eina_list_data_get(l_ec);
        if(ec)
          {
             INF("HWC : set '%s' on overlay(%d)\n", ec->icccm.title, num_p);
             e_client_redirected_set(ec, 0);
             ep->ec = ec;
          }
        num_p--;
        l_ec = eina_list_next(l_ec);
     }
   e_comp->hwc_mode = mode;

   return EINA_TRUE;
}

EINTERN void
e_output_screens_setup(int rw, int rh)
{
   int i;
   E_Screen *screen;
   Eina_List *screens = NULL, *screens_rem;
   Eina_List *e_screens = NULL;
   Eina_List *l, *ll;
   E_Output_Screen *s, *s2, *s_chosen;
   Eina_Bool removed;

   if ((!e_output) || (!e_output->screens)) goto out;
   // put screens in tmp list
   EINA_LIST_FOREACH(e_output->screens, l, s)
     {
        if ((s->config.enabled) &&
            (s->config.geom.w > 0) &&
            (s->config.geom.h > 0))
          {
             screens = eina_list_append(screens, s);
          }
     }
   // remove overlapping screens - if a set of screens overlap, keep the
   // smallest/lowest res
   do
     {
        removed = EINA_FALSE;

        EINA_LIST_FOREACH(screens, l, s)
          {
             screens_rem = NULL;

             EINA_LIST_FOREACH(l->next, ll, s2)
               {
                  if (E_INTERSECTS(s->config.geom.x, s->config.geom.y,
                                   s->config.geom.w, s->config.geom.h,
                                   s2->config.geom.x, s2->config.geom.y,
                                   s2->config.geom.w, s2->config.geom.h))
                    {
                       if (!screens_rem)
                         screens_rem = eina_list_append(screens_rem, s);
                       screens_rem = eina_list_append(screens_rem, s2);
                    }
               }
             // we have intersecting screens - choose the lowest res one
             if (screens_rem)
               {
                  removed = EINA_TRUE;
                  // find the smallest screen (chosen one)
                  s_chosen = NULL;
                  EINA_LIST_FOREACH(screens_rem, ll, s2)
                    {
                       if (!s_chosen) s_chosen = s2;
                       else
                         {
                            if ((s_chosen->config.geom.w *
                                 s_chosen->config.geom.h) >
                                (s2->config.geom.w *
                                 s2->config.geom.h))
                              s_chosen = s2;
                         }
                    }
                  // remove all from screens but the chosen one
                  EINA_LIST_FREE(screens_rem, s2)
                    {
                       if (s2 != s_chosen)
                         screens = eina_list_remove_list(screens, l);
                    }
                  // break our list walk and try again
                  break;
               }
          }
     }
   while (removed);
   // sort screens by priority etc.
   screens = eina_list_sort(screens, 0, _screen_sort_cb);
   i = 0;
   EINA_LIST_FOREACH(screens, l, s)
     {
        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = i;
        screen->x = s->config.geom.x;
        screen->y = s->config.geom.y;
        screen->w = s->config.geom.w;
        screen->h = s->config.geom.h;
        if (s->id) screen->id = strdup(s->id);

        e_screens = eina_list_append(e_screens, screen);
        INF("E INIT: SCREEN: [%i][%i], %ix%i+%i+%i",
            i, i, screen->w, screen->h, screen->x, screen->y);
        i++;
     }
   eina_list_free(screens);
   // if we have NO screens at all (above - i will be 0) AND we have no
   // existing screens set up in xinerama - then just say root window size
   // is the entire screen. this should handle the case where you unplug ALL
   // screens from an existing setup (unplug external monitors and/or close
   // laptop lid), in which case as long as at least one screen is configured
   // in xinerama, it will be left-as is until next time we re-eval screen
   // setup and have at least one screen
   printf("e_output_screens_setup............... %i %p\n", i, all_screens);
   if ((i == 0) && (!all_screens))
     {
out:
        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = 0;
        screen->x = 0;
        screen->y = 0;
        if ((rw > 0) && (rh > 0))
          screen->w = rw, screen->h = rh;
        else
          ecore_evas_screen_geometry_get(e_comp->ee, NULL, NULL, &screen->w, &screen->h);
        e_screens = eina_list_append(e_screens, screen);
     }
   _escreens_set(e_screens);
}

E_API const Eina_List *
e_output_screens_get(void)
{
   return all_screens;
}

EINTERN E_Output_Screen *
e_output_screen_id_find(const char *id)
{
   E_Output_Screen *s;
   Eina_List *l;
   EINA_LIST_FOREACH(e_output->screens, l, s)
     {
        if (!strcmp(s->id, id)) return s;
     }
   return NULL;
}


E_API Eina_Bool
e_output_planes_prepare(E_Output_Screen * screen, E_Hwc_Mode mode, Eina_List* clist)
{
   if (!e_comp) return EINA_FALSE;

   e_comp->prepare_mode = mode;
   if (e_comp->prepare_ec_list)
     {
        eina_list_free(e_comp->prepare_ec_list);
        e_comp->prepare_ec_list = NULL;
     }
   e_comp->prepare_ec_list = eina_list_clone(clist);
   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_screen_apply(E_Output_Screen * screen)
{
   e_comp->hwc_mode = e_comp->prepare_mode;
   switch (e_comp->prepare_mode)
     {
      case E_HWC_MODE_NO_COMPOSITE:
      case E_HWC_MODE_HWC_COMPOSITE:
      case E_HWC_MODE_HWC_NO_COMPOSITE:
         return _hwc_set(screen, e_comp->prepare_mode, e_comp->prepare_ec_list);

      default :
         e_output_screen_clear(screen);
         break;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_output_screen_clear(E_Output_Screen * screen)
{
   Eina_List *l, *ll;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(screen->planes, EINA_FALSE);

   e_comp->hwc_mode = 0;

   EINA_LIST_FOREACH_SAFE(screen->planes, l, ll, ep)
     {
        if (ep->ec) e_client_redirected_set(ep->ec, 1);
        ep->ec = NULL;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_screen_need_change()
{
   E_Zone *zone;
   E_Output_Screen * screen;
   E_Plane *ep;
   int num_ov;
   Eina_List *l_p, *l_ov, *l, *ll;

   if (!e_comp) return EINA_FALSE;
   if (e_comp->hwc_mode != e_comp->prepare_mode)
     return EINA_FALSE;

   zone = eina_list_data_get(e_comp->zones);
   if (!zone) return EINA_FALSE;
   screen = zone->screen;
   if (!screen) return EINA_FALSE;
   l_p = screen->planes;
   if (!l_p) return EINA_FALSE;

   num_ov = eina_list_count(e_comp->prepare_ec_list);
   if ((num_ov > screen->plane_count) ||
       (num_ov < 1))
     return EINA_FALSE;

   l_ov = e_comp->prepare_ec_list;

   if ((e_comp->prepare_mode == E_HWC_MODE_COMPOSITE) ||
       (e_comp->prepare_mode == E_HWC_MODE_HWC_COMPOSITE))
     {
        ep = eina_list_data_get(l_p);
        if (ep) return EINA_FALSE;
        l_p = eina_list_next(l_p);
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(l_p, EINA_FALSE);
   EINA_LIST_FOREACH_SAFE(l_p, l, ll, ep)
     {
        E_Client *ec = NULL;

        if (!l_ov) break;
        ec = eina_list_data_get(l_ov);
        if(ec)
          {
             if (ep->ec != ec) return EINA_TRUE;
          }

        l_ov = eina_list_next(l_ov);
     }

   return EINA_FALSE;
}

E_API Eina_Bool
e_output_util_planes_print(void)
{
   Eina_List *l, *ll;
   E_Zone *zone;

   EINA_LIST_FOREACH_SAFE(e_comp->zones, l, ll, zone)
     {
        E_Output_Screen * screen = NULL;
        E_Plane *ep;
        E_Client *ec;

        if (!zone && !zone->screen) continue;
        screen = zone->screen;
        if (!screen) continue;
        if (!screen->planes) continue;

        EINA_LIST_FOREACH_SAFE(screen->planes, l, ll, ep)
          {
             ec = ep->ec;
             if (ec) INF("HWC:\t|---\t %s 0x%08x\n", ec->icccm.title, (unsigned int)ec->frame);
          }
     }

   return EINA_FALSE; // SHALL BE EINA_TRUE after hwc multi plane implementation
}
