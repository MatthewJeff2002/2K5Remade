/* First start checks and the crash report (Windows desktop), 0.8.1.
 *
 * 0.8.0 players saw the game close with nothing on the screen when the disc
 * files were missing, when it ran from inside the .zip, from a folder it may
 * not write to, or when it crashed; a .2k5patch dropped on NFL2K5.exe made it
 * quit without a word. Here every one of those gets a message that says what
 * to do, and a crash leaves logs\crash-<time>.txt (+ .dmp) next to
 * session.log. NFL2K5_TEST_NO_DIALOGS=1 logs the messages instead of showing
 * them (automated tests). */
#if defined(_WIN32) && !defined(NFL2K5_UWP)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dbghelp.lib")

#ifndef NFL2K5_RELEASE_VERSION
#define NFL2K5_RELEASE_VERSION "0.8.1"
#endif

extern char *xbox_user_path(const char *rel, char *out, size_t n);

const char *nfl2k5_release_version(void) { return NFL2K5_RELEASE_VERSION; }

static int no_dialogs(void)
{
    const char *e = getenv("NFL2K5_TEST_NO_DIALOGS");
    return e && *e == '1';
}

/* A message box, or (tests) the text in the log. Returns the button. */
static int say(const wchar_t *text, UINT flags)
{
    if (no_dialogs()) {
        char u[2048];
        WideCharToMultiByte(CP_UTF8, 0, text, -1, u, sizeof u, NULL, NULL);
        fprintf(stderr, "[FIRSTRUN] message: %s\n", u);
        fflush(stderr);
        return (flags & 0xF) == MB_OK ? IDOK : IDCANCEL;
    }
    return MessageBoxW(NULL, text, L"ESPN NFL 2K5", flags | MB_TOPMOST | MB_SETFOREGROUND);
}

static void widen(const char *s, wchar_t *w, size_t n)
{
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, w, (int)n)) w[0] = 0;
}

static int wexists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static int wis_dir(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int ends_with_i(const wchar_t *s, const wchar_t *suf)
{
    size_t a = wcslen(s), b = wcslen(suf);
    return a >= b && !_wcsicmp(s + a - b, suf);
}

static const wchar_t *base_name(const wchar_t *p)
{
    const wchar_t *b = p, *q;
    for (q = p; *q; q++) if (*q == L'\\' || *q == L'/') b = q + 1;
    return b;
}

/* ---- the disc --------------------------------------------------------------
 * Needed once: <root>\original\disc\default.xbe. From a disc image the game
 * runs tools\extract-xiso.exe itself, in a window that shows its progress. */
static wchar_t s_iso_arg[MAX_PATH];          /* an .iso dropped on NFL2K5.exe */

static int extract_iso(const wchar_t *root, const wchar_t *iso)
{
    wchar_t tool[MAX_PATH], disc[MAX_PATH], orig[MAX_PATH], cmd[3 * MAX_PATH + 64], msg[1024];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    _snwprintf(tool, MAX_PATH, L"%ls\\tools\\extract-xiso.exe", root);
    _snwprintf(orig, MAX_PATH, L"%ls\\original", root);
    _snwprintf(disc, MAX_PATH, L"%ls\\original\\disc", root);
    if (!wexists(tool)) {
        _snwprintf(msg, 1024, L"The disc image cannot be unpacked: tools\\extract-xiso.exe is missing from\n%ls\n\n"
                              L"Unzip the whole NFL2K5-PC download again (all of its folders), then start NFL2K5.exe.", root);
        say(msg, MB_OK | MB_ICONERROR);
        return 0;
    }
    CreateDirectoryW(orig, NULL);
    CreateDirectoryW(disc, NULL);
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" -x -d \"%ls\" \"%ls\"", tool, disc, iso);
    fprintf(stderr, "[FIRSTRUN] extracting the disc image with tools\\extract-xiso.exe\n");
    fflush(stderr);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(tool, cmd, NULL, NULL, FALSE, no_dialogs() ? CREATE_NO_WINDOW : CREATE_NEW_CONSOLE,
                        NULL, root, &si, &pi)) {
        _snwprintf(msg, 1024, L"tools\\extract-xiso.exe could not start (Windows error %lu).", GetLastError());
        say(msg, MB_OK | MB_ICONERROR);
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    {   /* extract-xiso puts the files straight into -d; an older one made a
         * sub-folder named after the image: lift it up one level */
        wchar_t xbe[MAX_PATH], pat[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE h;
        _snwprintf(xbe, MAX_PATH, L"%ls\\default.xbe", disc);
        if (!wexists(xbe)) {
            _snwprintf(pat, MAX_PATH, L"%ls\\*", disc);
            h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    wchar_t sub[MAX_PATH], tmp[MAX_PATH], sx[MAX_PATH];
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                    _snwprintf(sub, MAX_PATH, L"%ls\\%ls", disc, fd.cFileName);
                    _snwprintf(sx, MAX_PATH, L"%ls\\default.xbe", sub);
                    if (!wexists(sx)) continue;
                    _snwprintf(tmp, MAX_PATH, L"%ls\\disc-unpacked", orig);
                    FindClose(h);
                    h = INVALID_HANDLE_VALUE;
                    if (MoveFileW(sub, tmp) && RemoveDirectoryW(disc)) MoveFileW(tmp, disc);
                    break;
                } while (FindNextFileW(h, &fd));
                if (h != INVALID_HANDLE_VALUE) FindClose(h);
            }
        }
        if (wexists(xbe)) {
            fprintf(stderr, "[FIRSTRUN] disc image extracted (extract-xiso exit %lu)\n", code);
            return 1;
        }
    }
    _snwprintf(msg, 1024, L"The disc image could not be unpacked (extract-xiso stopped with code %lu).\n\n"
                          L"Make sure it is a full dump of the USA ESPN NFL 2K5 Xbox disc (an .iso of about 4-7 GB), "
                          L"and that the drive has at least 8 GB free. Your image was not changed.", code);
    say(msg, MB_OK | MB_ICONERROR);
    return 0;
}

