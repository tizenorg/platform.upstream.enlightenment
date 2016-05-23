#include "e.h"


/////////////////////////////////////////////////////////////////////////
static void                    _do_apply(void);
static void                    _info_free(E_Output *r);
static void                    _screen_config_eval(void);
static void                    _screen_config_maxsize(void);

/////////////////////////////////////////////////////////////////////////

E_API E_Output        *e_output = NULL;

E_API int              E_EVENT_SCREEN_CHANGE = 0;

static Eina_List *all_screens = NULL; // e_screen list

/////////////////////////////////////////////////////////////////////////
EINTERN Eina_Bool
e_output_init(void)
{
   if (!E_EVENT_SCREEN_CHANGE) E_EVENT_SCREEN_CHANGE = ecore_event_type_new();

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

E_API void
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
e_output_planes_clear(E_Output_Screen * screen)
{
   Eina_List *l, *ll;
   E_Plane *ep;
   INF("HWC : %s\n",__FUNCTION__);

   EINA_SAFETY_ON_NULL_RETURN_VAL(screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(screen->planes, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(screen->planes, l, ll, ep)
     {
        ep->ec = NULL;
     }
   return EINA_TRUE;
}

E_API Eina_Bool
e_output_planes_set(E_Output_Screen * screen, E_Hwc_Mode mode, Eina_List* clist)
{
   Eina_List *l_p, *l_ec;
   Eina_List *l, *ll;
   E_Plane *ep;
   int num_c;
   INF("HWC : %s\n",__FUNCTION__);

   num_c = eina_list_count(clist);

   EINA_SAFETY_ON_NULL_RETURN_VAL(screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(screen->planes, EINA_FALSE);
   if ((num_c > screen->plane_count) ||
       (num_c < 1))
     return EINA_FALSE;

   l_p = screen->planes;
   l_ec = clist;
   if ((mode == E_HWC_MODE_COMPOSITE) ||
       (mode == E_HWC_MODE_HWC_COMPOSITE))
     {
        ep = eina_list_data_get(l_p);
        if (ep) ep->ec = NULL; // 1st plane is assigned for e_comp->evas
        l_p = eina_list_next(l_p);
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(l_p, EINA_FALSE);
   EINA_LIST_FOREACH_SAFE(l_p, l, ll, ep)
     {
        E_Client *ec = NULL;

        if (!l_ec) break;
        ec = eina_list_data_get(l_ec);

        if(ec)
          {
             ep->ec = ec;
          }
        l_ec = eina_list_next(l_ec);
     }

   return EINA_TRUE;
}

E_API Eina_Bool
e_output_update(E_Output_Screen * screen)
{
   Eina_List *l, *ll;
   E_Plane *ep;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN_VAL(screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(screen->planes, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(screen->planes, l, ll, ep)
     {
        ec = ep->ec;
        if (ec) INF("HWC:\t|---\t %s 0x%08x\n", ec->icccm.title, (unsigned int)ec->frame);
     }

   // TODO: hwc mode change
   return EINA_FALSE; // SHALL BE EINA_TRUE after hwc multi plane implementation
}
