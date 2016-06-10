#include "e.h"

static char *
_e_output_model_get(Ecore_Drm_Output *output)
{
   const char *model;

   model = ecore_drm_output_model_get(output);
   if (!model) return NULL;

   return strdup(model);
}


EINTERN E_Output *
e_output_new(Ecore_Drm_Output *output)
{
   E_Output *eout = NULL;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   eout = E_NEW(E_Output, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, NULL);

   eout->info.name = ecore_drm_output_name_get(output);
   printf("E_OUTPUT: .... out %s\n", eout->info.name);

    // TODO: get proper value using libtdm
   eout->plane_count = 1;
   printf("COMP TDM: planes %i\n", eout->plane_count);
   for (i = 0; i < eout->plane_count; i++)
     {
        printf("COMP TDM: added plane %i\n", i);
        Eina_Bool pri = EINA_FALSE;
        E_Plane *ep = NULL;
        // TODO: primary layer condition (0 is temp condition)
        if (i == 0) pri = EINA_TRUE;
        ep = e_plane_new(eout, i, pri);
        // TODO: fb target condition (0 is temp condition)
        if (i == 0) e_plane_fb_set(ep, EINA_TRUE);
     }

   eout->output = output;

   return eout;
}

EINTERN void
e_output_del(E_Output *eout)
{
   E_Plane *ep;
   E_Output_Mode *m;

   if (!eout) return;

   free(eout->id);
   free(eout->info.screen);
   free(eout->info.name);
   free(eout->info.edid);
   EINA_LIST_FREE(eout->info.modes, m) free(m);

   EINA_LIST_FREE(eout->planes, ep) e_plane_free(ep);
   free(eout);
}

EINTERN Eina_Bool
e_output_update(E_Output *eout)
{
   Eina_List *m = NULL;
   Eina_List *modes = NULL;
   Eina_Bool connected;
   E_Comp_Screen *e_comp_screen;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);

   e_comp_screen = e_comp->e_comp_screen;

   connected = ecore_drm_output_connected_get(eout->output);
   if (connected)
     {
        /* disconnect --> connect */
        if (connected != eout->info.connected)
          {
             int len = 0;
             char *id;
             char *screen;
             char *edid;
             int phy_w, phy_h;
             Ecore_Drm_Output_Mode *omode;

             screen = _e_output_model_get(eout->output);
             edid = ecore_drm_output_edid_get(eout->output);
             if (eout->info.edid)
               id = malloc(strlen(eout->info.name) + 1 + strlen(eout->info.edid) + 1);
             else
               id = malloc(strlen(eout->info.name) + 1 + 1);
             if (!id)
               {
                  free(edid);
                  return EINA_FALSE;
               }
             len = strlen(eout->info.name);
             strncpy(id, eout->info.name, len + 1);
             strncat(id, "/", 1);
             if (eout->info.edid) strncat(id, edid, strlen(edid));

             printf("E_OUTPUT: ...... screen: %s\n", id);

             ecore_drm_output_physical_size_get(eout->output, &phy_w, &phy_h);

             EINA_LIST_FOREACH(ecore_drm_output_modes_get(eout->output), m, omode)
               {
                  E_Output_Mode *rmode;

                  rmode = malloc(sizeof(E_Output_Mode));
                  if (!rmode) continue;

                  rmode->w = omode->width;
                  rmode->h = omode->height;
                  rmode->refresh = omode->refresh;
                  rmode->preferred = (omode->flags & DRM_MODE_TYPE_PREFERRED);

                  modes = eina_list_append(modes, rmode);
               }

             free(eout->id);
             free(eout->info.screen);
             free(eout->info.edid);
             EINA_LIST_FREE(eout->info.modes, m) free(m);

             eout->id = id;
             eout->info.screen = screen;
             eout->info.edid = edid;
             eout->info.modes = modes;
             eout->info.size.w = phy_w;
             eout->info.size.h = phy_h;

             eout->info.connected = EINA_TRUE;

             printf("E_OUTPUT: connected.. id: %s\n", eout->id);
          }

        /* check the crtc setting */
        const Eina_List *l;
        int i;
        unsigned int refresh;
        Ecore_Drm_Device *dev;

        EINA_LIST_FOREACH(e_comp_screen->devices, l, dev)
          {
             if (ecore_drm_output_primary_get(dev) == eout->output)
               eout->config.priority = 100;

             for (i = 0; i < dev->crtc_count; i++)
               {
                  if (dev->crtcs[i] == ecore_drm_output_crtc_id_get(eout->output))
                    {
                       ecore_drm_output_position_get(eout->output, &eout->config.geom.x,
                                                     &eout->config.geom.y);
                       ecore_drm_output_crtc_size_get(eout->output, &eout->config.geom.w,
                                                      &eout->config.geom.h);
                       ecore_drm_output_current_resolution_get(eout->output,
                                                               &eout->config.mode.w,
                                                               &eout->config.mode.h,
                                                               &refresh);
                       eout->config.mode.refresh = refresh;
                       eout->config.enabled =
                           ((eout->config.mode.w != 0) && (eout->config.mode.h != 0));

                       printf("E_OUTPUT: '%s' %i %i %ix%i\n", eout->info.name,
                              eout->config.geom.x, eout->config.geom.y,
                              eout->config.geom.w, eout->config.geom.h);
                       break;
                    }
               }
          }
     }
   else
     {
        eout->info.connected = EINA_FALSE;

        /* reset eout info */
        free(eout->id);
        free(eout->info.screen);
        free(eout->info.edid);
        EINA_LIST_FREE(eout->info.modes, m) free(m);

        eout->id = malloc(strlen(eout->info.name) + 1 + 1);
        eout->info.size.w = 0;
        eout->info.size.h = 0;

        printf("E_OUTPUT: disconnected.. id: %s\n", eout->id);
     }

   return EINA_TRUE;
}

E_API E_Output *
e_output_find(const char *id)
{
   E_Output *eout;
   E_Comp_Screen *e_comp_screen;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, NULL);

   e_comp_screen = e_comp->e_comp_screen;

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
   E_Comp_Screen *e_comp_screen = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   e_comp_screen = e_comp->e_comp_screen;

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
