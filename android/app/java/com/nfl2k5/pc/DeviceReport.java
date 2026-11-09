package com.nfl2k5.pc;

import android.app.Activity;
import android.app.ActivityManager;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;

/* What a player's log needs to explain a crash on a phone we do not have
 * (2026-11-09: Ayn Thor and Retroid Pocket 6 reports). Runs in onCreate,
 * before the game:
 *  - the device, its chip, Android version, memory and displays, into the
 *    environment (NFL2K5_DEVICE, NFL2K5_DEVICE_PROFILE, NFL2K5_VOLUMES) for
 *    src/main.c and src/platform/android/android_settings.c, which log it;
 *  - how the previous run ended, from Android's own record (Android 11+:
 *    crash, native crash, killed for memory, ANR, ...), with the system's
 *    crash report (tombstone) when there is one, into previous-run-exit.txt
 *    in the app's own folder; the game copies it beside nfl2k5.log. */
final class DeviceReport {
    private DeviceReport() { }

    static String reasonName(int r) {
        switch (r) {
        case 1: return "exited by itself";
        case 2: return "killed by a signal";
        case 3: return "killed for low memory";
        case 4: return "crashed (Java)";
        case 5: return "crashed (native code)";
        case 6: return "not responding (ANR)";
        case 7: return "failed to start";
        case 8: return "permission change";
        case 9: return "excessive resource use";
        case 10: return "closed by the user";
        case 11: return "user stopped";
        case 12: return "dependency died";
        case 13: return "other";
        case 14: return "frozen";
        case 15: return "package state change";
        case 16: return "package updated";
        default: return "unknown (" + r + ")";
        }
    }

    /* Devices that get the graphics safety profile (one pipeline compile at a
     * time, a 60 Hz display mode): Snapdragon 8 Gen 2 and newer (Adreno 740+)
     * and the Retroid / AYN handhelds. Both reports came from such devices
     * while the Adreno 730 S22+ ran without a crash. */
    static String profile() {
        String m = (Build.MANUFACTURER + " " + Build.BRAND + " " + Build.MODEL).toLowerCase();
        String soc = Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "";
        if (soc == null) soc = "";
        soc = soc.toUpperCase();
        if (m.contains("retroid")) return "retroid";
        if (m.contains("ayn")) return "ayn";
        if (soc.startsWith("SM8550") || soc.startsWith("SM8650") || soc.startsWith("SM8635") ||
            soc.startsWith("SM8750") || soc.startsWith("SM8735") || soc.startsWith("SM7675") ||
            soc.startsWith("QCS8550") || soc.startsWith("SM8845") || soc.startsWith("SM8850"))
            return "adreno7-new";
        return "";
    }

    private static void env(String k, String v) {
        try { android.system.Os.setenv(k, v == null ? "" : v, true); } catch (Exception e) { }
    }

