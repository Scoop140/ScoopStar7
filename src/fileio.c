/*
 * ScoopStar 7.0 - fileio.c
 * RTF writer and reader, plain text, and WordStar (DOS) file import.
 */
#include "scoopstar.h"

/* ------------------------------------------------------------------ growable byte buffer */
typedef struct { char *b; size_t n, cap; } Buf;
static void bput(Buf *x, const char *s, size_t n)
{
    if (x->n + n + 1 > x->cap) {
        size_t nc = x->cap ? x->cap * 2 : 4096;
        while (nc < x->n + n + 1) nc *= 2;
        x->b = xrealloc(x->b, nc); x->cap = nc;
    }
    memcpy(x->b + x->n, s, n); x->n += n; x->b[x->n] = 0;
}
static void bputs(Buf *x, const char *s) { bput(x, s, strlen(s)); }
static void bprintf(Buf *x, const char *fmt, ...)
{
    char tmp[512]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    if (n > 0) bput(x, tmp, n < (int)sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

static int utf8_enc(wch c, char *o)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63)); o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}

static uint32_t text_hash_add(uint32_t h, wch c) { h ^= c; h *= 16777619u; return h; }

/* ------------------------------------------------------------------ private ScoopStar data (exact round trip) */
static void ser_text(Buf *x, const wch *t, int s, int e)
{
    char u[8];
    for (int i = s; i < e; i++) {
        wch c = t[i];
        if (IS_NOTE(c)) { bprintf(x, "~N%d~", (int)(c - NOTEBASE)); continue; }
        if (IS_PNUM(c)) { bprintf(x, "~P%d~", (int)(c - PNUMBASE)); continue; }
        if (c == '\n') { bputs(x, "~n~"); continue; }
        if (IS_TAG(c)) {
            switch (c) {
            case TG_BOLD: bputs(x, "~B~"); break;
            case TG_ITALIC: bputs(x, "~I~"); break;
            case TG_UNDER: bputs(x, "~U~"); break;
            case TG_STRIKE: bputs(x, "~X~"); break;
            case TG_SUPER: bputs(x, "~T~"); break;
            case TG_SUB: bputs(x, "~V~"); break;
            case TG_DOUBLE: bputs(x, "~D~"); break;
            case TG_INDEX: bputs(x, "~K~"); break;
            default:
                if (IS_FONTTAG(c) && (int)(c - TG_FONT0) < g_nfonts) {
                    FontDef *f = &g_fonts[c - TG_FONT0];
                    bprintf(x, "~F%d,", f->halfpts);
                    for (const wchar_t *q = f->name; *q; q++) { int k = utf8_enc(*q, u); bput(x, u, k); }
                    bputs(x, "~");
                }
            }
            continue;
        }
        if (c == '~') { bputs(x, "~~"); continue; }
        if (c == '\r') continue;
        int k = utf8_enc(c, u); bput(x, u, k);
    }
}

static void ser_para(Buf *x, Para *p, int s, int e)
{
    bprintf(x, "%d:", p->style);
    ser_text(x, p->t, s, e);
    bputs(x, "\n");
}

/* ------------------------------------------------------------------ RTF writer */
static void rtf_text(Buf *x, wch c);
static void rtf_plain_line(Buf *x, const wchar_t *s, int pagefield);
static uint32_t rtf_footnote(Buf *x, Doc *d, int idx, uint32_t hash)
{
    Note *k0 = (idx >= 0 && idx < d->nnotes) ? &d->notes[idx] : NULL;
    if (k0 && k0->kind == NOTE_ANNOT) {
        const wchar_t *mk = k0->mark[0] ? k0->mark : L"*";
        bputs(x, "{\\super ");
        for (const wchar_t *q = mk; *q; q++) { rtf_text(x, *q); hash = text_hash_add(hash, *q); }
        bputs(x, "}{\\footnote\\pard\\plain\\s0\\ql\\f0\\fs20 {\\super ");
        for (const wchar_t *q = mk; *q; q++) rtf_text(x, *q);
        bputs(x, "} ");
    } else if (k0 && k0->kind == NOTE_END)
        bputs(x, "{\\super\\chftn}{\\footnote\\ftnalt\\pard\\plain\\s0\\ql\\f0\\fs20 {\\super\\chftn} ");
    else
        bputs(x, "{\\super\\chftn}{\\footnote\\pard\\plain\\s0\\ql\\f0\\fs20 {\\super\\chftn} ");
    if (idx >= 0 && idx < d->nnotes) {
        Note *nt = &d->notes[idx];
        uint8_t fx = 0;
        for (int i = 0; i < nt->n; i++) {
            wch c = nt->t[i];
            if (c == '\n') {
                bputs(x, "\\par\\pard\\plain\\s0\\ql\\f0\\fs20 ");
                if (fx & FX_BOLD) bputs(x, "\\b ");
                if (fx & FX_ITALIC) bputs(x, "\\i ");
                if (fx & FX_UNDER) bputs(x, "\\ul ");
                continue;
            }
            if (IS_TAG(c)) {
                uint8_t old = fx;
                if (c == TG_BOLD || c == TG_DOUBLE) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC; else if (c == TG_UNDER) fx ^= FX_UNDER;
                else if (c == TG_STRIKE) fx ^= FX_STRIKE; else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
                uint8_t ch = old ^ fx;
                if (ch & FX_BOLD) bputs(x, (fx & FX_BOLD) ? "\\b " : "\\b0 ");
                if (ch & FX_ITALIC) bputs(x, (fx & FX_ITALIC) ? "\\i " : "\\i0 ");
                if (ch & FX_UNDER) bputs(x, (fx & FX_UNDER) ? "\\ul " : "\\ulnone ");
                if (ch & FX_STRIKE) bputs(x, (fx & FX_STRIKE) ? "\\strike " : "\\strike0 ");
                if (ch & (FX_SUPER | FX_SUB)) bputs(x, (fx & FX_SUPER) ? "\\super " : (fx & FX_SUB) ? "\\sub " : "\\nosupersub ");
                continue;
            }
            if (IS_SPECIAL(c)) continue;
            if (c == SHY) { bputs(x, "\\-"); continue; }
            rtf_text(x, c);
        }
    }
    bputs(x, "\\par}");
    return hash;
}

