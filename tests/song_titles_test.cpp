// Song titles test (src/nfl2k5_song_titles.inc.cpp): the file-name clean-up on real-world style
// names, and the tag readers on files given on the command line.
//   cl /std:c++17 /EHsc /utf-8 /I src tests\song_titles_test.cpp
//   song_titles_test.exe [file ...]     prints "title | artist | album" of each file's tags
// Exit code 0 = every name case passed.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
namespace fs = std::filesystem;

static std::string u8(const fs::path &p) { auto s = p.u8string(); return std::string(s.begin(), s.end()); }
static FILE *open_path(const fs::path &p, const char *mode)
{
#ifdef _WIN32
    wchar_t wm[8]; size_t i = 0;
    for (; mode[i] && i + 1 < 8; i++) wm[i] = (wchar_t)mode[i];
    wm[i] = 0;
    return _wfopen(p.c_str(), wm);
#else
    return fopen(p.c_str(), mode);
#endif
}
static std::u16string u8_to_u16(const std::string &s)
{
    std::u16string out;
    size_t i = 0, n = s.size();
    while (i < n) {
        uint32_t cp; uint8_t c = (uint8_t)s[i++];
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0 && i < n) cp = ((c & 0x1F) << 6) | ((uint8_t)s[i++] & 0x3F);
        else if ((c & 0xF0) == 0xE0 && i + 1 < n) { cp = ((c & 0x0F) << 12) | (((uint8_t)s[i] & 0x3F) << 6) | ((uint8_t)s[i + 1] & 0x3F); i += 2; }
        else if ((c & 0xF8) == 0xF0 && i + 2 < n) { cp = ((c & 0x07) << 18) | (((uint8_t)s[i] & 0x3F) << 12) | (((uint8_t)s[i + 1] & 0x3F) << 6) | ((uint8_t)s[i + 2] & 0x3F); i += 3; }
        else cp = '?';
        if (cp <= 0xFFFF) out.push_back((char16_t)cp);
        else { cp -= 0x10000; out.push_back((char16_t)(0xD800 + (cp >> 10))); out.push_back((char16_t)(0xDC00 + (cp & 0x3FF))); }
    }
    return out;
}
static std::string to8(const std::u16string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size()) { c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00); }
        if (c < 0x80) o.push_back((char)c);
        else if (c < 0x800) { o.push_back((char)(0xC0 | c >> 6)); o.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { o.push_back((char)(0xE0 | c >> 12)); o.push_back((char)(0x80 | (c >> 6 & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
        else { o.push_back((char)(0xF0 | c >> 18)); o.push_back((char)(0x80 | (c >> 12 & 0x3F))); o.push_back((char)(0x80 | (c >> 6 & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return o;
}

#include "nfl2k5_song_titles.inc.cpp"

struct Case { const char *file, *folder, *hint_tags, *want; };

int main(int argc, char **argv)
{
    static const Case cases[] = {
        /* the user's example: no separators; the folder names the artist and album */
        { "1982 Subdivisions Rush-Signals", "Rush - Signals", "", "Subdivisions" },
        { "1982 Subdivisions Rush-Signals", "Rush", "Rush|Signals", "Subdivisions" },   /* album from other files' tags */
        { "1982 Subdivisions Rush-Signals", "Mixed", "", "Subdivisions Rush-Signals" },  /* no hints: kept whole (year off) */
        { "01 - Tom Sawyer", "Rush", "", "Tom Sawyer" },
        { "01. Tom Sawyer", "Rush", "", "Tom Sawyer" },
        { "1-03 Limelight", "Rush", "", "Limelight" },
        { "07 Red Barchetta", "Rush", "", "Red Barchetta" },
        { "Rush - The Spirit Of Radio", "Classic Rock", "", "Rush - The Spirit Of Radio" },   /* neither part known: both stay */
        { "Rush - The Spirit Of Radio", "Classic Rock", "Rush", "The Spirit Of Radio" },
        { "The Spirit Of Radio - Rush", "Rush", "", "The Spirit Of Radio" },           /* artist known: the other part */
        { "03 - Rush - Moving Pictures - YYZ", "Rush", "", "YYZ" },
        { "Bohemian_Rhapsody_(Official_Video)", "Queen", "", "Bohemian Rhapsody" },
        { "Queen - Bohemian Rhapsody (Remastered 2011)", "Queen", "", "Bohemian Rhapsody" },
        { "Daft Punk - Get Lucky [Official Audio] [5NV6Rdv1a3I]", "Pop", "", "Daft Punk - Get Lucky" },
        { "Daft Punk - Get Lucky [Official Audio] [5NV6Rdv1a3I]", "Pop", "Daft Punk", "Get Lucky" },
        { "Africa  (Lyrics)", "Toto", "", "Africa" },
        { "(Don't Fear) The Reaper", "Blue Oyster Cult", "", "(Don't Fear) The Reaper" },
        { "05 - 1979", "Smashing Pumpkins", "", "1979" },                                /* never to nothing */
        { "1999", "Prince", "", "1999" },
        { "02 Signals", "Rush - Signals", "", "Signals" },                               /* the title is the album */
        { "The Rush", "Rush", "", "The Rush" },                                          /* lone word: stays */
        { "Beyonc\xc3\xa9 - Halo", "R&B", "Beyonce", "Halo" },                          /* accents folded for the match */
        { "Sigur R\xc3\xb3s - Hopp\xc3\xadpolla", "Iceland", "Sigur Ros", "Hopp\xc3\xadpolla" },
        { "Beyonc\xc3\xa9 - Halo", "R&B", "", "Beyonc\xc3\xa9 - Halo" },               /* neither known: both stay */
        { "09_-_Walk_This_Way_-_Aerosmith", "Downloads", "", "Walk This Way - Aerosmith" },
        { "Lose Yourself - Eminem - 8 Mile", "Eminem", "8 Mile", "Lose Yourself" },
        { "09_-_Walk_This_Way_-_Aerosmith", "Aerosmith", "", "Walk This Way" },
        { "Hey Jude (feat. Nobody)", "Beatles", "", "Hey Jude (feat. Nobody)" },
        { "Highway to Hell - AC-DC - Highway to Hell", "AC-DC", "Highway to Hell", "Highway to Hell" },
    };
    int fail = 0;
    for (const Case &c : cases) {
        std::vector<std::u16string> hints;
        std::string f = c.folder;
        add_hint(hints, u8_to_u16(f));
        std::string ht = c.hint_tags;
        for (size_t a = 0; a <= ht.size();) {
            size_t e = ht.find('|', a);
            if (e == std::string::npos) e = ht.size();
            if (e > a) add_hint(hints, u8_to_u16(ht.substr(a, e - a)));
            a = e + 1;
        }
        std::u16string artist;
        std::string got = to8(clean_name(u8_to_u16(c.file), hints, &artist));
        bool ok = got == c.want;
        if (!ok) fail++;
        printf("%s  %-52s [%s] -> \"%s\"%s\n", ok ? "PASS" : "FAIL", c.file, c.folder, got.c_str(),
               ok ? "" : (std::string("  (want \"") + c.want + "\")").c_str());
    }
    for (int i = 1; i < argc; i++) {
        fs::path p = fs::u8path(argv[i]);
        Tags t = read_file_tags(p);
        printf("TAGS  %-40s title \"%s\" | artist \"%s\" | album \"%s\"\n", u8(p.filename()).c_str(),
               to8(t.title).c_str(), to8(t.artist).c_str(), to8(t.album).c_str());
    }
    printf("%d of %d name cases failed\n", fail, (int)(sizeof cases / sizeof cases[0]));
    return fail ? 1 : 0;
}
