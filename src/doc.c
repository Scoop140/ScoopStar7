/*
 * ScoopStar 7.0 - doc.c
 * Document model (paragraphs of characters + embedded command tags),
 * dot-command formatting, word-wrap layout, pagination, undo.
 */
#include "scoopstar.h"

FontDef g_fonts[256];
int g_nfonts;

ParaStyle g_pstyles[] = {
    { L"Body text",        0, 0, 0,   0, 0, 0, 0 },
    { L"Title",            1, 0, 'C', 0, 0, 0, 0 },
    { L"Heading 1",        1, 0, 0,   0, 0, 0, 1 },
    { L"Heading 2",        1, 1, 0,   0, 0, 0, 1 },
    { L"Heading 3",        0, 1, 0,   0, 0, 0, 0 },
    { L"Hanging Indent 1", 0, 0, 0,   5, 0, -5, 0 },
    { L"Hanging Indent 2", 0, 0, 0,  10, 0, -5, 0 },
    { L"Indented Quote",   0, 0, 0,   5, 5, 0, 0 },
    { L"First Line Indent",0, 0, 0,   0, 0, 5, 0 },
    { L"Centered",         0, 0, 'C', 0, 0, 0, 0 },
    { L"Right Aligned",    0, 0, 'R', 0, 0, 0, 0 },
    { L"Justified",        0, 0, 'J', 0, 0, 0, 0 },
};
int g_npstyles = sizeof(g_pstyles) / sizeof(g_pstyles[0]);

int font_find_or_add(const wchar_t *name, int halfpts)
{
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].halfpts == halfpts && !_wcsicmp(g_fonts[i].name, name)) return i;
    if (g_nfonts >= 256) return 0;
    wcsncpy(g_fonts[g_nfonts].name, name, 39);
    g_fonts[g_nfonts].name[39] = 0;
    g_fonts[g_nfonts].halfpts = halfpts;
    return g_nfonts++;
}

int wch_isword(wch c)
{
    if (IS_TAG(c) || IS_SPECIAL(c)) return 0;
    if (c == SHY) return 1;
    if (c < 128) return isalnum((int)c) || c == '\'' || c == '_';
    return c != 0xA0 && c != 0x2014 && c != 0x2013 && !(c >= 0x2000 && c <= 0x206F);
}

int pos_cmp(Pos a, Pos b) { return a.p != b.p ? (a.p < b.p ? -1 : 1) : (a.o < b.o ? -1 : a.o > b.o); }

/* ------------------------------------------------------------------ doc */
Doc *doc_new(void)
{
    Doc *d = xmalloc(sizeof(Doc));
    memset(d, 0, sizeof(*d));
    fmt_default(&d->base);
    d->fmt = FMT_RTF;
    d->crlf = 1;
    doc_append_para(d, NULL, 0);
    d->regs[d->nregs++] = &d->bb;
    d->regs[d->nregs++] = &d->bk;
    d->regs[d->nregs++] = &d->prevpos;
    d->regs[d->nregs++] = &d->lastfind;
    for (int i = 0; i < 10; i++) d->regs[d->nregs++] = &d->mark[i];
    d->ver = 1;
    return d;
}

void doc_clear(Doc *d)
{
    for (int i = 0; i < d->np; i++) free(d->p[i].t);
    d->np = 0;
    for (int i = 0; i < d->nnotes; i++) free(d->notes[i].t);
    d->nnotes = 0;
    for (int i = 0; i < d->nregs; i++) { d->regs[i]->p = 0; d->regs[i]->o = 0; }
    d->bbset = d->bkset = 0;
    memset(d->markset, 0, sizeof(d->markset));
    doc_touch(d);
}

void doc_free(Doc *d)
{
    if (!d) return;
    doc_clear(d);
    free(d->p); free(d->L); free(d->notes);
    undo_clear(d);
    free(d->undo);
    free(d);
}

void doc_touch(Doc *d) { d->ver++; }

void doc_register_pos(Doc *d, Pos *p)
{
    for (int i = 0; i < d->nregs; i++) if (d->regs[i] == p) return;
    if (d->nregs < 24) d->regs[d->nregs++] = p;
}
void doc_unregister_pos(Doc *d, Pos *p)
{
    for (int i = 0; i < d->nregs; i++)
        if (d->regs[i] == p) { d->regs[i] = d->regs[--d->nregs]; return; }
}

Para *doc_para(Doc *d, int i) { return (i >= 0 && i < d->np) ? &d->p[i] : NULL; }

static void para_reserve(Para *p, int n)
{
    if (n <= p->cap) return;
    int nc = p->cap ? p->cap : 16;
    while (nc < n) nc *= 2;
    p->t = xrealloc(p->t, nc * sizeof(wch));
    p->cap = nc;
}

static void doc_reserve(Doc *d, int n)
{
    if (n <= d->cap) return;
    int nc = d->cap ? d->cap : 64;
    while (nc < n) nc *= 2;
    d->p = xrealloc(d->p, nc * sizeof(Para));
    d->cap = nc;
}

static void doc_insert_para_at(Doc *d, int at)
{
    doc_reserve(d, d->np + 1);
    memmove(&d->p[at + 1], &d->p[at], (d->np - at) * sizeof(Para));
    memset(&d->p[at], 0, sizeof(Para));
    d->np++;
}

void doc_append_para(Doc *d, const wch *s, int n)
{
    doc_insert_para_at(d, d->np);
    Para *p = &d->p[d->np - 1];
    para_reserve(p, n + 1);
    if (n) memcpy(p->t, s, n * sizeof(wch));
    p->n = n;
    doc_touch(d);
}

void para_ins(Doc *d, int pi, int at, const wch *s, int n)
{
    Para *p = &d->p[pi];
    if (n <= 0) return;
    if (at > p->n) at = p->n;
    para_reserve(p, p->n + n + 1);
    memmove(p->t + at + n, p->t + at, (p->n - at) * sizeof(wch));
    memcpy(p->t + at, s, n * sizeof(wch));
    p->n += n;
    for (int i = 0; i < d->nregs; i++) {
        Pos *r = d->regs[i];
        if (r->p == pi && (r->o > at || (r->o == at && r != &d->bb))) r->o += n;
    }
    d->dirty = 1;
    doc_touch(d);
}