/* embeds a PNG or JPEG picture */
static void rtf_picture(Buf *x, Doc *d, const wchar_t *path, double w, double h)
{
    wchar_t full[MAX_PATH * 2];
    if ((path[0] && path[1] == ':') || (path[0] == '\\' && path[1] == '\\')) wcscpy(full, path);
    else { wcscpy(full, d->path); wchar_t *q = wcsrchr(full, '\\'); if (q) q[1] = 0; else full[0] = 0; wcscat(full, path); }
    FILE *f = _wfopen(full, L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 8 || n > 40000000) { fclose(f); return; }
    unsigned char *b = xmalloc(n);
    size_t got = fread(b, 1, n, f); fclose(f);
    const char *kind = NULL;
    if (got >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') kind = "\\pngblip";
    else if (got >= 3 && b[0] == 0xFF && b[1] == 0xD8) kind = "\\jpegblip";
    if (kind) {
        int pw = 0, ph = 0;
        if (!image_size(full, &pw, &ph)) { pw = (int)(w * 96); ph = (int)(h * 96); }
        bprintf(x, "{\\pict%s\\picw%d\\pich%d\\picwgoal%d\\pichgoal%d\n", kind, pw, ph, (int)(w * 1440), (int)(h * 1440));
        static const char hx[] = "0123456789abcdef";
        for (size_t i = 0; i < got; i++) {
            char t[2] = { hx[b[i] >> 4], hx[b[i] & 15] };
            bput(x, t, 2);
            if ((i & 63) == 63) bputs(x, "\n");
        }
        bputs(x, "}");
    }
    free(b);
}

/* header or footer lines collected from dot commands */
typedef struct { wchar_t t[3][5][160]; int any_eo; } HFSet;
static void rtf_hf_group(Buf *x, const char *kw, wchar_t (*lines)[160])
{
    int n = 0; for (int j = 0; j < 5; j++) if (lines[j][0]) n = j + 1;
    if (!n) return;
    bprintf(x, "{\\%s\\pard\\plain\\f0\\fs24 ", kw);
    for (int j = 0; j < n; j++) { if (j) bputs(x, "\\par "); rtf_plain_line(x, lines[j], 1); }
    bputs(x, "\\par}\n");
}

static void rtf_text(Buf *x, wch c)
{
    char t[32];
    if (c == '\\' || c == '{' || c == '}') { t[0] = '\\'; t[1] = (char)c; bput(x, t, 2); return; }
    if (c == '\t') { bputs(x, "\\tab "); return; }
    if (c == 0xA0) { bputs(x, "\\~"); return; }
    if (c < 0x80) { t[0] = (char)c; bput(x, t, 1); return; }
    if (c >= 0x10000) {
        wch v = c - 0x10000;
        int hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
        snprintf(t, sizeof t, "\\u%d?\\u%d?", hi - 65536, lo - 65536);
        bputs(x, t); return;
    }
    snprintf(t, sizeof t, "\\u%d?", c > 32767 ? (int)c - 65536 : (int)c);
    bputs(x, t);
}

static void rtf_plain_line(Buf *x, const wchar_t *s, int pagefield)
{
    for (; *s; s++) {
        if (*s == '#' && pagefield) { bputs(x, "{\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}"); continue; }
        rtf_text(x, *s);
    }
}

static void wcs_from_dot(Para *p, wchar_t *arg, int n)
{
    wchar_t cmd[8]; dot_parse(p->t, p->n, cmd, 8, arg, n);
}

char *rtf_build(Doc *d, Pos *pa, Pos *pb, size_t *outlen)
{
    Buf x = { 0 };
    if (!d->nondoc) doc_layout(d, 0);     /* paragraph number labels */
    Pos a = { 0, 0 }, b = { d->np - 1, d->p[d->np - 1].n };
    if (pa && pb) { a = *pa; b = *pb; if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; } }
    int whole = !(pa && pb);
    Fmt f = doc_fmt_at(d, a.p);
    /* header / footer / numbering from the dot commands before the first text */
    HFSet hs, fs; memset(&hs, 0, sizeof(hs)); memset(&fs, 0, sizeof(fs));
    int op = f.op, pn = f.pn, cols0 = 1, gut0 = 4;
    Fmt pagefmt = f;
    {
        Fmt g = d->base;
        int seen_text = 0;
        for (int i = 0; i < d->np; i++) {
            Para *p = &d->p[i];
            if (!para_is_dot(p)) { if (i >= a.p) { seen_text = 1; } continue; }
            if (seen_text) break;
            wchar_t cmd[8], arg[256];
            dot_parse(p->t, p->n, cmd, 8, arg, 256);
            int ln = 0, par = 0, kk = 0;
            if (!wcscmp(cmd, L"he")) kk = 'h'; else if (!wcscmp(cmd, L"fo")) kk = 'f';
            else if ((cmd[0] == 'h' || cmd[0] == 'f') && cmd[1] >= '1' && cmd[1] <= '5' && (!cmd[2] || cmd[2] == 'e' || cmd[2] == 'o')) {
                kk = cmd[0]; ln = cmd[1] - '1'; par = cmd[2] == 'e' ? 1 : cmd[2] == 'o' ? 2 : 0;
            }
            if (kk) {
                HFSet *t = kk == 'h' ? &hs : &fs;
                wcsncpy(t->t[par][ln], arg, 159);
                if (par) t->any_eo = 1;
            }
            else if (!wcscmp(cmd, L"op")) op = 1;
            else if (!wcscmp(cmd, L"pg")) op = 0;
            else if (!wcscmp(cmd, L"pn")) pn = _wtoi(arg);
        }
        Fmt c0 = doc_fmt_at(d, a.p);
        for (int i = a.p; i < d->np && para_is_dot(&d->p[i]); i++) c0 = doc_fmt_at(d, i + 1);
        cols0 = c0.cols; gut0 = c0.gutter;
        (void)g;
        /* page geometry from the first page's settings */
        pagefmt = doc_fmt_at(d, a.p);
        for (int i = a.p; i < d->np && para_is_dot(&d->p[i]); i++) { pagefmt = doc_fmt_at(d, i + 1); }
    }
    int twc = 144, twl = 240;     /* twips per column / per line */
    int paperw = 12240, paperh = pagefmt.pl * twl;
    int margl = pagefmt.po * twc;
    int textw = pagefmt.rm * twc;
    int margr = paperw - margl - textw; if (margr < 360) margr = 360;
    int margt = pagefmt.mt * twl, margb = pagefmt.mb * twl;
    int headery = (pagefmt.mt - pagefmt.hm) * twl; if (headery < 360) headery = 360;
    int footery = (pagefmt.mb - pagefmt.fm) * twl; if (footery < 360) footery = 360;

    bputs(&x, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl{\\f0\\fmodern\\fcharset0 Courier New;}");
    for (int i = 0; i < g_nfonts; i++) {
        bprintf(&x, "{\\f%d\\fnil\\fcharset0 ", i + 1);
        for (const wchar_t *q = g_fonts[i].name; *q; q++) rtf_text(&x, *q);
        bputs(&x, ";}");
    }
    bputs(&x, "}\n{\\stylesheet{\\s0\\f0\\fs24 Normal;}");
    for (int i = 1; i < g_npstyles; i++) {
        char nm[64]; int k = 0;
        for (const wchar_t *q = g_pstyles[i].name; *q && k < 60; q++) nm[k++] = (char)*q;
        nm[k] = 0;
        if (i >= 2 && i <= 4) { nm[0] = 'h'; }   /* Word's built-in "heading 1" names */
        bprintf(&x, "{\\s%d\\sbasedon0\\snext0%s%s %s;}", i, g_pstyles[i].bold ? "\\b" : "", g_pstyles[i].italic ? "\\i" : "", nm);
    }
    bputs(&x, "}\n");
    bprintf(&x, "\\paperw%d\\paperh%d\\margl%d\\margr%d\\margt%d\\margb%d\\headery%d\\footery%d\\widowctrl\n",
            paperw, paperh, margl, margr, margt, margb, headery, footery);
    if (pn > 0) bprintf(&x, "\\pgnstarts%d\\pgnrestart", pn);
    if (hs.any_eo || fs.any_eo) bputs(&x, "\\facingp");
    bputs(&x, "\\sectd");
    if (cols0 > 1) bprintf(&x, "\\cols%d\\colsx%d", cols0, gut0 * twc);
    bputs(&x, "\n");
    if (whole) {
        int anyfo = 0;
        for (int j = 0; j < 5; j++) for (int q = 0; q < 3; q++) if (fs.t[q][j][0]) anyfo = 1;
        for (int k = 0; k < 2; k++) {
            HFSet *t = k ? &fs : &hs;
            const char *b0 = k ? "footer" : "header";
            if (t->any_eo) {
                /* left = even pages, right = odd pages; lines not given for one side fall back to the common line */
                for (int side = 1; side <= 2; side++) {
                    wchar_t ln[5][160];
                    for (int j = 0; j < 5; j++) wcscpy(ln[j], t->t[side][j][0] ? t->t[side][j] : t->t[0][j]);
                    char kw[16]; snprintf(kw, 16, "%s%c", b0, side == 1 ? 'l' : 'r');
                    rtf_hf_group(&x, kw, ln);
                }
            } else rtf_hf_group(&x, b0, t->t[0]);
        }
        if (!anyfo && !op) bputs(&x, "{\\footer\\pard\\plain\\qc\\f0\\fs24 {\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}\\par}\n");
    }

    /* body */
    uint32_t hash = 2166136261u;
    uint8_t fx = 0; int font = -1;
    /* effect state at block start */
    for (int i = 0; i < a.p || (i == a.p && 0); i++) for (int k = 0; k < d->p[i].n; k++) {
        wch c = d->p[i].t[k];
        if (IS_FONTTAG(c)) font = (int)(c - TG_FONT0);
        else if (c == TG_BOLD) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC; else if (c == TG_UNDER) fx ^= FX_UNDER;
        else if (c == TG_STRIKE) fx ^= FX_STRIKE; else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
        else if (c == TG_DOUBLE) fx ^= FX_DOUBLE;
    }
    for (int k = 0; k < a.o && a.p < d->np && k < d->p[a.p].n; k++) {
        wch c = d->p[a.p].t[k];
        if (IS_FONTTAG(c)) font = (int)(c - TG_FONT0);
        else if (c == TG_BOLD) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC; else if (c == TG_UNDER) fx ^= FX_UNDER;
        else if (c == TG_STRIKE) fx ^= FX_STRIKE; else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
        else if (c == TG_DOUBLE) fx ^= FX_DOUBLE;
    }
    int pagebreak = 0, first = 1;
    int cp_keep = 0;
    Fmt cur = f;
    for (int i = a.p; i <= b.p; i++) {
        Para *p = &d->p[i];
        int s = i == a.p ? a.o : 0, e = i == b.p ? b.o : p->n;
        if (para_is_dot(p) && s == 0 && !d->nondoc) {
            wchar_t cmd[8], arg[256];
            dot_parse(p->t, p->n, cmd, 8, arg, 256);
            Fmt prev = cur;
            if (!wcscmp(cmd, L"pa")) pagebreak = 1;
            else if (!wcscmp(cmd, L"cp")) cp_keep = _wtoi(arg);
            else if (!wcscmp(cmd, L"cb")) { if (!first) bputs(&x, "\\column "); }
            else if (!wcscmp(cmd, L"ix")) { bputs(&x, "{\\xe {"); for (wchar_t *q = arg; *q; q++) rtf_text(&x, *q); bputs(&x, "}}"); }
            else if (!wcscmp(cmd, L"tc")) {
                int lv = 1; wchar_t *q = arg;
                wchar_t *hsh = wcsrchr(arg, '#'); if (hsh && iswdigit(hsh[1])) { lv = _wtoi(hsh + 1); *hsh = 0; }
                bputs(&x, "{\\tc {"); for (; *q; q++) rtf_text(&x, *q); bprintf(&x, "}\\tcl%d}", lv);
            }
            else if (!wcscmp(cmd, L"gr")) {
                double gw, gh; wchar_t gp[MAX_PATH];
                if (graphic_parse(p->t, p->n, &gw, &gh, gp, MAX_PATH)) {
                    if (!first) bputs(&x, "\\par\n");
                    bputs(&x, "\\pard\\ql\\plain ");
                    rtf_picture(&x, d, gp, gw, gh);
                    first = 0;
                }
            }
            cur = doc_fmt_at(d, i + 1);
            if (cur.cols != prev.cols && !first) {
                bprintf(&x, "\\par\n\\sect\\sectd\\sbkpage\\cols%d\\colsx%d\n", cur.cols, cur.gutter * twc);
                first = 1;
            }
            (void)wcs_from_dot;
            continue;
        }
        if (!first) bputs(&x, "\\par\n");
        first = 0;
        ParaStyle *ps = &g_pstyles[p->style < g_npstyles ? p->style : 0];
        int lm = cur.lm + ps->lmadd, rm = cur.rm - ps->rmsub;
        int pm = (ps->pmadd || ps->lmadd) ? cur.pm + ps->lmadd + ps->pmadd : cur.pm;
        char al = ps->align ? ps->align : cur.align;
        bprintf(&x, "\\pard\\s%d", p->style < g_npstyles ? p->style : 0);
        if (al == 'C') bputs(&x, "\\qc"); else if (al == 'R') bputs(&x, "\\qr"); else if (al == 'J') bputs(&x, "\\qj"); else bputs(&x, "\\ql");
        int li = (lm - 1) * twc, ri = (pagefmt.rm - rm) * twc, fi = (pm - lm) * twc;
        if (li) bprintf(&x, "\\li%d", li);
        if (ri > 0) bprintf(&x, "\\ri%d", ri);
        if (fi) bprintf(&x, "\\fi%d", fi);
        if (cur.ls > 2) bprintf(&x, "\\sl%d\\slmult1", cur.ls * 120);
        if (pagebreak) { bputs(&x, "\\pagebb"); pagebreak = 0; }
        if (cp_keep > 0) { bputs(&x, "\\keep\\keepn"); cp_keep = 0; }
        if (cur.ntabs && cur.ntabs != d->base.ntabs) for (int t = 0; t < cur.ntabs && cur.tabs[t] <= cur.rm; t++) bprintf(&x, "\\tx%d", (cur.tabs[t] - 1) * twc);
        /* paragraph-start character formatting */
        uint8_t base = (ps->bold ? FX_BOLD : 0) | (ps->italic ? FX_ITALIC : 0);
        uint8_t eff = fx ^ (fx & base) ? (fx | base) : (fx | base);
        eff = fx | base;
        bprintf(&x, "\\plain\\f%d\\fs%d", font >= 0 ? font + 1 : 0, font >= 0 ? g_fonts[font].halfpts : DEFAULT_HALFPTS);
        if (eff & (FX_BOLD | FX_DOUBLE)) bputs(&x, "\\b");
        if (eff & FX_ITALIC) bputs(&x, "\\i");
        if (eff & FX_UNDER) bputs(&x, "\\ul");
        if (eff & FX_STRIKE) bputs(&x, "\\strike");
        if (eff & FX_SUPER) bputs(&x, "\\super");
        if (eff & FX_SUB) bputs(&x, "\\sub");
        bputs(&x, " ");
        for (int k = s; k < e; k++) {
            wch c = p->t[k];
            if (IS_TAG(c)) {
                uint8_t old = fx | base;
                if (IS_FONTTAG(c)) {
                    font = (int)(c - TG_FONT0);
                    if (font < g_nfonts) bprintf(&x, "\\f%d\\fs%d ", font + 1, g_fonts[font].halfpts);
                    continue;
                }
                if (c == TG_BOLD) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC; else if (c == TG_UNDER) fx ^= FX_UNDER;
                else if (c == TG_STRIKE) fx ^= FX_STRIKE; else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
                else if (c == TG_DOUBLE) fx ^= FX_DOUBLE;
                uint8_t nw = fx | base;
                uint8_t ch = old ^ nw;
                if (ch & (FX_BOLD | FX_DOUBLE)) bputs(&x, (nw & (FX_BOLD | FX_DOUBLE)) ? "\\b " : "\\b0 ");
                if (ch & FX_ITALIC) bputs(&x, (nw & FX_ITALIC) ? "\\i " : "\\i0 ");
                if (ch & FX_UNDER) bputs(&x, (nw & FX_UNDER) ? "\\ul " : "\\ulnone ");
                if (ch & FX_STRIKE) bputs(&x, (nw & FX_STRIKE) ? "\\strike " : "\\strike0 ");
                if (ch & (FX_SUPER | FX_SUB)) bputs(&x, (nw & FX_SUPER) ? "\\super " : (nw & FX_SUB) ? "\\sub " : "\\nosupersub ");
                continue;
            }
            if (IS_NOTE(c)) { hash = rtf_footnote(&x, d, (int)(c - NOTEBASE), hash); continue; }
            if (IS_PNUM(c)) { for (const wchar_t *q = p->pnlab; *q; q++) { rtf_text(&x, *q); hash = text_hash_add(hash, *q); } continue; }
            if (c == SHY) { bputs(&x, "\\-"); hash = text_hash_add(hash, c); continue; }
            rtf_text(&x, c);
            hash = text_hash_add(hash, c);
        }
    }
    bputs(&x, "\\par\n");

    /* private copy for exact round trip */
    if (whole) {
        Buf pr = { 0 };
        bprintf(&pr, "SCOOPSTAR 1 %d %d %d %d\n", d->base.lm, d->base.rm, d->base.pm, S.tabw);
        bprintf(&pr, "H%08X\n", hash);
        for (int i = 0; i < d->np; i++) ser_para(&pr, &d->p[i], 0, d->p[i].n);
        for (int i = 0; i < d->nnotes; i++) {
            bprintf(&pr, "#N%d,%d,", i, d->notes[i].kind);
            char u[8]; for (const wchar_t *q = d->notes[i].mark; *q; q++) { if (*q == ':' || *q == '\n') continue; int k = utf8_enc(*q, u); bput(&pr, u, k); }
            bputs(&pr, ":");
            ser_text(&pr, d->notes[i].t, 0, d->notes[i].n); bputs(&pr, "\n"); }
        bputs(&x, "{\\*\\scoopstar ");
        static const char hx[] = "0123456789abcdef";
        for (size_t i = 0; i < pr.n; i++) {
            char t[2] = { hx[(unsigned char)pr.b[i] >> 4], hx[(unsigned char)pr.b[i] & 15] };
            bput(&x, t, 2);
            if ((i & 63) == 63) bputs(&x, "\n");
        }
        bputs(&x, "}\n");
        free(pr.b);
    }
    bputs(&x, "}\n");
    if (outlen) *outlen = x.n;
    return x.b;
}

static int write_all(const wchar_t *path, const void *data, size_t n)
{
    wchar_t tmp[MAX_PATH + 8];
    swprintf(tmp, MAX_PATH + 8, L"%ls.$$$", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w; BOOL ok = WriteFile(h, data, (DWORD)n, &w, NULL) && w == n;
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp); return 0; }
    if (S.backups && file_exists(path)) {
        wchar_t bak[MAX_PATH + 8]; wcscpy(bak, path);
        wchar_t *dot = wcsrchr(bak, '.'), *bs = wcsrchr(bak, '\\');
        if (dot && (!bs || dot > bs)) wcscpy(dot, L".BAK"); else wcscat(bak, L".BAK");
        CopyFileW(path, bak, FALSE);
    }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp); return 0; }
    return 1;
}

