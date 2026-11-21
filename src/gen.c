/*
 * ScoopStar 7.0 - gen.c
 * Index and table of contents generation.
 *
 * Index entries:  text marked with ^PK ... ^PK, and .ix lines (".ix entry" or ".ix main;sub").
 * Contents entries: .tc lines (".tc text #2" gives level 2) and paragraphs in Heading 1-3 style.
 * The result is written as an RTF document beside the original.
 */
#include "scoopstar.h"

typedef struct { wchar_t key[160]; int page; int level; int seq; } Entry;
static Entry *E; static int nE, capE;

static void add_entry(const wchar_t *key, int page, int level)
{
    wchar_t k[160]; int n = 0;
    for (const wchar_t *q = key; *q && n < 159; q++) {
        if (*q == '\n' || *q == '\t') { if (n && k[n - 1] != ' ') k[n++] = ' '; continue; }
        if (*q == ' ' && (n == 0 || k[n - 1] == ' ')) continue;
        k[n++] = *q;
    }
    while (n && (k[n - 1] == ' ' || k[n - 1] == ',' || k[n - 1] == '.' || k[n - 1] == ':')) n--;
    k[n] = 0;
    if (!n) return;
    if (nE >= capE) { capE = capE ? capE * 2 : 128; E = xrealloc(E, capE * sizeof(Entry)); }
    wcscpy(E[nE].key, k); E[nE].page = page; E[nE].level = level; E[nE].seq = nE; nE++;
}

static int sortkey_cmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && !iswalnum(*a)) a++;
    while (*b && !iswalnum(*b)) b++;
    int r = _wcsicmp(a, b);
    return r;
}
static int idx_cmp(const void *x, const void *y)
{
    const Entry *a = x, *b = y;
    int r = sortkey_cmp(a->key, b->key);
    if (!r) r = wcscmp(a->key, b->key);
    if (!r) r = a->page - b->page;
    return r;
}

static void put_line(Doc *d, const wchar_t *s)
{
    int n = (int)wcslen(s);
    wch *b = xmalloc((n + 1) * sizeof(wch));
    for (int i = 0; i < n; i++) b[i] = s[i];
    doc_append_para(d, b, n);
    free(b);
}
static void put_tagged(Doc *d, const wchar_t *s, wch tag)
{
    int n = (int)wcslen(s);
    wch *b = xmalloc((n + 3) * sizeof(wch));
    int k = 0;
    b[k++] = tag;
    for (int i = 0; i < n; i++) b[k++] = s[i];
    b[k++] = tag;
    doc_append_para(d, b, k);
    free(b);
}

/* page number as printed (honours .pn) */
static int printed_page(Doc *d, int page, int *pnstart, int *pnpage)
{
    (void)d;
    return *pnstart > 0 ? *pnstart + (page - *pnpage) : page;
}

static void collect(Doc *d, int toc)
{
    nE = 0;
    doc_layout(d, 0);
    int pnstart = -1, pnpage = 1;
    int inmark = 0; wchar_t mk[160]; int mkn = 0, mkpage = 1;
    int lastpara = -1;
    for (int i = 0; i < d->nl; i++) {
        Line *l = &d->L[i];
        if (l->type != LT_TEXT) continue;
        Para *p = &d->p[l->p];
        int pg = printed_page(d, l->page, &pnstart, &pnpage);
        if (l->flags & LF_DOT) {
            wchar_t cmd[8], arg[256];
            dot_parse(p->t, p->n, cmd, 8, arg, 256);
            /* a dot line belongs to the page of the text that follows it */
            int fpage = l->page;
            for (int j = i + 1; j < d->nl; j++) if (d->L[j].type == LT_TEXT && !(d->L[j].flags & LF_DOT)) { fpage = d->L[j].page; break; }
            int fpg = printed_page(d, fpage, &pnstart, &pnpage);
            if (!wcscmp(cmd, L"pn") && _wtoi(arg) > 0) { pnstart = _wtoi(arg); pnpage = fpage; }
            else if (!toc && !wcscmp(cmd, L"ix")) add_entry(arg, fpg, 0);
            else if (toc && !wcscmp(cmd, L"tc")) {
                int lv = 1;
                wchar_t *h = wcsrchr(arg, '#');
                if (h && iswdigit(h[1])) { lv = _wtoi(h + 1); *h = 0; }
                else if (h && !h[1]) *h = 0;
                if (lv < 1) lv = 1;
                if (lv > 5) lv = 5;
                add_entry(arg, fpg, lv);
            }
            continue;
        }
        if (toc) {
            if (l->p != lastpara && p->style >= 2 && p->style <= 4) {
                wchar_t t[160]; int n = 0;
                for (int k = 0; k < p->n && n < 159; k++) if (!IS_TAG(p->t[k]) && !IS_SPECIAL(p->t[k]) && p->t[k] < 0x10000 && p->t[k] != SHY) t[n++] = (wchar_t)p->t[k];
                t[n] = 0;
                add_entry(t, pg, p->style - 1);
            }
            lastpara = l->p;
            continue;
        }
        for (int k = l->s; k < l->e; k++) {
            wch c = p->t[k];
            if (c == TG_INDEX) {
                if (inmark) { mk[mkn] = 0; add_entry(mk, mkpage, 0); inmark = 0; }
                else { inmark = 1; mkn = 0; mkpage = pg; }
                continue;
            }
            if (!inmark || IS_TAG(c) || IS_SPECIAL(c) || c == SHY) continue;
            if (mkn < 159 && c < 0x10000) mk[mkn++] = (wchar_t)c;
        }
        if (inmark && (l->flags & LF_LAST) && mkn < 159) mk[mkn++] = ' ';
    }
    if (inmark) { mk[mkn] = 0; add_entry(mk, mkpage, 0); }
}

