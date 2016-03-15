#include "e.h"
#ifdef __linux__
# include <sys/prctl.h>
#endif

#define MAX_LEVEL 80

#define TS_DO
#ifdef TS_DO
# define TS(x)                                                    \
  {                                                               \
     t1 = ecore_time_unix_get();                                  \
     printf("ESTART: %1.5f [%1.5f] - %s\n", t1 - t0, t1 - t2, x); \
     t2 = t1;                                                     \
  }
static double t0, t1, t2;
#else
# define TS(x)
#endif
/*
 * i need to make more use of these when i'm baffled as to when something is
 * up. other hooks:
 *
 *      void *(*__malloc_hook)(size_t size, const void *caller);
 *
 *      void *(*__realloc_hook)(void *ptr, size_t size, const void *caller);
 *
 *      void *(*__memalign_hook)(size_t alignment, size_t size,
 *                               const void *caller);
 *
 *      void (*__free_hook)(void *ptr, const void *caller);
 *
 *      void (*__malloc_initialize_hook)(void);
 *
 *      void (*__after_morecore_hook)(void);
 *

   static void my_init_hook(void);
   static void my_free_hook(void *p, const void *caller);

   static void (*old_free_hook)(void *ptr, const void *caller) = NULL;
   void (*__free_hook)(void *ptr, const void *caller);

   void (*__malloc_initialize_hook) (void) = my_init_hook;
   static void
   my_init_hook(void)
   {
   old_free_hook = __free_hook;
   __free_hook = my_free_hook;
   }

   //void *magicfree = NULL;

   static void
   my_free_hook(void *p, const void *caller)
   {
   __free_hook = old_free_hook;
   //   if ((p) && (p == magicfree))
   //     {
   //	printf("CAUGHT!!!!! %p ...\n", p);
   //	abort();
   //     }
   free(p);
   __free_hook = my_free_hook;
   }
 */

/* local function prototypes */
static void      _e_main_shutdown(int errcode);
static void      _e_main_shutdown_push(int (*func)(void));
static void      _e_main_parse_arguments(int argc, char **argv);
static Eina_Bool _e_main_cb_signal_exit(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED);
static Eina_Bool _e_main_cb_signal_hup(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED);
static Eina_Bool _e_main_cb_signal_user(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev);
static int       _e_main_dirs_init(void);
static int       _e_main_dirs_shutdown(void);
static int       _e_main_path_init(void);
static int       _e_main_path_shutdown(void);
static int       _e_main_screens_init(void);
static int       _e_main_screens_shutdown(void);
static void      _e_main_desk_save(void);
static void      _e_main_desk_restore(void);
static void      _e_main_modules_load(Eina_Bool safe_mode);
static Eina_Bool _e_main_cb_idle_before(void *data EINA_UNUSED);
static Eina_Bool _e_main_cb_idle_after(void *data EINA_UNUSED);

/* local variables */
static Eina_Bool really_know = EINA_FALSE;
static Eina_Bool inloop = EINA_FALSE;
static jmp_buf x_fatal_buff;

static int _e_main_lvl = 0;
static int(*_e_main_shutdown_func[MAX_LEVEL]) (void);

static Ecore_Idle_Enterer *_idle_before = NULL;
static Ecore_Idle_Enterer *_idle_after = NULL;

static Ecore_Event_Handler *mod_init_end = NULL;

/* external variables */
E_API Eina_Bool e_precache_end = EINA_FALSE;
E_API Eina_Bool x_fatal = EINA_FALSE;
E_API Eina_Bool good = EINA_FALSE;
E_API Eina_Bool evil = EINA_FALSE;
E_API Eina_Bool starting = EINA_TRUE;
E_API Eina_Bool stopping = EINA_FALSE;
E_API Eina_Bool restart = EINA_FALSE;
E_API Eina_Bool e_nopause = EINA_FALSE;
EINTERN const char *e_first_frame = NULL;
EINTERN double e_first_frame_start_time = -1;

static Eina_Bool
_xdg_check_str(const char *env, const char *str)
{
   const char *p;
   size_t len;

   len = strlen(str);
   for (p = strstr(env, str); p; p++, p = strstr(p, str))
     {
        if ((!p[len]) || (p[len] == ':')) return EINA_TRUE;
     }
   return EINA_FALSE;
}

