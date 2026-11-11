/* ── Where the game folder lives (Android, 2026-11-09) ──────────────────────
 *
 * Options > Phone Settings > Game Folder: internal storage (/sdcard/NFL2K5)
 * or an SD card (/storage/XXXX-XXXX/NFL2K5). A pick only takes note
 * (game-folder-move.txt); the move happens at the next start, before the game
 * runs, after the player confirms on a screen of its own (controller A / X /
 * B, or touch). The move copies every file, reads each copy back and checks
 * it against the original (CRC-32), and only then switches the game to the
 * new place (game-folder.txt in /sdcard/NFL2K5, which is also where the log
 * stays). Nothing is ever deleted: the old copy is kept, and the player can
 * delete it with a file manager once happy. A copy into an empty place goes
 * into <dest>.part first and is renamed at the end, so an interrupted copy is
 * never taken for a game folder.
 *
 * With All files access the app writes to /storage/XXXX-XXXX/NFL2K5. Where
 * Android refuses that (some phones, Android 10 and older) the card's app
 * folder (Android/data/com.nfl2k5.pc/files/NFL2K5) is used and the screen says
 * that Android deletes it with the app. */
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define INTERNAL "/sdcard/NFL2K5"
#define POINTER INTERNAL "/game-folder.txt"
#define PENDING INTERNAL "/game-folder-move.txt"
#define MAXVOL 4

extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
extern void nfl2k5_android_notice(const char *title, const char *msg);   /* xiso_extract.c */

static char s_vol[MAXVOL][256];        /* SD cards and USB drives: /storage/XXXX-XXXX */
static int s_nvol = -1;

static void volumes(void)
{
    const char *env = getenv("NFL2K5_VOLUMES");    /* from DeviceReport.java: getExternalFilesDirs */
    DIR *d;
    struct dirent *e;
    if (s_nvol >= 0) return;
    s_nvol = 0;
    while (env && *env && s_nvol < MAXVOL) {
        size_t l = strcspn(env, ";");
        if (l > 0 && l < sizeof s_vol[0]) { memcpy(s_vol[s_nvol], env, l); s_vol[s_nvol][l] = 0; s_nvol++; }
        env += l;
        if (*env == ';') env++;
    }
    if ((d = opendir("/storage"))) {
        while ((e = readdir(d)) && s_nvol < MAXVOL) {
            char p[300];
            int i, dup = 0;
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "emulated") || !strcmp(e->d_name, "self")) continue;
            snprintf(p, sizeof p, "/storage/%s", e->d_name);
            for (i = 0; i < s_nvol; i++) if (!strcmp(s_vol[i], p)) dup = 1;
            if (!dup && access(p, R_OK) == 0) snprintf(s_vol[s_nvol++], sizeof s_vol[0], "%s", p);
        }
        closedir(d);
    }
}

static void choice_path(int i, char *out, size_t n)
{
    volumes();
    if (i <= 0 || i > s_nvol) snprintf(out, n, "%s", INTERNAL);
    else snprintf(out, n, "%s/NFL2K5", s_vol[i - 1]);
}

static int read_line_file(const char *file, const char *key, char *out, size_t n)
{
    char line[1100];
    FILE *f = fopen(file, "r");
    size_t kl = strlen(key);
    out[0] = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, kl) || line[kl] != ' ' || line[kl + 1] != '=') continue;
        snprintf(out, n, "%s", line + kl + 2 + (line[kl + 2] == ' '));
        out[strcspn(out, "\r\n")] = 0;
    }
    fclose(f);
    return out[0] != 0;
}

/* Which choice a game folder path is (on the same card: its app folder too). */
static int choice_of(const char *path)
{
    int i;
    volumes();
    for (i = 0; i < s_nvol; i++)
        if (!strncmp(path, s_vol[i], strlen(s_vol[i])) && path[strlen(s_vol[i])] == '/') return i + 1;
    return 0;
}

int nfl2k5_folder_count(void) { volumes(); return 1 + s_nvol; }

int nfl2k5_folder_selected(void)
{
    char p[1024];
    if (read_line_file(PENDING, "to", p, sizeof p)) return choice_of(p);
    return getenv("NFL2K5_ROOT") ? choice_of(getenv("NFL2K5_ROOT")) : 0;
}