void para_del(Doc *d, int pi, int at, int n)
{
    Para *p = &d->p[pi];
    if (at < 0) { n += at; at = 0; }
    if (at + n > p->n) n = p->n - at;
    if (n <= 0) return;
    memmove(p->t + at, p->t + at + n, (p->n - at - n) * sizeof(wch));
    p->n -= n;
    for (int i = 0; i < d->nregs; i++) {
        Pos *r = d->regs[i];
        if (r->p == pi) {
            if (r->o >= at + n) r->o -= n;
            else if (r->o > at) r->o = at;
        }
    }
    d->dirty = 1;
    doc_touch(d);
}

void doc_split(Doc *d, Pos at)
{
    Para *p = &d->p[at.p];
    if (at.o > p->n) at.o = p->n;
    int tail = p->n - at.o;
    doc_insert_para_at(d, at.p + 1);
    p = &d->p[at.p];
    Para *q = &d->p[at.p + 1];
    para_reserve(q, tail + 1);
    memcpy(q->t, p->t + at.o, tail * sizeof(wch));
    q->n = tail;
    q->style = p->style;
    p->n = at.o;
    for (int i = 0; i < d->nregs; i++) {
        Pos *r = d->regs[i];
        if (r->p > at.p) r->p++;
        else if (r->p == at.p && r->o >= at.o && !(r == &d->bb && r->o == at.o && 0)) { r->p++; r->o -= at.o; }
    }
    d->dirty = 1;
    doc_touch(d);
}

void doc_join(Doc *d, int pi)
{
    if (pi + 1 >= d->np) return;
    Para *p = &d->p[pi], *q = &d->p[pi + 1];
    int base = p->n;
    para_reserve(p, p->n + q->n + 1);
    q = &d->p[pi + 1];
    memcpy(p->t + p->n, q->t, q->n * sizeof(wch));
    p->n += q->n;
    if (base == 0) p->style = q->style;
    free(q->t);
    memmove(&d->p[pi + 1], &d->p[pi + 2], (d->np - pi - 2) * sizeof(Para));
    d->np--;
    for (int i = 0; i < d->nregs; i++) {
        Pos *r = d->regs[i];
        if (r->p == pi + 1) { r->p = pi; r->o += base; }
        else if (r->p > pi + 1) r->p--;
    }
    d->dirty = 1;
    doc_touch(d);
}

Pos doc_insert(Doc *d, Pos at, const wch *s, int n)
{
    Pos cur = at;
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && s[j] != '\n') j++;
        if (j > i) { para_ins(d, cur.p, cur.o, s + i, j - i); cur.o += j - i; }
        if (j < n) { doc_split(d, cur); cur.p++; cur.o = 0; j++; }
        i = j;
    }
    return cur;
}

void doc_delete(Doc *d, Pos a, Pos b)
{
    if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
    if (a.p == b.p) { para_del(d, a.p, a.o, b.o - a.o); return; }
    para_del(d, a.p, a.o, d->p[a.p].n - a.o);
    para_del(d, b.p, 0, b.o);
    int cnt = b.p - a.p - 1;
    if (cnt > 0) {
        for (int i = a.p + 1; i < b.p; i++) free(d->p[i].t);
        memmove(&d->p[a.p + 1], &d->p[b.p], (d->np - b.p) * sizeof(Para));
        d->np -= cnt;
        for (int i = 0; i < d->nregs; i++) {
            Pos *r = d->regs[i];
            if (r->p > a.p && r->p < b.p) { r->p = a.p; r->o = a.o; }
            else if (r->p >= b.p) r->p -= cnt;
        }
    }
    doc_join(d, a.p);
}

wch *doc_extract(Doc *d, Pos a, Pos b, int *outn)
{
    if (pos_cmp(a, b) > 0) { Pos t = a; a = b; b = t; }
    int total = 0;
    for (int i = a.p; i <= b.p; i++) total += d->p[i].n + 1;
    wch *out = xmalloc((total + 1) * sizeof(wch));
    int n = 0;
    for (int i = a.p; i <= b.p; i++) {
        Para *p = &d->p[i];
        int s = (i == a.p) ? a.o : 0, e = (i == b.p) ? b.o : p->n;
        if (s > p->n) s = p->n;
        if (e > p->n) e = p->n;
        if (e > s) { memcpy(out + n, p->t + s, (e - s) * sizeof(wch)); n += e - s; }
        if (i < b.p) out[n++] = '\n';
    }
    *outn = n;
    return out;
}

int para_is_dot(Para *pp) { return pp->n >= 1 && pp->t[0] == '.'; }

/* ------------------------------------------------------------------ formatting */
void fmt_default(Fmt *f)
{
    memset(f, 0, sizeof(*f));
    f->lm = S.lm ? S.lm : 1; f->rm = S.rm ? S.rm : 65; f->pm = S.pm ? S.pm : f->lm;
    f->align = S.def_align ? S.def_align : 'L'; f->ls = (char)(S.def_ls >= 2 && S.def_ls <= 18 ? S.def_ls : 2);
    f->pl = 66; f->mt = (short)(S.def_mt > 0 ? S.def_mt : 3); f->mb = (short)(S.def_mb > 0 ? S.def_mb : 8);
    f->hm = 2; f->fm = 2; f->po = (short)(S.def_po >= 0 ? S.def_po : 8);
    if (f->hm >= f->mt) f->hm = f->mt > 1 ? f->mt - 1 : 1;
    f->ntabs = 0;
    int tw = S.tabw > 0 ? S.tabw : 5;
    for (int c = 1 + tw; c < 256 && f->ntabs < MAXTABS; c += tw) f->tabs[f->ntabs++] = c;
    f->pn = -1; f->op = 0; f->ww = 1; f->lang = S.lang;
    f->cols = 1; f->gutter = 4; f->hy = 0;
    f->pw = 85; f->land = 0;
}

int tab_next(const Fmt *f, int col)
{
    for (int i = 0; i < f->ntabs; i++) if (f->tabs[i] > col) return f->tabs[i];
    int tw = S.tabw > 0 ? S.tabw : 5;
    return ((col - 1) / tw + 1) * tw + 1;
}

int parse_measure(const wchar_t *s, int horiz, int *out)
{
    while (*s == ' ') s++;
    if (!*s) return 0;
    wchar_t *end;
    double v = wcstod(s, &end);
    if (end == s) return 0;
    while (*end == ' ') end++;
    if (*end == '"' || *end == 'i' || *end == 'I') v *= horiz ? 10.0 : 6.0;
    *out = (int)(v + (v >= 0 ? 0.5 : -0.5));
    return 1;
}

