/*
 * ScoopStar 7.0 - print.c
 * Printing through Windows printers and the graphical Page Preview (^OP).
 * Pages are laid out exactly as on screen: 10 characters per inch across,
 * 6 lines per inch down, with WordStar's margins, headers and footers.
 */
#include "scoopstar.h"
#include <math.h>
#include <commdlg.h>

#define HFN 5
typedef struct {
    wchar_t he[HFN][160], fo[HFN][160];
    int op, number;
    int en0, en1;          /* endnote pages: range in g_enlist */
    Fmt f;
} PageInfo;

extern void (*g_overlay)(HDC hdc, int w, int h);
static int g_ptotal;                 /* pages including endnote pages */
static int *g_enlist, g_nen;         /* endnote indices in number order */

/* GDI+ flat API, for graphics */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
int WINAPI GdiplusStartup(ULONG_PTR *, const GpStartupInput *, void *);
int WINAPI GdipLoadImageFromFile(const WCHAR *, void **);
int WINAPI GdipCreateFromHDC(HDC, void **);
int WINAPI GdipDrawImageRectI(void *, void *, INT, INT, INT, INT);
int WINAPI GdipDisposeImage(void *);
int WINAPI GdipDeleteGraphics(void *);
int WINAPI GdipGetImageWidth(void *, UINT *);
int WINAPI GdipGetImageHeight(void *, UINT *);
static ULONG_PTR gp_token; static int gp_ok;
static int gp_init(void)
{
    if (!gp_ok) { GpStartupInput in = { 1, NULL, FALSE, FALSE }; gp_ok = GdiplusStartup(&gp_token, &in, NULL) == 0 ? 1 : -1; }
    return gp_ok == 1;
}

/* pixel size of an image file; 0 if it can't be read */
int image_size(const wchar_t *path, int *w, int *h)
{
    if (!gp_init()) return 0;
    void *img = NULL;
    if (GdipLoadImageFromFile(path, &img) || !img) return 0;
    UINT a = 0, b = 0; GdipGetImageWidth(img, &a); GdipGetImageHeight(img, &b);
    GdipDisposeImage(img);
    *w = (int)a; *h = (int)b;
    return a && b;
}

static void graphic_path(Doc *d, const wchar_t *in, wchar_t *out)
{
    if ((in[0] && in[1] == ':') || (in[0] == '\\' && in[1] == '\\')) { wcscpy(out, in); return; }
    wcscpy(out, d->path);
    wchar_t *q = wcsrchr(out, '\\'); if (q) q[1] = 0; else out[0] = 0;
    wcscat(out, in);
}

static void draw_graphic(Doc *d, HDC dc, const wchar_t *path, double x, double y, double w, double h)
{
    wchar_t full[MAX_PATH * 2]; graphic_path(d, path, full);
    void *img = NULL, *g = NULL;
    if (gp_init() && !GdipLoadImageFromFile(full, &img) && img && !GdipCreateFromHDC(dc, &g) && g) {
        GdipDrawImageRectI(g, img, (int)x, (int)y, (int)w, (int)h);
    } else {
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, (int)x, (int)y, (int)(x + w), (int)(y + h));
        SelectObject(dc, ob);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutW(dc, (int)x + 4, (int)y + 4, L"[graphic not found]", 19);
        SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    }
    if (g) GdipDeleteGraphics(g);
    if (img) GdipDisposeImage(img);
}

/* header/footer line from a dot command: returns 'h' or 'f', sets line index and parity (0 both, 1 even, 2 odd) */
static int hf_cmd(const wchar_t *cmd, int *line, int *par)
{
    *line = 0; *par = 0;
    if (!wcscmp(cmd, L"he")) return 'h';
    if (!wcscmp(cmd, L"fo")) return 'f';
    if ((cmd[0] == 'h' || cmd[0] == 'f') && cmd[1] >= '1' && cmd[1] <= '5') {
        if (cmd[2] == 'e') *par = 1; else if (cmd[2] == 'o') *par = 2; else if (cmd[2]) return 0;
        *line = cmd[1] - '1';
        return cmd[0];
    }
    return 0;
}

static int en_cmp(const void *a, const void *b);
static Doc *en_doc;
static int en_cmp(const void *a, const void *b)
{
    return en_doc->notes[*(const int *)a].num - en_doc->notes[*(const int *)b].num;
}