static void
_xdg_data_dirs_augment(void)
{
   const char *s;
   const char *p = e_prefix_get();
   char newpath[4096], buf[4096];

   if (!p) return;

   s = getenv("XDG_DATA_DIRS");
   if (s)
     {
        Eina_Bool pfxdata, pfx;

        pfxdata = !_xdg_check_str(s, e_prefix_data_get());
        snprintf(newpath, sizeof(newpath), "%s/share", p);
        pfx = !_xdg_check_str(s, newpath);
        if (pfxdata || pfx)
          {
             snprintf(buf, sizeof(buf), "%s%s%s%s%s",
               pfxdata ? e_prefix_data_get() : "",
               pfxdata ? ":" : "",
               pfx ? newpath : "",
               pfx ? ":" : "",
               s);
             e_util_env_set("XDG_DATA_DIRS", buf);
          }
     }
   else
     {
        snprintf(buf, sizeof(buf), "%s:%s/share:/usr/local/share:/usr/share", e_prefix_data_get(), p);
        e_util_env_set("XDG_DATA_DIRS", buf);
     }

   s = getenv("XDG_CONFIG_DIRS");
   snprintf(newpath, sizeof(newpath), "%s/etc/xdg", p);
   if (s)
     {
        if (!_xdg_check_str(s, newpath))
          {
             snprintf(buf, sizeof(buf), "%s:%s", newpath, s);
             e_util_env_set("XDG_CONFIG_DIRS", buf);
          }
     }
   else
     {
        snprintf(buf, sizeof(buf), "%s:/etc/xdg", newpath);
        e_util_env_set("XDG_CONFIG_DIRS", buf);
     }

   if (!getenv("XDG_RUNTIME_DIR"))
     {
        const char *dir;

        snprintf(buf, sizeof(buf), "/tmp/xdg-XXXXXX");
        dir = mkdtemp(buf);
        if (!dir) dir = "/tmp";
        else
          {
             e_util_env_set("XDG_RUNTIME_DIR", dir);
             snprintf(buf, sizeof(buf), "%s/.e-deleteme", dir);
             ecore_file_mkdir(buf);
          }
     }

   /* set menu prefix so we get our e menu */
   if (!getenv("XDG_MENU_PREFIX"))
     {
        e_util_env_set("XDG_MENU_PREFIX", "e-");
     }
}

static Eina_Bool
_e_main_subsystem_defer(void *data EINA_UNUSED)
{
   int argc;
   char **argv;

   TRACE_DS_BEGIN(MAIN:SUBSYSTEMS DEFER);

   ecore_app_args_get(&argc, &argv);

   /* try to init delayed subsystems */

   TRACE_DS_BEGIN(MAIN:DEFFERED EFL INIT);

   TS("[DEFERRED] Edje Init");
   if (!edje_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Edje!\n"));
        TRACE_DS_END();
        _e_main_shutdown(-1);
     }
   TS("[DEFERRED] Edje Init Done");
   _e_main_shutdown_push(edje_shutdown);

   TRACE_DS_END();
   TRACE_DS_BEGIN(MAIN:DEFERRED INTERNAL SUBSYSTEMS INIT);

   TS("[DEFERRED] Screens Init: win");
   if (!e_win_init())
     {
        e_error_message_show(_("Enlightenment cannot setup elementary trap!\n"));
        TRACE_DS_END();
        _e_main_shutdown(-1);
     }
   TS("[DEFERRED] Screens Init: win Done");

   TS("[DEFERRED] E_Dnd Init");
   if (!e_dnd_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its dnd system.\n"));
        _e_main_shutdown(-1);
     }
   TS("[DEFERRED] E_Dnd Init Done");
   _e_main_shutdown_push(e_dnd_shutdown);

   TS("[DEFERRED] E_Pointer Init");
   if (!e_pointer_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its pointer system.\n"));
        TRACE_DS_END();
        _e_main_shutdown(-1);
     }

   TS("[DEFERRED] E_Pointer Init Done");
   _e_main_shutdown_push(e_pointer_shutdown);

   TS("[DEFERRED] E_Scale Init");
   if (!e_scale_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its scale system.\n"));
        TRACE_DS_END();
        _e_main_shutdown(-1);
     }
   TS("[DEFERRED E_Scale Init Done");
   _e_main_shutdown_push(e_scale_shutdown);

   TS("[DEFERRED] E_Test_Helper Init");
   e_test_helper_init();
   _e_main_shutdown_push(e_test_helper_shutdown);
   TS("[DEFERRED] E_Test_Helper Done");

   TS("[DEFERRED] E_INFO_SERVER Init");
   e_info_server_init();
   _e_main_shutdown_push(e_info_server_shutdown);
   TS("[DEFERRED] E_INFO_SERVER Done");

   TRACE_DS_END();
   TRACE_DS_BEGIN(MAIN:DEFERRED COMP JOB);

   /* try to do deferred job of any subsystems*/
   TS("[DEFERRED] Compositor's deferred job");
   e_comp_deferred_job();
   TS("[DEFERRED] Compositor's deferred job Done");

   TRACE_DS_END();
   TRACE_DS_BEGIN(MAIN:DEFERRED MODULE JOB);

   TS("[DEFERRED] E_Module's deferred job");
   e_module_deferred_job();
   TS("[DEFERRED] E_Module's deferred job Done");

   TRACE_DS_END();
   TRACE_DS_END();

   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_e_main_deferred_job_schedule(void *d EINA_UNUSED, int type EINA_UNUSED, void *ev EINA_UNUSED)
{
   ecore_idler_add(_e_main_subsystem_defer, NULL);
   return ECORE_CALLBACK_DONE;
}

/* externally accessible functions */
int
main(int argc, char **argv)
{
   Eina_Bool safe_mode = EINA_FALSE;
   double t = 0.0, tstart = 0.0;
   char *s = NULL, buff[32];
   struct sigaction action;

#ifdef __linux__
# ifdef PR_SET_PTRACER
#  ifdef PR_SET_PTRACER_ANY
   prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
#  endif
# endif
#endif
#ifdef TS_DO
   t0 = t1 = t2 = ecore_time_unix_get();
   printf("ESTART(main) %1.5f\n", t0);
#endif
   TRACE_DS_BEGIN(MAIN:BEGIN STARTUP);
   TS("Begin Startup");

   /* trap deadly bug signals and allow some form of sane recovery */
   /* or ability to gdb attach and debug at this point - better than your */
   /* wm/desktop vanishing and not knowing what happened */

   /* don't install SIGBUS handler */
   /* Wayland shm sets up a sigbus handler for catching invalid shm region */
   /* access. If we setup our sigbus handler here, then the wl-shm sigbus */
   /* handler will not function properly */
   if (!getenv("NOTIFY_SOCKET"))
     {
        TS("Signal Trap");
        action.sa_sigaction = e_sigseg_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, NULL);

        action.sa_sigaction = e_sigill_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGILL, &action, NULL);

        action.sa_sigaction = e_sigfpe_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGFPE, &action, NULL);

        action.sa_sigaction = e_sigabrt_act;
        action.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGABRT, &action, NULL);
        TS("Signal Trap Done");
     }

   t = ecore_time_unix_get();
   s = getenv("E_START_TIME");
   if ((s) && (!getenv("E_RESTART_OK")))
     {
        tstart = atof(s);
        if ((t - tstart) < 5.0) safe_mode = EINA_TRUE;
     }
   tstart = t;
   snprintf(buff, sizeof(buff), "%1.1f", tstart);
   e_util_env_set("E_START_TIME", buff);

   if (getenv("E_START_MTRACK"))
     e_util_env_set("MTRACK", NULL);
   TS("Eina Init");
   if (!eina_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Eina!\n"));
        _e_main_shutdown(-1);
     }
   TS("Eina Init Done");
   _e_main_shutdown_push(eina_shutdown);