void dot_parse(const wch *t, int n, wchar_t *cmd, int cmdsz, wchar_t *arg, int argsz)
{
    int i = 1, k = 0;
    cmd[0] = 0; arg[0] = 0;
    if (n >= 2 && t[1] == '.') { wcscpy(cmd, L".."); return; }
    /* WordStar commands are two characters, so ".ls2" means ".ls 2"; header and footer
       lines may add e or o (".h1e", ".f2o") */
    while (i < n && k < cmdsz - 1 && !IS_TAG(t[i]) && t[i] != ' ' && t[i] != '\t' &&
           (iswalnum((wint_t)t[i]) || t[i] == '#') && k < 2) {
        cmd[k++] = (wchar_t)towlower((wint_t)t[i]); i++;
    }
    cmd[k] = 0;
    if (k == 2 && (cmd[0] == 'h' || cmd[0] == 'f') && cmd[1] >= '1' && cmd[1] <= '5' && i < n &&
        (towlower((wint_t)t[i]) == 'e' || towlower((wint_t)t[i]) == 'o') &&
        (i + 1 >= n || t[i + 1] == ' ' || t[i + 1] == '\t' || IS_TAG(t[i + 1])) && k < cmdsz - 1) {
        cmd[k++] = (wchar_t)towlower((wint_t)t[i]); i++; cmd[k] = 0;
    }
    while (i < n && (t[i] == ' ' || t[i] == '\t')) i++;
    k = 0;
    for (; i < n && k < argsz - 1; i++) if (!IS_TAG(t[i])) arg[k++] = (wchar_t)(t[i] < 0x10000 ? t[i] : '?');
    while (k > 0 && arg[k - 1] == ' ') k--;
    arg[k] = 0;
}

int pw_parse(const wchar_t *s)
{
    while (*s == ' ') s++;
    wchar_t *e;
    double v = wcstod(s, &e);
    if (e == s || v <= 0) return 0;
    while (*e == ' ') e++;
    wint_t u = towlower(*e);
    if (u == 'c') v = v / 2.54 * 10;          /* centimetres */
    else if (u == 'p') v = v / 7.2;           /* points */
    else if (u == 'r') ;                      /* ruler columns */
    else v *= 10;                             /* inches */
    int c = (int)(v + 0.5);
    return (c >= 20 && c <= 300) ? c : 0;
}

int ls_parse(const wchar_t *s)
{
    while (*s == ' ') s++;
    wchar_t *e;
    double v = wcstod(s, &e);
    if (e == s || v < 0.75) return 0;
    int h = (int)(v * 2 + 0.5);
    if (h < 2) h = 2;
    if (h > 18) h = 18;
    return h;
}

void ls_text(int halves, wchar_t *out)
{
    if (halves < 2) halves = 2;
    if (halves & 1) swprintf(out, 8, L"%d.5", halves / 2);
    else swprintf(out, 8, L"%d", halves / 2);
}

static int is_on(const wchar_t *a) { return !_wcsnicmp(a, L"on", 2) || !*a; }

/* apply one dot command to fmt; returns 1 for .pa, 2 for .cp (with *cp lines) */
static int apply_dot(Fmt *f, const wch *t, int n, int *cp)
{
    wchar_t cmd[8], arg[256]; int v;
    dot_parse(t, n, cmd, 8, arg, 256);
    if (!wcscmp(cmd, L"lm") && parse_measure(arg, 1, &v)) { f->lm = (wcschr(arg, '"') ? v + 1 : v); if (f->lm < 1) f->lm = 1; f->pm = f->lm; }
    else if (!wcscmp(cmd, L"rm") && parse_measure(arg, 1, &v)) { f->rm = v; if (f->rm < f->lm + 2) f->rm = f->lm + 2; if (f->rm > 250) f->rm = 250; }
    else if (!wcscmp(cmd, L"pm") && parse_measure(arg, 1, &v)) { f->pm = (wcschr(arg, '"') ? v + 1 : v); if (f->pm < 1) f->pm = 1; }
    else if (!wcscmp(cmd, L"oj")) {
        if (!_wcsnicmp(arg, L"on", 2) || !_wcsicmp(arg, L"j")) f->align = 'J';
        else if (!_wcsnicmp(arg, L"off", 3) || !_wcsicmp(arg, L"l")) f->align = 'L';
        else if (!_wcsicmp(arg, L"c")) f->align = 'C';
        else if (!_wcsicmp(arg, L"r")) f->align = 'R';
    }
    else if (!wcscmp(cmd, L"oc")) f->align = is_on(arg) ? 'C' : 'L';
    else if (!wcscmp(cmd, L"ls")) { int h = ls_parse(arg); if (h) f->ls = (char)h; }
    else if (!wcscmp(cmd, L"pl") && parse_measure(arg, 0, &v)) { if (v > 10) f->pl = v; }
    else if (!wcscmp(cmd, L"pw")) { int c = pw_parse(arg); if (c) f->pw = (short)c; }
    else if (!wcscmp(cmd, L"pr")) {
        /* .pr or=l / .pr or=p: turning the paper swaps its width and length */
        const wchar_t *o = wcsstr(arg, L"or=");
        if (!o) o = wcsstr(arg, L"OR=");
        if (o) {
            int l = towlower(o[3]) == 'l';
            if (l != f->land) {
                int npw = (f->pl * 10 + 3) / 6, npl = (f->pw * 6 + 5) / 10;
                if (npw >= 20 && npw <= 300) f->pw = (short)npw;
                if (npl > 10) f->pl = (short)npl;
                f->land = (char)l;
            }
        }
    }
    else if (!wcscmp(cmd, L"mt") && parse_measure(arg, 0, &v)) f->mt = v;
    else if (!wcscmp(cmd, L"mb") && parse_measure(arg, 0, &v)) f->mb = v;
    else if (!wcscmp(cmd, L"hm") && parse_measure(arg, 0, &v)) f->hm = v;
    else if (!wcscmp(cmd, L"fm") && parse_measure(arg, 0, &v)) f->fm = v;
    else if (!wcscmp(cmd, L"po") && parse_measure(arg, 1, &v)) f->po = v;
    else if (!wcscmp(cmd, L"pn") && parse_measure(arg, 0, &v)) f->pn = v;
    else if (!wcscmp(cmd, L"op")) f->op = 1;
    else if (!wcscmp(cmd, L"pg")) f->op = 0;
    else if (!wcscmp(cmd, L"aw")) f->ww = is_on(arg);
    else if (!wcscmp(cmd, L"tb")) {
        /* .tb 6,11,16 or #20 (decimal ignored) */
        int nt = 0; const wchar_t *s = arg;
        while (*s && nt < MAXTABS) {
            while (*s == ' ' || *s == ',' || *s == '#') s++;
            if (!*s) break;
            wchar_t *e; long c = wcstol(s, &e, 10);
            if (e == s) break;
            if (c > 1) f->tabs[nt++] = (short)c;
            s = e;
        }
        if (nt) f->ntabs = nt;
    }
    else if (!wcscmp(cmd, L"la")) { int l = lang_from_name(arg); if (l >= 0) f->lang = l; }
    else if (!wcscmp(cmd, L"hy")) f->hy = is_on(arg);
    else if (!wcscmp(cmd, L"co")) {
        /* .co n[,gutter]  gutter in columns or inches */
        int n = _wtoi(arg); if (n < 1) n = 1; if (n > 8) n = 8;
        f->cols = n;
        const wchar_t *g = wcschr(arg, ',');
        if (g) { int gv; if (parse_measure(g + 1, 1, &gv) && gv >= 0 && gv < 30) f->gutter = gv; }
    }
    else if (!wcscmp(cmd, L"pa")) return 1;
    else if (!wcscmp(cmd, L"cp") && parse_measure(arg, 0, &v)) { *cp = v; return 2; }
    return 0;
}