int save_rtf(Doc *d, const wchar_t *path, Pos *a, Pos *b)
{
    size_t n; char *r = rtf_build(d, a, b, &n);
    int ok = write_all(path, r, n);
    free(r);
    return ok;
}

int save_text(Doc *d, const wchar_t *path, Pos *pa, Pos *pb)
{
    Buf x = { 0 };
    doc_layout(d, 0);
    Pos a = { 0, 0 }, b = { d->np - 1, d->p[d->np - 1].n };
    if (pa && pb) { a = *pa; b = *pb; }
    if (d->nondoc && d->textenc == 2) bputs(&x, "\xFF\xFE");
    else if (d->nondoc && d->textenc == 3) bputs(&x, "\xEF\xBB\xBF");
    for (int i = a.p; i <= b.p; i++) {
        Para *p = &d->p[i];
        if (!d->nondoc && para_is_dot(p)) continue;
        int s = i == a.p ? a.o : 0, e = i == b.p ? b.o : p->n;
        for (int k = s; k < e; k++) {
            wch c = p->t[k];
            if (IS_TAG(c) || c == SHY) continue;
            if (IS_PNUM(c)) { char u[4]; for (const wchar_t *q = p->pnlab; *q; q++) { int k2 = utf8_enc(*q, u); bput(&x, u, k2); } continue; }
            if (IS_NOTE(c)) {
                int ni = (int)(c - NOTEBASE);
                char nb[24];
                if (ni < d->nnotes && d->notes[ni].kind == NOTE_ANNOT) { char mk[16] = "*"; WideCharToMultiByte(CP_UTF8, 0, d->notes[ni].mark[0] ? d->notes[ni].mark : L"*", -1, mk, 16, NULL, NULL); snprintf(nb, 24, "[%s]", mk); }
                else snprintf(nb, 24, "[%d]", ni < d->nnotes ? d->notes[ni].num : 0);
                bputs(&x, nb);
                continue;
            }
            if (c == 0xA0 && !d->nondoc) c = ' ';
            if (d->nondoc && d->textenc == 1) {
                wchar_t wc = (wchar_t)(c < 0x10000 ? c : '?'); char mb[4]; BOOL used = FALSE;
                int k2 = WideCharToMultiByte(CP_ACP, 0, &wc, 1, mb, 4, "?", &used);
                if (k2 > 0) bput(&x, mb, k2);
            } else if (d->nondoc && d->textenc == 2) {
                if (c >= 0x10000) { wch v = c - 0x10000; uint16_t hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF); bput(&x, (char *)&hi, 2); bput(&x, (char *)&lo, 2); }
                else { uint16_t u = (uint16_t)c; bput(&x, (char *)&u, 2); }
            } else { char u[4]; int k2 = utf8_enc(c, u); bput(&x, u, k2); }
        }
        if (i < b.p || (d->nondoc && i == d->np - 1 && 0)) {
            if (d->nondoc && d->textenc == 2) bput(&x, d->crlf ? "\r\0\n\0" : "\n\0", d->crlf ? 4 : 2);
            else bputs(&x, d->crlf ? "\r\n" : "\n");
        }
    }
    int ok = write_all(path, x.b ? x.b : "", x.n);
    free(x.b);
    return ok;
}

