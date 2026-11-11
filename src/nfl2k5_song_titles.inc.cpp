/* Song titles: tag readers and the file-name clean-up for the music library
 * (included by src/nfl2k5_local_music.cpp, and by tests/song_titles_test.cpp).
 * Needs, before the #include: <filesystem> as fs, u8(path) -> UTF-8 std::string,
 * u8_to_u16(std::string), open_path(path, mode). */
/* ── Song titles (2026-11-11) ───────────────────────────────────────────────
 * "make it shorten the song title, like turn something like '1982 Subdivisions
 * Rush-Signals' to just 'Subdivisions'". A song's short title, in this order:
 *
 *  1. Music\titles.txt, the player's own: one "file name = Short Title" per line
 *     (the name with or without its extension, or its path under Music with '/';
 *     '#' starts a comment). It wins over everything.
 *  2. The file's own title tag, read at scan time from the header only: ID3v2.2/
 *     2.3/2.4 (any text encoding) and ID3v1 for mp3, Vorbis comments for ogg/opus/
 *     flac, the MP4 ilst atoms for m4a/mp4/aac, RIFF INFO for wav. The artist and
 *     album tags are kept too (the "Title - Artist" of Now Playing, and hints).
 *  3. The file name, cleaned (clean_name): underscores to spaces, repeated
 *     spaces once; "(Official Video)", "[Lyrics]", "(Remastered 2011)"-style
 *     brackets dropped (only brackets naming such things, or a video id); one
 *     leading track number ("01 - ", "01. ", "1-03 ", "07 ") and then a year
 *     ("1982 "); for "A - B" names the part that is not a known artist/album
 *     (if neither is known both stay: "Artist - Title" or "Title - Artist"
 *     cannot be told apart), for "A - B - C" the last part; then a trailing or
 *     leading run of known names is dropped when it is clearly glued on: two or
 *     more names ("Rush-Signals"), or one after a dash or a bracket ("- Rush").
 *     Known names are the folder's name (and its parts split at " - "), the
 *     parent folders', and the artist/album tags of the other songs in the
 *     folder. A lone trailing word that only happens to be the folder's name
 *     ("The Rush" in Music\Rush) stays.
 *  4. Whatever is left must still have a letter or digit; if not, the whole
 *     file name is used (never shortened to nothing).
 *
 * Options > Music Player > Song Titles (and the F1 page) switches between Short
 * (default) and Full (the file name as it is); kept in nfl2k5_music.ini. */
static bool tag_utf8_valid(const std::string &s)
{
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = (uint8_t)s[i];
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || i + (size_t)n >= s.size()) return false;
        for (int k = 1; k <= n; k++) if (((uint8_t)s[i + k] & 0xC0) != 0x80) return false;
        i += (size_t)n + 1;
    }
    return true;
}

static std::u16string latin1_to_u16(const uint8_t *p, size_t n)
{
    std::u16string o;
    for (size_t i = 0; i < n && p[i]; i++) o.push_back((char16_t)p[i]);
    return o;
}

/* Text of unknown encoding (RIFF INFO, ID3v1): UTF-8 when it is valid UTF-8, else Latin-1. */
static std::u16string guess_text(const uint8_t *p, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n && p[i]; i++) s.push_back((char)p[i]);
    return tag_utf8_valid(s) ? u8_to_u16(s) : latin1_to_u16(p, n);
}

static std::u16string utf16_text(const uint8_t *p, size_t n, bool big)
{
    std::u16string o;
    if (n >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) { big = p[0] == 0xFE; p += 2; n -= 2; }
    for (size_t i = 0; i + 1 < n; i += 2) {
        char16_t c = big ? (char16_t)(p[i] << 8 | p[i + 1]) : (char16_t)(p[i + 1] << 8 | p[i]);
        if (!c) break;
        o.push_back(c);
    }
    return o;
}

