#include "e.h"


/////////////////////////////////////////////////////////////////////////
static void                    _do_apply(void);
static void                    _info_free(E_Comp_Screen *r);
static void                    _screen_config_eval(void);
static void                    _screen_config_maxsize(void);

/////////////////////////////////////////////////////////////////////////

E_API E_Comp_Screen   *e_comp_screen = NULL;

E_API int              E_EVENT_SCREEN_CHANGE = 0;

static Eina_List *all_screens = NULL; // e_screen list

/////////////////////////////////////////////////////////////////////////
EINTERN Eina_Bool
e_output_init(void)
{
   if (!E_EVENT_SCREEN_CHANGE) E_EVENT_SCREEN_CHANGE = ecore_event_type_new();

   e_comp_screen = e_comp_screen_init_outputs();

   _do_apply();

   ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);

   return EINA_TRUE;
}

EINTERN int
e_output_shutdown(void)
{
   // free up screen info
   _info_free(e_comp_screen);
   e_comp_screen = NULL;

   return 1;
}

/////////////////////////////////////////////////////////////////////////

static void
_do_apply(void)
{
   // take current e_output config and apply it to the driver
   _screen_config_maxsize();
   printf("OUTPUT: eval config...\n");
   _screen_config_eval();
   printf("OUTPUT: really apply config...\n");
   e_comp_screen_apply();
   printf("OUTPUT: done config...\n");
}

static void
_info_free(E_Comp_Screen *r)
{
   E_Output *eout;
   E_Output_Mode *m;
   E_Plane *ep;

   if (!r) return;
   // free up our output screen data
   EINA_LIST_FREE(r->outputs, eout)
     {
        free(eout->id);
        free(eout->info.screen);
        free(eout->info.name);
        free(eout->info.edid);
        EINA_LIST_FREE(eout->info.modes, m) free(m);
        EINA_LIST_FREE(eout->planes, ep) e_plane_free(ep);
        free(eout);
     }
   free(r);
}

static void
_screen_config_eval(void)
{
   Eina_List *l;
   E_Output *eout;
   int minx, miny, maxx, maxy;

   minx = 65535;
   miny = 65535;
   maxx = -65536;
   maxy = -65536;

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if (!eout->config.enabled) continue;
        if (eout->config.geom.x < minx) minx = eout->config.geom.x;
        if (eout->config.geom.y < miny) miny = eout->config.geom.y;
        if ((eout->config.geom.x + eout->config.geom.w) > maxx)
          maxx = eout->config.geom.x + eout->config.geom.w;
        if ((eout->config.geom.y + eout->config.geom.h) > maxy)
          maxy = eout->config.geom.y + eout->config.geom.h;
        printf("OUTPUT: s: '%s' @ %i %i - %ix%i\n",
               eout->info.name,
               eout->config.geom.x, eout->config.geom.y,
               eout->config.geom.w, eout->config.geom.h);
     }
   printf("OUTPUT:--- %i %i -> %i %i\n", minx, miny, maxx, maxy);
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        eout->config.geom.x -= minx;
        eout->config.geom.y -= miny;
     }
   e_comp_screen->w = maxx - minx;
   e_comp_screen->h = maxy - miny;
}

static void
_screen_config_maxsize(void)
{
   Eina_List *l;
   E_Output *eout;
   int maxx, maxy;

   maxx = -65536;
   maxy = -65536;
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if (!eout->config.enabled) continue;
        if ((eout->config.geom.x + eout->config.geom.w) > maxx)
          maxx = eout->config.geom.x + eout->config.geom.w;
        if ((eout->config.geom.y + eout->config.geom.h) > maxy)
          maxy = eout->config.geom.y + eout->config.geom.h;
        printf("OUTPUT: '%s': %i %i %ix%i\n",
               eout->info.name,
               eout->config.geom.x, eout->config.geom.y,
               eout->config.geom.w, eout->config.geom.h);
     }
   printf("OUTPUT: result max: %ix%i\n", maxx, maxy);
   e_comp_screen->w = maxx;
   e_comp_screen->h = maxy;
}