    static void run(Activity a) {
        StringBuilder dev = new StringBuilder();
        String soc = "";
        if (Build.VERSION.SDK_INT >= 31) soc = Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL;
        dev.append(Build.MANUFACTURER).append(" | ").append(Build.BRAND).append(" | ").append(Build.MODEL)
           .append(" | ").append(Build.DEVICE).append(" | hardware ").append(Build.HARDWARE)
           .append(" | SoC ").append(soc.trim().isEmpty() ? Build.BOARD : soc)
           .append(" | Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")");
        try {
            ActivityManager am = (ActivityManager) a.getSystemService(Activity.ACTIVITY_SERVICE);
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            dev.append(" | RAM ").append(mi.totalMem >> 20).append(" MB, free ").append(mi.availMem >> 20)
               .append(" MB, low-memory line ").append(mi.threshold >> 20).append(" MB")
               .append(mi.lowMemory ? " (LOW NOW)" : "");
        } catch (Exception e) { }
        try {
            android.hardware.display.DisplayManager dm =
                (android.hardware.display.DisplayManager) a.getSystemService(Activity.DISPLAY_SERVICE);
            for (android.view.Display d : dm.getDisplays()) {
                android.view.Display.Mode cur = d.getMode();
                dev.append(" | display ").append(d.getDisplayId()).append(" \"").append(d.getName()).append("\" ")
                   .append(cur.getPhysicalWidth()).append("x").append(cur.getPhysicalHeight()).append("@")
                   .append(Math.round(cur.getRefreshRate())).append(" Hz (modes:");
                for (android.view.Display.Mode md : d.getSupportedModes())
                    dev.append(" ").append(md.getPhysicalWidth()).append("x").append(md.getPhysicalHeight())
                       .append("@").append(Math.round(md.getRefreshRate()));
                dev.append(")");
            }
        } catch (Exception e) { }
        env("NFL2K5_DEVICE", dev.toString());
        env("NFL2K5_DEVICE_PROFILE", profile());
        try {
            StringBuilder vols = new StringBuilder();
            File[] dirs = a.getExternalFilesDirs(null);
            for (int i = 1; dirs != null && i < dirs.length; i++) {
                if (dirs[i] == null) continue;
                String p = dirs[i].getPath();
                int cut = p.indexOf("/Android/data/");
                if (cut > 0) p = p.substring(0, cut);
                if (vols.length() > 0) vols.append(';');
                vols.append(p);
            }
            env("NFL2K5_VOLUMES", vols.toString());
        } catch (Exception e) { }
        if (Build.VERSION.SDK_INT >= 30) lastExit(a);
    }

    private static void lastExit(Activity a) {
        File dir = a.getExternalFilesDir(null);
        if (dir == null) return;
        File txt = new File(dir, "previous-run-exit.txt");
        try {
            ActivityManager am = (ActivityManager) a.getSystemService(Activity.ACTIVITY_SERVICE);
            java.util.List<android.app.ApplicationExitInfo> list =
                am.getHistoricalProcessExitReasons(a.getPackageName(), 0, 3);
            if (list == null || list.isEmpty()) { txt.delete(); return; }
            android.app.ApplicationExitInfo last = list.get(0);
            env("NFL2K5_LAST_EXIT", last.getReason() + "|" + reasonName(last.getReason()) + "|" +
                (last.getDescription() != null ? last.getDescription() : ""));
            try (PrintWriter w = new PrintWriter(new FileOutputStream(txt))) {
                for (int i = 0; i < list.size(); i++) {
                    android.app.ApplicationExitInfo e = list.get(i);
                    w.println((i == 0 ? "Previous run" : "Run before that (" + i + ")") + ": " + reasonName(e.getReason()) +
                              " at " + new java.util.Date(e.getTimestamp()) + ", status " + e.getStatus() +
                              ", memory PSS " + (e.getPss() >> 10) + " MB RSS " + (e.getRss() >> 10) + " MB" +
                              (e.getDescription() != null ? ", \"" + e.getDescription() + "\"" : ""));
                }
                /* The system's crash report of the last run: for a native crash a
                 * tombstone (protocol buffer, its text pieces are listed), for an
                 * ANR the threads' stacks as text. */
                int r = last.getReason();
                if (r == 5 || r == 6 || r == 4) {
                    try (InputStream in = last.getTraceInputStream()) {
                        if (in != null) {
                            File raw = new File(dir, "previous-run-trace.bin");
                            byte[] b = new byte[1 << 16];
                            int n, total = 0;
                            StringBuilder cur = new StringBuilder();
                            int lines = 0;
                            w.println();
                            w.println("System crash report (text found in it):");
                            try (OutputStream out = new FileOutputStream(raw)) {
                                while ((n = in.read(b)) > 0 && total < (8 << 20)) {
                                    out.write(b, 0, n);
                                    total += n;
                                    for (int k = 0; k < n; k++) {
                                        int c = b[k] & 0xFF;
                                        if (c >= 32 && c < 127) { cur.append((char) c); continue; }
                                        if (cur.length() >= 5 && lines < 4000) { w.println(cur); lines++; }
                                        cur.setLength(0);
                                    }
                                }
                            }
                            if (cur.length() >= 5) w.println(cur);
                        }
                    } catch (Exception e) {
                        w.println("(no system crash report: " + e + ")");
                    }
                }
            }
        } catch (Exception e) { }
    }
}
