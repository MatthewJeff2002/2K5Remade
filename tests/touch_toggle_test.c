/* The Android touch controls' HIDE/SHOW button (2026-11-10), on any host.
 *
 * Builds src/platform/android/touch_pad.c as on a phone with SDL stubbed: a
 * clock the test moves, a controller that can be connected, scripted finger
 * events, and the Phone Settings values. Checks what is drawn: nothing while
 * a controller is used or the controls are Off, the SHOW button fading after
 * the player hid the controls, a touch bringing it back, a tap on it showing
 * the controls, Never with the three-finger tap, and Off -> Automatic by a
 * three-finger tap. */
#define __ANDROID__ 1
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Uint64 s_now = 1000;
static int s_pad, s_fingers_down, s_mode, s_btn, s_hidden;
static SDL_EventFilter s_watch;
bool SDL_HasGamepad(void) { return s_pad != 0; }
Uint64 SDL_GetTicks(void) { return s_now; }
void SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...) { va_list a; va_start(a, fmt); printf("[LOG] "); vprintf(fmt, a); printf("\n"); va_end(a); }
SDL_Mutex *SDL_CreateMutex(void) { return (SDL_Mutex *)1; }
void SDL_LockMutex(SDL_Mutex *m) { (void)m; }
void SDL_UnlockMutex(SDL_Mutex *m) { (void)m; }
bool SDL_SetHint(const char *n, const char *v) { (void)n; (void)v; return true; }
bool SDL_AddEventWatch(SDL_EventFilter f, void *u) { (void)u; s_watch = f; return true; }
SDL_Finger **SDL_GetTouchFingers(SDL_TouchID t, int *n) { (void)t; *n = s_fingers_down; return (SDL_Finger **)malloc(8); }
void SDL_free(void *p) { free(p); }

int nfl2k5_touch_mode(void) { return s_mode; }
void nfl2k5_touch_set_mode(int m) { s_mode = m; }
float nfl2k5_touch_scale(void) { return 1.0f; }
float nfl2k5_touch_alpha(void) { return 1.0f; }
int nfl2k5_in_match(void) { return 1; }
int nfl2k5_touch_button_mode(void) { return s_btn; }
int nfl2k5_touch_hidden(void) { return s_hidden; }
void nfl2k5_touch_pad_changed(void);
void nfl2k5_touch_set_hidden(int h) { s_hidden = h; nfl2k5_touch_pad_changed(); }

#include "../src/platform/android/touch_pad.c"
void xbox_PresentSetTopHudCallback(XboxHudCallback cb) { (void)cb; }

/* 0 nothing drawn, else 1 + (SHOW/HIDE drawn ? 2 : 0) + (controls drawn ? 4 : 0) */
static int screen(void)
{
    XboxHudFrame f = { 1920, 1080, 0, 0, 1920, 1080 };
    XboxHudImage img;
    int r;
    memset(&img, 0, sizeof img);
    r = hud(&f, &img);
    if (!r) return 0;
    {
        Rect t = btn_rect(C_TOGGLE);
        Circle a = face(C_A);
        uint32_t tp = s_img[(int)(t.y + t.h * 0.5f) * s_W + (int)(t.x + 2)];
        uint32_t ap = s_img[(int)a.y * s_W + (int)a.x];
        return 1 + ((tp >> 24) ? 2 : 0) + ((ap >> 24) ? 4 : 0);
    }
}