static int find_iso(const wchar_t *root, wchar_t *out)
{
    static const wchar_t *const dirs[] = { L"", L"\\original" };
    static const wchar_t *const pats[] = { L"*.iso", L"*.xiso" };
    int d, p;
    for (d = 0; d < 2; d++)
        for (p = 0; p < 2; p++) {
            wchar_t pat[MAX_PATH];
            WIN32_FIND_DATAW fd;
            HANDLE h;
            _snwprintf(pat, MAX_PATH, L"%ls%ls\\%ls", root, dirs[d], pats[p]);
            h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                _snwprintf(out, MAX_PATH, L"%ls%ls\\%ls", root, dirs[d], fd.cFileName);
                FindClose(h);
                return 1;
            }
        }
    return 0;
}

static int disc_check(const wchar_t *root)
{
    wchar_t xbe1[MAX_PATH], xbe2[MAX_PATH], iso[MAX_PATH], msg[2048];
    _snwprintf(xbe1, MAX_PATH, L"%ls\\original\\disc\\default.xbe", root);
    _snwprintf(xbe2, MAX_PATH, L"%ls\\original\\default.xbe", root);
    if (wexists(xbe1) || wexists(xbe2))
        return 1;
    fprintf(stderr, "[FIRSTRUN] no game disc: original\\disc\\default.xbe is missing\n");
    iso[0] = 0;
    if (s_iso_arg[0]) wcscpy(iso, s_iso_arg);
    else find_iso(root, iso);
    if (iso[0]) {
        _snwprintf(msg, 2048, L"The game's files from your NFL 2K5 disc are not installed yet.\n\n"
                              L"Found a disc image:\n%ls\n\n"
                              L"Unpack it into this game folder now? It takes a few minutes and needs about 8 GB "
                              L"of free space; a window shows the progress. The image itself is not changed.", iso);
        if (say(msg, MB_YESNO | MB_ICONQUESTION) == IDYES)
            return extract_iso(root, iso);
        return 0;
    }
    _snwprintf(msg, 2048,
               L"The game's files from your NFL 2K5 disc are not installed yet, so the game cannot start.\n\n"
               L"You need your own dump of the USA ESPN NFL 2K5 Xbox disc, as a disc image (.iso) or as the "
               L"extracted folder (the one with default.xbe and a vc_53450030 folder in it).\n\n"
               L"YES - choose your .iso now; the game unpacks it for you.\n"
               L"NO - open the folder where an already extracted disc goes:\n%ls\\original\\disc\n"
               L"(copy the CONTENTS of your extracted disc folder there, so default.xbe sits directly inside it).\n\n"
               L"HOW-TO-INSTALL.txt in the game folder explains both ways.", root);
    switch (say(msg, MB_YESNOCANCEL | MB_ICONINFORMATION)) {
    case IDYES: {
        OPENFILENAMEW of;
        memset(&of, 0, sizeof of);
        of.lStructSize = sizeof of;
        of.lpstrFilter = L"Xbox disc image (*.iso, *.xiso)\0*.iso;*.xiso\0All files\0*.*\0";
        of.lpstrFile = iso;
        of.nMaxFile = MAX_PATH;
        of.lpstrTitle = L"Your ESPN NFL 2K5 disc image";
        of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        iso[0] = 0;
        if (GetOpenFileNameW(&of))
            return extract_iso(root, iso);
        return 0;
    }
    case IDNO: {
        wchar_t d[MAX_PATH];
        _snwprintf(d, MAX_PATH, L"%ls\\original", root);
        CreateDirectoryW(d, NULL);
        _snwprintf(d, MAX_PATH, L"%ls\\original\\disc", root);
        CreateDirectoryW(d, NULL);
        ShellExecuteW(NULL, L"open", d, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }
    default:
        return 0;
    }
}

/* ---- files handed to NFL2K5.exe (dropped on it, or "Open with") ---------- */
int nfl2k5_first_run_args(const char *root8, int *argc, char **argv)
{
    int wargc = 0, i, out = 1;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    wchar_t root[MAX_PATH], msg[2048];
    widen(root8, root, MAX_PATH);
    for (i = 1; i < *argc; i++) {
        const wchar_t *w = wargv && i < wargc ? wargv[i] : NULL;
        if (argv[i][0] == '-' || !w || !wexists(w) || wis_dir(w)) {
            argv[out++] = argv[i];
            continue;
        }
        if (ends_with_i(w, L".2k5patch")) {
            wchar_t dir[MAX_PATH], dst[MAX_PATH];
            _snwprintf(dir, MAX_PATH, L"%ls\\mods", root);
            CreateDirectoryW(dir, NULL);
            _snwprintf(dir, MAX_PATH, L"%ls\\mods\\patches", root);
            CreateDirectoryW(dir, NULL);
            _snwprintf(dst, MAX_PATH, L"%ls\\%ls", dir, base_name(w));
            if (!_wcsicmp(w, dst) || CopyFileW(w, dst, FALSE)) {
                fprintf(stderr, "[FIRSTRUN] mod pack copied into mods\\patches\n");
                _snwprintf(msg, 2048, L"%ls is now in the game's mods\\patches folder.\n\n"
                                      L"The game installs it by itself: a few seconds after the menus appear a "
                                      L"message shows the progress (it takes a few minutes; you can keep playing). "
                                      L"When it says the pack is installed, quit and start the game again.",
                           base_name(w));
                say(msg, MB_OK | MB_ICONINFORMATION);
            } else {
                _snwprintf(msg, 2048, L"%ls could not be copied into\n%ls\n(Windows error %lu). Is the drive full?",
                           base_name(w), dir, GetLastError());
                say(msg, MB_OK | MB_ICONERROR);
            }
        } else if (ends_with_i(w, L".iso") || ends_with_i(w, L".xiso")) {
            wcsncpy(s_iso_arg, w, MAX_PATH - 1);     /* used by the disc check below when the disc is missing */
        } else {
            _snwprintf(msg, 2048, L"NFL2K5.exe does not open %ls.\n\nMod packs are .2k5patch files: drop one on "
                                  L"NFL2K5.exe, or put it in the game's mods\\patches folder.", base_name(w));
            say(msg, MB_OK | MB_ICONINFORMATION);
        }
    }
    argv[out] = NULL;
    *argc = out;
    if (wargv) LocalFree(wargv);
    return 0;
}

/* ---- the checks before anything starts -------------------------------------
 * 0: go on; otherwise the exit code (the player was told why). */
int nfl2k5_first_run_check(const char *root8)
{
    wchar_t root[MAX_PATH], exe[MAX_PATH], msg[2048], p[MAX_PATH];
    widen(root8, root, MAX_PATH);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    fprintf(stderr, "[BOOT] ESPN NFL 2K5 PC %s\n", NFL2K5_RELEASE_VERSION);
    {   /* started from inside the .zip: Windows runs it from a temporary folder */
        wchar_t low[MAX_PATH];
        size_t k;
        wcscpy(low, exe);
        for (k = 0; low[k]; k++) low[k] = (wchar_t)towlower(low[k]);
        if ((wcsstr(low, L"\\appdata\\local\\temp\\") && (wcsstr(low, L".zip") || wcsstr(low, L"\\temp1_") ||
                                                          wcsstr(low, L"\\rar$") || wcsstr(low, L"\\7z"))) ||
            wcsstr(low, L".zip\\")) {
            fprintf(stderr, "[FIRSTRUN] started from inside a .zip: %ls\n", exe);
            say(L"NFL2K5.exe was started from inside the .zip file, where it cannot run.\n\n"
                L"Right-click the downloaded .zip and choose \"Extract All...\", pick a folder you can write to "
                L"(Desktop, Documents, D:\\Games - not Program Files), then start NFL2K5.exe from the extracted "
                L"NFL2K5-PC folder.", MB_OK | MB_ICONWARNING);
            return 2;
        }
    }
    {   /* a folder the game may not write to (Program Files, a read-only copy) */
        HANDLE h;
        _snwprintf(p, MAX_PATH, L"%ls\\saves", root);
        CreateDirectoryW(p, NULL);
        _snwprintf(p, MAX_PATH, L"%ls\\saves\\.write-test", root);
        h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            fprintf(stderr, "[FIRSTRUN] the game folder cannot be written to (error %lu)\n", e);
            _snwprintf(msg, 2048, L"The game cannot save in its folder:\n%ls\n(Windows error %lu)\n\n"
                                  L"Saves, installed mod packs and settings live there. Move the whole NFL2K5-PC folder "
                                  L"somewhere you can write to - Desktop, Documents or D:\\Games, not Program Files - "
                                  L"and start NFL2K5.exe there.", root, e);
            say(msg, MB_OK | MB_ICONWARNING);
            return 2;
        }
        CloseHandle(h);
    }
    {   /* the release's own folders: a partial copy (only the exe) cannot run */
        static const wchar_t *const need[] = { L"assets", L"data", L"mods" };
        wchar_t missing[256] = L"";
        int i;
        for (i = 0; i < 3; i++) {
            _snwprintf(p, MAX_PATH, L"%ls\\%ls", root, need[i]);
            if (!wis_dir(p)) { if (missing[0]) wcscat(missing, L", "); wcscat(missing, need[i]); }
        }
        if (missing[0]) {
            fprintf(stderr, "[FIRSTRUN] release folders missing: %ls\n", missing);
            _snwprintf(msg, 2048, L"Some of the game's own folders are missing from\n%ls\n\nMissing: %ls\n\n"
                                  L"Unzip the whole NFL2K5-PC download again (every folder, not only NFL2K5.exe) "
                                  L"and start NFL2K5.exe from there. Your saves folder can be copied over.", root, missing);
            say(msg, MB_OK | MB_ICONWARNING);
        }
    }
    if (!disc_check(root))
        return 2;
    {   /* tests of the crash report: NFL2K5_TEST_CRASH=av|abort */
        const char *c = getenv("NFL2K5_TEST_CRASH");
        if (c && !strcmp(c, "abort")) abort();
        if (c && !strcmp(c, "av")) {
            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)(uintptr_t)8, NULL, 0, NULL);
            Sleep(INFINITE);
        }
    }
    return 0;
}