void nfl2k5_folder_name(int i, char *b, size_t n)
{
    char p[1024];
    int pending = read_line_file(PENDING, "to", p, sizeof p) && choice_of(p) == i;
    volumes();
    if (i <= 0 || i > s_nvol) snprintf(b, n, "%s", pending ? "Internal (next start)" : "Internal storage");
    else {
        const char *id = strrchr(s_vol[i - 1], '/') ? strrchr(s_vol[i - 1], '/') + 1 : s_vol[i - 1];
        snprintf(b, n, pending ? "SD %.12s (next start)" : "SD card %.12s", id);
    }
}

void nfl2k5_folder_choose(int i)
{
    char to[1024];
    const char *root = getenv("NFL2K5_ROOT");
    choice_path(i, to, sizeof to);
    if (root && choice_of(root) == i) {
        remove(PENDING);
        fprintf(stderr, "[FOLDER] stays at %s\n", root);
        xbox_PresentToast("GAME FOLDER", "No move: the game folder stays where it is", root, 4000);
        return;
    }
    {
        FILE *f = fopen(PENDING, "w");
        if (!f) { xbox_PresentToast("GAME FOLDER", "Could not note the move", strerror(errno), 5000); return; }
        fprintf(f, "; Options > Phone Settings > Game Folder: moved at the next start, after asking\nto = %s\n", to);
        fclose(f);
    }
    fprintf(stderr, "[FOLDER] move to %s requested (next start)\n", to);
    xbox_PresentToast("GAME FOLDER", "Close NFL 2K5 and open it again to move it.",
                      "You are asked first. Nothing is deleted.", 7000);
}

/* ---- the move ------------------------------------------------------------- */

static uint32_t s_crc_tab[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
    if (!s_crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            s_crc_tab[i] = v;
        }
    c = ~c;
    while (n--) c = s_crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Gamepad *pad[4];
    uint64_t total, done, files;
    Uint64 last_draw;
    char err[512];
} Move;

/* files that belong to this run or to the folder choice itself */
static int skip_name(const char *rel)
{
    return !strcmp(rel, "nfl2k5.log") || !strcmp(rel, "game-folder.txt") || !strcmp(rel, "game-folder-move.txt") ||
           !strncmp(rel, "previous-run-", 13);
}

static void mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0775); *p = '/'; }
    mkdir(tmp, 0775);
}

static void draw(Move *m, const char *title, const char *body, const char *buttons)
{
    int ww = 0, hh = 0, line = 0;
    const char *p = body;
    SDL_Event e;
    if (!m->ren) return;
    while (SDL_PollEvent(&e)) { }
    SDL_GetRenderOutputSize(m->ren, &ww, &hh);
    SDL_SetRenderScale(m->ren, 1, 1);
    SDL_SetRenderDrawColor(m->ren, 10, 14, 30, 255);
    SDL_RenderClear(m->ren);
    SDL_SetRenderScale(m->ren, 2.5f, 2.5f);
    SDL_SetRenderDrawColor(m->ren, 255, 200, 60, 255);
    SDL_RenderDebugText(m->ren, 20, 20, title);
    SDL_SetRenderDrawColor(m->ren, 255, 255, 255, 255);
    while (*p) {
        char buf[256];
        int cols = (int)(ww / 2.5f / 8) - 6, n = 0, last_space = -1;
        if (cols < 20) cols = 20;
        if (cols > 250) cols = 250;
        while (p[n] && p[n] != '\n' && n < cols) { if (p[n] == ' ') last_space = n; n++; }
        if (p[n] && p[n] != '\n' && last_space > 0) n = last_space;
        memcpy(buf, p, (size_t)n); buf[n] = 0;
        SDL_RenderDebugText(m->ren, 20, 50 + line++ * 12, buf);
        p += n;
        if (*p == '\n' || *p == ' ') p++;
    }
    if (m->total) {
        float w = ww / 2.5f - 40, f = (float)((double)m->done / (double)m->total);
        SDL_FRect r = { 20, hh / 2.5f - 60, w, 14 };
        SDL_SetRenderDrawColor(m->ren, 60, 60, 80, 255);
        SDL_RenderFillRect(m->ren, &r);
        r.w = w * (f > 1 ? 1 : f);
        SDL_SetRenderDrawColor(m->ren, 80, 200, 120, 255);
        SDL_RenderFillRect(m->ren, &r);
    }
    if (buttons) {
        SDL_SetRenderDrawColor(m->ren, 160, 200, 255, 255);
        SDL_RenderDebugText(m->ren, 20, hh / 2.5f - 30, buttons);
    }
    SDL_RenderPresent(m->ren);
}