static int has_formatting(Doc *d)
{
    for (int i = 0; i < d->np; i++) {
        if (d->p[i].style) return 1;
        if (para_is_dot(&d->p[i])) return 1;
        for (int k = 0; k < d->p[i].n; k++) if (IS_TAG(d->p[i].t[k])) return 1;
    }
    return 0;
}

int save_file(Doc *d, const wchar_t *path)
{
    wchar_t ext[16]; path_ext(path, ext, 16);
    if (d->nondoc) return save_text(d, path, NULL, NULL);
    if (!_wcsicmp(ext, L"txt") || !_wcsicmp(ext, L"asc")) {
        if (has_formatting(d)) {
            int r = msgbox(L"Save", L"This is a plain text file, so bold, fonts, styles\nand dot commands cannot be stored in it.", L"Save as &text anyway|&Cancel");
            if (r != 0) return 0;
        }
        return save_text(d, path, NULL, NULL);
    }
    d->fmt = FMT_RTF;
    return save_rtf(d, path, NULL, NULL);
}

/* ------------------------------------------------------------------ text reader */
static void load_text_mem(Doc *d, const unsigned char *b, size_t n)
{
    doc_clear(d);
    d->np = 0;
    wchar_t *w = NULL; int wn = 0;
    d->textenc = 0;
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        d->textenc = 2;
        wn = (int)((n - 2) / 2); w = xmalloc((wn + 1) * sizeof(wchar_t)); memcpy(w, b + 2, wn * 2);
    } else {
        int off = 0;
        if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { off = 3; d->textenc = 3; }
        int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)b + off, (int)(n - off), NULL, 0);
        if (len > 0 || n - off == 0) {
            w = xmalloc((len + 1) * sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, 0, (const char *)b + off, (int)(n - off), w, len);
            wn = len;
        } else {
            d->textenc = 1;
            len = MultiByteToWideChar(CP_ACP, 0, (const char *)b, (int)n, NULL, 0);
            w = xmalloc((len + 1) * sizeof(wchar_t));
            MultiByteToWideChar(CP_ACP, 0, (const char *)b, (int)n, w, len);
            wn = len;
        }
    }
    wch *line = xmalloc((wn + 1) * sizeof(wch)); int ln = 0;
    int sawcr = 0, sawlf = 0;
    for (int i = 0; i <= wn; i++) {
        if (i == wn || w[i] == '\n' || w[i] == '\r') {
            if (i < wn && w[i] == '\r') { sawcr = 1; if (i + 1 < wn && w[i + 1] == '\n') i++; }
            else if (i < wn) sawlf = 1;
            doc_append_para(d, line, ln);
            ln = 0;
            continue;
        }
        wchar_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < wn) { line[ln++] = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00); i++; continue; }
        if (c == 0x1A && i == wn - 1) continue;
        line[ln++] = c;
    }
    /* a final newline produces an empty last paragraph: drop it */
    if (d->np > 1 && d->p[d->np - 1].n == 0 && wn > 0 && (w[wn - 1] == '\n' || w[wn - 1] == '\r')) { free(d->p[d->np - 1].t); d->np--; d->crlf = sawcr || !sawlf; d->textenc |= 0; }
    if (d->np == 0) doc_append_para(d, NULL, 0);
    d->crlf = sawcr || !sawlf;
    free(line); free(w);
}

/* ------------------------------------------------------------------ RTF reader */
typedef struct { uint8_t fx; int font; int uc; int skip; int dest; } RState;
enum { D_BODY, D_FONTTBL, D_STYLESHEET, D_HEADER, D_FOOTER, D_SCOOP, D_FLDINST, D_NOTE };

typedef struct {
    Doc *d;
    RState st[256]; int sp;
    wch *pb; int pn, pcap;      /* current paragraph */
    uint8_t efx; int efont;     /* state already emitted as tags */
    int pstyle, align, li, ri, fi, sl, pagebb;
    int cur_lm, cur_rm, cur_pm, cur_ls; char cur_al;
    int rtffont[512]; wchar_t fname[128]; int fnl; int curfont;
    int stylemap[256]; wchar_t sname[128]; int snl; int curstyle;
    wchar_t hf[256]; int hfn; int hftype;
    Buf scoop;
    int codepage;
    int ucskip;
    uint32_t hash;
    int paperw, margl, margr, margt, margb, paperh;
    int fldpage;
    int hadtext;
    wch *nb; int nn, ncap; uint8_t nfx; int innote, nkind;     /* footnote being read */
    int curcols;
    int sv_align, sv_li, sv_ri, sv_fi, sv_sl, sv_pstyle;
} RCtx;

static void note_push(RCtx *c, wch ch)
{
    if (c->nn + 2 > c->ncap) { c->ncap = c->ncap ? c->ncap * 2 : 256; c->nb = xrealloc(c->nb, c->ncap * sizeof(wch)); }
    c->nb[c->nn++] = ch;
}

static void note_begin(RCtx *c, RState *s)
{
    s->dest = D_NOTE; c->innote = c->sp; c->nn = 0; c->nfx = 0; c->nkind = NOTE_FOOT;
    c->sv_align = c->align; c->sv_li = c->li; c->sv_ri = c->ri; c->sv_fi = c->fi; c->sv_sl = c->sl; c->sv_pstyle = c->pstyle;
}

static void rpush(RCtx *c, wch ch);
static void note_end(RCtx *c)
{
    int a = 0, b = c->nn;
    while (a < b && (c->nb[a] == ' ' || c->nb[a] == '\n' || c->nb[a] == '\t')) a++;
    while (b > a && (c->nb[b - 1] == ' ' || c->nb[b - 1] == '\n')) b--;
    int idx = note_add_kind(c->d, c->nb + a, b - a, c->nkind, NULL);
    /* some writers put a stray superscript space before the reference */
    if (c->pn >= 2 && c->pb[c->pn - 1] == ' ' && c->pb[c->pn - 2] == TG_SUPER) { c->pn -= 2; c->efx ^= FX_SUPER; }
    rpush(c, NOTEBASE + (wch)idx);
    c->align = c->sv_align; c->li = c->sv_li; c->ri = c->sv_ri; c->fi = c->sv_fi; c->sl = c->sv_sl; c->pstyle = c->sv_pstyle;
    c->innote = 0;
}

