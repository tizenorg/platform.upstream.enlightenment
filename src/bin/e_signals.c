/*
 * NOTE TO FreeBSD users. Install libexecinfo from
 * ports/devel/libexecinfo and add -lexecinfo to LDFLAGS
 * to add backtrace support.
 */
#include "e.h"

#ifdef HAVE_WL_DRM
# include <Ecore_Drm.h>
#endif

#ifdef HAVE_EXECINFO_H
# include <execinfo.h>
#endif

static volatile Eina_Bool _e_x_composite_shutdown_try = 0;

static void
_e_crash(void)
{
   if (e_comp->comp_type == E_PIXMAP_TYPE_WL)
     {
#ifdef HAVE_WL_DRM
        const Eina_List *list, *l, *ll;
        Ecore_Drm_Device *dev;

        if (!strstr(ecore_evas_engine_name_get(e_comp->ee), "drm")) return;
        list = ecore_drm_devices_get();
        EINA_LIST_FOREACH_SAFE(list, l, ll, dev)
          {
             ecore_drm_inputs_destroy(dev);
             ecore_drm_sprites_destroy(dev);
             ecore_drm_device_close(dev);
             ecore_drm_launcher_disconnect(dev);
             ecore_drm_device_free(dev);
          }

        ecore_drm_shutdown();
#endif
        return;
     }
}

/* a tricky little devil, requires e and it's libs to be built
 * with the -rdynamic flag to GCC for any sort of decent output.
 */
E_API void
e_sigseg_act(int x EINA_UNUSED, siginfo_t *info EINA_UNUSED, void *data EINA_UNUSED)
{
   _e_crash();
}

E_API void
e_sigill_act(int x EINA_UNUSED, siginfo_t *info EINA_UNUSED, void *data EINA_UNUSED)
{
   // In case of a SIGILL in Enlightenment, Enlightenment start will catch the SIGILL and continue,
   // because evas cpu detection use that behaviour. But if we get a SIGILL after that, we end up in
   // this sig handler. So E start remember the SIGILL, and we will commit suicide with a USR1, followed
   // by a SEGV.
   kill(getpid(), SIGUSR1);
   kill(getpid(), SIGSEGV);
   pause();
}

E_API void
e_sigfpe_act(int x EINA_UNUSED, siginfo_t *info EINA_UNUSED, void *data EINA_UNUSED)
{
   _e_crash();
}

E_API void
e_sigbus_act(int x EINA_UNUSED, siginfo_t *info EINA_UNUSED, void *data EINA_UNUSED)
{
   _e_crash();
}

E_API void
e_sigabrt_act(int x EINA_UNUSED, siginfo_t *info EINA_UNUSED, void *data EINA_UNUSED)
{
   _e_crash();
}