/* 'A', 'X' or 'B' from a controller, the keyboard or a tap on a third of
 * the screen (left A, middle X, right B); 'B' also for the system Back. */
static int wait_choice(Move *m, int allow_x)
{
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                int i;
                for (i = 0; i < 4 && m->pad[i]; i++) {}
                if (i < 4) m->pad[i] = SDL_OpenGamepad(e.gdevice.which);
            } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) return 'A';
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) return 'B';
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST && allow_x) return 'X';
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_AC_BACK || e.key.scancode == SDL_SCANCODE_ESCAPE ||
                    e.key.scancode == SDL_SCANCODE_B) return 'B';
                if (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_A) return 'A';
                if (e.key.scancode == SDL_SCANCODE_X && allow_x) return 'X';
            } else if (e.type == SDL_EVENT_FINGER_UP) {
                if (e.tfinger.y > 0.6f) {
                    if (e.tfinger.x < 0.33f) return 'A';
                    if (e.tfinger.x < 0.66f) { if (allow_x) return 'X'; }
                    else return 'B';
                }
            } else if (e.type == SDL_EVENT_QUIT) return 'B';
        }
        SDL_Delay(30);
    }
}

static void tree_size(const char *dir, const char *rel, uint64_t *bytes, uint64_t *files)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char p[1024], r[1024];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) tree_size(p, r, bytes, files);
        else if (!skip_name(r)) { *bytes += (uint64_t)st.st_size; (*files)++; }
    }
    closedir(d);
}

static int dir_empty(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int empty = 1;
    if (!d) return 1;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { empty = 0; break; }
    closedir(d);
    return empty;
}

static int copy_file(Move *m, const char *src, const char *dst)
{
    static uint8_t buf[1 << 20];
    FILE *in = fopen(src, "rb"), *out;
    uint32_t c1 = 0, c2 = 0;
    size_t n;
    if (!in) { snprintf(m->err, sizeof m->err, "cannot read %s (%s)", src, strerror(errno)); return 0; }
    if (!(out = fopen(dst, "wb"))) {
        snprintf(m->err, sizeof m->err, "cannot write %s (%s)", dst, strerror(errno));
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        c1 = crc32_update(c1, buf, n);
        if (fwrite(buf, 1, n, out) != n) {
            snprintf(m->err, sizeof m->err, "writing %s failed (%s): is the card full?", dst, strerror(errno));
            fclose(in); fclose(out);
            return 0;
        }
        m->done += n;
        if (SDL_GetTicks() - m->last_draw > 250) {
            char body[700];
            m->last_draw = SDL_GetTicks();
            snprintf(body, sizeof body, "Copying and checking every file. Keep the app open.\n\n%.1f of %.1f GB\n\n%s",
                     m->done / 1e9, m->total / 1e9, dst);
            draw(m, "NFL 2K5: moving the game folder", body, NULL);
        }
    }
    fclose(in);
    if (fflush(out) || fsync(fileno(out)) || fclose(out)) {
        snprintf(m->err, sizeof m->err, "finishing %s failed (%s)", dst, strerror(errno));
        return 0;
    }
    /* read the copy back: the card itself must hold what was written */
    if (!(in = fopen(dst, "rb"))) { snprintf(m->err, sizeof m->err, "cannot read back %s", dst); return 0; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) c2 = crc32_update(c2, buf, n);
    fclose(in);
    if (c1 != c2) { snprintf(m->err, sizeof m->err, "the copy of %s does not match the original", src); return 0; }
    m->files++;
    return 1;
}

