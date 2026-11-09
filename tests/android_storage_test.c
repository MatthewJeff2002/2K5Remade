/* Android disc import and game-folder move, tested on ARM64 Linux (the test VM).
 *
 * Builds src/platform/android/xiso_extract.c and game_folder.c as on a phone
 * (__ANDROID__), with SDL's window calls stubbed: a scripted controller
 * press answers each question. Paths are the phone's (/sdcard/...,
 * /storage/TEST-CARD), so run it as root in a throw-away machine.
 *
 *   android_storage_test xiso   <retail default.xbe>   xiso image in a Download subfolder
 *   android_storage_test redump <retail default.xbe>   full-disc layout in Download
 *   android_storage_test packed                        only a .7z: refused with a message
 *   android_storage_test move                          copy /sdcard/NFL2K5 to the card (A)
 *   android_storage_test decline                       a waiting move, answered B
 *   android_storage_test missing                       chosen card gone: internal this time
 */
#define __ANDROID__ 1
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stubs ---- */
static char s_answer = 'A';
static int s_armed, s_notices, s_toasts;
bool SDL_InitSubSystem(SDL_InitFlags f) { (void)f; return true; }
bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags f, const char *t, const char *m, SDL_Window *w)
{ (void)f; (void)w; printf("[TEST] message box: %s: %s\n", t, m); s_notices++; return true; }
const char *SDL_GetError(void) { return "stub"; }
bool SDL_CreateWindowAndRenderer(const char *t, int w, int h, SDL_WindowFlags f, SDL_Window **win, SDL_Renderer **r)
{ (void)t; (void)w; (void)h; (void)f; *win = (SDL_Window *)1; *r = (SDL_Renderer *)1; return true; }
Uint64 SDL_GetTicks(void) { static Uint64 t; return t += 300; }
bool SDL_PollEvent(SDL_Event *e)
{
    if (!s_armed) return false;
    s_armed = 0;
    memset(e, 0, sizeof *e);
    e->type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e->gbutton.button = s_answer == 'A' ? SDL_GAMEPAD_BUTTON_SOUTH : s_answer == 'X' ? SDL_GAMEPAD_BUTTON_WEST : SDL_GAMEPAD_BUTTON_EAST;
    printf("[TEST] pressed %c\n", s_answer);
    return true;
}
bool SDL_GetRenderOutputSize(SDL_Renderer *r, int *w, int *h) { (void)r; *w = 1280; *h = 720; return true; }
bool SDL_SetRenderScale(SDL_Renderer *r, float x, float y) { (void)r; (void)x; (void)y; return true; }
bool SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 a, Uint8 b, Uint8 c, Uint8 d) { (void)r; (void)a; (void)b; (void)c; (void)d; return true; }
bool SDL_RenderClear(SDL_Renderer *r) { (void)r; return true; }
bool SDL_RenderDebugText(SDL_Renderer *r, float x, float y, const char *s)
{ (void)r; (void)x; if (y < 300) printf("[SCREEN] %s\n", s); return true; }
bool SDL_RenderPresent(SDL_Renderer *r) { (void)r; s_armed = 1; return true; }
bool SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *f) { (void)r; (void)f; return true; }
void SDL_Delay(Uint32 ms) { (void)ms; }
void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
SDL_JoystickID *SDL_GetGamepads(int *n) { *n = 0; return NULL; }
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) { (void)id; return NULL; }
void SDL_CloseGamepad(SDL_Gamepad *g) { (void)g; }
void SDL_free(void *p) { free(p); }
const char *SDL_GetAndroidExternalStoragePath(void) { return "/sdcard/Android/data/com.nfl2k5.pc/files"; }
void xbox_PresentToast(const char *h, const char *a, const char *b, unsigned ms)
{ (void)ms; printf("[TEST] toast %s: %s / %s\n", h, a, b); s_toasts++; }

#include "../src/platform/android/xiso_extract.c"
#include "../src/platform/android/game_folder.c"

static int sh(const char *cmd) { printf("[TEST] $ %s\n", cmd); fflush(stdout); return system(cmd); }

static int check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); return ok ? 0 : 1; }