static PageInfo *build_pages(Doc *d)
{
    doc_layout(d, 0);
    /* endnotes, in number order, paginated after the body */
    free(g_enlist); g_enlist = xmalloc((d->nnotes + 1) * sizeof(int)); g_nen = 0;
    for (int i = 0; i < d->nl; i++) {
        Line *l = &d->L[i];
        if (l->type != LT_TEXT) continue;
        Para *p = &d->p[l->p];
        for (int k = l->s; k < l->e; k++) if (IS_NOTE(p->t[k])) {
            int ni = (int)(p->t[k] - NOTEBASE);
            if (ni < d->nnotes && d->notes[ni].kind == NOTE_END) g_enlist[g_nen++] = ni;
        }
    }
    en_doc = d; qsort(g_enlist, g_nen, sizeof(int), en_cmp);
    int extra = 0;
    {
        Fmt *f = &d->base;
        int usable = f->pl - f->mt - f->mb, used = 2;   /* heading */
        int w = f->rm - f->lm + 1;
        if (g_nen) extra = 1;
        for (int i = 0; i < g_nen; i++) {
            int n = note_wrap_lines(d, g_enlist[i], w) + 1;
            if (used + n > usable && used > 0) { extra++; used = 0; }
            used += n;
        }
    }
    g_ptotal = d->npages + extra;
    PageInfo *pg = xmalloc((g_ptotal + 2) * sizeof(PageInfo));
    memset(pg, 0, (g_ptotal + 2) * sizeof(PageInfo));
    wchar_t hd[3][HFN][160], ft[3][HFN][160];
    memset(hd, 0, sizeof(hd)); memset(ft, 0, sizeof(ft));
    int op = d->base.op, pnstart = -1, pnpage = 1;
    int lastpage = 0;
    for (int i = 0; i < d->nl; i++) {
        Line *l = &d->L[i];
        if (l->type != LT_TEXT && l->type != LT_GRAPHIC) continue;
        Para *p = &d->p[l->p];
        if (l->type == LT_TEXT && (l->flags & LF_DOT)) {
            wchar_t cmd[8], arg[256];
            dot_parse(p->t, p->n, cmd, 8, arg, 256);
            int ln, par, k = hf_cmd(cmd, &ln, &par);
            if (k == 'h') { wcsncpy(hd[par][ln], arg, 159); hd[par][ln][159] = 0; }
            else if (k == 'f') { wcsncpy(ft[par][ln], arg, 159); ft[par][ln][159] = 0; }
            else if (!wcscmp(cmd, L"op")) op = 1;
            else if (!wcscmp(cmd, L"pg")) op = 0;
            else if (!wcscmp(cmd, L"pn")) { pnstart = _wtoi(arg); pnpage = l->page; }
            continue;
        }
        if (l->page != lastpage && l->page <= d->npages) {
            PageInfo *q = &pg[l->page];
            q->op = op;
            q->number = pnstart > 0 ? pnstart + (l->page - pnpage) : l->page;
            int par = (q->number % 2 == 0) ? 1 : 2;
            for (int j = 0; j < HFN; j++) {
                wcscpy(q->he[j], hd[par][j][0] ? hd[par][j] : hd[0][j]);
                wcscpy(q->fo[j], ft[par][j][0] ? ft[par][j] : ft[0][j]);
            }
            q->f = doc_fmt_at(d, l->p);
            lastpage = l->page;
        }
    }
    for (int p = 1; p <= d->npages; p++) if (!pg[p].f.pl) { pg[p].f = d->base; pg[p].number = p; }
    /* endnote pages carry on the numbering and the last headers */
    if (extra) {
        Fmt *f = &d->base;
        int usable = f->pl - f->mt - f->mb, used = 2, w = f->rm - f->lm + 1, pgn = d->npages + 1;
        pg[pgn].en0 = 0;
        for (int i = 0; i < g_nen; i++) {
            int n = note_wrap_lines(d, g_enlist[i], w) + 1;
            if (used + n > usable && used > 0) { pg[pgn].en1 = i; pgn++; pg[pgn].en0 = i; used = 0; }
            used += n;
        }
        pg[pgn].en1 = g_nen;
        for (int p = d->npages + 1; p <= g_ptotal; p++) {
            PageInfo *q = &pg[p], *last = &pg[d->npages];
            q->f = last->f; q->op = last->op; q->number = last->number + (p - d->npages);
            for (int j = 0; j < HFN; j++) { wcscpy(q->he[j], last->he[j]); wcscpy(q->fo[j], last->fo[j]); }
        }
    }
    return pg;
}

typedef struct { int font, fx, h; HFONT hf; } FontCache;
static FontCache fcache[64]; static int nfc;

static void fcache_clear(void)
{
    for (int i = 0; i < nfc; i++) DeleteObject(fcache[i].hf);
    nfc = 0;
}

