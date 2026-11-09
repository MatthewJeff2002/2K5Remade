/* ── Phone settings (Android) ─────────────────────────────────────────────
 *
 * The rows of Options > Phone Settings (src/nfl2k5_video_menu.c): the touch
 * controls (src/platform/android/touch_pad.c), the battery saver
 * (nv2a_gpu_present.inc.c), heat control, pausing the game while the app is
 * in the background, adding songs from the phone (NFL2K5Activity.pickSongs),
 * the touch layout editor, graphics safety (below) and the game folder's
 * place (platform/android/game_folder.c).
 * Saved in <root>/android.ini. */
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <sys/prctl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void xbox_ggl_enter(void);
extern void xbox_ggl_leave(void);
extern void xbox_ggl_set_priority(int prio);
extern int xbox_ggl_enabled(void);
extern volatile int g_xbox_battery_saver;           /* nv2a_gpu_present.inc.c */
extern volatile int g_xbox_heat_level;              /* nv2a_gpu_present.inc.c: 1 = 1x, 2 = also 30 FPS */
extern void nfl2k5_touch_edit_begin(void);          /* touch_pad.c */
extern void nfl2k5_local_music_rescan(void);        /* nfl2k5_local_music.cpp */
extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
extern void nfl2k5_touch_pad_changed(void);         /* touch_pad.c: redraw */
extern void (*g_xbox_fps_limit_changed)(void);      /* src/main.c: re-reads the frame-rate cap */

static struct {
    int touch;          /* 0 automatic (hidden while a controller is used), 1 on, 2 off */
    int touch_size;     /* 0 small, 1 medium, 2 large */
    int touch_opacity;  /* 0 25%, 1 50%, 2 75%, 3 100% */
    int battery_saver;  /* 1x internal resolution and 30 FPS */
    int pause_bg;       /* freeze the game while the app is in the background */
    int heat;           /* lower the quality while the phone is hot */
    int gpu_safety;     /* 0 automatic (on after a GPU crash), 1 off, 2 on */
    int gpu_level;      /* what automatic reached: 0 normal, 1 safe, 2 safe + fresh pipeline cache */
} s_set = { 0, 1, 2, 0, 1, 1, 0, 0 };
static int s_safety_notice;            /* graphics safety: toast once the menus are up */

static void ini_path(char *out, size_t n)
{
    const char *root = getenv("NFL2K5_ROOT");
    snprintf(out, n, "%s/android.ini", root ? root : "/sdcard/NFL2K5");
}

static void save(void)
{
    char p[1024];
    FILE *f;
    ini_path(p, sizeof p);
    if (!(f = fopen(p, "w"))) return;
    fprintf(f, "; Options > Phone Settings\n"
               "; touch: 0 automatic (hidden while a controller is in use), 1 on, 2 off\n"
               "touch = %d\n; touch_size: 0 small, 1 medium, 2 large\ntouch_size = %d\n"
               "; touch_opacity: 0 25%%, 1 50%%, 2 75%%, 3 100%%\ntouch_opacity = %d\n"
               "battery_saver = %d\npause_in_background = %d\nheat_control = %d\n"
               "; graphics_safety: 0 automatic (on after a crash in the GPU driver), 1 off, 2 on\n"
               "graphics_safety = %d\n; graphics_safety_level: what automatic has reached (0 normal, 1-2 safe)\n"
               "graphics_safety_level = %d\n",
            s_set.touch, s_set.touch_size, s_set.touch_opacity, s_set.battery_saver, s_set.pause_bg, s_set.heat,
            s_set.gpu_safety, s_set.gpu_level);
    fclose(f);
}