static int g_rdeff;
static RCtx *g_rc;
static const wchar_t *rfont_name(RCtx *c, int font)
{
    if (font == -2) {
        int f = (g_rdeff >= 0 && g_rdeff < 512) ? c->rtffont[g_rdeff] : -1;
        return f >= 0 ? g_fonts[f].name : DEFAULT_FONT;
    }
    return font >= 0 ? g_fonts[font].name : DEFAULT_FONT;
}
static int rfont_size(int font) { return font >= 0 ? g_fonts[font].halfpts : DEFAULT_HALFPTS; }
static int rfont_make(const wchar_t *nm, int hp)
{
    if (!_wcsicmp(nm, DEFAULT_FONT) && hp == DEFAULT_HALFPTS) return -1;
    return font_find_or_add(nm, hp);
}

static void rpush(RCtx *c, wch ch)
{
    if (c->pn + 2 > c->pcap) { c->pcap = c->pcap ? c->pcap * 2 : 256; c->pb = xrealloc(c->pb, c->pcap * sizeof(wch)); }
    c->pb[c->pn++] = ch;
}

static void rchar(RCtx *c, wch ch)
{
    RState *s = &c->st[c->sp];
    if (s->skip && s->dest != D_SCOOP) return;
    if (s->dest == D_NOTE) {
        static const struct { uint8_t bit; wch tag; } nm[] = {
            { FX_BOLD, TG_BOLD }, { FX_ITALIC, TG_ITALIC }, { FX_UNDER, TG_UNDER }, { FX_STRIKE, TG_STRIKE },
            { FX_SUPER, TG_SUPER }, { FX_SUB, TG_SUB } };
        uint8_t diff = s->fx ^ c->nfx;
        for (int i = 0; i < 6; i++) if (diff & nm[i].bit) note_push(c, nm[i].tag);
        c->nfx = s->fx;
        note_push(c, ch);
        return;
    }
    if (s->dest == D_FONTTBL) { if (ch == ';') { c->fname[c->fnl] = 0; if (c->curfont >= 0 && c->curfont < 512) c->rtffont[c->curfont] = font_find_or_add(c->fname, 24); c->fnl = 0; } else if (c->fnl < 127 && ch < 0x10000) c->fname[c->fnl++] = (wchar_t)ch; return; }
    if (s->dest == D_STYLESHEET) { if (ch == ';') { c->sname[c->snl] = 0; if (c->curstyle >= 0 && c->curstyle < 256) {
                int m = 0; wchar_t lw[128]; wcscpy(lw, c->sname); _wcslwr(lw);
                if (wcsstr(lw, L"heading 1")) m = 2; else if (wcsstr(lw, L"heading 2")) m = 3; else if (wcsstr(lw, L"heading 3")) m = 4;
                else if (!wcscmp(lw, L"title")) m = 1;
                else for (int i = 1; i < g_npstyles; i++) { wchar_t pn[64]; wcscpy(pn, g_pstyles[i].name); _wcslwr(pn); if (!wcscmp(pn, lw)) m = i; }
                c->stylemap[c->curstyle] = m; }
            c->snl = 0; } else if (c->snl < 127 && ch < 0x10000) c->sname[c->snl++] = (wchar_t)ch; return; }
    if (s->dest == D_HEADER || s->dest == D_FOOTER) {
        if (c->fldpage) return;
        if (c->hfn < 250 && ch != '\n' && ch < 0x10000) c->hf[c->hfn++] = ch == '\t' ? ' ' : (wchar_t)ch;
        return;
    }
    if (s->dest == D_FLDINST) { if (ch == 'P' || ch == 'p') {} return; }
    if (s->dest == D_SCOOP) { char b = (char)ch; bput(&c->scoop, &b, 1); return; }
    if (s->skip) return;
    /* emit tags for state differences */
    uint8_t want = s->fx;
    uint8_t diff = want ^ c->efx;
    static const struct { uint8_t bit; wch tag; } map[] = {
        { FX_BOLD, TG_BOLD }, { FX_ITALIC, TG_ITALIC }, { FX_UNDER, TG_UNDER }, { FX_STRIKE, TG_STRIKE },
        { FX_SUPER, TG_SUPER }, { FX_SUB, TG_SUB } };
    for (int i = 0; i < 6; i++) if (diff & map[i].bit) rpush(c, map[i].tag);
    c->efx = want;
    if (s->font == -2) s->font = rfont_make(rfont_name(c, -2), DEFAULT_HALFPTS);
    if (s->font != c->efont) {
        rpush(c, TG_FONT0 + (wch)(s->font >= 0 ? s->font : font_find_or_add(DEFAULT_FONT, DEFAULT_HALFPTS)));
        c->efont = s->font;
    }
    rpush(c, ch);
    c->hash = text_hash_add(c->hash, ch);
    c->hadtext = 1;
}

static void append_dot(RCtx *c, const wchar_t *s)
{
    int n = (int)wcslen(s);
    wch b[300]; for (int i = 0; i < n && i < 299; i++) b[i] = s[i];
    doc_append_para(c->d, b, n);
}

static void rpar(RCtx *c)
{
    /* layout dot commands for paragraph properties */
    int lm = 1 + (c->li + 72) / 144;
    int rm = c->cur_rm;
    int base_rm = c->d->base.rm;
    rm = base_rm - (c->ri + 72) / 144;
    int pm = lm + (c->fi >= 0 ? (c->fi + 72) / 144 : -((-c->fi + 72) / 144));
    if (pm < 1) pm = 1;
    if (lm < 1) lm = 1;
    if (rm < lm + 10) rm = lm + 10;
    int style = c->pstyle;
    char al = (char)c->align;
    if (style && g_pstyles[style].align) al = g_pstyles[style].align;
    if (c->pagebb && c->d->np > 0) { append_dot(c, L".pa"); c->pagebb = 0; }
    if (style == 0 || style > 4) {
        if (lm != c->cur_lm) { wchar_t t[32]; swprintf(t, 32, L".lm %d", lm); append_dot(c, t); c->cur_lm = lm; c->cur_pm = lm; }
        if (rm != c->cur_rm) { wchar_t t[32]; swprintf(t, 32, L".rm %d", rm); append_dot(c, t); c->cur_rm = rm; }
        if (pm != c->cur_pm) { wchar_t t[32]; swprintf(t, 32, L".pm %d", pm); append_dot(c, t); c->cur_pm = pm; }
    }
    if (!(style && g_pstyles[style].align) && al != c->cur_al) {
        append_dot(c, al == 'J' ? L".oj on" : al == 'C' ? L".oj c" : al == 'R' ? L".oj r" : L".oj off");
        c->cur_al = al;
    }
    int ls = c->sl > 0 ? (c->sl + 60) / 120 : 2;     /* half lines; 360 twips = 1.5 */
    if (ls < 2) ls = 2;
    if (ls > 18) ls = 18;
    if (ls != c->cur_ls) { wchar_t t[16], v[8]; ls_text(ls, v); swprintf(t, 16, L".ls %ls", v); append_dot(c, t); c->cur_ls = ls; }
    doc_append_para(c->d, c->pb, c->pn);
    c->d->p[c->d->np - 1].style = (uint8_t)style;
    c->pn = 0;
}