#ifdef OBJECT_HASH_CHECK
   TS("E_Object Hash Init");
   e_object_hash_init();
   TS("E_Object Hash Init Done");
#endif

   TS("E_Log Init");
   if (!e_log_init())
     {
        e_error_message_show(_("Enlightenment could not create a logging domain!\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Log Init Done");
   _e_main_shutdown_push(e_log_shutdown);

   TS("Determine Prefix");
   if (!e_prefix_determine(argv[0]))
     {
        fprintf(stderr,
                "ERROR: Enlightenment cannot determine it's installed\n"
                "       prefix from the system or argv[0].\n"
                "       This is because it is not on Linux AND has been\n"
                "       executed strangely. This is unusual.\n");
     }
   TS("Determine Prefix Done");

   /* for debugging by redirecting stdout of e to a log file to tail */
   setvbuf(stdout, NULL, _IONBF, 0);

   TS("Parse Arguments");
   _e_main_parse_arguments(argc, argv);
   TS("Parse Arguments Done");

   /*** Initialize Core EFL Libraries We Need ***/

   TS("Eet Init");
   if (!eet_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Eet!\n"));
        _e_main_shutdown(-1);
     }
   TS("Eet Init Done");
   _e_main_shutdown_push(eet_shutdown);

   /* Allow ecore to not load system modules.
    * Without it ecore_init will block until dbus authentication
    * and registration are complete.
    */
   ecore_app_no_system_modules();

   TS("Ecore Init");
   if (!ecore_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore!\n"));
        _e_main_shutdown(-1);
     }
   TS("Ecore Init Done");
   _e_main_shutdown_push(ecore_shutdown);

   e_first_frame = getenv("E_FIRST_FRAME");
   if (e_first_frame && e_first_frame[0])
     e_first_frame_start_time = ecore_time_get();
   else
     e_first_frame = NULL;

   TS("EIO Init");
   if (!eio_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize EIO!\n"));
        _e_main_shutdown(-1);
     }
   TS("EIO Init Done");
   _e_main_shutdown_push(eio_shutdown);

   ecore_app_args_set(argc, (const char **)argv);

   TS("Ecore Event Handlers");
   if (!ecore_event_handler_add(ECORE_EVENT_SIGNAL_EXIT,
                                _e_main_cb_signal_exit, NULL))
     {
        e_error_message_show(_("Enlightenment cannot set up an exit signal handler.\n"
                               "Perhaps you are out of memory?"));
        _e_main_shutdown(-1);
     }
   if (!ecore_event_handler_add(ECORE_EVENT_SIGNAL_HUP,
                                _e_main_cb_signal_hup, NULL))
     {
        e_error_message_show(_("Enlightenment cannot set up a HUP signal handler.\n"
                               "Perhaps you are out of memory?"));
        _e_main_shutdown(-1);
     }
   if (!ecore_event_handler_add(ECORE_EVENT_SIGNAL_USER,
                                _e_main_cb_signal_user, NULL))
     {
        e_error_message_show(_("Enlightenment cannot set up a USER signal handler.\n"
                               "Perhaps you are out of memory?"));
        _e_main_shutdown(-1);
     }
   TS("Ecore Event Handlers Done");

   TS("Ecore_File Init");
   if (!ecore_file_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore_File!\n"));
        _e_main_shutdown(-1);
     }
   TS("Ecore_File Init Done");
   _e_main_shutdown_push(ecore_file_shutdown);

   _idle_before = ecore_idle_enterer_before_add(_e_main_cb_idle_before, NULL);

   TS("XDG_DATA_DIRS Init");
   _xdg_data_dirs_augment();
   TS("XDG_DATA_DIRS Init Done");

   TS("Ecore_Evas Init");
   if (!ecore_evas_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize Ecore_Evas!\n"));
        _e_main_shutdown(-1);
     }
   TS("Ecore_Evas Init Done");

   /* e doesn't sync to compositor - it should be one */
   ecore_evas_app_comp_sync_set(0);


   /*** Initialize E Subsystems We Need ***/

   TS("E Directories Init");
   /* setup directories we will be using for configurations storage etc. */
   if (!_e_main_dirs_init())
     {
        e_error_message_show(_("Enlightenment cannot create directories in your home directory.\n"
                               "Perhaps you have no home directory or the disk is full?"));
        _e_main_shutdown(-1);
     }
   TS("E Directories Init Done");
   _e_main_shutdown_push(_e_main_dirs_shutdown);

   TS("E_Config Init");
   if (!e_config_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its config system.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Config Init Done");
   _e_main_shutdown_push(e_config_shutdown);

   TS("E_Env Init");
   if (!e_env_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its environment.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Env Init Done");
   _e_main_shutdown_push(e_env_shutdown);

   ecore_exe_run_priority_set(e_config->priority);

   TS("E Paths Init");
   if (!_e_main_path_init())
     {
        e_error_message_show(_("Enlightenment cannot set up paths for finding files.\n"
                               "Perhaps you are out of memory?"));
        _e_main_shutdown(-1);
     }
   TS("E Paths Init Done");
   _e_main_shutdown_push(_e_main_path_shutdown);

   ecore_animator_frametime_set(1.0 / e_config->framerate);

   TS("E_Theme Init");
   if (!e_theme_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its theme system.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Theme Init Done");
   _e_main_shutdown_push(e_theme_shutdown);

   e_screensaver_preinit();

   TS("E_Actions Init");
   if (!e_actions_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its actions system.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Actions Init Done");
   _e_main_shutdown_push(e_actions_shutdown);

   /* these just add event handlers and can't fail
    * timestamping them is dumb.
    */
   e_zone_init();
   e_desk_init();

   TS("E_Screensaver Init");
   if (!e_screensaver_init())
     {
        e_error_message_show(_("Enlightenment cannot configure the X screensaver.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Screensaver Init Done");
   _e_main_shutdown_push(e_screensaver_shutdown);

   TRACE_DS_BEGIN(MAIN:SCREEN INIT);
   TS("Screens Init");
   if (!_e_main_screens_init())
     {
        e_error_message_show(_("Enlightenment set up window management for all the screens on your system\n"
                               "failed. Perhaps another window manager is running?\n"));
        _e_main_shutdown(-1);
     }
   TS("Screens Init Done");
   _e_main_shutdown_push(_e_main_screens_shutdown);
   TRACE_DS_END();

   TS("E_Comp Freeze");
   e_comp_all_freeze();
   TS("E_Comp Freeze Done");

   TS("E_Grabinput Init");
   if (!e_grabinput_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its grab input handling system.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Grabinput Init Done");
   _e_main_shutdown_push(e_grabinput_shutdown);

   ecore_event_handler_add(E_EVENT_MODULE_INIT_END, _e_main_deferred_job_schedule, NULL);

   TS("E_Module Init");
   if (!e_module_init())
     {
        e_error_message_show(_("Enlightenment cannot set up its module system.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Module Init Done");
   _e_main_shutdown_push(e_module_shutdown);

   TS("E_Remember Init");
   if (!e_remember_init())
     {
        e_error_message_show(_("Enlightenment cannot setup remember settings.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Remember Init Done");
   _e_main_shutdown_push(e_remember_shutdown);

   TS("E_Mouse Init");
   if (!e_mouse_update())
     {
        e_error_message_show(_("Enlightenment cannot configure the mouse settings.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Mouse Init Done");

   TS("E_Icon Init");
   if (!e_icon_init())
     {
        e_error_message_show(_("Enlightenment cannot initialize the Icon Cache system.\n"));
        _e_main_shutdown(-1);
     }
   TS("E_Icon Init Done");
   _e_main_shutdown_push(e_icon_shutdown);

   TS("Load Modules");
   _e_main_modules_load(safe_mode);
   TS("Load Modules Done");

   TS("E_Comp Thaw");
   e_comp_all_thaw();
   TS("E_Comp Thaw Done");

   _idle_after = ecore_idle_enterer_add(_e_main_cb_idle_after, NULL);

   starting = EINA_FALSE;
   inloop = EINA_TRUE;

   e_util_env_set("E_RESTART", "1");

   TS("MAIN LOOP AT LAST");

   TRACE_DS_END();
   if (!setjmp(x_fatal_buff))
     ecore_main_loop_begin();
   else
     CRI("FATAL: X Died. Connection gone. Abbreviated Shutdown\n");

   inloop = EINA_FALSE;
   stopping = EINA_TRUE;

   _e_main_desk_save();
   e_remember_internal_save();
   e_comp_internal_save();

   _e_main_shutdown(0);

   if (restart)
     {
        e_util_env_set("E_RESTART_OK", "1");
        if (getenv("E_START_MTRACK"))
          e_util_env_set("MTRACK", "track");
        ecore_app_restart();
     }

   e_prefix_shutdown();

   return 0;
}

E_API double
e_main_ts(const char *str)
{
   double ret;
   t1 = ecore_time_unix_get();
   printf("ESTART: %1.5f [%1.5f] - %s\n", t1 - t0, t1 - t2, str);
   ret = t1 - t2;
   t2 = t1;
   return ret;
}

/* local functions */
static void
_e_main_shutdown(int errcode)
{
   int i = 0;
   char buf[PATH_MAX];
   const char *dir;

   printf("E: Begin Shutdown Procedure!\n");

   if (_idle_before) ecore_idle_enterer_del(_idle_before);
   _idle_before = NULL;
   if (_idle_after) ecore_idle_enterer_del(_idle_after);
   _idle_after = NULL;

   dir = getenv("XDG_RUNTIME_DIR");
   if (dir)
     {
        char buf_env[PATH_MAX];
        snprintf(buf_env, sizeof(buf_env), "%s", dir);
        snprintf(buf, sizeof(buf), "%s/.e-deleteme", buf_env);
        if (ecore_file_exists(buf)) ecore_file_recursive_rm(buf_env);
     }
   for (i = (_e_main_lvl - 1); i >= 0; i--)
     (*_e_main_shutdown_func[i])();
#ifdef OBJECT_HASH_CHECK
   e_object_hash_shutdown();
#endif
   if (errcode < 0) exit(errcode);
}

static void
_e_main_shutdown_push(int (*func)(void))
{
   _e_main_lvl++;
   if (_e_main_lvl > MAX_LEVEL)
     {
        _e_main_lvl--;
        e_error_message_show("WARNING: too many init levels. MAX = %i\n",
                             MAX_LEVEL);
        return;
     }
   _e_main_shutdown_func[_e_main_lvl - 1] = func;
}

static void
_e_main_parse_arguments(int argc, char **argv)
{
   char *s = NULL;
   int i = 0;

   /* handle some command-line parameters */
   for (i = 1; i < argc; i++)
     {
        if ((!strcmp(argv[i], "-display")) && (i < (argc - 1)))
          {
             i++;
             e_util_env_set("DISPLAY", argv[i]);
          }
        else if ((!strcmp(argv[i], "-fake-xinerama-screen")) && (i < (argc - 1)))
          {
             int x, y, w, h;

             i++;
             if (sscanf(argv[i], "%ix%i+%i+%i", &w, &h, &x, &y) == 4)
               e_xinerama_fake_screen_add(x, y, w, h);
          }
        else if (!strcmp(argv[i], "-good"))
          {
             good = EINA_TRUE;
             evil = EINA_FALSE;
             printf("LA LA LA\n");
          }
        else if (!strcmp(argv[i], "-evil"))
          {
             good = EINA_FALSE;
             evil = EINA_TRUE;
             printf("MUHAHAHAHHAHAHAHAHA\n");
          }
        else if (!strcmp(argv[i], "-psychotic"))
          {
             good = EINA_TRUE;
             evil = EINA_TRUE;
             printf("MUHAHALALALALALALALA\n");
          }
        else if ((!strcmp(argv[i], "-profile")) && (i < (argc - 1)))
          {
             i++;
             if (!getenv("E_CONF_PROFILE"))
               e_util_env_set("E_CONF_PROFILE", argv[i]);
          }
        else if (!strcmp(argv[i], "-i-really-know-what-i-am-doing-and-accept-full-responsibility-for-it"))
          really_know = EINA_TRUE;
        else if (!strcmp(argv[i], "-nopause"))
          e_nopause = EINA_TRUE;
        else if ((!strcmp(argv[i], "-version")) ||
                 (!strcmp(argv[i], "--version")))
          {
             printf(_("Version: %s\n"), PACKAGE_VERSION);
             _e_main_shutdown(-1);
          }
        else if ((!strcmp(argv[i], "-h")) ||
                 (!strcmp(argv[i], "-help")) ||
                 (!strcmp(argv[i], "--help")))
          {
             printf
               (_(
                 "Options:\n"
                 "\t-display DISPLAY\n"
                 "\t\tConnect to display named DISPLAY.\n"
                 "\t\tEG: -display :1.0\n"
                 "\t-fake-xinerama-screen WxH+X+Y\n"
                 "\t\tAdd a FAKE xinerama screen (instead of the real ones)\n"
                 "\t\tgiven the geometry. Add as many as you like. They all\n"
                 "\t\treplace the real xinerama screens, if any. This can\n"
                 "\t\tbe used to simulate xinerama.\n"
                 "\t\tEG: -fake-xinerama-screen 800x600+0+0 -fake-xinerama-screen 800x600+800+0\n"
                 "\t-profile CONF_PROFILE\n"
                 "\t\tUse the configuration profile CONF_PROFILE instead of the user selected default or just \"default\".\n"
                 "\t-good\n"
                 "\t\tBe good.\n"
                 "\t-evil\n"
                 "\t\tBe evil.\n"
                 "\t-psychotic\n"
                 "\t\tBe psychotic.\n"
                 "\t-i-really-know-what-i-am-doing-and-accept-full-responsibility-for-it\n"
                 "\t\tIf you need this help, you don't need this option.\n"
                 "\t-version\n"
                 )
               );
             _e_main_shutdown(-1);
          }
     }

   /* fix up DISPLAY to be :N.0 if no .screen is in it */
   s = getenv("DISPLAY");
   if (s)
     {
        char *p, buff[4096];

        if (!(p = strrchr(s, ':')))
          {
             snprintf(buff, sizeof(buff), "%s:0.0", s);
             e_util_env_set("DISPLAY", buff);
          }
        else
          {
             if (!strrchr(p, '.'))
               {
                  snprintf(buff, sizeof(buff), "%s.0", s);
                  e_util_env_set("DISPLAY", buff);
               }
          }
     }

   /* we want to have been launched by enlightenment_start. there is a very */
   /* good reason we want to have been launched this way, thus check */
   if (!getenv("E_START"))
     {
        e_error_message_show(_("You are executing enlightenment directly. This is\n"
                               "bad. Please do not execute the \"enlightenment\"\n"
                               "binary. Use the \"enlightenment_start\" launcher. It\n"
                               "will handle setting up environment variables, paths,\n"
                               "and launching any other required services etc.\n"
                               "before enlightenment itself begins running.\n"));
        _e_main_shutdown(-1);
     }
}

EINTERN void
_e_main_cb_x_fatal(void *data EINA_UNUSED)
{
   e_error_message_show("Lost X Connection.\n");
   ecore_main_loop_quit();
   if (!x_fatal)
     {
        x_fatal = EINA_TRUE;
        if (inloop) longjmp(x_fatal_buff, -99);
     }
}

static Eina_Bool
_e_main_cb_signal_exit(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED)
{
   /* called on ctrl-c, kill (pid) (also SIGINT, SIGTERM and SIGQIT) */
   //e_sys_action_do(E_SYS_EXIT, NULL);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_main_cb_signal_hup(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev EINA_UNUSED)
{
   //e_sys_action_do(E_SYS_RESTART, NULL);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_main_cb_signal_user(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev)
{
   Ecore_Event_Signal_User *e = ev;

   if (e->number == 1)
     {
//        E_Action *a = e_action_find("configuration");
//        if ((a) && (a->func.go)) a->func.go(NULL, NULL);
     }
   else if (e->number == 2)
     {
        // comp module has its own handler for this for enabling/disabling fps debug
     }
   return ECORE_CALLBACK_RENEW;

}

static int
_e_main_dirs_init(void)
{
   const char *base;
   const char *dirs[] =
   {
      "images",
      "fonts",
      "themes",
      "icons",
      "backgrounds",
      "applications",
      "applications/menu",
      "applications/menu/favorite",
      "applications/menu/all",
      "applications/bar",
      "applications/bar/default",
      "applications/startup",
      "applications/restart",
      "applications/trash",
      "applications/desk-lock",
      "applications/desk-unlock",
      "modules",
      "config",
      "locale",
      "input_methods",
      NULL
   };

   base = e_user_dir_get();
   if (ecore_file_mksubdirs(base, dirs) != sizeof(dirs) / sizeof(dirs[0]) - 1)
     {
        e_error_message_show("Could not create one of the required "
                             "subdirectories of '%s'\n", base);
        return 0;
     }

   return 1;
}

static int
_e_main_dirs_shutdown(void)
{
   return 1;
}

static int
_e_main_path_init(void)
{
   char buf[PATH_MAX];

   /* setup data paths */
   path_data = e_path_new();
   if (!path_data)
     {
        e_error_message_show("Cannot allocate path for path_data\n");
        return 0;
     }
   e_prefix_data_concat_static(buf, "data");
   e_path_default_path_append(path_data, buf);

   /* setup image paths */
   path_images = e_path_new();
   if (!path_images)
     {
        e_error_message_show("Cannot allocate path for path_images\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/images");
   e_path_default_path_append(path_images, buf);
   e_prefix_data_concat_static(buf, "data/images");
   e_path_default_path_append(path_images, buf);

   /* setup font paths */
   path_fonts = e_path_new();
   if (!path_fonts)
     {
        e_error_message_show("Cannot allocate path for path_fonts\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/fonts");
   e_path_default_path_append(path_fonts, buf);
   e_prefix_data_concat_static(buf, "data/fonts");
   e_path_default_path_append(path_fonts, buf);

   /* setup icon paths */
   path_icons = e_path_new();
   if (!path_icons)
     {
        e_error_message_show("Cannot allocate path for path_icons\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/icons");
   e_path_default_path_append(path_icons, buf);
   e_prefix_data_concat_static(buf, "data/icons");
   e_path_default_path_append(path_icons, buf);

   /* setup module paths */
   path_modules = e_path_new();
   if (!path_modules)
     {
        e_error_message_show("Cannot allocate path for path_modules\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/modules");
   e_path_default_path_append(path_modules, buf);
   snprintf(buf, sizeof(buf), "%s/enlightenment/modules", e_prefix_lib_get());
   e_path_default_path_append(path_modules, buf);
   /* FIXME: eventually this has to go - moduels should have installers that
    * add appropriate install paths (if not installed to user homedir) to
    * e's module search dirs
    */
   snprintf(buf, sizeof(buf), "%s/enlightenment/modules_extra", e_prefix_lib_get());
   e_path_default_path_append(path_modules, buf);

   /* setup background paths */
   path_backgrounds = e_path_new();
   if (!path_backgrounds)
     {
        e_error_message_show("Cannot allocate path for path_backgrounds\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/backgrounds");
   e_path_default_path_append(path_backgrounds, buf);
   e_prefix_data_concat_static(buf, "data/backgrounds");
   e_path_default_path_append(path_backgrounds, buf);

   path_messages = e_path_new();
   if (!path_messages)
     {
        e_error_message_show("Cannot allocate path for path_messages\n");
        return 0;
     }
   e_user_dir_concat_static(buf, "/locale");
   e_path_default_path_append(path_messages, buf);
   e_path_default_path_append(path_messages, e_prefix_locale_get());

   return 1;
}

static int
_e_main_path_shutdown(void)
{
   if (path_data)
     {
        e_object_del(E_OBJECT(path_data));
        path_data = NULL;
     }
   if (path_images)
     {
        e_object_del(E_OBJECT(path_images));
        path_images = NULL;
     }
   if (path_fonts)
     {
        e_object_del(E_OBJECT(path_fonts));
        path_fonts = NULL;
     }
   if (path_icons)
     {
        e_object_del(E_OBJECT(path_icons));
        path_icons = NULL;
     }
   if (path_modules)
     {
        e_object_del(E_OBJECT(path_modules));
        path_modules = NULL;
     }
   if (path_backgrounds)
     {
        e_object_del(E_OBJECT(path_backgrounds));
        path_backgrounds = NULL;
     }
   if (path_messages)
     {
        e_object_del(E_OBJECT(path_messages));
        path_messages = NULL;
     }
   return 1;
}

static int
_e_main_screens_init(void)
{
   TS("\tscreens: client");
   if (!e_client_init()) return 0;

   TS("Compositor Init");
   if (!e_comp_init())
     {
        e_error_message_show(_("Enlightenment cannot create a compositor.\n"));
        _e_main_shutdown(-1);
     }

   _e_main_desk_restore();

   return 1;
}

static int
_e_main_screens_shutdown(void)
{
   e_win_shutdown();
   e_comp_shutdown();
   e_client_shutdown();

   e_desk_shutdown();
   e_zone_shutdown();
   return 1;
}

static void
_e_main_desk_save(void)
{
   const Eina_List *l;
   char env[1024], name[1024];
   E_Zone *zone;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        snprintf(name, sizeof(name), "DESK_%d_%d", 0, zone->num);
        snprintf(env, sizeof(env), "%d,%d", zone->desk_x_current, zone->desk_y_current);
        e_util_env_set(name, env);
     }
}

static void
_e_main_desk_restore(void)
{
   const Eina_List *l;
   E_Zone *zone;
   E_Client *ec;
   char *env;
   char name[1024];

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Desk *desk;
        int desk_x, desk_y;
        char buf_e[64];

        snprintf(name, sizeof(name), "DESK_%d_%d", 0, zone->num);
        env = getenv(name);
        if (!env) continue;
        snprintf(buf_e, sizeof(buf_e), "%s", env);
        if (!sscanf(buf_e, "%d,%d", &desk_x, &desk_y)) continue;
        desk = e_desk_at_xy_get(zone, desk_x, desk_y);
        if (!desk) continue;
        e_desk_show(desk);
     }

   E_CLIENT_REVERSE_FOREACH(ec)
     if ((!e_client_util_ignored_get(ec)) && e_client_util_desk_visible(ec, e_desk_current_get(ec->zone)))
       {
          ec->want_focus = ec->take_focus = 1;
          break;
       }
}

static Eina_Bool
_e_main_modules_load_after(void *d EINA_UNUSED, int type EINA_UNUSED, void *ev EINA_UNUSED)
{
   E_FREE_FUNC(mod_init_end, ecore_event_handler_del);
   return ECORE_CALLBACK_RENEW;
}

static void
_e_main_modules_load(Eina_Bool safe_mode)
{
   if (!safe_mode)
     e_module_all_load();
   else
     {
        E_Module *m;
        char *crashmodule;

        crashmodule = getenv("E_MODULE_LOAD");
        if (crashmodule) m = e_module_new(crashmodule);

        if ((crashmodule) && (m))
          {
             e_module_disable(m);
             e_object_del(E_OBJECT(m));

             e_error_message_show
               (_("Enlightenment crashed early on start and has<br>"
                  "been restarted. There was an error loading the<br>"
                  "module named: %s. This module has been disabled<br>"
                  "and will not be loaded."), crashmodule);
             e_util_dialog_show
               (_("Enlightenment crashed early on start and has been restarted"),
               _("Enlightenment crashed early on start and has been restarted.<br>"
                 "There was an error loading the module named: %s<br><br>"
                 "This module has been disabled and will not be loaded."), crashmodule);
             e_module_all_load();
          }
        else
          {
             e_error_message_show
               (_("Enlightenment crashed early on start and has<br>"
                  "been restarted. All modules have been disabled<br>"
                  "and will not be loaded to help remove any problem<br>"
                  "modules from your configuration. The module<br>"
                  "configuration dialog should let you select your<br>"
                  "modules again.\n"));
             e_util_dialog_show
               (_("Enlightenment crashed early on start and has been restarted"),
               _("Enlightenment crashed early on start and has been restarted.<br>"
                 "All modules have been disabled and will not be loaded to help<br>"
                 "remove any problem modules from your configuration.<br><br>"
                 "The module configuration dialog should let you select your<br>"
                 "modules again."));
          }
        mod_init_end = ecore_event_handler_add(E_EVENT_MODULE_INIT_END, _e_main_modules_load_after, NULL);
     }
}

static Eina_Bool
_e_main_cb_idle_before(void *data EINA_UNUSED)
{
   e_client_idler_before();
   e_pointer_idler_before();
   edje_thaw();
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_main_cb_idle_after(void *data EINA_UNUSED)
{
   static int first_idle = 1;

   eet_clearcache();
   edje_freeze();

#ifdef E_RELEASE_BUILD
   if (first_idle)
     {
        TS("SLEEP");
        first_idle = 0;
        e_precache_end = EINA_TRUE;
     }
#else
   if (first_idle++ < 60)
     {
        TS("SLEEP");
        if (!first_idle)
          e_precache_end = EINA_TRUE;
     }
#endif

   return ECORE_CALLBACK_RENEW;
}