static HFONT get_font_sz(int font, int fx, double dpiy, int hp);
static HFONT get_font(int font, int fx, double dpiy)
{
    return get_font_sz(font, fx, dpiy, font >= 0 && font < g_nfonts ? g_fonts[font].halfpts : DEFAULT_HALFPTS);
}
static HFONT get_font_sz(int font, int fx, double dpiy, int hp)
{
    int h = (int)(hp / 2.0 * dpiy / 72.0 + 0.5);
    if (fx & (FX_SUPER | FX_SUB)) h = h * 2 / 3;
    if (h < 1) h = 1;
    for (int i = 0; i < nfc; i++) if (fcache[i].font == font && fcache[i].fx == fx && fcache[i].h == h) return fcache[i].hf;
    (void)0;
    if (nfc >= 64) fcache_clear();
    const wchar_t *name = font >= 0 && font < g_nfonts ? g_fonts[font].name : DEFAULT_FONT;
    HFONT hf = CreateFontW(-h, 0, 0, 0, (fx & (FX_BOLD | FX_DOUBLE)) ? FW_BOLD : FW_NORMAL, (fx & FX_ITALIC) != 0,
                           (fx & FX_UNDER) != 0, (fx & FX_STRIKE) != 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, name);
    fcache[nfc].font = font; fcache[nfc].fx = fx; fcache[nfc].h = h; fcache[nfc].hf = hf; nfc++;
    return hf;
}

static void draw_hf(HDC dc, const wchar_t *text, int number, double x, double y, double dpiy)
{
    wchar_t out[300]; int k = 0;
    for (const wchar_t *s = text; *s && k < 290; s++) {
        if (*s == '#') { wchar_t nb[16]; swprintf(nb, 16, L"%d", number); for (wchar_t *q = nb; *q; q++) out[k++] = *q; }
        else out[k++] = *s;
    }
    out[k] = 0;
    SelectObject(dc, get_font(-1, 0, dpiy));
    TextOutW(dc, (int)x, (int)y, out, k);
}

/* lays out (and optionally draws) one footnote; returns the number of lines used */
#define NOTE_HP 20
static int note_flow(HDC dc, Doc *d, int idx, int font, double x0, double width, double ytop, double line_h, double dpiy, int draw)
{
    if (idx < 0 || idx >= d->nnotes) return 0;
    Note *nt = &d->notes[idx];
    int lines = 1;
    double base = ytop + line_h * 0.78, x = x0, right = x0 + width;
    wchar_t lab[16];
    if (nt->kind == NOTE_ANNOT) wcscpy(lab, nt->mark[0] ? nt->mark : L"*"); else swprintf(lab, 16, L"%d", nt->num);
    SIZE sz;
    SelectObject(dc, get_font_sz(font, FX_SUPER, dpiy, NOTE_HP));
    GetTextExtentPoint32W(dc, lab, (int)wcslen(lab), &sz);
    if (draw) TextOutW(dc, (int)x, (int)(base - line_h * 0.28), lab, (int)wcslen(lab));
    x += sz.cx;
    SelectObject(dc, get_font_sz(font, 0, dpiy, NOTE_HP));
    GetTextExtentPoint32W(dc, L" ", 1, &sz);
    x += sz.cx;
    uint8_t fx = 0;
    int i = 0;
    while (i < nt->n) {
        wch c = nt->t[i];
        if (IS_TAG(c)) {
            if (c == TG_BOLD || c == TG_DOUBLE) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC; else if (c == TG_UNDER) fx ^= FX_UNDER;
            else if (c == TG_STRIKE) fx ^= FX_STRIKE; else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
            i++; continue;
        }
        if (c == '\n') { lines++; base += line_h; x = x0; i++; continue; }
        if (c == ' ' || c == '\t' || c == 0xA0 || IS_SPECIAL(c) || c == SHY) {
            if (!IS_SPECIAL(c) && c != SHY) { SelectObject(dc, get_font_sz(font, fx, dpiy, NOTE_HP)); GetTextExtentPoint32W(dc, L" ", 1, &sz); x += sz.cx; }
            i++; continue;
        }
        /* a word, possibly in several formatted pieces */
        int ps[24], pl[24]; uint8_t pf[24]; int np = 0;
        uint8_t wfx = fx;
        int j = i;
        while (j < nt->n && np < 24) {
            wch q = nt->t[j];
            if (q == ' ' || q == '\t' || q == '\n' || q == 0xA0 || IS_SPECIAL(q)) break;
            if (q == SHY) { j++; continue; }
            if (IS_TAG(q)) {
                if (q == TG_BOLD || q == TG_DOUBLE) wfx ^= FX_BOLD; else if (q == TG_ITALIC) wfx ^= FX_ITALIC; else if (q == TG_UNDER) wfx ^= FX_UNDER;
                else if (q == TG_STRIKE) wfx ^= FX_STRIKE; else if (q == TG_SUPER) wfx ^= FX_SUPER; else if (q == TG_SUB) wfx ^= FX_SUB;
                j++; continue;
            }
            if (np == 0 || pf[np - 1] != wfx || ps[np - 1] + pl[np - 1] != j) { ps[np] = j; pl[np] = 0; pf[np] = wfx; np++; }
            pl[np - 1]++;
            j++;
        }
        double ww = 0;
        for (int k = 0; k < np; k++) {
            wchar_t buf[256]; int bn = 0;
            for (int m = 0; m < pl[k] && bn < 250; m++) { wch q = nt->t[ps[k] + m]; if (q >= 0x10000) { q -= 0x10000; buf[bn++] = 0xD800 + (q >> 10); buf[bn++] = 0xDC00 + (q & 0x3FF); } else buf[bn++] = (wchar_t)q; }
            SelectObject(dc, get_font_sz(font, pf[k], dpiy, NOTE_HP));
            GetTextExtentPoint32W(dc, buf, bn, &sz); ww += sz.cx;
        }
        if (x + ww > right && x > x0 + 1) { lines++; base += line_h; x = x0; }
        for (int k = 0; k < np; k++) {
            wchar_t buf[256]; int bn = 0;
            for (int m = 0; m < pl[k] && bn < 250; m++) { wch q = nt->t[ps[k] + m]; if (q >= 0x10000) { q -= 0x10000; buf[bn++] = 0xD800 + (q >> 10); buf[bn++] = 0xDC00 + (q & 0x3FF); } else buf[bn++] = (wchar_t)q; }
            SelectObject(dc, get_font_sz(font, pf[k], dpiy, NOTE_HP));
            GetTextExtentPoint32W(dc, buf, bn, &sz);
            double yy = base;
            if (pf[k] & FX_SUPER) yy -= line_h * 0.28;
            if (pf[k] & FX_SUB) yy += line_h * 0.15;
            if (draw) TextOutW(dc, (int)x, (int)yy, buf, bn);
            x += sz.cx;
        }
        fx = wfx;
        i = j;
    }
    return lines;
}