/* ---- crash report ------------------------------------------------------- */
static volatile LONG s_crashing;

static void log_tail(FILE *out)
{
    char lp[MAX_PATH], buf[8192];
    FILE *f;
    long sz;
    size_t n;
    fflush(stderr);
    f = fopen(xbox_user_path("logs\\session.log", lp, sizeof lp), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, sz > (long)sizeof buf ? sz - (long)sizeof buf : 0, SEEK_SET);
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    fprintf(out, "\n---- last lines of session.log ----\n");
    fwrite(buf, 1, n, out);
}

void nfl2k5_crash_report(EXCEPTION_POINTERS *ep, const char *what)
{
    char base[64], rel[96], txt[MAX_PATH], dmp[MAX_PATH], exe[MAX_PATH];
    wchar_t msg[1024], wtxt[MAX_PATH];
    SYSTEMTIME t;
    FILE *f;
    if (InterlockedExchange(&s_crashing, 1))
        return;
    GetLocalTime(&t);
    snprintf(base, sizeof base, "crash-%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    snprintf(rel, sizeof rel, "logs\\%s.txt", base);
    xbox_user_path(rel, txt, sizeof txt);
    snprintf(rel, sizeof rel, "logs\\%s.dmp", base);
    xbox_user_path(rel, dmp, sizeof dmp);
    GetModuleFileNameA(NULL, exe, sizeof exe);
    f = fopen(txt, "w");
    if (f) {
        OSVERSIONINFOEXW ov;
        typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOEXW *);
        RtlGetVersionFn rgv = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        memset(&ov, 0, sizeof ov);
        ov.dwOSVersionInfoSize = sizeof ov;
        if (rgv) rgv(&ov);
        fprintf(f, "ESPN NFL 2K5 PC %s crash report\n", NFL2K5_RELEASE_VERSION);
        fprintf(f, "time: %04u-%02u-%02u %02u:%02u:%02u\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        fprintf(f, "exe: %s\nWindows: %lu.%lu build %lu\n", exe, ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber);
        if (what) fprintf(f, "what: %s\n", what);
        if (ep && ep->ExceptionRecord) {
            DWORD64 rip = ep->ContextRecord ? ep->ContextRecord->Rip : 0;
            HMODULE mod = NULL;
            char mname[MAX_PATH] = "?";
            fprintf(f, "exception: 0x%08lX at 0x%llX\n", ep->ExceptionRecord->ExceptionCode, (unsigned long long)rip);
            if (rip && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          (LPCSTR)(uintptr_t)rip, &mod) && mod) {
                GetModuleFileNameA(mod, mname, sizeof mname);
                fprintf(f, "module: %s+0x%llX\n", mname, (unsigned long long)(rip - (DWORD64)(uintptr_t)mod));
            }
        }
        log_tail(f);
        fclose(f);
    }
    {   /* a small minidump (threads, stacks, modules) for whoever looks into it */
        HANDLE h = CreateFileA(dmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                              (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                              ep ? &mei : NULL, NULL, NULL);
            CloseHandle(h);
        }
    }
    fprintf(stderr, "[CRASH] report written: %s\n", txt);
    fflush(stderr);
    if (!MultiByteToWideChar(CP_ACP, 0, txt, -1, wtxt, MAX_PATH)) wcscpy(wtxt, L"logs\\crash-*.txt");
    _snwprintf(msg, 1024, L"ESPN NFL 2K5 stopped because of an error%hs%hs.\n\n"
                          L"A crash report was saved:\n%ls\n\n"
                          L"If you report this, please send that file, the .dmp file next to it and session.log "
                          L"from the same folder.", what ? ": " : "", what ? what : "", wtxt);
    say(msg, MB_OK | MB_ICONERROR);
}

