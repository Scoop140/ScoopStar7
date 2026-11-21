/*
 * ScoopStar 7.0 - spell.c
 * Spelling engines and thesaurus data.
 *   English: the spell checker built into Windows (ISpellChecker), with the
 *            bundled Hunspell en_US dictionary as a fallback.
 *   Latin, German: Hunspell with the dictionaries in the Dictionaries folder.
 *   Thesaurus: the LibreOffice/OpenOffice English thesaurus (th_en_US_v2.dat).
 */
#define COBJMACROS
#include "scoopstar.h"
#include <objbase.h>
#include <spellcheck.h>
#include "../hunspell/hunspell.h"

static const GUID my_CLSID_SpellCheckerFactory = { 0x7AB36653, 0x1796, 0x484B, { 0xBD, 0xFA, 0xE7, 0x4F, 0x1D, 0xB7, 0xC1, 0xDC } };
static const GUID my_IID_ISpellCheckerFactory = { 0x8E018A9D, 0x2415, 0x4677, { 0xBF, 0x08, 0x79, 0x4E, 0xA6, 0x1F, 0x94, 0xBB } };

const wchar_t *g_langnames[NLANGS] = { L"English", L"Latin", L"German" };
static const wchar_t *lang_files[NLANGS] = { L"en_US", L"la", L"de_DE" };

typedef struct {
    int tried, ok;
    Hunhandle *h;
    UINT cp;              /* dictionary code page */
} HunLang;
static HunLang hl[NLANGS];

static int win_tried;
static ISpellChecker *winsc;

/* session "ignore all" list */
static wchar_t **ignore; static int nignore, capignore;

static void app_dir(wchar_t *out)
{
    GetModuleFileNameW(NULL, out, MAX_PATH);
    wchar_t *b = wcsrchr(out, '\\');
    if (b) b[1] = 0;
}

static void personal_path(int lang, wchar_t *out)
{
    wchar_t app[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH)) wcscpy(app, L".");
    swprintf(out, MAX_PATH, L"%ls\\ScoopStar\\personal_%ls.dic", app, lang_files[lang]);
}

int lang_from_name(const wchar_t *a)
{
    wchar_t s[32]; int k = 0;
    while (*a == ' ') a++;
    for (; *a && *a != ' ' && k < 31; a++) s[k++] = (wchar_t)towlower(*a);
    s[k] = 0;
    if (!k) return -1;
    if (!wcsncmp(s, L"la", 2) || !wcscmp(s, L"39") || !wcsncmp(s, L"lat", 3)) return LANG_LA;
    if (!wcsncmp(s, L"de", 2) || !wcsncmp(s, L"ger", 3) || !wcsncmp(s, L"deu", 3) || !wcscmp(s, L"49")) return LANG_DE;
    if (!wcsncmp(s, L"en", 2) || !wcscmp(s, L"us") || !wcscmp(s, L"uk") || !wcscmp(s, L"1") || !wcscmp(s, L"44")) return LANG_EN;
    return -1;
}

static UINT cp_from_name(const char *e)
{
    if (!e) return CP_UTF8;
    if (!_stricmp(e, "UTF-8")) return CP_UTF8;
    if (!_stricmp(e, "ISO8859-1") || !_stricmp(e, "ISO-8859-1")) return 28591;
    if (!_stricmp(e, "ISO8859-15") || !_stricmp(e, "ISO-8859-15")) return 28605;
    if (!_stricmp(e, "ISO8859-2")) return 28592;
    return CP_UTF8;
}

static int to_dic(int lang, const wchar_t *w, char *out, int n)
{
    BOOL bad = FALSE;
    UINT cp = hl[lang].cp;
    int k = WideCharToMultiByte(cp, 0, w, -1, out, n, NULL, cp == CP_UTF8 ? NULL : &bad);
    return k > 0 && !bad;
}
static void from_dic(int lang, const char *s, wchar_t *out, int n)
{
    if (!MultiByteToWideChar(hl[lang].cp, 0, s, -1, out, n)) out[0] = 0;
}