Fmt doc_fmt_at(Doc *d, int pi)
{
    Fmt f = d->base;
    int cp;
    for (int i = 0; i < pi && i < d->np; i++)
        if (para_is_dot(&d->p[i])) apply_dot(&f, d->p[i].t, d->p[i].n, &cp);
    return f;
}

int note_add_kind(Doc *d, const wch *t, int n, int kind, const wchar_t *mark)
{
    int i = note_add(d, t, n);
    d->notes[i].kind = kind;
    if (mark) { wcsncpy(d->notes[i].mark, mark, 11); d->notes[i].mark[11] = 0; }
    return i;
}

int note_add(Doc *d, const wch *t, int n)
{
    if (d->nnotes >= d->notecap) {
        d->notecap = d->notecap ? d->notecap * 2 : 16;
        d->notes = xrealloc(d->notes, d->notecap * sizeof(Note));
    }
    Note *nt = &d->notes[d->nnotes];
    memset(nt, 0, sizeof(*nt));
    nt->t = xmalloc((n + 1) * sizeof(wch));
    if (n) memcpy(nt->t, t, n * sizeof(wch));
    nt->n = n; nt->num = d->nnotes + 1;
    return d->nnotes++;
}

/* give every footnote reference in s its own copy of the note (from src) in dst */
void notes_dup(Doc *dst, Doc *src, wch *s, int n)
{
    for (int i = 0; i < n; i++) {
        if (!IS_NOTE(s[i])) continue;
        int k = (int)(s[i] - NOTEBASE);
        if (k >= src->nnotes) { s[i] = ' '; continue; }
        Note *o = &src->notes[k];
        /* copy first: note_add may move src->notes when dst == src */
        int len = o->n;
        wch *tmp = xmalloc((len + 1) * sizeof(wch));
        memcpy(tmp, o->t, len * sizeof(wch));
        int kind = o->kind; wchar_t mk[12]; wcscpy(mk, o->mark);
        s[i] = NOTEBASE + note_add_kind(dst, tmp, len, kind, mk);
        free(tmp);
    }
}

int note_label(Doc *d, wch c, wchar_t *buf, int n)
{
    static const wchar_t sup[10] = { 0x2070, 0xB9, 0xB2, 0xB3, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079 };
    int k = (int)(c - NOTEBASE);
    int num = (k >= 0 && k < d->nnotes) ? d->notes[k].num : 0;
    if (k >= 0 && k < d->nnotes && d->notes[k].kind == NOTE_ANNOT) {
        swprintf(buf, n, L"[%ls]", d->notes[k].mark[0] ? d->notes[k].mark : L"*");
        return (int)wcslen(buf);
    }
    wchar_t dg[16]; swprintf(dg, 16, L"%d", num);
    int i = 0;
    for (; dg[i] && i < n - 1; i++) buf[i] = sup[dg[i] - '0'];
    buf[i] = 0;
    return i;
}

/* estimate of printed lines a note needs at 'width' characters per line */
int note_wrap_lines(Doc *d, int idx, int width)
{
    if (idx < 0 || idx >= d->nnotes || width < 10) return 1;
    Note *nt = &d->notes[idx];
    int lines = 1, col = 4, lastsp = -1, wordlen = 0;
    (void)lastsp;
    for (int i = 0; i < nt->n; i++) {
        wch c = nt->t[i];
        if (IS_TAG(c)) continue;
        if (c == '\n') { lines++; col = 0; wordlen = 0; continue; }
        col++;
        if (c == ' ') wordlen = 0; else wordlen++;
        if (col > width) { lines++; col = wordlen; }
    }
    return lines;
}

int special_width(Doc *d, Para *p, wch c)
{
    if (IS_NOTE(c)) { wchar_t b[24]; return note_label(d, c, b, 24); }
    if (IS_PNUM(c)) return p->pnlab[0] ? (int)wcslen(p->pnlab) : 1;
    return 1;
}

/* .gr width height path   (width and height in inches) */
int graphic_parse(const wch *t, int n, double *w, double *h, wchar_t *path, int pathsz)
{
    wchar_t cmd[8], arg[MAX_PATH + 64];
    dot_parse(t, n, cmd, 8, arg, MAX_PATH + 64);
    if (wcscmp(cmd, L"gr")) return 0;
    wchar_t *e;
    *w = wcstod(arg, &e); if (*e == '"') e++;
    while (*e == ' ') e++;
    *h = wcstod(e, &e); if (*e == '"') e++;
    while (*e == ' ') e++;
    wcsncpy(path, e, pathsz - 1); path[pathsz - 1] = 0;
    if (*w <= 0 || *h <= 0) return 0;
    return 1;
}