static int parse_scoop(Doc *d, const char *hex, size_t n, uint32_t hash)
{
    size_t bn = n / 2;
    unsigned char *b = xmalloc(bn + 1); size_t k = 0;
    for (size_t i = 0; i + 1 < n; ) {
        int h1 = hex[i], h2 = hex[i + 1];
        if (!isxdigit(h1)) { i++; continue; }
        int v1 = isdigit(h1) ? h1 - '0' : (tolower(h1) - 'a' + 10);
        int v2 = isdigit(h2) ? h2 - '0' : (tolower(h2) - 'a' + 10);
        b[k++] = (unsigned char)(v1 * 16 + v2);
        i += 2;
    }
    b[k] = 0;
    int ok = 0;
    if (strncmp((char *)b, "SCOOPSTAR 1 ", 12) == 0) {
        int lm, rm, pm, tw; unsigned h;
        char *line2 = strchr((char *)b, '\n');
        if (line2 && sscanf((char *)b + 12, "%d %d %d %d", &lm, &rm, &pm, &tw) == 4 && sscanf(line2 + 1, "H%08X", &h) == 1) {
            if (h == hash) {
                char *s = strchr(line2 + 1, '\n');
                if (s) {
                    s++;
                    /* decode */
                    int wl = MultiByteToWideChar(CP_UTF8, 0, s, (int)strlen(s), NULL, 0);
                    wchar_t *w = xmalloc((wl + 1) * sizeof(wchar_t));
                    MultiByteToWideChar(CP_UTF8, 0, s, (int)strlen(s), w, wl);
                    w[wl] = 0;
                    for (int i = 0; i < d->np; i++) free(d->p[i].t);
                    d->np = 0;
                    d->base.lm = (short)lm; d->base.rm = (short)rm; d->base.pm = (short)pm;
                    wch *pb = xmalloc((wl + 1) * sizeof(wch));
                    for (int i2 = 0; i2 < d->nnotes; i2++) free(d->notes[i2].t);
                    d->nnotes = 0;
                    /* pass 1: footnotes ("#N<k>:" lines) get the same indices, in order */
                    for (int pass = 0; pass < 2; pass++) {
                        int i = 0;
                        while (i < wl) {
                            int isnote = (w[i] == '#' && i + 1 < wl && w[i + 1] == 'N');
                            if (isnote != (pass == 0)) { while (i < wl && w[i] != '\n') i++; i++; continue; }
                            int style = 0, nkind = 0; wchar_t nmark[12] = L"";
                            if (isnote) {
                                i += 2; while (i < wl && iswdigit(w[i])) i++;
                                if (i < wl && w[i] == ',') {
                                    i++; while (i < wl && iswdigit(w[i])) nkind = nkind * 10 + (w[i++] - '0');
                                    if (i < wl && w[i] == ',') { i++; int mk = 0; while (i < wl && w[i] != ':' && w[i] != '\n') { if (mk < 11) nmark[mk++] = w[i]; i++; } nmark[mk] = 0; }
                                }
                            }
                            else while (i < wl && iswdigit(w[i])) style = style * 10 + (w[i++] - '0');
                            if (i < wl && w[i] == ':') i++;
                            int pn = 0;
                            while (i < wl && w[i] != '\n') {
                                wchar_t c = w[i];
                                if (c == '~') {
                                    if (w[i + 1] == '~') { pb[pn++] = '~'; i += 2; continue; }
                                    wchar_t t = w[i + 1];
                                    if (t == 'F') {
                                        int j = i + 2, hp = 0;
                                        while (j < wl && iswdigit(w[j])) hp = hp * 10 + (w[j++] - '0');
                                        if (w[j] == ',') j++;
                                        wchar_t nm[40]; int nk = 0;
                                        while (j < wl && w[j] != '~' && w[j] != '\n') { if (nk < 39) nm[nk++] = w[j]; j++; }
                                        nm[nk] = 0;
                                        pb[pn++] = TG_FONT0 + font_find_or_add(nm, hp ? hp : 24);
                                        i = j + 1; continue;
                                    }
                                    if (t == 'N') {
                                        int j = i + 2, k = 0;
                                        while (j < wl && iswdigit(w[j])) k = k * 10 + (w[j++] - '0');
                                        if (k < d->nnotes) pb[pn++] = NOTEBASE + (wch)k;
                                        i = j + 1; continue;
                                    }
                                    if (t == 'n') { pb[pn++] = '\n'; i += 3; continue; }
                                    if (t == 'P') {
                                        int j = i + 2, k = 0;
                                        while (j < wl && iswdigit(w[j])) k = k * 10 + (w[j++] - '0');
                                        if (k >= 1 && k < 16) pb[pn++] = PNUMBASE + (wch)k;
                                        i = j + 1; continue;
                                    }
                                    if (t == 'K') { pb[pn++] = TG_INDEX; i += 3; continue; }
                                    wch tg = 0;
                                    switch (t) { case 'B': tg = TG_BOLD; break; case 'I': tg = TG_ITALIC; break; case 'U': tg = TG_UNDER; break;
                                        case 'X': tg = TG_STRIKE; break; case 'T': tg = TG_SUPER; break; case 'V': tg = TG_SUB; break; case 'D': tg = TG_DOUBLE; break; }
                                    if (tg) pb[pn++] = tg;
                                    i += 3; continue;
                                }
                                if (c >= 0xD800 && c < 0xDC00 && i + 1 < wl) { pb[pn++] = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00); i += 2; continue; }
                                pb[pn++] = c; i++;
                            }
                            if (isnote) note_add_kind(d, pb, pn, nkind, nmark);
                            else {
                                doc_append_para(d, pb, pn);
                                d->p[d->np - 1].style = (uint8_t)(style < g_npstyles ? style : 0);
                            }
                            i++;
                        }
                    }
                    free(pb); free(w);
                    if (d->np == 0) doc_append_para(d, NULL, 0);
                    ok = 1;
                }
            }
        }
    }
    free(b);
    return ok;
}

static const char *skip_dests[] = {
    "colortbl", "info", "pict", "object", "datastore", "themedata", "colorschememapping", "latentstyles",
    "listtable", "listoverridetable", "rsidtbl", "generator", "xmlnstbl", "mmathPr", "pgdsctbl",
    "ftnsep", "ftnsepc", "aftnsep", "aftnsepc", "bkmkstart", "bkmkend", "nonshppict", "shp", "shpinst", "sp",
    "revtbl", "filetbl", "listtext", "pntext", "pntxta", "pntxtb", "fldrslt_skip", "xe", "tc", "userprops",
    "docvar", "wgrffmtfilter", "passwordhash", "protusertbl", "defchp", "defpap", "template", "atnid", "atnauthor",
    "annotation", "atrfstart", "atrfend", "comment", "objdata", "result", NULL
};