static Hunhandle *hun(int lang)
{
    HunLang *L = &hl[lang];
    if (L->tried) return L->h;
    L->tried = 1;
    wchar_t dir[MAX_PATH], aff[MAX_PATH], dic[MAX_PATH];
    app_dir(dir);
    swprintf(aff, MAX_PATH, L"%lsDictionaries\\%ls.aff", dir, lang_files[lang]);
    swprintf(dic, MAX_PATH, L"%lsDictionaries\\%ls.dic", dir, lang_files[lang]);
    if (!file_exists(aff) || !file_exists(dic)) return NULL;
    /* Hunspell opens "\\?\"-prefixed UTF-8 paths with the wide-character API */
    char a8[MAX_PATH * 3 + 8] = "\\\\?\\", d8[MAX_PATH * 3 + 8] = "\\\\?\\";
    WideCharToMultiByte(CP_UTF8, 0, aff, -1, a8 + 4, MAX_PATH * 3, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, dic, -1, d8 + 4, MAX_PATH * 3, NULL, NULL);
    L->h = Hunspell_create(a8, d8);
    if (!L->h) return NULL;
    L->cp = cp_from_name(Hunspell_get_dic_encoding(L->h));
    /* personal dictionary */
    wchar_t pp[MAX_PATH]; personal_path(lang, pp);
    FILE *f = _wfopen(pp, L"rb");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            size_t k = strlen(line);
            while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
            if (!k) continue;
            wchar_t w[128]; MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 128);
            char d[512]; if (to_dic(lang, w, d, 512)) Hunspell_add(L->h, d);
        }
        fclose(f);
    }
    L->ok = 1;
    return L->h;
}

static ISpellChecker *winspell(void)
{
    if (win_tried) return winsc;
    win_tried = 1;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ISpellCheckerFactory *f = NULL;
    if (FAILED(CoCreateInstance(&my_CLSID_SpellCheckerFactory, NULL, CLSCTX_INPROC_SERVER, &my_IID_ISpellCheckerFactory, (void **)&f)) || !f) return NULL;
    wchar_t loc[LOCALE_NAME_MAX_LENGTH] = L"en-US";
    GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
    if (_wcsnicmp(loc, L"en", 2)) wcscpy(loc, L"en-US");
    BOOL sup = FALSE;
    if (FAILED(ISpellCheckerFactory_IsSupported(f, loc, &sup)) || !sup) { wcscpy(loc, L"en-US"); ISpellCheckerFactory_IsSupported(f, loc, &sup); }
    if (sup) ISpellCheckerFactory_CreateSpellChecker(f, loc, &winsc);
    ISpellCheckerFactory_Release(f);
    return winsc;
}

int spell_available(int lang)
{
    if (lang < 0 || lang >= NLANGS) return 0;
    if (lang == LANG_EN && winspell()) return 1;
    return hun(lang) != NULL;
}

const wchar_t *spell_engine_name(int lang)
{
    if (lang == LANG_EN && winspell()) return L"Windows spelling checker";
    return hun(lang) ? L"Hunspell dictionary" : L"(no dictionary)";
}

static int ignored(const wchar_t *w)
{
    for (int i = 0; i < nignore; i++) if (!wcscmp(ignore[i], w)) return 1;
    return 0;
}

void spell_ignore_all(const wchar_t *w)
{
    if (ignored(w)) return;
    if (nignore >= capignore) { capignore = capignore ? capignore * 2 : 64; ignore = xrealloc(ignore, capignore * sizeof(wchar_t *)); }
    ignore[nignore++] = wcsdup_(w);
}

/* 1 = correct, 0 = misspelled, -1 = no dictionary */
int spell_check(const wchar_t *w, int lang)
{
    if (ignored(w)) return 1;
    if (lang == LANG_EN) {
        ISpellChecker *sc = winspell();
        if (sc) {
            IEnumSpellingError *en = NULL;
            if (SUCCEEDED(ISpellChecker_Check(sc, w, &en)) && en) {
                ISpellingError *err = NULL;
                HRESULT hr = IEnumSpellingError_Next(en, &err);
                int bad = 0;
                if (hr == S_OK && err) {
                    CORRECTIVE_ACTION ca = CORRECTIVE_ACTION_NONE;
                    ISpellingError_get_CorrectiveAction(err, &ca);
                    bad = ca != CORRECTIVE_ACTION_NONE;
                    ISpellingError_Release(err);
                }
                IEnumSpellingError_Release(en);
                return !bad;
            }
        }
    }
    Hunhandle *h = hun(lang);
    if (!h) return -1;
    char d[512];
    if (!to_dic(lang, w, d, 512)) return 0;
    if (Hunspell_spell(h, d)) return 1;
    if (lang == LANG_LA) {
        /* the Latin dictionary uses classical spelling: accept j for i and ae/oe ligatures */
        wchar_t alt[128]; int k = 0, changed = 0;
        for (const wchar_t *q = w; *q && k < 120; q++) {
            if (*q == 'j') { alt[k++] = 'i'; changed = 1; }
            else if (*q == 'J') { alt[k++] = 'I'; changed = 1; }
            else if (*q == 0xE6) { alt[k++] = 'a'; alt[k++] = 'e'; changed = 1; }
            else if (*q == 0x153) { alt[k++] = 'o'; alt[k++] = 'e'; changed = 1; }
            else if (*q == 0xC6) { alt[k++] = 'A'; alt[k++] = 'e'; changed = 1; }
            else alt[k++] = *q;
        }
        alt[k] = 0;
        if (changed && to_dic(lang, alt, d, 512) && Hunspell_spell(h, d)) return 1;
    }
    return 0;
}