int tag_label(wch c, wchar_t *buf, int n)
{
    const wchar_t *s = NULL;
    switch (c) {
    case TG_BOLD: s = L"<B>"; break;
    case TG_ITALIC: s = L"<I>"; break;
    case TG_UNDER: s = L"<U>"; break;
    case TG_STRIKE: s = L"<X>"; break;
    case TG_SUPER: s = L"<T>"; break;
    case TG_SUB: s = L"<V>"; break;
    case TG_DOUBLE: s = L"<D>"; break;
    case TG_INDEX: s = L"<K>"; break;
    }
    if (s) { wcsncpy(buf, s, n); buf[n - 1] = 0; return (int)wcslen(buf); }
    if (IS_FONTTAG(c)) {
        int fi = (int)(c - TG_FONT0);
        if (fi < g_nfonts) swprintf(buf, n, L"<%ls %g>", g_fonts[fi].name, g_fonts[fi].halfpts / 2.0);
        else swprintf(buf, n, L"<Font>");
        return (int)wcslen(buf);
    }
    swprintf(buf, n, L"<?>");
    return 3;
}

static int show_tags_now(void) { return S.show_tags; }

static uint8_t fx_toggle(uint8_t fx, wch c)
{
    switch (c) {
    case TG_BOLD: return fx ^ FX_BOLD;
    case TG_ITALIC: return fx ^ FX_ITALIC;
    case TG_UNDER: return fx ^ FX_UNDER;
    case TG_STRIKE: return fx ^ FX_STRIKE;
    case TG_SUPER: return fx ^ FX_SUPER;
    case TG_SUB: return fx ^ FX_SUB;
    case TG_DOUBLE: return fx ^ FX_DOUBLE;
    }
    return fx;
}

/* ------------------------------------------------------------------ layout */
static void addline(Doc *d, Line *l)
{
    if (d->nl >= d->lcap) {
        d->lcap = d->lcap ? d->lcap * 2 : 256;
        d->L = xrealloc(d->L, d->lcap * sizeof(Line));
    }
    d->L[d->nl++] = *l;
}

static const wchar_t *roman(int n, int upper, wchar_t *buf)
{
    static const int v[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *r[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    int k = 0; buf[0] = 0;
    if (n <= 0) { wcscpy(buf, L"0"); return buf; }
    for (int i = 0; i < 13; i++) while (n >= v[i]) { for (const char *q = r[i]; *q; q++) buf[k++] = upper ? towupper(*q) : *q; n -= v[i]; }
    buf[k] = 0;
    return buf;
}

static void pnum_label(int *cnt, int level, int style, wchar_t *out)
{
    out[0] = 0;
    if (style == 0) {   /* compound: 1.2.3. */
        for (int i = 1; i <= level; i++) { wchar_t t[16]; swprintf(t, 16, L"%d.", cnt[i] > 0 ? cnt[i] : 1); wcscat(out, t); }
        return;
    }
    /* outline: I. A. 1. a. (1) (a) i. (i) */
    int n = cnt[level] > 0 ? cnt[level] : 1;
    wchar_t t[32], rb[32];
    switch ((level - 1) % 8) {
    case 0: swprintf(t, 32, L"%ls.", roman(n, 1, rb)); break;
    case 1: swprintf(t, 32, L"%c.", 'A' + (n - 1) % 26); break;
    case 2: swprintf(t, 32, L"%d.", n); break;
    case 3: swprintf(t, 32, L"%c.", 'a' + (n - 1) % 26); break;
    case 4: swprintf(t, 32, L"(%d)", n); break;
    case 5: swprintf(t, 32, L"(%c)", 'a' + (n - 1) % 26); break;
    case 6: swprintf(t, 32, L"%ls.", roman(n, 0, rb)); break;
    default: swprintf(t, 32, L"(%ls)", roman(n, 0, rb)); break;
    }
    wcscpy(out, t);
}

static int hy_letter(wch c) { return c < 0x10000 && !IS_TAG(c) && iswalpha((wint_t)c); }

void doc_layout(Doc *d, int hint)
{
    (void)hint;
    if (d->layver == d->ver && d->L) return;
    int hide = !d->nondoc && !S.show_tags && !S.dots_always;
relayout:
    d->nl = 0;
    d->hidedots = hide;
    Fmt f = d->base;
    int page = 1, pline = 1, pending = 0, pendcol = 0, cp = 0, colno = 0;
    uint8_t fx = 0; short font = -1;
    int nondoc = d->nondoc;
    int fnum = 1, enumn = 1, fnl = 0;   /* next footnote/endnote number; footnote lines reserved on this page */
    int pncnt[10] = { 0 }, pnstyle = 0;
    int vcstart = -1, vcpage = 0;
    int cury2 = 0;      /* half lines used on this page (or column) */
    int usable = f.pl - f.mt - f.mb; if (usable < 1) usable = 1;

#define FINISH_VC() do { if (vcstart >= 0) { int rem = usable - (pline - 1) - fnl; int sh = rem > 0 ? rem / 2 : 0; \
        for (int q = vcstart; q < d->nl; q++) { if (d->L[q].page == vcpage) { d->L[q].pline += sh; d->L[q].y2 += sh * 2; } } vcstart = -1; } } while (0)
#define NEW_PAGE(pi_, s_, forced) do { FINISH_VC(); Line pb; memset(&pb, 0, sizeof(pb)); \
        pb.type = LT_PAGEBRK; pb.p = (pi_); pb.s = pb.e = (s_); pb.page = page; pb.flags = (forced) ? LF_FORCED : 0; \
        addline(d, &pb); page++; pline = 1; cury2 = 0; colno = 0; fnl = 0; pending = 0; pendcol = 0; } while (0)