/* render one page; x0,y0 = device position of the paper's top-left corner */
static void render_page(Doc *d, PageInfo *pi, HDC dc, int page, double dpix, double dpiy, double x0, double y0)
{
    int saved_tags = S.show_tags;
    S.show_tags = 0;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    PageInfo *P = &pi[page];
    Fmt *f = &P->f;
    double col_w = dpix / 10.0, line_h = dpiy / 6.0;
    double left = x0 + f->po * col_w;
    double base_off = line_h * 0.78;
    int nhe = 0, nfo = 0;
    for (int j = 0; j < HFN; j++) { if (P->he[j][0]) nhe = j + 1; if (P->fo[j][0]) nfo = j + 1; }
    for (int j = 0; j < nhe; j++) {
        double y = y0 + (f->mt - f->hm - 1 - (nhe - 1 - j)) * line_h;
        if (y < y0 + dpiy * 0.25) y = y0 + dpiy * 0.25 + j * line_h;
        if (P->he[j][0]) draw_hf(dc, P->he[j], P->number, left + (f->lm - 1) * col_w, y + base_off, dpiy);
    }
    double fy = y0 + (f->pl - f->mb + f->fm - 1) * line_h + base_off;
    if (nfo) { for (int j = 0; j < nfo; j++) if (P->fo[j][0]) draw_hf(dc, P->fo[j], P->number, left + (f->lm - 1) * col_w, fy + j * line_h, dpiy); }
    else if (!P->op) {
        wchar_t nb[16]; swprintf(nb, 16, L"%d", P->number);
        SelectObject(dc, get_font(-1, 0, dpiy));
        SetTextAlign(dc, TA_CENTER | TA_BASELINE);
        TextOutW(dc, (int)(left + (f->lm - 1 + (f->rm - f->lm + 1) / 2.0) * col_w), (int)fy, nb, (int)wcslen(nb));
        SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    }
    if (page > d->npages) {
        /* endnote page */
        double nx0 = left + (f->lm - 1) * col_w, width = (f->rm - f->lm + 1) * col_w;
        double y = y0 + f->mt * line_h;
        if (P->en0 == 0) {
            SelectObject(dc, get_font(-1, FX_BOLD, dpiy));
            SetTextAlign(dc, TA_CENTER | TA_BASELINE);
            TextOutW(dc, (int)(nx0 + width / 2), (int)(y + base_off), L"Notes", 5);
            SetTextAlign(dc, TA_LEFT | TA_BASELINE);
            y += 2 * line_h;
        }
        for (int i = P->en0; i < P->en1; i++) y += (note_flow(dc, d, g_enlist[i], -1, nx0, width, y, line_h, dpiy, 1) + 0.5) * line_h;
        S.show_tags = saved_tags;
        return;
    }
    int refs[64], reffont[64], nref = 0;
    for (int i = 0; i < d->nl; i++) {
        Line *l = &d->L[i];
        if (l->page != page) { if (l->page > page) break; continue; }
        if (l->type == LT_GRAPHIC) {
            if (l->s == 0) {
                Para *gp = &d->p[l->p];
                double gw, gh; wchar_t path[MAX_PATH];
                if (graphic_parse(gp->t, gp->n, &gw, &gh, path, MAX_PATH))
                    draw_graphic(d, dc, path, left + (l->col + l->xoff) * col_w, y0 + (f->mt + l->pline - 1) * line_h + line_h * 0.15, gw * dpix, gh * dpiy);
            }
            continue;
        }
        if (l->type != LT_TEXT || (l->flags & LF_DOT)) continue;
        Para *p = &d->p[l->p];
        ParaStyle *ps = &g_pstyles[p->style < g_npstyles ? p->style : 0];
        short *cols;
        int len = line_cols(d, l, &cols);
        short *cc = xmalloc((len + 1) * sizeof(short));
        memcpy(cc, cols, (len + 1) * sizeof(short));
        for (int j = 0; j <= len; j++) cc[j] += l->xoff;
        double y = y0 + (f->mt + l->y2 / 2.0) * line_h + base_off;
        uint8_t fx = l->fx0; int font = l->font0;
        int k = 0;
        double xend = -1e9; int gap = 0;
        (void)0;
        while (k < len) {
            wch c = p->t[l->s + k];
            if (IS_TAG(c)) {
                if (IS_FONTTAG(c)) font = (int)(c - TG_FONT0);
                else if (c == TG_BOLD) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC; else if (c == TG_UNDER) fx ^= FX_UNDER;
                else if (c == TG_STRIKE) fx ^= FX_STRIKE; else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
                else if (c == TG_DOUBLE) fx ^= FX_DOUBLE;
                k++; continue;
            }
            uint8_t efx = fx | (ps->bold ? FX_BOLD : 0) | (ps->italic ? FX_ITALIC : 0);
            if (IS_PNUM(c)) {
                SelectObject(dc, get_font(font, efx, dpiy));
                double x = left + cc[k] * col_w;
                if (gap) { SIZE sp; GetTextExtentPoint32W(dc, L" ", 1, &sp); if (x < xend + sp.cx) x = xend + sp.cx; }
                else if (x < xend) x = xend;
                TextOutW(dc, (int)x, (int)y, p->pnlab, (int)wcslen(p->pnlab));
                SIZE sz; GetTextExtentPoint32W(dc, p->pnlab, (int)wcslen(p->pnlab), &sz);
                xend = x + sz.cx; gap = 0;
                k++; continue;
            }
            if (IS_NOTE(c)) {
                int ni = (int)(c - NOTEBASE);
                wchar_t nb[16];
                if (ni < d->nnotes && d->notes[ni].kind == NOTE_ANNOT) wcscpy(nb, d->notes[ni].mark[0] ? d->notes[ni].mark : L"*");
                else swprintf(nb, 16, L"%d", ni < d->nnotes ? d->notes[ni].num : 0);
                SelectObject(dc, get_font(font, (efx & ~FX_SUB) | FX_SUPER, dpiy));
                double x = left + cc[k] * col_w;
                if (x < xend) x = xend;
                TextOutW(dc, (int)x, (int)(y - line_h * 0.33), nb, (int)wcslen(nb));
                SIZE sz; GetTextExtentPoint32W(dc, nb, (int)wcslen(nb), &sz);
                xend = x + sz.cx; gap = 0;
                if (nref < 64 && ni < d->nnotes && d->notes[ni].kind != NOTE_END) { refs[nref] = ni; reffont[nref] = font; nref++; }
                k++; continue;
            }
            if (c == '\t' || c == ' ' || c == 0xA0) {
                gap = 1;
                if (efx & (FX_UNDER | FX_STRIKE)) {
                    /* keep underline/strikeout continuous across spaces between marked words */
                    int more = 0;
                    for (int j = k + 1; j < len; j++) { wch q = p->t[l->s + j]; if (q == TG_UNDER || q == TG_STRIKE) break; if (!IS_TAG(q) && q != ' ' && q != '\t') { more = 1; break; } }
                    if (more) {
                        HPEN pen = CreatePen(PS_SOLID, (int)(dpiy / 100) + 1, RGB(0, 0, 0));
                        HGDIOBJ op = SelectObject(dc, pen);
                        double x1 = left + cc[k] * col_w, x2 = left + cc[k + 1] * col_w;
                        if (efx & FX_UNDER) { MoveToEx(dc, (int)x1, (int)(y + dpiy * 0.03), NULL); LineTo(dc, (int)x2, (int)(y + dpiy * 0.03)); }
                        if (efx & FX_STRIKE) { MoveToEx(dc, (int)x1, (int)(y - dpiy * 0.045), NULL); LineTo(dc, (int)x2, (int)(y - dpiy * 0.045)); }
                        SelectObject(dc, op); DeleteObject(pen);
                    }
                }
                k++; continue;
            }
            /* a word run with the same formatting */
            wchar_t run[512]; int rn = 0, start = k;
            while (k < len && rn < 500) {
                wch q = p->t[l->s + k];
                if (IS_TAG(q) || IS_SPECIAL(q) || q == ' ' || q == '\t' || q == 0xA0) break;
                if (q == SHY) { if (k == len - 1 && !(l->flags & LF_LAST)) run[rn++] = '-'; k++; continue; }
                if (q >= 0x10000) { wch v = q - 0x10000; run[rn++] = 0xD800 + (v >> 10); run[rn++] = 0xDC00 + (v & 0x3FF); }
                else run[rn++] = (wchar_t)q;
                k++;
            }
            SelectObject(dc, get_font(font, efx, dpiy));
            double yy = y;
            if (efx & FX_SUPER) yy -= line_h * 0.33;
            if (efx & FX_SUB) yy += line_h * 0.18;
            /* words sit on the 10-per-inch grid, but never overlap a wider (larger or proportional) word before them */
            double x = left + cc[start] * col_w;
            if (gap) { SIZE sp; GetTextExtentPoint32W(dc, L" ", 1, &sp); if (x < xend + sp.cx) x = xend + sp.cx; }
            else if (x < xend) x = xend;
            TextOutW(dc, (int)x, (int)yy, run, rn);
            SIZE sz; GetTextExtentPoint32W(dc, run, rn, &sz);
            xend = x + sz.cx; gap = 0;
        }
        if (l->flags & LF_HYPH) TextOutW(dc, (int)xend, (int)y, L"-", 1);
        free(cc);
    }
    /* footnotes at the foot of the page */
    if (nref) {
        int usable = f->pl - f->mt - f->mb;
        double bottom = y0 + (f->mt + usable) * line_h;
        double nx0 = left + (f->lm - 1) * col_w, width = (f->rm - f->lm + 1) * col_w;
        int total = 0;
        for (int i = 0; i < nref; i++) total += note_flow(dc, d, refs[i], reffont[i], nx0, width, 0, line_h, dpiy, 0);
        double y = bottom - total * line_h;
        HPEN pen = CreatePen(PS_SOLID, (int)(dpiy / 100) + 1, RGB(0, 0, 0));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, (int)nx0, (int)(y - line_h * 0.45), NULL);
        LineTo(dc, (int)(nx0 + dpix * 2), (int)(y - line_h * 0.45));
        SelectObject(dc, op); DeleteObject(pen);
        for (int i = 0; i < nref; i++) y += note_flow(dc, d, refs[i], reffont[i], nx0, width, y, line_h, dpiy, 1) * line_h;
    }
    S.show_tags = saved_tags;
}