static int
_screen_sort_cb(const void *data1, const void *data2)
{
   const E_Output *s1 = data1, *s2 = data2;
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

EINTERN void
e_output_screens_setup(int rw, int rh)
{
   int i;
   E_Screen *screen;
   Eina_List *outputs = NULL, *outputs_rem;
   Eina_List *e_screens = NULL;
   Eina_List *l, *ll;
   E_Output *eout, *s2, *s_chosen;
   Eina_Bool removed;

   if ((!e_comp_screen) || (!e_comp_screen->outputs)) goto out;
   // put screens in tmp list
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if ((eout->config.enabled) &&
            (eout->config.geom.w > 0) &&
            (eout->config.geom.h > 0))
          {
             outputs = eina_list_append(outputs, eout);
          }
     }
   // remove overlapping screens - if a set of screens overlap, keep the
   // smallest/lowest res
   do
     {
        removed = EINA_FALSE;

        EINA_LIST_FOREACH(outputs, l, eout)
          {
             outputs_rem = NULL;

             EINA_LIST_FOREACH(l->next, ll, s2)
               {
                  if (E_INTERSECTS(eout->config.geom.x, eout->config.geom.y,
                                   eout->config.geom.w, eout->config.geom.h,
                                   s2->config.geom.x, s2->config.geom.y,
                                   s2->config.geom.w, s2->config.geom.h))
                    {
                       if (!outputs_rem)
                         outputs_rem = eina_list_append(outputs_rem, eout);
                       outputs_rem = eina_list_append(outputs_rem, s2);
                    }
               }
             // we have intersecting screens - choose the lowest res one
             if (outputs_rem)
               {
                  removed = EINA_TRUE;
                  // find the smallest screen (chosen one)
                  s_chosen = NULL;
                  EINA_LIST_FOREACH(outputs_rem, ll, s2)
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
                  EINA_LIST_FREE(outputs_rem, s2)
                    {
                       if (s2 != s_chosen)
                         outputs = eina_list_remove_list(outputs, l);
                    }
                  // break our list walk and try again
                  break;
               }
          }
     }
   while (removed);
   // sort screens by priority etc.
   outputs = eina_list_sort(outputs, 0, _screen_sort_cb);
   i = 0;
   EINA_LIST_FOREACH(outputs, l, eout)
     {
        screen = E_NEW(E_Screen, 1);
        screen->escreen = screen->screen = i;
        screen->x = eout->config.geom.x;
        screen->y = eout->config.geom.y;
        screen->w = eout->config.geom.w;
        screen->h = eout->config.geom.h;
        if (eout->id) screen->id = strdup(eout->id);

        e_screens = eina_list_append(e_screens, screen);
        INF("E INIT: SCREEN: [%i][%i], %ix%i+%i+%i",
            i, i, screen->w, screen->h, screen->x, screen->y);
        i++;
     }
   eina_list_free(outputs);
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

EINTERN const Eina_List *
e_output_screens_get(void)
{
   return all_screens;
}

EINTERN E_Output *
e_output_find(const char *id)
{
   E_Output *eout;
   Eina_List *l;
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if (!strcmp(eout->id, id)) return eout;
     }
   return NULL;
}

E_API const Eina_List *
e_output_planes_get(E_Output *eout)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, NULL);

   return eout->planes;
}

E_API void
e_output_util_planes_print(void)
{
   Eina_List *l, *ll, *p_l;
   E_Output * eout = NULL;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, eout)
     {
        E_Plane *ep;
        E_Client *ec;

        if (!eout && !eout->planes) continue;

        fprintf(stderr, "HWC in %s .. \n", eout->id);
        fprintf(stderr, "HWC \tzPos \t on_plane \t\t\t\t on_prepare \t \n");

        EINA_LIST_REVERSE_FOREACH(eout->planes, p_l, ep)
          {
             ec = ep->ec;
             if (ec) fprintf(stderr, "HWC \t[%d]%s\t %s (0x%08x)",
                             ep->zpos,
                             ep->is_primary ? "--" : "  ",
                             ec->icccm.title, (unsigned int)ec->frame);

             ec = ep->prepare_ec;
             if (ec) fprintf(stderr, "\t\t\t %s (0x%08x)",
                             ec->icccm.title, (unsigned int)ec->frame);
             fputc('\n', stderr);
          }
        fputc('\n', stderr);
     }
}