int main(int argc, char **argv)
{
    const char *t = argc > 1 ? argv[1] : "";
    int fails = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(t, "xiso") || !strcmp(t, "redump")) {
        char cmd[2048];
        int r;
        sh("rm -rf /sdcard /tmp/r && mkdir -p /sdcard/Download/games /tmp/r");
        snprintf(cmd, sizeof cmd, "python3 %s/mkxiso.py %s %s '%s'", argc > 3 ? argv[3] : ".", argv[2],
                 !strcmp(t, "xiso") ? "0" : "0x18300000",
                 !strcmp(t, "xiso") ? "/sdcard/Download/games/ESPN NFL 2K5 (USA).xiso.iso" : "/sdcard/Download/NFL 2K5 redump.iso");
        if (sh(cmd)) return 2;
        r = nfl2k5_android_unpack_iso("/tmp/r");
        fails += check(r == 1, "unpack returned 1");
        fails += check(nfl2k5_android_xbe_file_ok("/tmp/r/original/default.xbe"), "original/default.xbe is the retail executable");
        fails += check(access("/tmp/r/original/vc_53450030/test.bin", R_OK) == 0, "sub-folder file unpacked");
        fails += check(access("/tmp/r/original.part", F_OK) != 0, "no original.part left");
    } else if (!strcmp(t, "packed")) {
        sh("rm -rf /sdcard /tmp/r && mkdir -p /sdcard/Download /tmp/r && echo x > '/sdcard/Download/ESPN NFL 2K5 (USA).7z'");
        fails += check(nfl2k5_android_unpack_iso("/tmp/r") == -1, "a .7z alone is refused (-1)");
        fails += check(s_notices == 1, "with one message");
    } else if (!strcmp(t, "move") || !strcmp(t, "decline")) {
        sh("rm -rf /sdcard /storage/TEST-CARD && mkdir -p /sdcard/NFL2K5/original/vc_53450030 /sdcard/NFL2K5/saves /storage/TEST-CARD");
        sh("head -c 3000000 /dev/urandom > /sdcard/NFL2K5/original/default.xbe && head -c 5000000 /dev/urandom > "
           "/sdcard/NFL2K5/original/vc_53450030/a.bin && echo save > /sdcard/NFL2K5/saves/s1 && echo log > /sdcard/NFL2K5/nfl2k5.log");
        setenv("NFL2K5_ROOT", "/sdcard/NFL2K5", 1);
        setenv("NFL2K5_VOLUMES", "/storage/TEST-CARD", 1);
        fails += check(nfl2k5_folder_count() == 2, "two choices (internal, the card)");
        nfl2k5_folder_choose(1);
        fails += check(access(PENDING, R_OK) == 0, "the move is only noted");
        {
            char nm[64];
            nfl2k5_folder_name(1, nm, sizeof nm);
            printf("[TEST] row value: %s\n", nm);
            fails += check(strstr(nm, "next start") != NULL && strlen(nm) <= 30, "row says next start, under 30 chars");
        }
        unsetenv("NFL2K5_ROOT");
        s_answer = !strcmp(t, "move") ? 'A' : 'B';
        {
            const char *root = nfl2k5_android_folder_boot();
            printf("[TEST] boot chose %s\n", root ? root : "(default)");
            if (!strcmp(t, "move")) {
                fails += check(root && !strcmp(root, "/storage/TEST-CARD/NFL2K5"), "now on the card");
                fails += check(sh("cmp /sdcard/NFL2K5/original/vc_53450030/a.bin /storage/TEST-CARD/NFL2K5/original/vc_53450030/a.bin") == 0,
                               "copy matches");
                fails += check(access("/sdcard/NFL2K5/original/default.xbe", R_OK) == 0, "old copy kept");
                fails += check(access("/storage/TEST-CARD/NFL2K5/nfl2k5.log", F_OK) != 0, "log not copied");
                fails += check(access("/storage/TEST-CARD/NFL2K5.part", F_OK) != 0, "no .part left");
                fails += check(access(POINTER, R_OK) == 0, "game-folder.txt written");
                fails += check(access(PENDING, F_OK) != 0, "the request is gone");
                root = nfl2k5_android_folder_boot();
                fails += check(root && !strcmp(root, "/storage/TEST-CARD/NFL2K5"), "next start uses the card without asking");
            } else {
                fails += check(root == NULL, "declined: default place");
                fails += check(access(POINTER, F_OK) != 0, "no pointer");
                fails += check(access(PENDING, F_OK) != 0, "asked once only");
            }
        }
    } else if (!strcmp(t, "missing")) {
        sh("rm -rf /sdcard /storage/GONE && mkdir -p /sdcard/NFL2K5 && printf 'path = /storage/GONE/NFL2K5\\n' > /sdcard/NFL2K5/game-folder.txt");
        fails += check(nfl2k5_android_folder_boot() == NULL, "a missing card falls back to internal");
        fails += check(s_notices == 1, "with a message");
    } else {
        fprintf(stderr, "usage: see the top of tests/android_storage_test.c\n");
        return 2;
    }
    printf("%s: %d failure(s)\n", t, fails);
    return fails ? 1 : 0;
}