/* sets the printer's paper to the page's size and orientation; returns 1 if the DC was changed */
static int set_paper(HDC dc, HGLOBAL hdm, Fmt *f, int *cur_w, int *cur_l, int *cur_land)
{
    if (!hdm) return 0;
    int w = f->pw > 0 ? f->pw : 85, l = f->pl, land = f->land;    /* tenths of an inch, lines */
    if (w == *cur_w && l == *cur_l && land == *cur_land) return 0;
    *cur_w = w; *cur_l = l; *cur_land = land;
    DEVMODEW *dm = GlobalLock(hdm);
    if (!dm) return 0;
    /* the paper as it goes into the printer is portrait; landscape turns the page */
    double win = w / 10.0, lin = l / 6.0;
    if (land) { double t = win; win = lin; lin = t; }
    static const struct { double w, h; short id; } std[] = {
        { 8.5, 11, DMPAPER_LETTER }, { 8.5, 14, DMPAPER_LEGAL }, { 5.5, 8.5, DMPAPER_STATEMENT },
        { 7.25, 10.5, DMPAPER_EXECUTIVE }, { 8.27, 11.69, DMPAPER_A4 }, { 5.83, 8.27, DMPAPER_A5 },
        { 4.125, 9.5, DMPAPER_ENV_10 },
    };
    short id = 0;
    for (int i = 0; i < (int)(sizeof(std) / sizeof(std[0])); i++)
        if (fabs(std[i].w - win) < 0.08 && fabs(std[i].h - lin) < 0.08) id = std[i].id;
    dm->dmFields |= DM_ORIENTATION | DM_PAPERSIZE;
    dm->dmOrientation = land ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    if (id) { dm->dmPaperSize = id; dm->dmFields &= ~(DM_PAPERWIDTH | DM_PAPERLENGTH); }
    else {
        dm->dmPaperSize = DMPAPER_USER;
        dm->dmFields |= DM_PAPERWIDTH | DM_PAPERLENGTH;
        dm->dmPaperWidth = (short)(win * 254 + 0.5);     /* tenths of a millimetre */
        dm->dmPaperLength = (short)(lin * 254 + 0.5);
    }
    HDC r = dc ? ResetDCW(dc, dm) : NULL;
    GlobalUnlock(hdm);
    return r != NULL;
}