static std::u16string trim16(std::u16string s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == u' ' || s[a] == u'\t' || s[a] == 0xFEFF || s[a] < 0x20)) a++;
    while (b > a && (s[b - 1] == u' ' || s[b - 1] == u'\t' || s[b - 1] < 0x20)) b--;
    return s.substr(a, b - a);
}

struct Tags { std::u16string title, artist, album; };

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static uint32_t syncsafe(const uint8_t *p) { return (uint32_t)(p[0] & 0x7F) << 21 | (uint32_t)(p[1] & 0x7F) << 14 | (uint32_t)(p[2] & 0x7F) << 7 | (p[3] & 0x7F); }

static void unsync(std::vector<uint8_t> &b)
{
    std::vector<uint8_t> o;
    o.reserve(b.size());
    for (size_t i = 0; i < b.size(); i++) {
        o.push_back(b[i]);
        if (b[i] == 0xFF && i + 1 < b.size() && b[i + 1] == 0x00) i++;
    }
    b.swap(o);
}

static std::u16string id3_text(const uint8_t *p, size_t n)
{
    if (!n) return u"";
    uint8_t enc = p[0];
    p++; n--;
    if (enc == 1) return utf16_text(p, n, false);
    if (enc == 2) return utf16_text(p, n, true);
    if (enc == 3) { std::string s; for (size_t i = 0; i < n && p[i]; i++) s.push_back((char)p[i]); return u8_to_u16(s); }
    return latin1_to_u16(p, n);
}

static void read_id3(FILE *f, Tags &t)
{
    uint8_t h[10];
    if (fseek(f, 0, SEEK_SET) == 0 && fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 2 && h[3] <= 4) {
        uint32_t size = syncsafe(h + 6), ver = h[3];
        if (size > (1u << 20)) size = 1u << 20;                /* titles come first; a cover picture can be large */
        std::vector<uint8_t> b(size);
        b.resize(fread(b.data(), 1, size, f));
        if ((h[5] & 0x80) && ver < 4) unsync(b);
        size_t pos = 0;
        if ((h[5] & 0x40) && b.size() >= 4)                     /* extended header */
            pos = ver == 4 ? syncsafe(&b[0]) : be32(&b[0]) + 4;
        size_t idlen = ver == 2 ? 3 : 4, hdr = ver == 2 ? 6 : 10;
        while (pos + hdr <= b.size() && b[pos]) {
            const uint8_t *fr = &b[pos];
            uint32_t fs = ver == 2 ? (uint32_t)fr[3] << 16 | fr[4] << 8 | fr[5] : ver == 4 ? syncsafe(fr + 4) : be32(fr + 4);
            if (pos + hdr + fs > b.size()) break;
            std::vector<uint8_t> data(fr + hdr, fr + hdr + fs);
            if (ver == 4 && (fr[9] & 0x02)) unsync(data);       /* this frame unsynchronised */
            if (ver == 4 && (fr[9] & 0x01) && data.size() >= 4) data.erase(data.begin(), data.begin() + 4);   /* data length indicator */
            std::string id((const char *)fr, idlen);
            std::u16string *dst = id == "TIT2" || id == "TT2" ? &t.title : id == "TPE1" || id == "TP1" ? &t.artist :
                                  id == "TALB" || id == "TAL" ? &t.album : nullptr;
            bool packed = ver == 4 ? (fr[9] & 0x0C) != 0 : ver == 3 ? (fr[9] & 0xC0) != 0 : false;   /* compressed / encrypted */
            if (dst && dst->empty() && !packed)
                *dst = trim16(id3_text(data.data(), data.size()));
            pos += hdr + fs;
        }
    }
    if (t.title.empty() && fseek(f, -128, SEEK_END) == 0) {    /* ID3v1 at the end */
        uint8_t v1[128];
        if (fread(v1, 1, 128, f) == 128 && !memcmp(v1, "TAG", 3)) {
            t.title = trim16(guess_text(v1 + 3, 30));
            if (t.artist.empty()) t.artist = trim16(guess_text(v1 + 33, 30));
            if (t.album.empty()) t.album = trim16(guess_text(v1 + 63, 30));
        }
    }
}