int load_rtf_mem(Doc *d, const char *buf, size_t n)
{
    RCtx *c = xmalloc(sizeof(RCtx));
    memset(c, 0, sizeof(*c));
    c->d = d;
    for (int i = 0; i < d->np; i++) free(d->p[i].t);
    d->np = 0;
    c->st[0].font = -2; c->st[0].uc = 1;
    g_rdeff = 0; g_rc = c;
    c->efont = -1;
    c->codepage = 1252;
    for (int i = 0; i < 512; i++) c->rtffont[i] = -1;
    c->cur_lm = d->base.lm; c->cur_rm = d->base.rm; c->cur_pm = d->base.pm; c->cur_ls = d->base.ls; c->cur_al = d->base.align;
    c->align = 'L';
    c->hash = 2166136261u;
    c->curcols = 1;
    c->curfont = -1;
    int deff = 0;
    int ignorable_next = 0;
    size_t i = 0;
    int depth0 = 0;
    while (i < n) {
        char ch = buf[i];
        RState *s = &c->st[c->sp];
        if (ch == '{') {
            if (c->sp < 255) { c->st[c->sp + 1] = *s; c->sp++; }
            i++; depth0++;
            continue;
        }
        if (ch == '}') {
            RState *cur = &c->st[c->sp];
            if (cur->dest == D_NOTE && c->innote == c->sp) note_end(c);
            if (cur->dest == D_HEADER || cur->dest == D_FOOTER) {
                if (c->sp > 0 && c->st[c->sp - 1].dest != cur->dest) {
                    c->hf[c->hfn] = 0;
                    /* trim */
                    while (c->hfn > 0 && c->hf[c->hfn - 1] == ' ') c->hf[--c->hfn] = 0;
                    if (c->hfn) {
                        wchar_t t[270]; swprintf(t, 270, cur->dest == D_HEADER ? L".he %ls" : L".fo %ls", c->hf);
                        append_dot(c, t);
                    }
                    c->hfn = 0;
                }
            }
            if (cur->dest == D_FLDINST) {
                /* nothing */
            }
            if (c->sp > 0) c->sp--;
            if (c->fldpage && c->st[c->sp].dest != D_FLDINST && 0) c->fldpage = 0;
            i++;
            continue;
        }
        if (ch == '\\') {
            i++;
            if (i >= n) break;
            char c1 = buf[i];
            if (!isalpha((unsigned char)c1)) {
                i++;
                switch (c1) {
                case '\\': case '{': case '}': rchar(c, (wch)c1); break;
                case '~': rchar(c, 0xA0); break;
                case '_': rchar(c, '-'); break;
                case '-': if (s->dest == D_BODY && !s->skip) rchar(c, SHY); break;
                case '*': ignorable_next = 1; break;
                case '\'': {
                    if (i + 1 < n) {
                        char hx[3] = { buf[i], buf[i + 1], 0 };
                        unsigned char bt = (unsigned char)strtol(hx, NULL, 16);
                        i += 2;
                        if (c->ucskip > 0) { c->ucskip--; break; }
                        wchar_t wc;
                        if (MultiByteToWideChar(c->codepage, 0, (char *)&bt, 1, &wc, 1) != 1) wc = bt;
                        if (s->dest == D_FONTTBL || s->dest == D_STYLESHEET || !s->skip) rchar(c, wc);
                    }
                    break; }
                case '\n': case '\r': if (s->dest == D_BODY && !s->skip) { rpar(c); } break;
                default: break;
                }
                continue;
            }
            char word[40]; int wl = 0;
            while (i < n && isalpha((unsigned char)buf[i]) && wl < 39) word[wl++] = buf[i++];
            word[wl] = 0;
            int hasp = 0, neg = 0; long param = 0;
            if (i < n && buf[i] == '-') { neg = 1; i++; }
            while (i < n && isdigit((unsigned char)buf[i])) { param = param * 10 + (buf[i] - '0'); i++; hasp = 1; }
            if (neg) param = -param;
            if (i < n && buf[i] == ' ') i++;
            /* destinations */
            int isdest = 0;
            if (ignorable_next) {
                ignorable_next = 0;
                if (!strcmp(word, "scoopstar")) { s->dest = D_SCOOP; continue; }
                if (!strcmp(word, "footnote")) { if (s->dest == D_BODY && !s->skip && !c->innote) note_begin(c, s); else s->skip = 1; continue; }
                if (!strcmp(word, "fldinst")) { s->dest = D_FLDINST; s->skip = 1;
                    /* check for PAGE field */
                    size_t j = i; while (j < n && (buf[j] == ' ' || buf[j] == '{')) j++;
                    if (j + 4 <= n && !strncmp(buf + j, "PAGE", 4) && (c->st[c->sp].dest == D_FLDINST) && c->sp > 0 &&
                        (c->st[c->sp - 1].dest == D_HEADER || c->st[c->sp - 1].dest == D_FOOTER || 1)) {
                        int hd = 0; for (int k = c->sp; k >= 0; k--) if (c->st[k].dest == D_HEADER || c->st[k].dest == D_FOOTER) hd = 1;
                        if (hd && c->hfn < 250) { c->hf[c->hfn++] = '#'; c->fldpage = 1; }
                    }
                    continue; }
                s->skip = 1; s->dest = s->dest == D_BODY ? D_BODY : s->dest;
                if (s->dest == D_HEADER || s->dest == D_FOOTER) s->dest = D_BODY, s->skip = 1;
                continue;
            }
            if (!strcmp(word, "footnote")) { if (s->dest == D_BODY && !s->skip && !c->innote) note_begin(c, s); else s->skip = 1; continue; }
            for (int k = 0; skip_dests[k]; k++) if (!strcmp(word, skip_dests[k])) { s->skip = 1; isdest = 1; if (s->dest == D_HEADER || s->dest == D_FOOTER) s->dest = D_BODY; break; }
            if (isdest) continue;
            if (!strcmp(word, "fonttbl")) { s->dest = D_FONTTBL; continue; }
            if (!strcmp(word, "stylesheet")) { s->dest = D_STYLESHEET; continue; }
            if (!strcmp(word, "header") || !strcmp(word, "headerr") || !strcmp(word, "footer") || !strcmp(word, "footerr")) {
                s->dest = word[0] == 'h' ? D_HEADER : D_FOOTER; c->hfn = 0; c->fldpage = 0; continue;
            }
            if (!strcmp(word, "headerl") || !strcmp(word, "headerf") || !strcmp(word, "footerl") || !strcmp(word, "footerf")) {
                s->dest = D_BODY; s->skip = 1; continue;   /* left/first-page variants are not used */
            }
            if (!strcmp(word, "fldrslt")) { if (c->fldpage) { s->skip = 1; c->fldpage = 0; s->dest = D_BODY; } continue; }
            if (!strcmp(word, "chpgn")) { int hd = 0; for (int k = c->sp; k >= 0; k--) if (c->st[k].dest == D_HEADER || c->st[k].dest == D_FOOTER) hd = 1; if (hd && c->hfn < 250) c->hf[c->hfn++] = '#'; continue; }
            /* font table entries */
            if (s->dest == D_FONTTBL) { if (!strcmp(word, "f")) c->curfont = (int)param; continue; }
            if (s->dest == D_STYLESHEET) { if (!strcmp(word, "s")) c->curstyle = (int)param; else if (!strcmp(word, "cs") || !strcmp(word, "ds") || !strcmp(word, "ts")) c->curstyle = -1; continue; }
            /* document */
            if (!strcmp(word, "ansicpg")) { c->codepage = (int)param; continue; }
            if (!strcmp(word, "deff")) { deff = (int)param; g_rdeff = deff; continue; }
            if (!strcmp(word, "paperw")) { c->paperw = (int)param; continue; }
            if (!strcmp(word, "paperh")) { c->paperh = (int)param; continue; }
            if (!strcmp(word, "margl")) { c->margl = (int)param; continue; }
            if (!strcmp(word, "margr")) { c->margr = (int)param; continue; }
            if (!strcmp(word, "margt")) { c->margt = (int)param; continue; }
            if (!strcmp(word, "margb")) { c->margb = (int)param; continue; }
            if (!strcmp(word, "uc")) { s->uc = (int)param; continue; }
            if (!strcmp(word, "u")) {
                long v = param; if (v < 0) v += 65536;
                static int hi;
                if (v >= 0xD800 && v < 0xDC00) hi = (int)v;
                else if (v >= 0xDC00 && v < 0xE000 && hi) { rchar(c, 0x10000 + ((hi - 0xD800) << 10) + (v - 0xDC00)); hi = 0; }
                else rchar(c, (wch)v);
                c->ucskip = s->uc;
                /* skip fallback characters */
                while (c->ucskip > 0 && i < n) {
                    if (buf[i] == '\\' && i + 1 < n && buf[i + 1] == '\'') { i += 4; c->ucskip--; }
                    else if (buf[i] == '\\' || buf[i] == '{' || buf[i] == '}') break;
                    else { i++; c->ucskip--; }
                }
                c->ucskip = 0;
                continue;
            }
            if (s->skip) continue;
            /* character formatting */
            if (!strcmp(word, "b")) { if (hasp && !param) s->fx &= ~FX_BOLD; else s->fx |= FX_BOLD; }
            else if (!strcmp(word, "i")) { if (hasp && !param) s->fx &= ~FX_ITALIC; else s->fx |= FX_ITALIC; }
            else if (!strcmp(word, "ul") || !strcmp(word, "uld") || !strcmp(word, "uldb") || !strcmp(word, "ulw")) { if (hasp && !param) s->fx &= ~FX_UNDER; else s->fx |= FX_UNDER; }
            else if (!strcmp(word, "ulnone")) s->fx &= ~FX_UNDER;
            else if (!strcmp(word, "strike") || !strcmp(word, "striked")) { if (hasp && !param) s->fx &= ~FX_STRIKE; else s->fx |= FX_STRIKE; }
            else if (!strcmp(word, "super")) { s->fx = (s->fx & ~FX_SUB) | FX_SUPER; }
            else if (!strcmp(word, "sub")) { s->fx = (s->fx & ~FX_SUPER) | FX_SUB; }
            else if (!strcmp(word, "nosupersub")) s->fx &= ~(FX_SUPER | FX_SUB);
            else if (!strcmp(word, "plain")) { s->fx = 0; s->font = -2; }
            else if (!strcmp(word, "f")) {
                int rf = (param >= 0 && param < 512) ? c->rtffont[param] : -1;
                int hp = s->font == -2 ? DEFAULT_HALFPTS : rfont_size(s->font);
                s->font = rfont_make(rf >= 0 ? g_fonts[rf].name : DEFAULT_FONT, hp);
            }
            else if (!strcmp(word, "fs")) {
                s->font = rfont_make(rfont_name(c, s->font), (int)param);
            }
            /* paragraph formatting */
            else if (!strcmp(word, "pard")) { c->align = 'L'; c->li = c->ri = c->fi = 0; c->sl = 0; c->pstyle = 0; }
            else if (!strcmp(word, "ql")) c->align = 'L';
            else if (!strcmp(word, "qc")) c->align = 'C';
            else if (!strcmp(word, "qr")) c->align = 'R';
            else if (!strcmp(word, "qj")) c->align = 'J';
            else if (!strcmp(word, "li")) c->li = (int)param;
            else if (!strcmp(word, "ri")) c->ri = (int)param;
            else if (!strcmp(word, "fi")) c->fi = (int)param;
            else if (!strcmp(word, "sl")) c->sl = (int)(param < 0 ? -param : param);
            else if (!strcmp(word, "s")) c->pstyle = (param >= 0 && param < 256) ? c->stylemap[param] : 0;
            else if (!strcmp(word, "pagebb")) c->pagebb = 1;
            else if (!strcmp(word, "par")) { if (s->dest == D_BODY) rpar(c); else if (s->dest == D_NOTE) note_push(c, '\n'); }
            else if (!strcmp(word, "line")) { if (s->dest == D_BODY) rpar(c); else if (s->dest == D_NOTE) note_push(c, '\n'); }
            else if (!strcmp(word, "row")) { if (s->dest == D_BODY) rpar(c); }
            else if (!strcmp(word, "cell")) rchar(c, '\t');
            else if (!strcmp(word, "page")) { if (s->dest == D_BODY) { if (c->pn) rpar(c); append_dot(c, L".pa"); } }
            else if (!strcmp(word, "column")) { if (s->dest == D_BODY && !s->skip) { if (c->pn) rpar(c); append_dot(c, L".cb"); } }
            else if (!strcmp(word, "cols") && hasp && param >= 1 && param <= 8) {
                if (s->dest == D_BODY && !s->skip && param != c->curcols) {
                    if (c->pn) rpar(c);
                    wchar_t t[24]; swprintf(t, 24, L".co %d", (int)param); append_dot(c, t); c->curcols = (int)param;
                }
            }
            else if (!strcmp(word, "ftnalt")) { if (c->innote) c->nkind = NOTE_END; }
            else if (!strcmp(word, "tab")) rchar(c, '\t');
            else if (!strcmp(word, "emdash")) rchar(c, 0x2014);
            else if (!strcmp(word, "endash")) rchar(c, 0x2013);
            else if (!strcmp(word, "bullet")) rchar(c, 0x2022);
            else if (!strcmp(word, "lquote")) rchar(c, 0x2018);
            else if (!strcmp(word, "rquote")) rchar(c, 0x2019);
            else if (!strcmp(word, "ldblquote")) rchar(c, 0x201C);
            else if (!strcmp(word, "rdblquote")) rchar(c, 0x201D);
            else if (!strcmp(word, "emspace") || !strcmp(word, "enspace") || !strcmp(word, "qmspace")) rchar(c, ' ');
            continue;
        }
        if (ch == '\r' || ch == '\n') { i++; continue; }
        if (ch == '\0') break;
        /* plain text byte (cp1252 or utf-8 if raw) */
        if ((unsigned char)ch >= 0x80) {
            wchar_t wc; char bt = ch;
            if (MultiByteToWideChar(c->codepage, 0, &bt, 1, &wc, 1) != 1) wc = (unsigned char)ch;
            if (!s->skip || s->dest != D_BODY) rchar(c, wc);
        } else if (!s->skip || (s->dest != D_BODY)) rchar(c, (wch)(unsigned char)ch);
        i++;
    }
    if (c->pn) rpar(c);
    /* page geometry from \paperw etc. */
    if (c->paperw > 0) {
        int pw = c->paperw, ml = c->margl ? c->margl : 1800, mr = c->margr ? c->margr : 1800;
        int cols = (pw - ml - mr) / 144;
        if (cols >= 20 && cols <= 200 && cols != d->base.rm) {
            /* express as dot commands at the top so the file keeps its line length */
            wchar_t t[32]; swprintf(t, 32, L".rm %d", cols);
            Doc tmp; (void)tmp;
            /* insert at top */
            Pos at = { 0, 0 };
            wch b[32]; int k = (int)wcslen(t);
            for (int j = 0; j < k; j++) b[j] = t[j];
            b[k] = '\n';
            if (d->np == 0) doc_append_para(d, NULL, 0);
            doc_insert(d, at, b, k + 1);
        }
    }
    if (d->np == 0) doc_append_para(d, NULL, 0);
    /* if our private copy matches the visible text, use it for an exact round trip */
    if (c->scoop.n) parse_scoop(d, c->scoop.b, c->scoop.n, c->hash);
    free(c->scoop.b); free(c->pb); free(c->nb);
    free(c);
    d->fmt = FMT_RTF;
    d->dirty = 0;
    doc_touch(d);
    return 1;
}