static void load(void)
{
    char p[1024], line[128], key[64];
    int v;
    FILE *f;
    ini_path(p, sizeof p);
    if (!(f = fopen(p, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " %63[a-z_] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "touch")) s_set.touch = v >= 0 && v <= 2 ? v : 0;
        else if (!strcmp(key, "touch_size")) s_set.touch_size = v >= 0 && v <= 2 ? v : 1;
        else if (!strcmp(key, "touch_opacity")) s_set.touch_opacity = v >= 0 && v <= 3 ? v : 2;
        else if (!strcmp(key, "battery_saver")) s_set.battery_saver = v != 0;
        else if (!strcmp(key, "pause_in_background")) s_set.pause_bg = v != 0;
        else if (!strcmp(key, "heat_control")) s_set.heat = v != 0;
        else if (!strcmp(key, "graphics_safety")) s_set.gpu_safety = v >= 0 && v <= 2 ? v : 0;
        else if (!strcmp(key, "graphics_safety_level")) s_set.gpu_level = v >= 0 && v <= 2 ? v : 0;
    }
    fclose(f);
}

/* ---- touch controls (touch_pad.c) ---- */
int nfl2k5_touch_mode(void) { return s_set.touch; }
void nfl2k5_touch_set_mode(int m) { s_set.touch = m; save(); nfl2k5_touch_pad_changed(); }
float nfl2k5_touch_scale(void) { static const float k[] = { 0.85f, 1.0f, 1.18f }; return k[s_set.touch_size % 3]; }
float nfl2k5_touch_alpha(void) { static const float k[] = { 0.45f, 0.75f, 1.0f, 1.35f }; return k[s_set.touch_opacity % 4]; }

/* ---- pausing in the background -------------------------------------------
 * Android stops drawing and SDL pauses the sound, but the game itself ran on
 * (and spent the battery) while the app was out of sight. A thread takes the
 * global guest lock at the top priority and keeps it until the app is back:
 * every guest thread, the timer's vblank included, stops at its next kernel
 * call, the way the console's game freezes when nothing schedules it. */
static SDL_Mutex *s_pause_lock;
static SDL_Condition *s_pause_cond;
static int s_paused_wanted, s_pause_running;

static int pause_thread(void *arg)
{
    (void)arg;
    xbox_ggl_set_priority(1000);
    xbox_ggl_enter();
    fprintf(stderr, "[ANDROID] game paused (app in the background)\n");
    SDL_LockMutex(s_pause_lock);
    while (s_paused_wanted) SDL_WaitCondition(s_pause_cond, s_pause_lock);
    s_pause_running = 0;
    SDL_UnlockMutex(s_pause_lock);
    xbox_ggl_leave();
    fprintf(stderr, "[ANDROID] game resumed\n");
    return 0;
}

static bool SDLCALL on_app_event(void *ud, SDL_Event *e)
{
    (void)ud;
    if (e->type == SDL_EVENT_LOW_MEMORY) fprintf(stderr, "[MEM] Android says the phone is low on memory\n");
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND && s_set.pause_bg && xbox_ggl_enabled()) {
        SDL_LockMutex(s_pause_lock);
        s_paused_wanted = 1;
        if (!s_pause_running) {
            SDL_Thread *t = SDL_CreateThread(pause_thread, "app_pause", NULL);
            if (t) { s_pause_running = 1; SDL_DetachThread(t); }
        }
        SDL_UnlockMutex(s_pause_lock);
    } else if (e->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        SDL_LockMutex(s_pause_lock);
        s_paused_wanted = 0;
        SDL_BroadcastCondition(s_pause_cond);
        SDL_UnlockMutex(s_pause_lock);
    }
    return true;
}

/* ---- adding songs ---------------------------------------------------------
 * Android's file picker (NFL2K5Activity.pickSongs) copies the chosen songs
 * into Music/Phone Songs; the library then rescans and converts them. */
static void JNICALL jni_songs_added(JNIEnv *env, jclass cls, jint n)
{
    char line[64];
    (void)env; (void)cls;
    fprintf(stderr, "[ANDROID] %d song(s) added to Music/Phone Songs\n", (int)n);
    if (n <= 0) return;
    snprintf(line, sizeof line, "%d song%s added to \"Phone Songs\"", (int)n, n == 1 ? "" : "s");
    xbox_PresentToast("MUSIC", line, "Converting: they appear in the soundtrack list shortly", 5000);
    nfl2k5_local_music_rescan();
}

/* Attaching to the VM renames the thread "Thread-N"; keep its own name. */
static JNIEnv *jni_env(void)
{
    char name[17] = { 0 };
    JNIEnv *env;
    prctl(PR_GET_NAME, name, 0, 0, 0);
    env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (name[0]) prctl(PR_SET_NAME, name, 0, 0, 0);
    return env;
}

static jclass activity_class(JNIEnv *env)
{
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass c = act ? (*env)->GetObjectClass(env, act) : NULL;
    if (act) (*env)->DeleteLocalRef(env, act);
    return c;
}

void nfl2k5_android_pick_songs(void)
{
    JNIEnv *env = jni_env();
    jclass c;
    jmethodID m;
    if (!env || !(c = activity_class(env))) return;
    m = (*env)->GetStaticMethodID(env, c, "pickSongs", "()V");
    if (m) (*env)->CallStaticVoidMethod(env, c, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
}

/* ---- GPU Driver > Add Driver (src/nfl2k5_gpu_driver.c) ----
 * The same picker; NFL2K5Activity copies the choice into app storage and
 * passes the copy here. */
extern int nfl2k5_gpu_driver_install(const char *file, const char *shown, char *msg, size_t msg_n);
extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
static void JNICALL jni_driver_picked(JNIEnv *env, jclass cls, jstring path, jstring shown)
{
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    const char *s = (*env)->GetStringUTFChars(env, shown, NULL);
    char msg[200];
    int ok;
    (void)cls;
    ok = nfl2k5_gpu_driver_install(p, s, msg, sizeof msg);
    xbox_PresentToast("GPU DRIVER", ok ? "Added:" : "Not added:", msg, 6000);
    (*env)->ReleaseStringUTFChars(env, path, p);
    (*env)->ReleaseStringUTFChars(env, shown, s);
}

void nfl2k5_android_pick_driver(void)
{
    JNIEnv *env = jni_env();
    jclass c;
    jmethodID m;
    if (!env || !(c = activity_class(env))) return;
    m = (*env)->GetStaticMethodID(env, c, "pickDriver", "()V");
    if (m) (*env)->CallStaticVoidMethod(env, c, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
}

/* ---- heat control -----------------------------------------------------------
 * A hot phone slows its own processor down hard; stepping the game's quality
 * down first keeps it smooth. Android's thermal status (Android 11+, through
 * libandroid) or else the battery temperature: warm = half the internal
 * resolution ceiling (the automatic resolution works under it), hot = 1x and
 * 30 FPS. A step back up waits for a minute below the line. */
typedef void *(*AcquireFn)(void);
typedef int (*StatusFn)(void *);

/* From the sticky battery intent (src/main.c): the sysfs file reads as nothing
 * on an S22+, which also left Heat Control's temperature limits dead. */
extern int nfl2k5_battery_temp10(void);
static int battery_tenths_c(void)
{
    int t = nfl2k5_battery_temp10();
    FILE *f;
    if (t > 0) return t;
    f = fopen("/sys/class/power_supply/battery/temp", "r");
    t = 0;
    if (!f) return 0;
    if (fscanf(f, "%d", &t) != 1) t = 0;
    fclose(f);
    return t;
}

static int heat_thread(void *arg)
{
    void *lib = dlopen("libandroid.so", RTLD_NOW), *mgr = NULL;
    AcquireFn acquire = lib ? (AcquireFn)dlsym(lib, "AThermal_acquireManager") : NULL;
    StatusFn status = lib ? (StatusFn)dlsym(lib, "AThermal_getCurrentThermalStatus") : NULL;
    Uint64 cool_since = 0;
    int level = 0;
    unsigned reports = 0;
    unsigned long peak[8] = { 0 };
    (void)arg;
    if (acquire && status) mgr = acquire();
    for (;;) {
        if (s_safety_notice && SDL_GetTicks() > 25000) {
            s_safety_notice = 0;
            xbox_PresentToast("GRAPHICS SAFETY ON", "The last run crashed in the graphics driver.",
                              "New scenes may pause briefly. Options > Phone Settings", 9000);
        }
        int st = mgr ? status(mgr) : -1, t = battery_tenths_c(), want = 0;
        if (reports++ % 3 == 0) {
            /* every 30 s, Heat Control on or off: tells heat from a heavy scene
             * in a log. The highest speed seen since the last line: one reading
             * can be the governor resting, not throttling. */
            char line[176];
            int o = snprintf(line, sizeof line, "[HEAT] thermal status %d, battery %.1f C, highest MHz in 30 s:", st, t / 10.0);
            for (int c = 0; c < 8 && o < (int)sizeof line - 12; c++) {
                if (peak[c]) o += snprintf(line + o, sizeof line - o, " %lu", peak[c] / 1000);
                peak[c] = 0;
            }
            fprintf(stderr, "%s%c", line, 10);
        }
        /* ATHERMAL_STATUS_MODERATE 2, SEVERE 3 */
        if (st >= 3 || t >= 460) want = 2;
        else if (st == 2 || t >= 420) want = 1;
        if (!s_set.heat) want = 0;
        if (want > level || !s_set.heat) { cool_since = 0; }
        if (want >= level) {
            if (want != level) fprintf(stderr, "[ANDROID] heat: level %d (thermal status %d, battery %.1f C)\n", want, st, t / 10.0);
            level = want;
        } else {
            if (!cool_since) cool_since = SDL_GetTicks();
            if (SDL_GetTicks() - cool_since >= 60000) {
                level--;
                cool_since = 0;
                fprintf(stderr, "[ANDROID] heat: level %d (cooler: status %d, battery %.1f C)\n", level, st, t / 10.0);
            }
        }
        if (g_xbox_heat_level != level) {
            g_xbox_heat_level = level;
            if (g_xbox_fps_limit_changed) g_xbox_fps_limit_changed();
        }
        for (int i = 0; i < 5; i++) {
            for (int c = 0; c < 8; c++) {
                char path[80];
                unsigned long khz = 0;
                FILE *f;
                snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
                if (!(f = fopen(path, "r"))) continue;
                if (fscanf(f, "%lu", &khz) == 1 && khz > peak[c]) peak[c] = khz;
                fclose(f);
            }
            SDL_Delay(2000);
        }
    }
    return 0;
}

/* ---- graphics safety (2026-11-09) -------------------------------------------
 * Players on Snapdragon 8 Gen 2 handhelds (Ayn Thor, Retroid Pocket 6) saw
 * the game crash at scene changes, where new pipelines are compiled in the
 * background. The crash handler (src/main.c) leaves <root>/cache/last-crash.txt
 * saying whether a GPU driver was in the crash; Android's own record of the
 * last exit (DeviceReport.java, NFL2K5_LAST_EXIT) covers what the handler
 * cannot catch. With Graphics Safety on Automatic, such a crash turns safe
 * mode on for the following starts: pipelines are compiled at the draw, one
 * at a time (a pause the first time a scene appears instead of a crash), and
 * a second crash also sets the pipeline cache file aside (kept as .bad).
 * Devices of the safety profile always compile one pipeline at a time. */

static int s_crash_kind;               /* 0 none, 1 GPU, 2 other, 3 native crash the handler missed */

static void gpu_safety_boot(void)
{
    const char *root = getenv("NFL2K5_ROOT") ? getenv("NFL2K5_ROOT") : "/sdcard/NFL2K5";
    const char *prof = getenv("NFL2K5_DEVICE_PROFILE"), *last = getenv("NFL2K5_LAST_EXIT");
    char marker[1024], prev[1024], line[160];
    int gpu = -1, level;
    FILE *f;
    snprintf(marker, sizeof marker, "%s/cache/last-crash.txt", root);
    snprintf(prev, sizeof prev, "%s/cache/last-crash.prev.txt", root);
    if ((f = fopen(marker, "r"))) {
        char what[160] = "";
        while (fgets(line, sizeof line, f)) {
            int g;
            if (sscanf(line, "gpu %d", &g) == 1) gpu = g;
            if (!strncmp(line, "thread ", 7) || !strncmp(line, "signal ", 7)) {
                size_t l = strlen(what);
                snprintf(what + l, sizeof what - l, "%s%.*s", l ? ", " : "", (int)strcspn(line, "\n"), line);
            }
        }
        fclose(f);
        remove(prev);
        rename(marker, prev);
        s_crash_kind = gpu == 1 ? 1 : 2;
        fprintf(stderr, "[SAFETY] the last run crashed (%s), %s\n", what,
                gpu == 1 ? "in the GPU driver or the renderer" : "not in the GPU driver");
    } else if (last && atoi(last) == 5) {
        s_crash_kind = 3;                   /* a native crash that left no marker: a driver abort or stack overflow */
        fprintf(stderr, "[SAFETY] the last run crashed in native code without a report of ours: counted as a GPU crash\n");
    }
    if (last && atoi(last) == 3)
        fprintf(stderr, "[SAFETY] Android closed the last run for low memory\n");
    if (s_set.gpu_safety == 0 && (s_crash_kind == 1 || s_crash_kind == 3) && s_set.gpu_level < 2) {
        s_set.gpu_level++;
        if (s_set.gpu_level == 2) {
            /* a second crash with safe mode on: a pipeline cache the driver chokes on is set aside, not deleted */
            const char *pc = getenv("SDL_GPU_VULKAN_PIPELINE_CACHE");
            if (pc && *pc) {
                char bad[1100];
                snprintf(bad, sizeof bad, "%s.bad", pc);
                remove(bad);
                if (rename(pc, bad) == 0) fprintf(stderr, "[SAFETY] pipeline cache set aside: %s\n", bad);
            }
        }
        s_safety_notice = 1;
        save();
    }
    level = s_set.gpu_safety == 1 ? 0 : s_set.gpu_safety == 2 ? 1 : s_set.gpu_level;
    if (level >= 1) setenv("RECOMP_GX_ASYNC_PIPES", "0", 0);
    if (level >= 1 || (prof && *prof)) setenv("RECOMP_GX_PIPE_SERIAL", "1", 0);
    fprintf(stderr, "[SAFETY] Graphics Safety %s, level %d%s: background compiles %s, one compile at a time %s\n",
            s_set.gpu_safety == 0 ? "Automatic" : s_set.gpu_safety == 1 ? "Off" : "On", s_set.gpu_level,
            prof && *prof ? " (device profile)" : "",
            getenv("RECOMP_GX_ASYNC_PIPES") && *getenv("RECOMP_GX_ASYNC_PIPES") == '0' ? "off" : "on",
            getenv("RECOMP_GX_PIPE_SERIAL") && *getenv("RECOMP_GX_PIPE_SERIAL") == '1' ? "yes" : "no");
}

static void JNICALL jni_trim_memory(JNIEnv *env, jclass cls, jint level)
{
    long rss = -1;
    char ml[128];
    FILE *f = fopen("/proc/self/status", "r");
    (void)env; (void)cls;
    while (f && fgets(ml, sizeof ml, f)) if (!strncmp(ml, "VmRSS:", 6)) { rss = atol(ml + 6); break; }
    if (f) fclose(f);
    /* 5/10/15 running moderate/low/critical, 20 hidden, 40-80 background */
    fprintf(stderr, "[MEM] Android asks the app to free memory (level %d), game holds %ld MB\n", (int)level, rss / 1024);
}

/* ---- Options > Phone Settings rows (src/nfl2k5_video_menu.c) ---- */
enum { PS_TOUCH, PS_SIZE, PS_OPACITY, PS_EDIT, PS_BATTERY, PS_HEAT, PS_PAUSE, PS_SONGS, PS_SAFETY, PS_FOLDER, PS_COUNT };
static const char *const k_labels[PS_COUNT] = { "Touch Controls", "Touch Button Size", "Touch Opacity",
                                                "Edit Touch Layout", "Battery Saver", "Heat Control",
                                                "Pause When Leaving", "Add Songs", "Graphics Safety", "Game Folder" };
extern int nfl2k5_folder_count(void);                         /* game_folder.c */
extern int nfl2k5_folder_selected(void);
extern void nfl2k5_folder_choose(int i);
extern void nfl2k5_folder_name(int i, char *b, size_t n);
int nfl2k5_phone_rows(void) { return PS_COUNT; }
const char *nfl2k5_phone_label(int r) { return r >= 0 && r < PS_COUNT ? k_labels[r] : ""; }
int nfl2k5_phone_toggle(int r) { return r == PS_PAUSE; }
int nfl2k5_phone_values(int r)
{
    switch (r) {
    case PS_TOUCH: case PS_SIZE: return 3;
    case PS_OPACITY: return 4;
    case PS_SONGS: case PS_EDIT: return 2;
    case PS_SAFETY: return 3;
    case PS_FOLDER: { int c = nfl2k5_folder_count(); return c < 1 ? 1 : c; }
    default: return 2;
    }
}
int nfl2k5_phone_get(int r)
{
    switch (r) {
    case PS_TOUCH: return s_set.touch;
    case PS_SIZE: return s_set.touch_size;
    case PS_OPACITY: return s_set.touch_opacity;
    case PS_BATTERY: return s_set.battery_saver;
    case PS_PAUSE: return s_set.pause_bg;
    case PS_HEAT: return s_set.heat;
    case PS_SAFETY: return s_set.gpu_safety;
    case PS_FOLDER: return nfl2k5_folder_selected();
    default: return 0;
    }
}
void nfl2k5_phone_set(int r, int v)
{
    switch (r) {
    case PS_TOUCH: s_set.touch = v; nfl2k5_touch_pad_changed(); break;
    case PS_SIZE: s_set.touch_size = v; nfl2k5_touch_pad_changed(); break;
    case PS_OPACITY: s_set.touch_opacity = v; nfl2k5_touch_pad_changed(); break;
    case PS_BATTERY: s_set.battery_saver = v != 0; g_xbox_battery_saver = s_set.battery_saver;
                    fprintf(stderr, "[ANDROID] Battery Saver %s\n", v ? "on" : "off");
                    if (g_xbox_fps_limit_changed) g_xbox_fps_limit_changed();
                    break;
    case PS_PAUSE: s_set.pause_bg = v != 0; break;
    case PS_HEAT: s_set.heat = v != 0; break;
    case PS_SAFETY:
        s_set.gpu_safety = v;
        if (v == 0) s_set.gpu_level = 0;                /* back to Automatic: a fresh start */
        fprintf(stderr, "[SAFETY] Graphics Safety set to %s (from the next start)\n", v == 0 ? "Automatic" : v == 1 ? "Off" : "On");
        xbox_PresentToast("GRAPHICS SAFETY", v == 0 ? "Automatic" : v == 1 ? "Off" : "On", "Applies the next time the game starts", 4000);
        break;
    case PS_FOLDER: nfl2k5_folder_choose(v); return;   /* moves at the next start, after a confirmation */
    case PS_EDIT: nfl2k5_touch_edit_begin(); return;      /* an action: opens the editor */
    case PS_SONGS: nfl2k5_android_pick_songs(); return;   /* an action: opens the picker */
    default: return;
    }
    save();
}
void nfl2k5_phone_value_name(int r, int v, char *b, size_t n)
{
    static const char *const touch[] = { "Automatic", "On", "Off" };
    static const char *const size[] = { "Small", "Medium", "Large" };
    static const char *const opacity[] = { "25%", "50%", "75%", "100%" };
    switch (r) {
    case PS_TOUCH: snprintf(b, n, "%s", touch[(unsigned)v % 3u]); break;
    case PS_SIZE: snprintf(b, n, "%s", size[(unsigned)v % 3u]); break;
    case PS_OPACITY: snprintf(b, n, "%s", opacity[(unsigned)v % 4u]); break;
    case PS_SONGS: snprintf(b, n, "%s", "Choose from phone"); break;
    case PS_EDIT: snprintf(b, n, "%s", "Move and resize"); break;
    /* the two settings allowed to change the picture say so */
    case PS_BATTERY: snprintf(b, n, "%s", v ? "On: 1x resolution, 30 FPS" : "Off"); break;
    case PS_HEAT: snprintf(b, n, "%s", v ? "On: can lower resolution" : "Off"); break;
    case PS_SAFETY:
        snprintf(b, n, "%s", v == 1 ? "Off" : v == 2 ? "On" : s_set.gpu_level ? "Automatic: on after a crash" : "Automatic");
        break;
    case PS_FOLDER: nfl2k5_folder_name(v, b, n); break;
    default: snprintf(b, n, "%s", v ? "On" : "Off"); break;
    }
}

void nfl2k5_android_settings_init(void)
{
    JNIEnv *env;
    jclass c;
    static const JNINativeMethod natives[] = {
        { (char *)"nativeSongsAdded", (char *)"(I)V", (void *)jni_songs_added },
        { (char *)"nativeDriverPicked", (char *)"(Ljava/lang/String;Ljava/lang/String;)V", (void *)jni_driver_picked },
        { (char *)"nativeTrimMemory", (char *)"(I)V", (void *)jni_trim_memory },
    };
    load();
    gpu_safety_boot();
    g_xbox_battery_saver = s_set.battery_saver;
    fprintf(stderr, "[ANDROID] Phone Settings: Battery Saver %s, Heat Control %s\n",
            s_set.battery_saver ? "on" : "off", s_set.heat ? "on" : "off");
    s_pause_lock = SDL_CreateMutex();
    s_pause_cond = SDL_CreateCondition();
    SDL_AddEventWatch(on_app_event, NULL);
    {
        SDL_Thread *t = SDL_CreateThread(heat_thread, "heat_control", NULL);
        if (t) SDL_DetachThread(t);
    }
    env = jni_env();
    if (env && (c = activity_class(env))) {
        if ((*env)->RegisterNatives(env, c, natives, 3) != 0) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, c);
    }
}
#endif