int print_document(Doc *d)
{
    doc_layout(d, 0);
    PRINTDLGW pd; memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = g_hwnd;
    pd.Flags = PD_RETURNDC | PD_ALLPAGES | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    PageInfo *pi = build_pages(d);
    pd.nMinPage = 1; pd.nMaxPage = (WORD)(g_ptotal > 65000 ? 65000 : g_ptotal);
    pd.nFromPage = 1; pd.nToPage = pd.nMaxPage;
    pd.nCopies = 1;
    /* start the Print dialog with the document's paper and orientation */
    {
        PRINTDLGW def; memset(&def, 0, sizeof(def));
        def.lStructSize = sizeof(def); def.hwndOwner = g_hwnd; def.Flags = PD_RETURNDEFAULT;
        if (PrintDlgW(&def) && def.hDevMode) {
            int a = -1, b = -1, c = -1;
            for (int p = 1; p <= g_ptotal; p++) if (pi[p].f.pl) { set_paper(NULL, def.hDevMode, &pi[p].f, &a, &b, &c); break; }
            pd.hDevMode = def.hDevMode; pd.hDevNames = def.hDevNames;
        }
    }
    if (!PrintDlgW(&pd)) { ev_flush(); free(pi); return 0; }
    ev_flush();
    HDC dc = pd.hDC;
    int from = 1, to = g_ptotal;
    if (pd.Flags & PD_PAGENUMS) { from = pd.nFromPage; to = pd.nToPage; }
    double dpix = GetDeviceCaps(dc, LOGPIXELSX), dpiy = GetDeviceCaps(dc, LOGPIXELSY);
    double offx = GetDeviceCaps(dc, PHYSICALOFFSETX), offy = GetDeviceCaps(dc, PHYSICALOFFSETY);
    DOCINFOW di; memset(&di, 0, sizeof(di)); di.cbSize = sizeof(di);
    const wchar_t *nm = wcsrchr(d->path, '\\');
    di.lpszDocName = nm ? nm + 1 : (d->path[0] ? d->path : L"ScoopStar document");
    int ok = 1;
    int cw = 85, cl = 66, cland = 0;     /* the printer's paper as chosen in the Print dialog is used until a page differs */
    {
        DEVMODEW *dm = pd.hDevMode ? GlobalLock(pd.hDevMode) : NULL;
        if (dm) { cw = -1; GlobalUnlock(pd.hDevMode); }    /* always set the first page's paper */
    }
    if (StartDocW(dc, &di) > 0) {
        fcache_clear();
        for (int pg = from; pg <= to && pg <= g_ptotal; pg++) {
            Cell *save = xmalloc(sizeof(scr)); scr_save(save);
            wchar_t m[80]; swprintf(m, 80, L" Printing page %d of %d ... (Esc cancels) ", pg, to);
            int w = (int)wcslen(m) + 4;
            scr_rect(SR / 2 - 1, (SC - w) / 2, 3, w, ' ', COL[C_DLG]);
            scr_box(SR / 2 - 1, (SC - w) / 2, 3, w, COL[C_DLG], 0);
            scr_puts(SR / 2, (SC - w) / 2 + 2, m, COL[C_DLG]);
            Event e; ev_get(&e, 0);
            scr_restore(save); free(save);
            if (e.type == EV_KEY && e.key == K_ESC) { AbortDoc(dc); ok = 0; break; }
            if (set_paper(dc, pd.hDevMode, &pi[pg].f, &cw, &cl, &cland)) {
                dpix = GetDeviceCaps(dc, LOGPIXELSX); dpiy = GetDeviceCaps(dc, LOGPIXELSY);
                offx = GetDeviceCaps(dc, PHYSICALOFFSETX); offy = GetDeviceCaps(dc, PHYSICALOFFSETY);
                fcache_clear();
            }
            StartPage(dc);
            render_page(d, pi, dc, pg, dpix, dpiy, -offx, -offy);
            EndPage(dc);
        }
        if (ok) EndDoc(dc);
        fcache_clear();
    } else { ok = 0; msgbox(L"Print", L"The printer could not be started.", L"OK"); }
    DeleteDC(dc);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    free(pi);
    return ok;
}