/* A Vorbis comment block (vendor, count, "KEY=value"...) as in Ogg Vorbis/Opus and FLAC. */
static void vorbis_comments(const uint8_t *p, size_t n, Tags &t)
{
    if (n < 8) return;
    uint32_t vl = le32(p);
    if ((size_t)vl + 8 > n) return;
    size_t pos = 4 + vl;
    uint32_t count = le32(p + pos);
    pos += 4;
    for (uint32_t i = 0; i < count && pos + 4 <= n; i++) {
        uint32_t l = le32(p + pos);
        pos += 4;
        if (pos + l > n) break;
        std::string kv((const char *)p + pos, l);
        pos += l;
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq);
        for (char &c : k) c = (char)std::toupper((unsigned char)c);
        std::u16string v = trim16(u8_to_u16(kv.substr(eq + 1)));
        if (k == "TITLE" && t.title.empty()) t.title = v;
        else if (k == "ARTIST" && t.artist.empty()) t.artist = v;
        else if (k == "ALBUM" && t.album.empty()) t.album = v;
    }
}

static void read_ogg(FILE *f, Tags &t)
{
    /* the packets of the first pages, joined (the comment packet is the second) */
    std::vector<uint8_t> in(256 * 1024), pk;
    in.resize(fread(in.data(), 1, in.size(), f));
    size_t pos = 0;
    while (pos + 27 <= in.size() && !memcmp(&in[pos], "OggS", 4) && pk.size() < 200 * 1024) {
        uint8_t segs = in[pos + 26];
        if (pos + 27 + segs > in.size()) break;
        size_t len = 0;
        for (uint8_t i = 0; i < segs; i++) len += in[pos + 27 + i];
        size_t d = pos + 27 + segs;
        if (d + len > in.size()) len = in.size() - d;
        pk.insert(pk.end(), in.begin() + (ptrdiff_t)d, in.begin() + (ptrdiff_t)(d + len));
        pos = d + len;
    }
    static const char vorbis[7] = { 3, 'v', 'o', 'r', 'b', 'i', 's' };
    for (size_t i = 0; i + 8 < pk.size(); i++) {
        if (!memcmp(&pk[i], vorbis, 7)) { vorbis_comments(&pk[i + 7], pk.size() - i - 7, t); return; }
        if (!memcmp(&pk[i], "OpusTags", 8)) { vorbis_comments(&pk[i + 8], pk.size() - i - 8, t); return; }
    }
}

static void read_flac(FILE *f, Tags &t)
{
    uint8_t h[4];
    if (fseek(f, 4, SEEK_SET)) return;
    for (int n = 0; n < 64 && fread(h, 1, 4, f) == 4; n++) {
        uint32_t len = (uint32_t)h[1] << 16 | h[2] << 8 | h[3];
        if ((h[0] & 0x7F) == 4 && len < (4u << 20)) {
            std::vector<uint8_t> b(len);
            if (fread(b.data(), 1, len, f) == len) vorbis_comments(b.data(), len, t);
            return;
        }
        if ((h[0] & 0x80) || fseek(f, (long)len, SEEK_CUR)) return;
    }
}

/* MP4: moov > udta > meta > ilst > (c)nam / (c)ART / (c)alb > data. */
static bool mp4_find(FILE *f, int64_t start, int64_t end, const char *type, int64_t &body, int64_t &body_end)
{
    int64_t pos = start;
    uint8_t h[16];
    for (int n = 0; n < 4096 && pos + 8 <= end; n++) {
        if (fseek(f, (long)pos, SEEK_SET) || fread(h, 1, 8, f) != 8) return false;
        int64_t size = be32(h), hl = 8;
        if (size == 1) {
            if (fread(h + 8, 1, 8, f) != 8) return false;
            size = (int64_t)be32(h + 8) << 32 | be32(h + 12);
            hl = 16;
        } else if (size == 0) size = end - pos;
        if (size < hl || pos + size > end) return false;
        if (!memcmp(h + 4, type, 4)) { body = pos + hl; body_end = pos + size; return true; }
        pos += size;
    }
    return false;
}