#define NEW_COL(pi_, s_) do { Line cb; memset(&cb, 0, sizeof(cb)); cb.type = LT_COLBRK; cb.p = (pi_); cb.s = cb.e = (s_); \
        cb.page = page; addline(d, &cb); colno++; pline = 1; cury2 = 0; pendcol = 0; } while (0)

    for (int pi = 0; pi < d->np; pi++) {
        Para *p = &d->p[pi];
        Line l; memset(&l, 0, sizeof(l));
        l.p = pi;
        p->pnlab[0] = 0;
        if (!nondoc && para_is_dot(p)) {
            int oldcols = f.cols;
            int r = apply_dot(&f, p->t, p->n, &cp);
            usable = f.pl - f.mt - f.mb; if (usable < 1) usable = 1;
            wchar_t c8[8], a8[64]; dot_parse(p->t, p->n, c8, 8, a8, 64);
            if (!wcscmp(c8, L"f#") && _wtoi(a8) > 0) fnum = _wtoi(a8);
            else if (!wcscmp(c8, L"e#") && _wtoi(a8) > 0) enumn = _wtoi(a8);
            else if (!wcscmp(c8, L"p#")) {
                if (!_wcsnicmp(a8, L"out", 3)) pnstyle = 1;
                else if (!_wcsnicmp(a8, L"com", 3)) pnstyle = 0;
                else if (_wtoi(a8) > 0) { memset(pncnt, 0, sizeof(pncnt)); pncnt[1] = _wtoi(a8) - 1; }
            }
            else if (!wcscmp(c8, L"cb")) { if (f.cols > 1) pendcol = 1; }
            else if (!wcscmp(c8, L"vc")) { if (vcstart < 0) { vcstart = d->nl; vcpage = page; } }
            if (f.cols != oldcols && (pline > 1 || colno > 0)) pending = 1;
            if (r == 1) pending = 1;
            else if (r == 2) { if (pline > 1 && pline + cp - 1 > usable) pending = 1; }
            l.s = 0; l.e = p->n; l.col = 0; l.type = LT_TEXT; l.flags = LF_DOT | LF_LAST;
            l.fx0 = fx; l.font0 = font; l.page = page; l.pline = pline; l.align = 'L'; l.rm = f.rm;
            if (cury2 < (pline - 1) * 2) cury2 = (pline - 1) * 2;
            l.y2 = (short)cury2;
            for (int k = 0; k < p->n; k++) if (IS_TAG(p->t[k])) { if (IS_FONTTAG(p->t[k])) font = (short)(p->t[k] - TG_FONT0); else fx = fx_toggle(fx, p->t[k]); }
            l.width = (short)p->n;
            if (!hide) addline(d, &l);
            if (vcstart >= 0 && vcpage == page && !wcscmp(c8, L"vc")) vcstart = d->nl;
            /* graphics occupy lines on the page */
            double gw, gh; wchar_t gp[MAX_PATH];
            if (graphic_parse(p->t, p->n, &gw, &gh, gp, MAX_PATH)) {
                int lines = (int)(gh * 6 + 0.99); if (lines < 1) lines = 1; if (lines > usable) lines = usable;
                if (pending) NEW_PAGE(pi, 0, 1);
                else if (pline > 1 && pline + lines - 1 > usable) NEW_PAGE(pi, 0, 0);
                for (int k = 0; k < lines; k++) {
                    Line g; memset(&g, 0, sizeof(g));
                    g.type = LT_GRAPHIC; g.p = pi; g.s = k; g.e = lines; g.page = page; g.pline = pline;
                    g.width = (short)(gw * 10 + 0.5); g.col = (short)(f.lm - 1);
                    g.y2 = (short)((pline - 1) * 2);
                    addline(d, &g); pline++;
                }
            }
            continue;
        }
        ParaStyle *ps = &g_pstyles[p->style < g_npstyles ? p->style : 0];
        int flm = f.lm, frm = f.rm, colw = 0, xoffunit = 0;
        if (!nondoc && f.cols > 1) {
            colw = (f.rm - f.lm + 1 - (f.cols - 1) * f.gutter) / f.cols;
            if (colw < 8) colw = 8;
            frm = f.lm + colw - 1;
            xoffunit = colw + f.gutter;
        }
        int lm = flm + ps->lmadd, rm = frm - ps->rmsub;
        int pm = (ps->pmadd || ps->lmadd) ? f.pm + ps->lmadd + ps->pmadd : f.pm;
        if (pm < 1) pm = 1;
        if (rm < lm + 4) rm = lm + 4;
        char align = ps->align ? ps->align : f.align;

        /* paragraph number and note numbers */
        for (int k = 0; k < p->n; k++) {
            wch c = p->t[k];
            if (IS_PNUM(c) && !p->pnlab[0]) {
                int lv = (int)(c - PNUMBASE); if (lv < 1) lv = 1; if (lv > 8) lv = 8;
                pncnt[lv]++;
                for (int q = lv + 1; q < 10; q++) pncnt[q] = 0;
                pnum_label(pncnt, lv, pnstyle, p->pnlab);
            }
            if (IS_NOTE(c)) {
                int ni = (int)(c - NOTEBASE);
                if (ni < d->nnotes) {
                    Note *nt = &d->notes[ni];
                    if (nt->kind == NOTE_END) nt->num = enumn++;
                    else if (nt->kind == NOTE_ANNOT) nt->num = 0;
                    else nt->num = fnum++;
                }
            }
        }
        int s = 0;
        int first = 1;
        do {
            int start = first ? pm : lm;
            int e, w = 0, col = start, hyph = 0;
            int lastbrk = -1, wbrk = 0;
            int wstart = s, wscol = start;
            uint8_t lfx = fx; short lfont = font;
            if (nondoc || !f.ww) {
                col = 1;
                for (e = s; e < p->n; e++) {
                    wch c = p->t[e];
                    if (c == '\t') col = nondoc ? ((col - 1) / 8 + 1) * 8 + 1 : tab_next(&f, col);
                    else if (IS_SPECIAL(c)) col += special_width(d, p, c);
                    else if (!IS_TAG(c) && c != SHY) col++;
                }
                w = col - 1;
                l.col = 0; start = 1;
            } else {
                int maxcol = rm;
                for (e = s; e < p->n; e++) {
                    wch c = p->t[e];
                    if (IS_TAG(c)) continue;
                    if (c == SHY) {
                        if (col <= maxcol && e > s) { lastbrk = e + 1; wbrk = col - start + 1; }
                        continue;
                    }
                    int ncol;
                    if (c == '\t') ncol = tab_next(&f, col);
                    else if (IS_SPECIAL(c)) ncol = col + special_width(d, p, c);
                    else ncol = col + 1;
                    if (c == ' ') {
                        int k = e;
                        while (k < p->n && (p->t[k] == ' ' || IS_TAG(p->t[k]))) k++;
                        lastbrk = k; wbrk = col - start;
                        col = ncol;
                        wstart = k; wscol = col;
                        continue;
                    }
                    if (e == wstart) wscol = col;
                    if (c == '-' && ncol - 1 <= maxcol && e + 1 < p->n && p->t[e + 1] != ' ' && e > s) {
                        lastbrk = e + 1; wbrk = ncol - start;
                    }
                    if (ncol - 1 > maxcol) {
                        /* automatic hyphenation of the word that doesn't fit */
                        if (f.hy && wstart >= s && wstart <= e && lastbrk <= wstart) {
                            int we = e;
                            while (we < p->n && (hy_letter(p->t[we]) || IS_TAG(p->t[we]))) we++;
                            wchar_t word[64]; int map[64], wl = 0, ok = 1;
                            for (int q = wstart; q < we && ok; q++) {
                                if (IS_TAG(p->t[q])) continue;
                                if (!hy_letter(p->t[q]) || wl >= 63) { ok = 0; break; }
                                map[wl] = q; word[wl++] = (wchar_t)p->t[q];
                            }
                            word[wl] = 0;
                            char pts[80];
                            if (ok && wl >= 5 && hyph_points(word, wl, f.lang, pts)) {
                                for (int i = wl - 3; i >= 1; i--) {
                                    if (!(pts[i] & 1)) continue;
                                    int hcol = wscol + i + 1;     /* column of the hyphen */
                                    if (hcol <= maxcol) {
                                        e = map[i] + 1; w = hcol - start + 1; hyph = 1;
                                        goto brk;
                                    }
                                }
                            }
                        }
                        if (lastbrk > s) { e = lastbrk; w = wbrk; goto brk; }
                        if (e == s) e = s + 1;
                        w = col - start;
                        goto brk;
                    }
                    col = ncol;
                }
                w = col - start;
                {
                    int k = e - 1, cc = 0;
                    while (k >= s && (p->t[k] == ' ' || IS_TAG(p->t[k]))) { if (p->t[k] == ' ') cc++; k--; }
                    w -= cc;
                }
            brk:;
            }
            if (e <= s && p->n > s) e = s + 1;
            if (!nondoc) {
                int need = 0;
                for (int k = s; k < e; k++)
                    if (IS_NOTE(p->t[k])) {
                        int ni = (int)(p->t[k] - NOTEBASE);
                        if (ni < d->nnotes && d->notes[ni].kind != NOTE_END) need += note_wrap_lines(d, ni, (frm - flm + 1));
                    }
                int sep = (need && !fnl) ? 1 : 0;
                if (pending) { NEW_PAGE(pi, s, 1); sep = need ? 1 : 0; }
                else if (pendcol) {
                    if (colno + 1 < f.cols) NEW_COL(pi, s); else { NEW_PAGE(pi, s, 1); sep = need ? 1 : 0; }
                }
                else if (pline > usable || (pline > 1 && (pline - 1) + fnl + 1 + need + sep > usable)) {
                    if (f.cols > 1 && colno + 1 < f.cols) NEW_COL(pi, s);
                    else { NEW_PAGE(pi, s, 0); sep = need ? 1 : 0; }
                }
                fnl += need + sep;
            }
            l.s = s; l.e = e; l.type = LT_TEXT; l.flags = hyph ? LF_HYPH : 0;
            l.fx0 = lfx; l.font0 = lfont; l.page = page; l.pline = pline;
            if (cury2 < (pline - 1) * 2) cury2 = (pline - 1) * 2;
            l.y2 = (short)cury2;
            l.width = (short)w; l.rm = (short)rm; l.align = align;
            l.colno = (uint8_t)colno; l.xoff = (short)(colno * xoffunit);
            if (e >= p->n) l.flags |= LF_LAST;
            if (nondoc || !f.ww) l.col = 0;
            else {
                int avail = rm - start + 1;
                int c0 = start - 1;
                if (align == 'C') c0 = start - 1 + (avail - w) / 2;
                else if (align == 'R') c0 = rm - w;
                if (c0 < 0) c0 = 0;
                l.col = (short)c0;
                if (align == 'J' && !(l.flags & LF_LAST)) l.flags |= LF_JUST;
            }
            for (int k = s; k < e; k++) if (IS_TAG(p->t[k])) { if (IS_FONTTAG(p->t[k])) font = (short)(p->t[k] - TG_FONT0); else fx = fx_toggle(fx, p->t[k]); }
            addline(d, &l);
            pline++;
            cury2 += nondoc ? 2 : (f.ls >= 2 ? f.ls : 2);
            /* blank screen rows keep the next line in its row: one after every line when
               double spaced, one after every second line at 1.5 */
            while (!nondoc && pline < cury2 / 2 + 1 && pline <= usable) {
                Line sp; memset(&sp, 0, sizeof(sp));
                sp.type = LT_SPACER; sp.p = pi; sp.s = sp.e = e; sp.page = page; sp.pline = pline; sp.colno = (uint8_t)colno;
                sp.y2 = (short)((pline - 1) * 2);
                addline(d, &sp); pline++;
            }
            s = e;
            first = 0;
        } while (s < p->n);
    }
    FINISH_VC();