static void finger(int type, float x, float y, SDL_FingerID id)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.tfinger.x = x / (float)s_W; e.tfinger.y = y / (float)s_H; e.tfinger.fingerID = id;
    if (type == SDL_EVENT_FINGER_DOWN) s_fingers_down++;
    s_watch(NULL, &e);
    if (type == SDL_EVENT_FINGER_UP && s_fingers_down) s_fingers_down--;
}
static void tap(float x, float y) { finger(SDL_EVENT_FINGER_DOWN, x, y, 7); s_now += 80; finger(SDL_EVENT_FINGER_UP, x, y, 7); s_now += 50; }
static void tap_toggle(void) { Rect t = btn_rect(C_TOGGLE); tap(t.x + t.w * 0.5f, t.y + t.h * 0.5f); }
static void tap_empty(void) { tap(s_W * 0.6f, s_H * 0.55f); }
static void three(void)
{
    s_now += 1000;                                 /* two three-finger taps less than 0.8 s apart count once */
    finger(SDL_EVENT_FINGER_DOWN, 600, 300, 1); finger(SDL_EVENT_FINGER_DOWN, 700, 300, 2); finger(SDL_EVENT_FINGER_DOWN, 800, 300, 3);
    s_now += 100;
    finger(SDL_EVENT_FINGER_UP, 600, 300, 1); finger(SDL_EVENT_FINGER_UP, 700, 300, 2); finger(SDL_EVENT_FINGER_UP, 800, 300, 3);
    s_now += 50;
}
static void pad_press(void)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    s_watch(NULL, &e);
    s_now += 20;
}

static int fails;
static void expect(int got, int want, const char *what)
{
    int ok = got == want;
    if (!ok) fails++;
    printf("%s %s (screen %d, wanted %d)\n", ok ? "PASS" : "FAIL", what, got, want);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    nfl2k5_touch_pad_init();
    screen();
    /* 1 = drawn with neither, 3 = SHOW/HIDE only, 7 = controls and HIDE, 5 = controls only */
    s_pad = 1;
    expect(screen(), 0, "controller connected, nothing touched: nothing on screen");
    tap_empty();
    expect(screen(), 7, "Auto, the screen touched: controls and HIDE come up");
    pad_press();
    expect(screen(), 0, "the controller pressed again: nothing on screen");
    s_mode = 1;
    expect(screen(), 0, "On: still nothing over the game while the controller is used");
    tap_empty();
    expect(screen(), 7, "On, touched: controls and HIDE");
    s_pad = 0; s_mode = 0;
    tap_toggle();
    expect(s_hidden, 1, "HIDE hides (the player's choice, Touch Controls setting unchanged)");
    expect(s_mode, 0, "Touch Controls setting not changed by HIDE");
    expect(screen(), 3, "hidden: only SHOW");
    s_now += 3000;
    expect(screen(), 3, "hidden, 3 s idle: SHOW still there");
    s_now += 1300;
    { int sc = screen(); expect(sc == 3 && s_toggle_level > 0 && s_toggle_level < 8, 1, "4.3 s idle: SHOW fading"); }
    s_now += 1000;
    expect(screen(), 0, "5.3 s idle: SHOW gone, nothing on screen");
    tap_toggle();                                  /* where SHOW was: brings it back only */
    expect(s_hidden, 1, "a tap where the faded SHOW was does not show the controls");
    expect(screen(), 3, "...it brings SHOW back");
    tap_toggle();
    expect(s_hidden, 0, "tapping the visible SHOW shows the controls");
    expect(screen(), 7, "controls and HIDE");
    tap_toggle(); s_now += 6000;
    expect(screen(), 0, "hidden again and faded");
    tap_empty();
    expect(screen(), 3, "any touch of the empty screen brings SHOW back");
    s_btn = 1; s_now += 9000;
    expect(screen(), 3, "Always: SHOW does not fade");
    s_btn = 2;
    expect(screen(), 0, "Never: no SHOW");
    tap_empty();
    expect(screen(), 0, "Never: a tap shows nothing");
    three();
    expect(s_hidden, 0, "Never: three-finger tap shows the controls");
    expect(screen(), 5, "...controls, no HIDE button");
    three();
    expect(s_hidden, 1, "three-finger tap hides them again");
    s_btn = 0; s_hidden = 0; s_mode = 2;
    nfl2k5_touch_pad_changed();
    tap_empty();
    expect(screen(), 0, "Touch Controls Off: nothing, not even SHOW");
    three();
    expect(s_mode, 0, "Off + three-finger tap: back to Automatic");
    expect(screen(), 7, "...and the controls are up");
    printf("%d failure(s)\n", fails);
    return fails != 0;
}