int spell_suggest(const wchar_t *w, int lang, wchar_t out[][64], int max)
{
    int n = 0;
    if (lang == LANG_EN) {
        ISpellChecker *sc = winspell();
        if (sc) {
            IEnumString *es = NULL;
            if (SUCCEEDED(ISpellChecker_Suggest(sc, w, &es)) && es) {
                LPOLESTR s; ULONG got;
                while (n < max && IEnumString_Next(es, 1, &s, &got) == S_OK && got) {
                    wcsncpy(out[n], s, 63); out[n][63] = 0; n++;
                    CoTaskMemFree(s);
                }
                IEnumString_Release(es);
                return n;
            }
        }
    }
    Hunhandle *h = hun(lang);
    if (!h) return 0;
    char d[512];
    if (!to_dic(lang, w, d, 512)) return 0;
    char **list = NULL;
    int k = Hunspell_suggest(h, &list, d);
    for (int i = 0; i < k && n < max; i++) { from_dic(lang, list[i], out[n], 64); if (out[n][0]) n++; }
    if (list) Hunspell_free_list(h, &list, k);
    return n;
}

void spell_add(const wchar_t *w, int lang)
{
    if (lang == LANG_EN && winspell()) { ISpellChecker_Add(winsc, w); return; }
    Hunhandle *h = hun(lang);
    if (h) { char d[512]; if (to_dic(lang, w, d, 512)) Hunspell_add(h, d); }
    wchar_t pp[MAX_PATH]; personal_path(lang, pp);
    FILE *f = _wfopen(pp, L"ab");
    if (f) {
        char u[512]; WideCharToMultiByte(CP_UTF8, 0, w, -1, u, 512, NULL, NULL);
        fprintf(f, "%s\r\n", u);
        fclose(f);
    }
}

/* stems from the English Hunspell dictionary, for thesaurus lookups */
int spell_stems(const wchar_t *w, wchar_t out[][64], int max)
{
    Hunhandle *h = hun(LANG_EN);
    if (!h) return 0;
    char d[512];
    if (!to_dic(LANG_EN, w, d, 512)) return 0;
    char **list = NULL;
    int k = Hunspell_stem(h, &list, d), n = 0;
    for (int i = 0; i < k && n < max; i++) { from_dic(LANG_EN, list[i], out[n], 64); if (out[n][0]) n++; }
    if (list) Hunspell_free_list(h, &list, k);
    return n;
}

/* ------------------------------------------------------------------ thesaurus */
static char *th_data; static size_t th_len;
static int *th_idx; static int th_n; static int th_tried;

static int th_lower_cmp(const char *a, const char *b)
{
    /* compare headword (up to '|') with key, ASCII case-insensitively */
    for (;;) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (x == '|') x = 0;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return (int)x - (int)y;
        a++; b++;
    }
}

static const char *th_sortbase;
static int th_sortcmp(const void *a, const void *b)
{
    const char *x = th_sortbase + *(const int *)a, *y = th_sortbase + *(const int *)b;
    for (;;) {
        unsigned char p = (unsigned char)*x, q = (unsigned char)*y;
        if (p == '|') p = 0;
        if (q == '|') q = 0;
        if (p >= 'A' && p <= 'Z') p += 32;
        if (q >= 'A' && q <= 'Z') q += 32;
        if (p != q || !p) return (int)p - (int)q;
        x++; y++;
    }
}

int thes_available(void)
{
    if (th_tried) return th_data != NULL;
    th_tried = 1;
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    app_dir(dir);
    swprintf(path, MAX_PATH, L"%lsDictionaries\\th_en_US_v2.dat", dir);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    th_len = (size_t)sz.QuadPart;
    th_data = xmalloc(th_len + 1);
    DWORD rd = 0; ReadFile(h, th_data, (DWORD)th_len, &rd, NULL);
    CloseHandle(h);
    th_len = rd; th_data[th_len] = 0;
    /* index the headword lines: "word|count" followed by count meaning lines */
    int cap = 1 << 17; th_idx = xmalloc(cap * sizeof(int)); th_n = 0;
    size_t i = 0;
    while (i < th_len && th_data[i] != '\n') i++;   /* skip encoding line */
    i++;
    while (i < th_len) {
        size_t ls = i;
        while (i < th_len && th_data[i] != '\n') i++;
        size_t le = i;
        i++;
        th_data[le] = 0;
        if (le > ls && th_data[le - 1] == '\r') th_data[le - 1] = 0;
        if (th_data[ls] == '(' ) continue;
        char *bar = strrchr(th_data + ls, '|');
        if (!bar) continue;
        if (th_n >= cap) { cap *= 2; th_idx = xrealloc(th_idx, cap * sizeof(int)); }
        th_idx[th_n++] = (int)ls;
    }
    th_sortbase = th_data;
    qsort(th_idx, th_n, sizeof(int), th_sortcmp);
    return 1;
}