/* ------------------------------------------------------------------ preview */
static Doc *pv_doc; static PageInfo *pv_pi; static int pv_page, pv_zoom, pv_scroll, pv_two;

static void draw_one(HDC dc, int page, int px, int py, int pw, int ph)
{
    double paperin = (pv_pi[page].f.pw > 0 ? pv_pi[page].f.pw : 85) / 10.0;
    RECT r = { px, py, px + pw, py + ph };
    RECT sh = { px + 5, py + 5, px + pw + 5, py + ph + 5 };
    FillRect(dc, &sh, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    double dpi = pw / paperin;
    HRGN rg = CreateRectRgn(px, py, px + pw, py + ph);
    SelectClipRgn(dc, rg);
    render_page(pv_doc, pv_pi, dc, page, dpi, dpi, px, py);
    SelectClipRgn(dc, NULL);
    DeleteObject(rg);
}

static void pv_paint(HDC hdc, int W, int H)
{
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ ob = SelectObject(dc, bm);
    RECT all = { 0, 0, W, H };
    HBRUSH bg = CreateSolidBrush(g_scheme >= 3 ? RGB(0x18, 0x18, 0x18) : RGB(0x55, 0x55, 0x55));
    FillRect(dc, &all, bg); DeleteObject(bg);
    int bar = H / 22 + 6;
    Fmt *f = &pv_pi[pv_page].f;
    double pagein = f->pl / 6.0, paperin = (f->pw > 0 ? f->pw : 85) / 10.0;
    int avail_h = H - bar - 20, avail_w = W - 40;
    int pw, ph;
    if (pv_zoom) { pw = avail_w; ph = (int)(pw * pagein / paperin); }
    else {
        ph = avail_h; pw = (int)(ph * paperin / pagein);
        int maxw = pv_two ? avail_w / 2 - 10 : avail_w;
        if (pw > maxw) { pw = maxw; ph = (int)(pw * pagein / paperin); }
    }
    fcache_clear();
    if (pv_zoom) {
        if (pv_scroll > ph - avail_h) pv_scroll = ph - avail_h;
        if (pv_scroll < 0) pv_scroll = 0;
        draw_one(dc, pv_page, 20, 10 - pv_scroll, pw, ph);
    } else if (pv_two && pv_page + 1 <= g_ptotal) {
        int x = (W - (pw * 2 + 20)) / 2;
        draw_one(dc, pv_page, x, 10, pw, ph);
        draw_one(dc, pv_page + 1, x + pw + 20, 10, pw, ph);
    } else draw_one(dc, pv_page, (W - pw) / 2, 10, pw, ph);
    fcache_clear();
    /* status bar */
    RECT sb = { 0, H - bar, W, H };
    uint32_t sbg = scheme_rgb(COL[C_STATUS] >> 4), sfg = scheme_rgb(COL[C_STATUS] & 15);
    HBRUSH b2 = CreateSolidBrush(RGB(sbg >> 16, (sbg >> 8) & 255, sbg & 255));
    FillRect(dc, &sb, b2); DeleteObject(b2);
    HFONT f2 = CreateFontW(-(bar * 6 / 10), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    HGDIOBJ of = SelectObject(dc, f2);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(sfg >> 16, (sfg >> 8) & 255, sfg & 255)); SetTextAlign(dc, TA_LEFT | TA_TOP);
    wchar_t t[200];
    swprintf(t, 200, L" Preview   Page %d of %d     PgUp/PgDn page   Z zoom   2 two pages   Home/End   P print   Esc return",
             pv_page, g_ptotal);
    TextOutW(dc, 4, H - bar + bar / 5, t, (int)wcslen(t));
    SelectObject(dc, of); DeleteObject(f2);
    BitBlt(hdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
}

void preview_document(Doc *d, int startpage)
{
    doc_layout(d, 0);
    pv_doc = d; pv_pi = build_pages(d);
    pv_page = startpage < 1 ? 1 : startpage > g_ptotal ? g_ptotal : startpage;
    pv_zoom = 0; pv_scroll = 0;
    g_overlay = pv_paint;
    for (;;) {
        InvalidateRect(g_hwnd, NULL, FALSE);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) break;
        if (e.type == EV_MOUSE) {
            if (e.mact == MB_WHEEL) {
                if (pv_zoom) pv_scroll += e.wheel * 40;
                else { pv_page += e.wheel > 0 ? 1 : -1; }
            } else if (e.mact == MB_DOWN) pv_zoom = !pv_zoom;
        } else if (e.type == EV_KEY) {
            int k = e.key;
            if (k == K_ESC || k == K_ENTER || k == K_F10 || k == CTRL('K')) break;
            if (k == K_PGDN || k == CTRL('C')) { pv_page += pv_two ? 2 : 1; pv_scroll = 0; }
            else if (k == K_PGUP || k == CTRL('R')) { pv_page -= pv_two ? 2 : 1; pv_scroll = 0; }
            else if (k == K_HOME || k == K_CHOME) pv_page = 1;
            else if (k == K_END || k == K_CEND) pv_page = g_ptotal;
            else if (k == K_DOWN || k == CTRL('X')) pv_scroll += 60;
            else if (k == K_UP || k == CTRL('E')) pv_scroll -= 60;
            else if (k == 'z' || k == 'Z' || k == '+' || k == '-') { pv_zoom = !pv_zoom; pv_scroll = 0; }
            else if (k == '2') pv_two = !pv_two;
            else if (k == 'p' || k == 'P') { g_overlay = NULL; print_document(d); g_overlay = pv_paint; }
        }
        if (pv_page < 1) pv_page = 1;
        if (pv_page > g_ptotal) pv_page = g_ptotal;
    }
    g_overlay = NULL;
    free(pv_pi); pv_pi = NULL;
    InvalidateRect(g_hwnd, NULL, FALSE);
}