/* ------------------------------------------------------------------ WordStar import */
static const wchar_t cp437hi[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0 };

int load_wordstar_mem(Doc *d, const unsigned char *b, size_t n)
{
    for (int i = 0; i < d->np; i++) free(d->p[i].t);
    d->np = 0;
    size_t i = 0;
    if (n >= 128 && b[0] == 0x1D && b[1] == 0x7D) i = 128;
    wch *pb = xmalloc((n + 2) * sizeof(wch)); int pn = 0;
    for (; i < n; i++) {
        unsigned char c = b[i];
        if (c == 0x1A) break;
        if (c == 0x0D && i + 1 < n && b[i + 1] == 0x0A) { doc_append_para(d, pb, pn); pn = 0; i++; continue; }
        if (c == 0x8D) {
            if (i + 1 < n && b[i + 1] == 0x0A) i++;
            if (pn > 0 && pb[pn - 1] != ' ' && pb[pn - 1] != '-') pb[pn++] = ' ';
            continue;
        }
        if (c == 0x0A) continue;
        if (c == 0x0D) { doc_append_para(d, pb, pn); pn = 0; continue; }
        if (c == 0x1B) {
            if (i + 2 < n && b[i + 2] == 0x1C) { unsigned char e = b[i + 1]; pb[pn++] = e >= 0x80 ? cp437hi[e - 0x80] : e; i += 2; continue; }
            continue;
        }
        if (c == 0x1D) {
            if (i + 2 < n) {
                size_t len = b[i + 1] | (b[i + 2] << 8);
                size_t end = i + 3 + len;
                if (end <= n) {
                    if (end > 0 && b[end - 1] == 0x1D) i = end - 1;
                    else if (end < n && b[end] == 0x1D) i = end;
                    else i = end - 1;
                }
            }
            continue;
        }
        switch (c) {
        case 0x02: pb[pn++] = TG_BOLD; continue;
        case 0x13: pb[pn++] = TG_UNDER; continue;
        case 0x19: pb[pn++] = TG_ITALIC; continue;
        case 0x18: pb[pn++] = TG_STRIKE; continue;
        case 0x14: pb[pn++] = TG_SUPER; continue;
        case 0x16: pb[pn++] = TG_SUB; continue;
        case 0x04: pb[pn++] = TG_DOUBLE; continue;
        case 0x09: pb[pn++] = '\t'; continue;
        case 0x0F: pb[pn++] = 0xA0; continue;
        case 0x1E: continue;
        case 0x1F: if (i + 1 < n && b[i + 1] == 0x8D) pb[pn++] = '-'; continue;
        }
        if (c < 0x20) continue;
        if (c >= 0x80) {
            c &= 0x7F;
            if (c == ' ' && pn > 0 && pb[pn - 1] == ' ') continue;  /* justification soft space */
            if (c < 0x20) continue;
        }
        pb[pn++] = c;
    }
    if (pn || d->np == 0) doc_append_para(d, pb, pn);
    free(pb);
    /* close any effect left open at the end of each dot-free document? (WordStar toggles span paragraphs) */
    d->fmt = FMT_WS;
    d->nondoc = 0;
    doc_touch(d);
    return 1;
}

static int looks_like_wordstar(const unsigned char *b, size_t n, const wchar_t *ext)
{
    if (n >= 2 && b[0] == 0x1D && b[1] == 0x7D) return 1;
    size_t soft = 0, hi = 0, ctl = 0;
    size_t lim = n < 65536 ? n : 65536;
    for (size_t i = 0; i < lim; i++) {
        if (b[i] == 0x8D && i + 1 < n && b[i + 1] == 0x0A) soft++;
        else if (b[i] >= 0x80) hi++;
        if (b[i] == 0x02 || b[i] == 0x13 || b[i] == 0x19) ctl++;
    }
    if (soft >= 2) return 1;
    if (ctl >= 2 && (!_wcsicmp(ext, L"ws") || !_wcsicmp(ext, L"doc") || !_wcsicmp(ext, L"ws7") || !_wcsicmp(ext, L"wsd"))) return 1;
    if ((!_wcsicmp(ext, L"ws") || !_wcsicmp(ext, L"ws7")) && hi) return 1;
    return 0;
}

int load_file(Doc *d, const wchar_t *path, int nondoc)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 200 * 1024 * 1024) { CloseHandle(h); return 0; }
    size_t n = (size_t)sz.QuadPart;
    unsigned char *b = xmalloc(n + 1);
    DWORD rd = 0; ReadFile(h, b, (DWORD)n, &rd, NULL);
    CloseHandle(h);
    n = rd; b[n] = 0;
    wchar_t ext[16]; path_ext(path, ext, 16);
    int ok = 1;
    if (nondoc) { load_text_mem(d, b, n); d->nondoc = 1; d->fmt = FMT_TXT; }
    else if (n >= 5 && !memcmp(b, "{\\rtf", 5)) { ok = load_rtf_mem(d, (const char *)b, n); d->nondoc = 0; }
    else if (n >= 4 && b[0] == 0xD0 && b[1] == 0xCF && b[2] == 0x11 && b[3] == 0xE0) {
        free(b);
        msgbox(L"Open", L"This is a Microsoft Word .DOC (binary) file.\nScoopStar can't read that format. Save it as\nRich Text Format (.RTF) from Word or LibreOffice.", L"OK");
        return 0;
    }
    else if (n >= 2 && b[0] == 'P' && b[1] == 'K') {
        free(b);
        msgbox(L"Open", L"This looks like a .DOCX or .ODT file.\nScoopStar can't read that format. Save it as\nRich Text Format (.RTF) from Word or LibreOffice.", L"OK");
        return 0;
    }
    else if (looks_like_wordstar(b, n, ext)) { load_wordstar_mem(d, b, n); }
    else { load_text_mem(d, b, n); d->nondoc = 0; d->fmt = FMT_TXT; }
    free(b);
    if (!ok) return 0;
    wcscpy(d->path, path);
    d->dirty = 0;
    undo_clear(d);
    for (int i = 0; i < d->nregs; i++) { d->regs[i]->p = 0; d->regs[i]->o = 0; }
    doc_touch(d);
    return 1;
}