static int copy_tree(Move *m, const char *src, const char *dst, const char *rel)
{
    DIR *d = opendir(src);
    struct dirent *e;
    int ok = 1;
    if (!d) { snprintf(m->err, sizeof m->err, "cannot open %s", src); return 0; }
    mkdirs(dst);
    while (ok && (e = readdir(d))) {
        char sp[1024], dp[1024], r[1024];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(sp, sizeof sp, "%s/%s", src, e->d_name);
        snprintf(dp, sizeof dp, "%s/%s", dst, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        if (stat(sp, &st)) continue;
        if (S_ISDIR(st.st_mode)) ok = copy_tree(m, sp, dp, r);
        else if (!skip_name(r)) ok = copy_file(m, sp, dp);
    }
    closedir(d);
    return ok;
}

static int writable(const char *dir)
{
    char t[1100];
    FILE *f;
    mkdirs(dir);
    snprintf(t, sizeof t, "%s/.nfl2k5-write-test", dir);
    if (!(f = fopen(t, "w"))) return 0;
    fclose(f);
    remove(t);       /* our own empty test file */
    return 1;
}

/* Where the game's data is now (before any choice was made): the folder
 * src/main.c android_pick_root would take. */
static void current_root(char *out, size_t n)
{
    char probe[1100];
    const char *app = SDL_GetAndroidExternalStoragePath();
    if (read_line_file(POINTER, "path", out, n)) return;
    snprintf(probe, sizeof probe, "%s/original/default.xbe", app ? app : "");
    if (app && access(probe, R_OK) == 0) { snprintf(out, n, "%s", app); return; }
    snprintf(out, n, "%s", INTERNAL);
}

static void set_pointer(const char *to)
{
    if (!strcmp(to, INTERNAL)) { remove(POINTER); return; }
    {
        FILE *f = fopen(POINTER, "w");
        if (!f) return;
        fprintf(f, "; The game folder (Options > Phone Settings > Game Folder). Delete this file to use\n"
                   "; Internal storage/NFL2K5 again.\npath = %s\n", to);
        fclose(f);
    }
}

static const char *do_move(const char *from, const char *want)
{
    static char to[1024];
    char body[2048], part[1100];
    Move m;
    struct statvfs vf;
    uint64_t freeb = 0;
    int empty, c, app_folder = 0;
    memset(&m, 0, sizeof m);
    snprintf(to, sizeof to, "%s", want);
    if (!writable(to)) {
        /* the card's own app folder: always allowed, but removed with the app */
        int v = choice_of(to);
        if (v > 0) {
            snprintf(to, sizeof to, "%s/Android/data/com.nfl2k5.pc/files/NFL2K5", s_vol[v - 1]);
            app_folder = 1;
        }
        if (!v || !writable(to)) {
            snprintf(body, sizeof body, "Android does not let NFL 2K5 write to %s. The game folder stays at %s.\n\n"
                     "Check that NFL 2K5 has All files access (Settings > Apps > NFL 2K5 > Permissions).", want, from);
            nfl2k5_android_notice("NFL 2K5: game folder not moved", body);
            return NULL;
        }
    }
    empty = dir_empty(to);
    tree_size(from, "", &m.total, &m.files);
    if (statvfs(to, &vf) == 0) freeb = (uint64_t)vf.f_bavail * vf.f_frsize;
    fprintf(stderr, "[FOLDER] move %s -> %s: %.2f GB in %llu files, %.2f GB free there, %s\n", from, to, m.total / 1e9,
            (unsigned long long)m.files, freeb / 1e9, empty ? "empty" : "not empty");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        SDL_CreateWindowAndRenderer("NFL 2K5", 1280, 720, SDL_WINDOW_FULLSCREEN, &m.win, &m.ren);
    if (!m.ren) {
        fprintf(stderr, "[FOLDER] no window for the question (%s): not moved\n", SDL_GetError());
        return NULL;
    }
    {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        for (int i = 0; ids && i < n && i < 4; i++) m.pad[i] = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    if (empty) {
        snprintf(body, sizeof body, "Move the game folder?\n\nFrom: %s\nTo:   %s\n\n%.1f GB to copy, %.1f GB free there.%s\n\n"
                 "Every file is copied and checked. The old folder is kept: nothing is deleted.",
                 from, to, m.total / 1e9, freeb / 1e9,
                 app_folder ? "\n\nAndroid only allows the app's own folder on this card: uninstalling NFL 2K5 deletes it." : "");
        draw(&m, "NFL 2K5: move the game folder", body, "A: copy and switch      B: keep it where it is");
        c = wait_choice(&m, 0);
    } else {
        snprintf(body, sizeof body, "%s already has files.\n\n"
                 "A: use that folder as it is (its own game files, saves and settings)\n"
                 "X: copy this folder's files over it (files with the same name are replaced)\n"
                 "B: keep the game folder where it is (%s)",
                 to, from);
        draw(&m, "NFL 2K5: game folder", body, "A: use it      X: copy over it      B: keep");
        c = wait_choice(&m, 1);
    }
    remove(PENDING);                       /* asked once: no loop if anything below fails */
    if (c == 'B') {
        fprintf(stderr, "[FOLDER] move declined: stays at %s\n", from);
        goto close_keep;
    }
    if (c == 'A' && !empty) {
        fprintf(stderr, "[FOLDER] switched to %s without copying\n", to);
        set_pointer(to);
        goto close_to;
    }
    if (m.total + (64ull << 20) > freeb) {
        snprintf(body, sizeof body, "Not enough space: %.1f GB needed, %.1f GB free on %s. Nothing was changed.",
                 m.total / 1e9, freeb / 1e9, to);
        draw(&m, "NFL 2K5: game folder not moved", body, "Press A or tap to continue");
        wait_choice(&m, 0);
        goto close_keep;
    }
    m.files = 0;
    if (empty) {
        snprintf(part, sizeof part, "%s.part", to);
        mkdirs(part);
        if (!copy_tree(&m, from, part, "")) goto failed;
        rmdir(to);                         /* the empty folder made for the write test */
        if (rename(part, to) != 0) {
            snprintf(m.err, sizeof m.err, "renaming %s failed (%s)", part, strerror(errno));
            goto failed;
        }
    } else if (!copy_tree(&m, from, to, "")) goto failed;
    set_pointer(to);
    fprintf(stderr, "[FOLDER] copied and checked %llu files (%.2f GB) into %s; old folder kept: %s\n",
            (unsigned long long)m.files, m.done / 1e9, to, from);
    snprintf(body, sizeof body, "Done: %llu files copied and checked. The game now uses %s.\n\n"
             "The old folder %s was kept. When you are happy, you can delete it with a file manager to free the space.",
             (unsigned long long)m.files, to, from);
    m.total = 0;
    draw(&m, "NFL 2K5: game folder moved", body, "Press A or tap to start the game");
    wait_choice(&m, 0);
close_to:
    for (int i = 0; i < 4; i++) if (m.pad[i]) SDL_CloseGamepad(m.pad[i]);
    SDL_DestroyRenderer(m.ren);
    SDL_DestroyWindow(m.win);
    return to;
failed:
    fprintf(stderr, "[FOLDER] move failed: %s; stays at %s\n", m.err, from);
    snprintf(body, sizeof body, "The move stopped: %s.\n\nThe game folder stays at %s; nothing there was changed. "
             "Files already copied are in %s%s and can be deleted.", m.err, from, to, empty ? ".part" : "");
    m.total = 0;
    draw(&m, "NFL 2K5: game folder not moved", body, "Press A or tap to continue");
    wait_choice(&m, 0);
close_keep:
    for (int i = 0; i < 4; i++) if (m.pad[i]) SDL_CloseGamepad(m.pad[i]);
    SDL_DestroyRenderer(m.ren);
    SDL_DestroyWindow(m.win);
    return NULL;
}

/* From src/main.c android_pick_root, before anything uses the folder: a
 * waiting move (asked first), then the chosen folder, or NULL for the usual
 * places. */
const char *nfl2k5_android_folder_boot(void)
{
    static char root[1024];
    char want[1024], from[1024];
    current_root(from, sizeof from);
    if (read_line_file(PENDING, "to", want, sizeof want)) {
        if (!strcmp(want, from)) remove(PENDING);
        else {
            const char *to = do_move(from, want);
            if (to) {
                snprintf(root, sizeof root, "%s", to);
                return strcmp(root, INTERNAL) ? root : NULL;
            }
        }
    }
    if (!read_line_file(POINTER, "path", root, sizeof root)) return NULL;
    {
        char probe[1100];
        snprintf(probe, sizeof probe, "%s/.", root);
        if (access(probe, R_OK | W_OK) != 0) {
            char msg[1400];
            snprintf(msg, sizeof msg, "The game folder is set to %s, which is not there now (SD card taken out?).\n\n"
                     "This start uses Internal storage/NFL2K5 instead. Put the card back and open the game again "
                     "to use it, or choose Internal storage under Options > Phone Settings > Game Folder.", root);
            fprintf(stderr, "[FOLDER] %s is missing: internal storage this time\n", root);
            nfl2k5_android_notice("NFL 2K5: SD card not found", msg);
            return NULL;
        }
    }
    fprintf(stderr, "[FOLDER] game folder from game-folder.txt: %s\n", root);
    return root;
}
#endif