static void __cdecl on_abort(int sig)
{
    (void)sig;
    nfl2k5_crash_report(NULL, "an internal check failed (abort)");
    TerminateProcess(GetCurrentProcess(), 3);
}

static void __cdecl on_purecall(void)
{
    nfl2k5_crash_report(NULL, "pure virtual function call");
    TerminateProcess(GetCurrentProcess(), 3);
}

static void __cdecl on_invalid_parameter(const wchar_t *expr, const wchar_t *fn, const wchar_t *file, unsigned line, uintptr_t r)
{
    static volatile LONG told;
    (void)expr; (void)fn; (void)file; (void)line; (void)r;
    /* the C library call reports an error and the game goes on */
    if (InterlockedIncrement(&told) <= 5)
        fprintf(stderr, "[CRASH] C library invalid parameter (ignored, the call failed)\n");
}

/* Until src/main.c installs its own (with guest registers), any crash in
 * start-up code is reported here too. */
static LONG CALLBACK early_filter(EXCEPTION_POINTERS *ep)
{
    nfl2k5_crash_report(ep, "during start-up");
    return EXCEPTION_EXECUTE_HANDLER;
}

void nfl2k5_crash_init(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(early_filter);
    signal(SIGABRT, on_abort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_purecall_handler(on_purecall);
    _set_invalid_parameter_handler(on_invalid_parameter);
}
#endif