/* Looks up a word. Fills meanings (part of speech + list) as UTF-16 strings.
   Each meaning string: "(noun)|syn1|syn2|..." Returns count, 0 if not found. */
int thes_lookup(const wchar_t *word, wchar_t ***meanings)
{
    *meanings = NULL;
    if (!thes_available() || !th_n) return 0;
    char key[256];
    WideCharToMultiByte(CP_UTF8, 0, word, -1, key, 256, NULL, NULL);
    int lo = 0, hi = th_n - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = th_lower_cmp(th_data + th_idx[mid], key);
        if (!c) { found = mid; break; }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    if (found < 0) return 0;
    const char *line = th_data + th_idx[found];
    const char *bar = strrchr(line, '|');
    int cnt = atoi(bar + 1);
    if (cnt <= 0) return 0;
    wchar_t **m = xmalloc(cnt * sizeof(wchar_t *));
    const char *p = line + strlen(line) + 1;
    int n = 0;
    for (int k = 0; k < cnt && p < th_data + th_len; k++) {
        while (p < th_data + th_len && *p == 0) p++;
        if (p >= th_data + th_len) break;
        size_t l = strlen(p);
        int wl = MultiByteToWideChar(CP_UTF8, 0, p, (int)l, NULL, 0);
        m[n] = xmalloc((wl + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, p, (int)l, m[n], wl);
        m[n][wl] = 0;
        n++;
        p += l + 1;
    }
    *meanings = m;
    return n;
}

void thes_free(wchar_t **m, int n)
{
    for (int i = 0; i < n; i++) free(m[i]);
    free(m);
}

/* ------------------------------------------------------------------ hyphenation (libhyphen) */
#include "../hyphen/hyphen.h"
static HyphenDict *hyd[NLANGS]; static int hy_tried[NLANGS]; static UINT hy_cp[NLANGS];

int hyph_points(const wchar_t *word, int len, int lang, char *out)
{
    if (lang < 0 || lang >= NLANGS || len < 2 || len > 60) return 0;
    if (!hy_tried[lang]) {
        hy_tried[lang] = 1;
        static const wchar_t *files[NLANGS] = { L"hyph_en_US.dic", NULL, L"hyph_de_DE.dic" };
        if (files[lang]) {
            wchar_t dir[MAX_PATH], path[MAX_PATH];
            app_dir(dir);
            swprintf(path, MAX_PATH, L"%lsDictionaries\\%ls", dir, files[lang]);
            FILE *f = _wfopen(path, L"rb");
            if (f) {
                char enc[64] = "";
                if (fgets(enc, sizeof enc, f)) {
                    size_t k = strlen(enc); while (k && (enc[k - 1] == '\n' || enc[k - 1] == '\r')) enc[--k] = 0;
                }
                hy_cp[lang] = cp_from_name(enc);
                rewind(f);
                hyd[lang] = hnj_hyphen_load_file(f);
                fclose(f);
            }
        }
    }
    HyphenDict *hd = hyd[lang];
    if (!hd) return 0;
    wchar_t low[64];
    for (int i = 0; i < len; i++) low[i] = towlower(word[i]);
    low[len] = 0;
    char bytes[256]; int boff[65];
    int bl = 0;
    for (int i = 0; i < len; i++) {
        boff[i] = bl;
        BOOL bad = FALSE;
        int k = WideCharToMultiByte(hy_cp[lang], 0, low + i, 1, bytes + bl, 8, NULL, hy_cp[lang] == CP_UTF8 ? NULL : &bad);
        if (k <= 0 || bad) return 0;
        bl += k;
    }
    boff[len] = bl;
    bytes[bl] = 0;
    char hy[264];
    char **rep = NULL; int *pos = NULL, *cut = NULL;
    if (hnj_hyphen_hyphenate2(hd, bytes, bl, hy, NULL, &rep, &pos, &cut)) return 0;
    for (int i = 0; i < len; i++) out[i] = hy[boff[i + 1] - 1];
    if (rep) { for (int i = 0; i < bl; i++) if (rep[i]) free(rep[i]); free(rep); }
    free(pos); free(cut);
    /* respect the usual minimums: at least 2 letters before, 3 after */
    for (int i = 0; i < len; i++) if (i < 1 || i > len - 4) out[i] = 0;
    return 1;
}