#undef FINISH_VC
#undef NEW_PAGE
#undef NEW_COL
    if (hide && d->nl == 0) { hide = 0; goto relayout; }
    d->npages = page;
    d->layver = d->ver;
}

/* index of the text line containing p (prefers line where p.o < e, else last line of para) */
int doc_line_of(Doc *d, Pos p)
{
    /* first line whose paragraph is >= p.p */
    int lo = 0, hi = d->nl;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (d->L[mid].p < p.p) lo = mid + 1; else hi = mid;
    }
    if (lo >= d->nl) lo = d->nl - 1;
    if (lo < 0) return 0;
    if (d->L[lo].p != p.p) {
        /* paragraph not shown (hidden dot command): nearest text line */
        while (lo < d->nl - 1 && d->L[lo].type != LT_TEXT) lo++;
        while (lo > 0 && d->L[lo].type != LT_TEXT) lo--;
        return lo;
    }
    int last = -1;
    for (int i = lo; i < d->nl && d->L[i].p == p.p; i++) {
        Line *l = &d->L[i];
        if (l->type != LT_TEXT) continue;
        last = i;
        if (p.o < l->e || (l->flags & LF_LAST)) return i;
    }
    return last >= 0 ? last : lo;
}

/* columns for each offset in the line (cols[k] for offset l->s + k, k = 0..len) */
int line_cols(Doc *d, Line *l, short **colsout)
{
    static short *cols; static int ccap;
    int len = l->e - l->s;
    if (len + 2 > ccap) { ccap = len + 64; cols = xrealloc(cols, ccap * sizeof(short)); }
    Para *p = &d->p[l->p];
    int col = l->col;
    int tags = show_tags_now() || (l->flags & LF_DOT);
    /* justification */
    int extra = 0, nsp = 0, spi = 0;
    if (l->flags & LF_JUST) {
        int c0 = l->col + 1;  /* 1-based start */
        extra = l->rm - (c0 + l->width - 1);
        int k = l->e - 1;
        while (k >= l->s && (p->t[k] == ' ' || IS_TAG(p->t[k]))) k--;
        int lead = l->s;
        while (lead <= k && p->t[lead] == ' ') lead++;
        for (int j = lead; j <= k; j++) if (p->t[j] == ' ') nsp++;
        if (extra < 0 || nsp == 0) extra = 0;
    }
    Fmt *fb = &d->base;
    for (int k = 0; k < len; k++) {
        cols[k] = (short)col;
        wch c = p->t[l->s + k];
        if (IS_TAG(c)) {
            if (tags) { wchar_t b[64]; col += tag_label(c, b, 64); }
        } else if (IS_SPECIAL(c)) {
            col += special_width(d, p, c);
        } else if (c == SHY) {
            if ((l->s + k == l->e - 1 && !(l->flags & LF_LAST)) || tags) col++;
        } else if (c == '\t') {
            int c1 = col + 1;
            col = (d->nondoc ? ((c1 - 1) / 8 + 1) * 8 + 1 : tab_next(fb, c1)) - 1;
        } else {
            col++;
            if (c == ' ' && extra && nsp) {
                /* only spaces between words get extra */
                int j = l->s + k + 1;
                int has_after = 0;
                for (; j < l->e; j++) if (p->t[j] != ' ' && !IS_TAG(p->t[j])) { has_after = 1; break; }
                int lead = 1;
                for (j = l->s; j < l->s + k; j++) if (p->t[j] != ' ' && !IS_TAG(p->t[j])) { lead = 0; break; }
                if (has_after && !lead) {
                    int add = extra / nsp + (spi < extra % nsp ? 1 : 0);
                    col += add; spi++;
                }
            }
        }
    }
    cols[len] = (short)col;
    *colsout = cols;
    return len;
}