static void read_mp4(FILE *f, Tags &t)
{
    if (fseek(f, 0, SEEK_END)) return;
    int64_t end = ftell(f), b = 0, e = 0;
    if (end <= 0 || end > 0x7FFFFFFF) return;
    if (!mp4_find(f, 0, end, "moov", b, e) || !mp4_find(f, b, e, "udta", b, e) || !mp4_find(f, b, e, "meta", b, e)) return;
    if (!mp4_find(f, b + 4, e, "ilst", b, e)) return;          /* meta is a full box: 4 bytes of version and flags */
    static const char *const keys[3] = { "\xA9nam", "\xA9" "ART", "\xA9" "alb" };
    std::u16string *dst[3] = { &t.title, &t.artist, &t.album };
    for (int k = 0; k < 3; k++) {
        int64_t ib, ie, db, de;
        if (!mp4_find(f, b, e, keys[k], ib, ie) || !mp4_find(f, ib, ie, "data", db, de) || de - db < 8 || de - db > 4096) continue;
        std::vector<uint8_t> d((size_t)(de - db));
        if (fseek(f, (long)db, SEEK_SET) || fread(d.data(), 1, d.size(), f) != d.size()) continue;
        std::string s((const char *)d.data() + 8, d.size() - 8);     /* type, locale, then UTF-8 */
        *dst[k] = trim16(u8_to_u16(s));
    }
}