static void out_path(Doc *d, const wchar_t *suffix, wchar_t *out)
{
    wcscpy(out, d->path[0] ? d->path : L"UNTITLED.rtf");
    wchar_t *dot = wcsrchr(out, '.'), *bs = wcsrchr(out, '\\');
    if (dot && (!bs || dot > bs)) *dot = 0;
    wcscat(out, suffix);
}

/* returns number of entries written, or -1 on error; *outpath receives the file name */
int generate_list(Doc *d, int toc, wchar_t *outpath)
{
    collect(d, toc);
    out_path(d, toc ? L" Contents.rtf" : L" Index.rtf", outpath);
    if (!nE) return 0;
    Doc *o = doc_new();
    o->base = d->base;
    o->base.cols = 1;
    int width = d->base.rm - d->base.lm + 1;
    if (width < 30) width = 30;
    put_tagged(o, toc ? L"CONTENTS" : L"INDEX", TG_BOLD);
    o->p[o->np - 1].style = 1;
    put_line(o, L"");
    if (toc) {
        for (int i = 0; i < nE; i++) {
            wchar_t line[400], pg[16];
            int ind = (E[i].level - 1) * 3;
            swprintf(pg, 16, L"%d", E[i].page);
            int room = width - ind - (int)wcslen(pg) - 2;
            wchar_t key[160]; wcsncpy(key, E[i].key, 159); key[159] = 0;
            if ((int)wcslen(key) > room) key[room > 4 ? room - 3 : 1] = 0, wcscat(key, L"...");
            int k = 0;
            for (int j = 0; j < ind; j++) line[k++] = 0xA0;
            for (wchar_t *q = key; *q; q++) line[k++] = *q;
            line[k++] = ' ';
            while (k < width - (int)wcslen(pg) - 1) line[k++] = '.';
            line[k++] = 0xA0;
            for (wchar_t *q = pg; *q; q++) line[k++] = *q;
            line[k] = 0;
            put_line(o, line);
            o->p[o->np - 1].style = 0;
        }
    } else {
        qsort(E, nE, sizeof(Entry), idx_cmp);
        wchar_t letter = 0;
        for (int i = 0; i < nE; ) {
            int j = i;
            /* merge same key */
            int pages[400], np = 0;
            while (j < nE && !wcscmp(E[j].key, E[i].key)) {
                if (np < 400 && (np == 0 || pages[np - 1] != E[j].page)) pages[np++] = E[j].page;
                j++;
            }
            const wchar_t *k0 = E[i].key; while (*k0 && !iswalnum(*k0)) k0++;
            wchar_t L0 = (wchar_t)towupper(*k0 ? *k0 : E[i].key[0]);
            if (L0 != letter) {
                if (letter) put_line(o, L"");
                wchar_t h[2] = { L0, 0 };
                put_tagged(o, h, TG_BOLD);
                letter = L0;
            }
            /* "main;sub" entries are shown indented under the main heading */
            wchar_t main[160], sub[160] = L"";
            wcscpy(main, E[i].key);
            wchar_t *semi = wcschr(main, ';');
            if (semi) { *semi = 0; wcscpy(sub, semi + 1); while (sub[0] == ' ') memmove(sub, sub + 1, wcslen(sub) * sizeof(wchar_t)); }
            wchar_t line[1200]; int k = 0;
            if (semi) {
                int had = 0;
                if (i > 0) { wchar_t pm[160]; wcscpy(pm, E[i - 1].key); wchar_t *s2 = wcschr(pm, ';'); if (s2) *s2 = 0; if (!wcscmp(pm, main)) had = 1; }
                if (!had) put_line(o, main);
                for (int q = 0; q < 4; q++) line[k++] = 0xA0;
                for (wchar_t *q = sub; *q && k < 600; q++) line[k++] = *q;
            } else for (wchar_t *q = main; *q && k < 600; q++) line[k++] = *q;
            line[k++] = ','; line[k++] = ' ';
            for (int a = 0; a < np && k < 1150; ) {
                int b = a;
                while (b + 1 < np && pages[b + 1] == pages[b] + 1) b++;
                wchar_t t[32];
                if (b > a + 1) swprintf(t, 32, L"%d-%d", pages[a], pages[b]);
                else if (b == a + 1) swprintf(t, 32, L"%d, %d", pages[a], pages[b]);
                else swprintf(t, 32, L"%d", pages[a]);
                for (wchar_t *q = t; *q; q++) line[k++] = *q;
                a = b + 1;
                if (a < np) { line[k++] = ','; line[k++] = ' '; }
            }
            line[k] = 0;
            put_line(o, line);
            i = j;
        }
    }
    wcscpy(o->path, outpath);
    int ok = save_rtf(o, outpath, NULL, NULL);
    doc_free(o);
    return ok ? nE : -1;
}