int line_col_of(Doc *d, Line *l, int off)
{
    short *cols;
    int len = line_cols(d, l, &cols);
    int k = off - l->s;
    if (k < 0) k = 0;
    if (k > len) k = len;
    return cols[k];
}

int line_off_at_col(Doc *d, Line *l, int col)
{
    short *cols;
    int len = line_cols(d, l, &cols);
    Para *p = &d->p[l->p];
    int lastok = l->s;
    for (int k = 0; k < len; k++) {
        wch c = p->t[l->s + k];
        if (IS_TAG(c) && !S.show_tags && !(l->flags & LF_DOT)) continue;
        if (cols[k + 1] > col) return l->s + k;
        lastok = l->s + k + 1;
    }
    /* past the end: last position on the line (before soft break spaces) */
    int e = l->e;
    if (!(l->flags & LF_LAST)) {
        /* stay on this line: position before the break point */
        int k = e;
        if (k > l->s) k--;
        return k < lastok ? k : lastok;
    }
    return e;
}

/* ------------------------------------------------------------------ undo */
typedef struct UndoRec { wch *data; int n; uint8_t *styles; int np; Pos cur; int dirty; } UndoRec;
#define MAXUNDO 200

void undo_clear(Doc *d)
{
    for (int i = 0; i < d->nundo; i++) { free(d->undo[i].data); free(d->undo[i].styles); }
    d->nundo = 0;
}

void undo_push(Doc *d, Pos cur, int kind)
{
    if (kind && kind == d->lastcmd && d->groupn < 40) { d->groupn++; return; }
    d->lastcmd = kind; d->groupn = 0;
    if (d->nundo >= MAXUNDO) {
        free(d->undo[0].data); free(d->undo[0].styles);
        memmove(&d->undo[0], &d->undo[1], (d->nundo - 1) * sizeof(UndoRec));
        d->nundo--;
    }
    if (d->nundo >= d->undocap) {
        d->undocap = d->undocap ? d->undocap * 2 : 16;
        d->undo = xrealloc(d->undo, d->undocap * sizeof(UndoRec));
    }
    /* cap total memory: drop oldest if document huge */
    UndoRec *u = &d->undo[d->nundo++];
    Pos a = { 0, 0 }, b = { d->np - 1, d->p[d->np - 1].n };
    u->data = doc_extract(d, a, b, &u->n);
    u->np = d->np;
    u->styles = xmalloc(d->np);
    for (int i = 0; i < d->np; i++) u->styles[i] = d->p[i].style;
    u->cur = cur;
    u->dirty = d->dirty;
}

int undo_pop(Doc *d, Pos *cur)
{
    if (!d->nundo) return 0;
    UndoRec *u = &d->undo[--d->nundo];
    for (int i = 0; i < d->np; i++) free(d->p[i].t);
    d->np = 0;
    int s = 0, pi = 0;
    for (int i = 0; i <= u->n; i++) {
        if (i == u->n || u->data[i] == '\n') {
            doc_append_para(d, u->data + s, i - s);
            d->p[d->np - 1].style = pi < u->np ? u->styles[pi] : 0;
            pi++;
            s = i + 1;
        }
    }
    if (d->np == 0) doc_append_para(d, NULL, 0);
    for (int i = 0; i < d->nregs; i++) {
        Pos *r = d->regs[i];
        if (r->p >= d->np) { r->p = d->np - 1; r->o = d->p[r->p].n; }
        if (r->o > d->p[r->p].n) r->o = d->p[r->p].n;
    }
    *cur = u->cur;
    if (cur->p >= d->np) cur->p = d->np - 1;
    if (cur->o > d->p[cur->p].n) cur->o = d->p[cur->p].n;
    d->dirty = 1;
    free(u->data); free(u->styles);
    d->lastcmd = 0;
    doc_touch(d);
    return 1;
}