static void read_riff(FILE *f, Tags &t)
{
    uint8_t h[12];
    if (fseek(f, 0, SEEK_SET) || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return;
    for (int n = 0; n < 256 && fread(h, 1, 8, f) == 8; n++) {
        uint32_t len = le32(h + 4);
        if (!memcmp(h, "LIST", 4) && len >= 4 && len < (1u << 20)) {
            std::vector<uint8_t> b(len);
            if (fread(b.data(), 1, len, f) != len) return;
            if (memcmp(b.data(), "INFO", 4)) continue;
            for (size_t p = 4; p + 8 <= b.size();) {
                uint32_t l = le32(&b[p + 4]);
                if (p + 8 + l > b.size()) break;
                std::u16string v = trim16(guess_text(&b[p + 8], l));
                if (!memcmp(&b[p], "INAM", 4)) t.title = v;
                else if (!memcmp(&b[p], "IART", 4)) t.artist = v;
                else if (!memcmp(&b[p], "IPRD", 4)) t.album = v;
                p += 8 + l + (l & 1);
            }
            return;
        }
        if (fseek(f, (long)(len + (len & 1)), SEEK_CUR)) return;
    }
}

/* The tags of a song file, by what the file starts with (not its extension). */
static Tags read_file_tags(const fs::path &p)
{
    Tags t;
    FILE *f = open_path(p, "rb");
    if (!f) return t;
    uint8_t m[12] = { 0 };
    size_t got = fread(m, 1, 12, f);
    try {
        if (got >= 4 && !memcmp(m, "OggS", 4)) { fseek(f, 0, SEEK_SET); read_ogg(f, t); }
        else if (got >= 4 && !memcmp(m, "fLaC", 4)) read_flac(f, t);
        else if (got >= 12 && !memcmp(m, "RIFF", 4)) read_riff(f, t);
        else if (got >= 8 && !memcmp(m + 4, "ftyp", 4)) read_mp4(f, t);
        else if (got >= 3) read_id3(f, t);                   /* mp3: ID3v2 at the front, ID3v1 at the end */
    } catch (...) {
        t = Tags();
    }
    fclose(f);
    return t;
}

/* ---- the file-name clean-up ---- */
static char16_t fold16(char16_t c)
{
    static const char latin1[] = "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
    if (c >= 0xC0 && c <= 0xFF) c = (char16_t)latin1[c - 0xC0];
    if (c >= u'A' && c <= u'Z') c = (char16_t)(c - u'A' + u'a');
    return c;
}

static bool alnum16(char16_t c)
{
    c = fold16(c);
    return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c > 0x7F;
}

static std::u16string key16(const std::u16string &s)        /* letters and digits only, lower case, no accents */
{
    std::u16string k;
    for (char16_t c : s) if (alnum16(c)) k.push_back(fold16(c));
    return k;
}

static bool has_alnum(const std::u16string &s) { for (char16_t c : s) if (alnum16(c)) return true; return false; }

static bool sep16(char16_t c) { return c == u' ' || c == u'-' || c == u'_' || c == u'.' || c == u',' || c == 0x2013 || c == 0x2014 || c == u'~' || c == u'|'; }

static std::u16string squash(const std::u16string &s)       /* trim, single spaces */
{
    std::u16string o;
    for (char16_t c : s) {
        if (c == u'\t') c = u' ';
        if (c == u' ' && (o.empty() || o.back() == u' ')) continue;
        o.push_back(c);
    }
    while (!o.empty() && o.back() == u' ') o.pop_back();
    return o;
}

static std::u16string trim_seps(std::u16string s)
{
    while (!s.empty() && (sep16(s.back()) && s.back() != u'.')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && sep16(s[a]) && s[a] != u'.') a++;
    return s.substr(a);
}

/* A bracket of the kind a download adds: "(Official Video)", "[Lyrics]", "(Remastered 2011)", "[HD]", a video id. */
static bool junk_bracket(const std::u16string &inner, char16_t open)
{
    static const char *const words[] = { "official", "video", "audio", "lyric", "lyrics", "visualizer", "visualiser",
                                         "remaster", "remastered", "hd", "hq", "4k", "1080p", "720p", "explicit",
                                         "clean", "mv", "musicvideo", "colorcoded", "audioonly", "fullsong",
                                         "bonustrack", "remasterversion", "remasteredversion" };
    std::u16string k = key16(inner);
    if (k.empty()) return true;
    for (const char *w : words) {
        std::u16string ww(w, w + strlen(w));
        if (k.find(ww) != std::u16string::npos && (ww.size() > 2 || k.size() <= 6)) return true;
    }
    if (open == u'[') {
        bool digits = true;
        for (char16_t c : k) if (c < u'0' || c > u'9') digits = false;
        if (digits) return true;                               /* [2011] */
        if (inner.size() == 11) {                              /* [dQw4w9WgXcQ] */
            bool id = true;
            for (char16_t c : inner) if (!(alnum16(c) || c == u'-' || c == u'_') || c > 0x7F) id = false;
            if (id) return true;
        }
    }
    return false;
}

static std::u16string drop_junk_brackets(const std::u16string &s)
{
    std::u16string o;
    for (size_t i = 0; i < s.size(); i++) {
        char16_t c = s[i], close = c == u'(' ? u')' : c == u'[' ? u']' : c == u'{' ? u'}' : 0;
        size_t j;
        if (close && (j = s.find(close, i + 1)) != std::u16string::npos && junk_bracket(s.substr(i + 1, j - i - 1), c)) {
            i = j;
            continue;
        }
        o.push_back(c);
    }
    return squash(o);
}

static bool digit16(char16_t c) { return c >= u'0' && c <= u'9'; }

/* One leading track number ("01 - ", "01. ", "1-03 ", "07 ", "07_"), then a year ("1982 "). */
static std::u16string drop_leading_numbers(const std::u16string &s)
{
    std::u16string r = s;
    size_t i = 0;
    while (i < r.size() && digit16(r[i])) i++;
    bool year = i == 4 && (r.compare(0, 2, u"19") == 0 || r.compare(0, 2, u"20") == 0);
    if (i >= 1 && i <= 3) {
        size_t j = i;
        if (j + 1 < r.size() && (r[j] == u'-' || r[j] == u'.') && digit16(r[j + 1])) {   /* disc-track: 1-03 */
            j++;
            while (j < r.size() && digit16(r[j])) j++;
        }
        size_t k = j;
        while (k < r.size() && (sep16(r[k]) || r[k] == u')')) k++;
        if (k > j && k < r.size() && has_alnum(r.substr(k))) r = r.substr(k);
        i = 0;
        while (i < r.size() && digit16(r[i])) i++;
        year = i == 4 && (r.compare(0, 2, u"19") == 0 || r.compare(0, 2, u"20") == 0);
    }
    if (year) {
        size_t k = 4;
        while (k < r.size() && sep16(r[k])) k++;
        if (k > 4 && k < r.size() && has_alnum(r.substr(k))) r = r.substr(k);
    }
    return r;
}

/* Split at spaced dashes: "A - B - C". */
static std::vector<std::u16string> dash_parts(const std::u16string &s)
{
    std::vector<std::u16string> parts;
    size_t from = 0;
    for (size_t i = 1; i + 1 < s.size(); i++) {
        if (s[i - 1] == u' ' && (s[i] == u'-' || s[i] == 0x2013 || s[i] == 0x2014) && s[i + 1] == u' ') {
            parts.push_back(trim_seps(s.substr(from, i - 1 - from)));
            from = i + 2;
        }
    }
    parts.push_back(trim_seps(s.substr(from)));
    return parts;
}

static bool is_hint(const std::u16string &part, const std::vector<std::u16string> &hints)
{
    std::u16string k = key16(part);
    if (k.size() < 2) return false;
    for (const std::u16string &h : hints) if (h == k) return true;
    return false;
}

/* How many letters/digits from the end of s (or the start, front = true) one known name covers, ending
 * on a word boundary; 0 if none. `cut` gets the position in s where it begins (ends). */
static size_t hint_at(const std::u16string &s, size_t limit, bool front, const std::u16string &h, size_t &cut)
{
    size_t hi = 0, i;
    if (!front) {
        i = limit;
        while (i > 0 && hi < h.size()) {
            char16_t c = s[i - 1];
            if (alnum16(c)) { if (fold16(c) != h[h.size() - 1 - hi]) return 0; hi++; }
            else if (!sep16(c) && c != u'\'') return 0;
            i--;
        }
        if (hi != h.size() || (i > 0 && alnum16(s[i - 1]))) return 0;
        cut = i;
        return hi;
    }
    i = limit;
    while (i < s.size() && hi < h.size()) {
        char16_t c = s[i];
        if (alnum16(c)) { if (fold16(c) != h[hi]) return 0; hi++; }
        else if (!sep16(c) && c != u'\'') return 0;
        i++;
    }
    if (hi != h.size() || (i < s.size() && alnum16(s[i]))) return 0;
    cut = i;
    return hi;
}

/* Drop a run of known names glued to the end (or the start) of a title: kept unless it is two or more
 * names, or one set off by a dash or a bracket. Never leaves less than a letter or digit. */
static std::u16string drop_known_names(const std::u16string &s, const std::vector<std::u16string> &hints, bool front)
{
    size_t edge = front ? 0 : s.size(), names = 0;
    for (int guard = 0; guard < 8; guard++) {
        size_t best = 0, bcut = 0;
        for (const std::u16string &h : hints) {
            size_t cut = 0, n = hint_at(s, edge, front, h, cut);
            if (n && (!best || n < best)) { best = n; bcut = cut; }   /* the shortest: "Rush-Signals" counts as two names */
        }
        if (!best) break;
        edge = bcut;
        names++;
    }
    if (!names) return s;
    std::u16string rest = front ? s.substr(edge) : s.substr(0, edge);
    if (!has_alnum(rest)) return s;
    /* what joins the names to the rest */
    std::u16string join;
    if (front) for (size_t i = 0; i < rest.size() && !alnum16(rest[i]); i++) join.push_back(rest[i]);
    else for (size_t i = rest.size(); i > 0 && !alnum16(rest[i - 1]); i--) join.push_back(rest[i - 1]);
    bool dash = join.find(u'-') != std::u16string::npos || join.find(0x2013) != std::u16string::npos ||
                join.find(u'(') != std::u16string::npos || join.find(u')') != std::u16string::npos;
    if (names < 2 && !dash) return s;
    return trim_seps(rest);
}

static std::u16string clean_name(const std::u16string &stem, const std::vector<std::u16string> &hints, std::u16string *artist)
{
    std::u16string s;
    for (char16_t c : stem) s.push_back(c == u'_' ? u' ' : c);
    s = squash(s);
    s = drop_junk_brackets(s);
    s = drop_leading_numbers(s);
    std::vector<std::u16string> parts = dash_parts(s);
    if (parts.size() >= 2) {
        std::vector<size_t> keep;
        for (size_t i = 0; i < parts.size(); i++) if (has_alnum(parts[i]) && !is_hint(parts[i], hints)) keep.push_back(i);
        /* one part left that is not a known name: that one; two unknown parts ("Walk This Way -
         * Aerosmith" or "Aerosmith - Walk This Way"?): unsure, both stay; three or more: the last
         * (the usual "Artist - Album - Title") */
        if (keep.size() == 1) {
            if (artist && parts.size() == 2) *artist = parts[keep[0] == 1 ? 0 : 1];
            s = drop_leading_numbers(parts[keep[0]]);
        } else if (parts.size() >= 3) {
            s = drop_leading_numbers(parts.back());
        }
    }
    s = drop_known_names(s, hints, false);
    s = drop_known_names(s, hints, true);
    s = trim_seps(squash(s));
    return has_alnum(s) ? s : squash(stem);
}

static void add_hint(std::vector<std::u16string> &hints, const std::u16string &name)
{
    std::u16string k = key16(name);
    if (k.size() >= 2 && std::find(hints.begin(), hints.end(), k) == hints.end()) hints.push_back(k);
    std::vector<std::u16string> parts = dash_parts(squash(name));
    if (parts.size() > 1) for (const std::u16string &p : parts) add_hint(hints, p);
}

/* Music\titles.txt: the player's own short titles. */
static std::vector<std::pair<std::string, std::u16string>> g_title_overrides;

static std::string lower8(std::string s) { for (char &c : s) c = (char)std::tolower((unsigned char)c); return s; }

static void load_title_overrides(const fs::path &music)
{
    g_title_overrides.clear();
    FILE *f = open_path(music / "titles.txt", "rb");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        if (l.size() >= 3 && (uint8_t)l[0] == 0xEF && (uint8_t)l[1] == 0xBB && (uint8_t)l[2] == 0xBF) l = l.substr(3);
        size_t hash = l.find('#');
        if (hash != std::string::npos) l = l.substr(0, hash);
        size_t eq = l.rfind('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        auto tr = [](std::string &x) { while (!x.empty() && std::isspace((unsigned char)x.back())) x.pop_back();
                                        size_t a = 0; while (a < x.size() && std::isspace((unsigned char)x[a])) a++; x = x.substr(a); };
        tr(k); tr(v);
        for (char &c : k) if (c == '\\') c = '/';
        if (!k.empty() && !v.empty()) g_title_overrides.push_back({ lower8(k), u8_to_u16(v) });
    }
    fclose(f);
    fprintf(stderr, "[MUSIC] titles.txt: %zu short title(s) of your own\n", g_title_overrides.size());
}

static bool title_override(const fs::path &file, const fs::path &music, std::u16string &out)
{
    if (g_title_overrides.empty()) return false;
    std::string name = lower8(u8(file.filename())), stem = lower8(u8(file.stem())), rel;
    std::error_code ec;
    fs::path r = fs::relative(file, music, ec);
    if (!ec) { rel = lower8(u8(r)); for (char &c : rel) if (c == '\\') c = '/'; }
    for (const auto &o : g_title_overrides)
        if (o.first == name || o.first == stem || (!rel.empty() && o.first == rel)) { out = o.second; return true; }
    return false;
}

