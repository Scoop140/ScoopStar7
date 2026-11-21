/*
 * ScoopStar 7.0 - edit.c
 * The WordStar editing screen: drawing, cursor movement, editing and block commands.
 */
#include "scoopstar.h"
#include "cmds.h"

EdWin g_win[2];
int g_nwin, g_cw;
int g_insert = 1;
int g_quit;

static wchar_t g_msg[256];
static int g_winsplit;          /* rows given to window 0 when two windows */
static int g_drag, g_dragged;
static Pos g_anchor;
static Pos g_prevbb, g_prevbk; static int g_prevset;
static int sb_btncol[9];
static int g_colblock, g_colrepl;   /* ^KN column block mode, ^KI column replace mode */
static int g_stylerow = -1, g_seprow = -1, g_rulerrow[2] = { -1, -1 }, g_statusrow = -1;

typedef struct {
    wchar_t find[128], repl[128];
    int whole, icase, back, global, noask, isrepl, valid;
} FindOpts;
static FindOpts F = { L"", L"", 0, 1, 0, 0, 0, 0, 0 };

#define WIN (&g_win[g_cw])
#define DOC (g_win[g_cw].doc)

static void flash(const wchar_t *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vswprintf(g_msg, 256, fmt, ap);
    va_end(ap);
}

static int textw(void) { return SC - (S.show_flags ? 1 : 0) - (S.show_scroll ? 1 : 0); }
static int tags_shown(void) { return S.show_tags; }

static int block_ok(Doc *d) { return d->bbset && d->bkset && pos_cmp(d->bb, d->bk) < 0; }
static int block_vis(Doc *d) { return block_ok(d) && !d->bhide; }
static int g_hl_on, g_hl_p, g_hl_s, g_hl_e;     /* temporary highlight (spelling) */
static int in_block(Doc *d, int p, int o)
{
    if (g_hl_on && p == g_hl_p && o >= g_hl_s && o < g_hl_e) return 1;
    if (!block_vis(d)) return 0;
    Pos x = { p, o };
    return pos_cmp(x, d->bb) >= 0 && pos_cmp(x, d->bk) < 0;
}

/* ------------------------------------------------------------------ geometry */
#define EDITMENU_ROWS 7
static const wchar_t *g_editmenu[EDITMENU_ROWS] = {
    L"                         < < <   E D I T   M E N U   > > >",
    L"   CURSOR             SCROLL        DELETE          OTHER          MENUS",
    L"^E up    ^X down    ^W up         ^G char         ^V insert      ^O onscreen",
    L"^S left  ^D right   ^Z down       ^T word         ^B align       ^K block/save",
    L"^A word left        ^R up screen  ^Y line         ^N split       ^P print ctrl",
    L"^F word right       ^C down scrn  Del char        ^U undo        ^Q quick  ^M ins",
    L"                                                  ^L find next   ^J help",
};
static int menu_top(void) { return S.helplevel >= 4 ? 2 : S.helplevel == 3 ? 1 + EDITMENU_ROWS : 1; }

static void layout_windows(void)
{
    int row = menu_top();
    g_stylerow = -1;
    if (S.show_stylebar && S.helplevel > 0) g_stylerow = row++;
    g_statusrow = S.show_status ? SR - 1 : -1;
    int bottom = S.show_status ? SR - 1 : SR;
    int avail = bottom - row;
    if (g_nwin == 1) {
        g_seprow = -1;
        g_rulerrow[0] = S.show_ruler ? row : -1;
        g_win[0].r0 = row + (S.show_ruler ? 1 : 0);
        g_win[0].rows = bottom - g_win[0].r0;
    } else {
        if (g_winsplit < 3 || g_winsplit > avail - 3) g_winsplit = avail / 2;
        g_rulerrow[0] = S.show_ruler ? row : -1;
        g_win[0].r0 = row + (S.show_ruler ? 1 : 0);
        g_win[0].rows = row + g_winsplit - g_win[0].r0;
        g_seprow = row + g_winsplit;
        g_rulerrow[1] = S.show_ruler ? g_seprow + 1 : -1;
        g_win[1].r0 = g_seprow + 1 + (S.show_ruler ? 1 : 0);
        g_win[1].rows = bottom - g_win[1].r0;
    }
    for (int i = 0; i < g_nwin; i++) if (g_win[i].rows < 1) g_win[i].rows = 1;
}

static Line *cur_line(EdWin *w)
{
    Doc *d = w->doc;
    doc_layout(d, 0);
    return &d->L[doc_line_of(d, w->cur)];
}

static int cur_col(EdWin *w)
{
    Doc *d = w->doc;
    Line *l = cur_line(w);
    return line_col_of(d, l, w->cur.o);
}

static int hidden_para(Doc *d, int pi)
{
    if (pi < 0 || pi >= d->np) return 0;
    doc_layout(d, 0);
    return d->hidedots && para_is_dot(&d->p[pi]);
}

static void clamp_cur(EdWin *w)
{
    Doc *d = w->doc;
    if (w->cur.p >= d->np) w->cur.p = d->np - 1;
    if (w->cur.p < 0) w->cur.p = 0;
    if (w->cur.o > d->p[w->cur.p].n) w->cur.o = d->p[w->cur.p].n;
    if (w->cur.o < 0) w->cur.o = 0;
    if (hidden_para(d, w->cur.p)) {
        /* the cursor may not rest on a hidden dot command line */
        int k = w->cur.p;
        while (k < d->np && hidden_para(d, k)) k++;
        if (k < d->np) { w->cur.p = k; w->cur.o = 0; return; }
        k = w->cur.p;
        while (k >= 0 && hidden_para(d, k)) k--;
        if (k >= 0) { w->cur.p = k; w->cur.o = d->p[k].n; }
    }
}

static void ensure_visible(EdWin *w)
{
    Doc *d = w->doc;
    doc_layout(d, 0);
    clamp_cur(w);
    int li = doc_line_of(d, w->cur);
    if (w->top > d->nl - 1) w->top = d->nl - 1;
    if (w->top < 0) w->top = 0;
    if (li < w->top) w->top = li;
    if (li >= w->top + w->rows) w->top = li - w->rows + 1;
    int col = line_col_of(d, &d->L[li], w->cur.o);
    int tw = textw();
    if (col - w->hscroll >= tw) w->hscroll = col - tw + 20;
    if (col < w->hscroll) w->hscroll = col - 20;
    if (w->hscroll < 0) w->hscroll = 0;
}

static void set_wantcol(EdWin *w) { w->wantcol = cur_col(w); }

/* ------------------------------------------------------------------ drawing */
static void draw_title(void)
{
    scr_fill(0, 0, SC, ' ', COL[C_TITLE]);
    wchar_t t[MAX_PATH + 40];
    Doc *d = DOC;
    if (d->noteparent) d = d->noteparent;
    const wchar_t *name = d->path[0] ? d->path : L"UNTITLED";
    int max = SC - 20 - (d->readonly ? 13 : 0);
    wchar_t nm[MAX_PATH];
    if ((int)wcslen(name) > max) swprintf(nm, MAX_PATH, L"...%ls", name + wcslen(name) - max + 3);
    else wcscpy(nm, name);
    swprintf(t, MAX_PATH + 40, L"ScoopStar - %ls%ls%ls", nm, d->nondoc ? L" (nondocument)" : L"", d->readonly ? L"  [PROTECTED]" : L"");
    int n = (int)wcslen(t);
    scr_puts(0, (SC - n) / 2 > 0 ? (SC - n) / 2 : 0, t, COL[C_TITLE]);
    scr_puts(0, 1, L"\x25A0", COL[C_TITLE]);
}

static void fx_at_cursor(EdWin *w, uint8_t *fxo, int *fonto)
{
    Doc *d = w->doc;
    Line *l = cur_line(w);
    uint8_t fx = l->fx0; int font = l->font0;
    Para *p = &d->p[l->p];
    for (int k = l->s; k < w->cur.o && k < p->n; k++) {
        wch c = p->t[k];
        if (IS_FONTTAG(c)) font = (int)(c - TG_FONT0);
        else if (c == TG_BOLD) fx ^= FX_BOLD;
        else if (c == TG_ITALIC) fx ^= FX_ITALIC;
        else if (c == TG_UNDER) fx ^= FX_UNDER;
        else if (c == TG_STRIKE) fx ^= FX_STRIKE;
        else if (c == TG_SUPER) fx ^= FX_SUPER;
        else if (c == TG_SUB) fx ^= FX_SUB;
        else if (c == TG_DOUBLE) fx ^= FX_DOUBLE;
    }
    *fxo = fx; *fonto = font;
}

static void draw_stylebar(void)
{
    if (g_stylerow < 0) return;
    int r = g_stylerow;
    EdWin *w = WIN; Doc *d = w->doc;
    uint8_t bar = COL[C_STYLEBAR], sep = COL[C_STYLESEP], on = COL[C_STYLEON];
    scr_fill(r, 0, SC, ' ', bar);
    int bcol = SC - 37;
    int fcol = bcol / 2 + 1;
    scr_put(r, 0, 0x2592, sep);
    const wchar_t *sname = d->nondoc ? L"Nondocument" : g_pstyles[d->p[w->cur.p].style].name;
    scr_putsn(r, 1, sname, fcol - 2, bar);
    scr_put(r, fcol - 1, 0x2592, sep);
    uint8_t fx; int font;
    fx_at_cursor(w, &fx, &font);
    wchar_t fn[64];
    if (font >= 0 && font < g_nfonts) swprintf(fn, 64, L"%ls %g", g_fonts[font].name, g_fonts[font].halfpts / 2.0);
    else swprintf(fn, 64, L"%ls %d", DEFAULT_FONT, DEFAULT_HALFPTS / 2);
    scr_putsn(r, fcol, fn, bcol - fcol, bar);
    Line *l = cur_line(w);
    Fmt f = doc_fmt_at(d, w->cur.p);
    char al = l->align ? l->align : 'L';
    wchar_t ls[12];        /* three cells wide: 1.5 shows as a half sign */
    if (f.ls & 1) swprintf(ls, 12, L"%c%d%c", 0x2195, f.ls / 2, 0xBD); else swprintf(ls, 12, L" %c%d", 0x2195, f.ls / 2);
    struct { const wchar_t *t; int on; } b[9] = {
        { L" B ", fx & FX_BOLD }, { L" I ", fx & FX_ITALIC }, { L" U ", fx & FX_UNDER }, { L"<*>", S.show_tags },
        { L" L ", al == 'L' }, { L" C ", al == 'C' }, { L" R ", al == 'R' }, { L" J ", al == 'J' }, { ls, 0 } };
    int c = bcol;
    for (int i = 0; i < 9; i++) {
        scr_put(r, c, 0x2592, sep);
        sb_btncol[i] = c + 1;
        scr_puts(r, c + 1, b[i].t, b[i].on ? on : bar);
        c += 4;
    }
    scr_put(r, c, 0x2592, sep);
}

static void draw_ruler(EdWin *w, int row)
{
    if (row < 0) return;
    Doc *d = w->doc;
    int tw = textw();
    uint8_t at = COL[C_RULER], ax = COL[C_RULERX];
    scr_fill(row, 0, SC, ' ', ax);
    if (d->nondoc) {
        for (int i = 0; i < tw; i++) {
            int col = i + 1 + w->hscroll;
            scr_put(row, i, (col % 8 == 1 && col > 1) ? 0x25BA : (col % 10 == 0 ? '|' : 0x2500), at);
        }
        return;
    }
    Fmt f = doc_fmt_at(d, w->cur.p);
    ParaStyle *ps = &g_pstyles[d->p[w->cur.p].style];
    int lm = f.lm + ps->lmadd, rm = f.rm - ps->rmsub;
    int pm = (ps->pmadd || ps->lmadd) ? f.pm + ps->lmadd + ps->pmadd : f.pm;
    for (int i = 0; i < tw; i++) {
        int col = i + 1 + w->hscroll;
        wch ch = ' ';
        uint8_t a = ax;
        if (col >= lm && col <= rm) { ch = 0x2500; a = at; }
        int istab = 0;
        for (int t = 0; t < f.ntabs; t++) if (f.tabs[t] == col) istab = 1;
        if (istab && col > lm && col < rm) ch = 0x25BA;
        if (col == lm) ch = 0x251C;
        if (col == rm) ch = 0x2524;
        if (col == pm && pm != lm) ch = 'P';
        if (col == lm || col == rm || col == pm) a = at;
        scr_put(row, i, ch, a);
    }
}

static uint8_t fx_color(uint8_t fx)
{
    if (fx & FX_BOLD) return COL[C_BOLD];
    if (fx & (FX_SUPER | FX_SUB)) return COL[C_SUPSUB];
    if (fx & FX_ITALIC) return COL[C_ITALIC];
    if (fx & FX_DOUBLE) return COL[C_BOLD];
    if (!S.realfx && (fx & (FX_UNDER | FX_STRIKE))) return COL[C_TAG];
    return COL[C_TEXT];
}

static int rect_geom(Doc *d, int *l0, int *l1, int *c0, int *c1);
static void draw_window(int wi)
{
    EdWin *w = &g_win[wi];
    Doc *d = w->doc;
    doc_layout(d, 0);
    int tw = textw();
    int flagc = S.show_flags ? tw : -1;
    int scrc = S.show_scroll ? SC - 1 : -1;
    int rl0 = -1, rl1 = -2, rc0 = 0, rc1 = 0;
    if (g_colblock && block_vis(d)) rect_geom(d, &rl0, &rl1, &rc0, &rc1);
    for (int i = 0; i < w->rows; i++) {
        int r = w->r0 + i;
        int li = w->top + i;
        scr_fill(r, 0, tw, ' ', COL[C_TEXT]);
        if (flagc >= 0) scr_put(r, flagc, ' ', COL[C_FLAG]);
        if (li >= d->nl) { if (flagc >= 0) scr_put(r, flagc, '^', COL[C_FLAG]); continue; }
        Line *l = &d->L[li];
        if (l->type == LT_PAGEBRK) {
            scr_fill(r, 0, tw, '-', COL[C_PAGEBRK]);
            if (flagc >= 0) scr_put(r, flagc, 'P', COL[C_FLAG]);
            continue;
        }
        if (l->type == LT_SPACER) { if (flagc >= 0) scr_put(r, flagc, ':', COL[C_FLAG]); continue; }
        if (l->type == LT_COLBRK) {
            scr_fill(r, 0, tw, 0x2508, COL[C_PAGEBRK]);
            const wchar_t *t = L" column break ";
            for (int j = 0; t[j]; j++) if (2 + j < tw) scr_put(r, 2 + j, t[j], COL[C_PAGEBRK]);
            if (flagc >= 0) scr_put(r, flagc, 'C', COL[C_FLAG]);
            continue;
        }
        if (l->type == LT_GRAPHIC) {
            int x0 = l->col - w->hscroll, x1 = x0 + (l->width > 2 ? l->width : 2) - 1;
            int top = l->s == 0, bot = l->s == l->e - 1;
            for (int x = x0; x <= x1; x++) {
                if (x < 0 || x >= tw) continue;
                wch ch = ' ';
                if (x == x0) ch = top ? 0x250C : bot ? 0x2514 : 0x2502;
                else if (x == x1) ch = top ? 0x2510 : bot ? 0x2518 : 0x2502;
                else if (top || bot) ch = 0x2500;
                scr_put(r, x, ch, COL[C_DOT]);
            }
            if (l->s == (l->e - 1) / 2) {
                double gw, gh; wchar_t gp[MAX_PATH];
                Para *gpp = &d->p[l->p];
                if (graphic_parse(gpp->t, gpp->n, &gw, &gh, gp, MAX_PATH)) {
                    const wchar_t *nm = wcsrchr(gp, '\\'); nm = nm ? nm + 1 : gp;
                    wchar_t lab[80]; swprintf(lab, 80, L"Graphic: %ls", nm);
                    int room = x1 - x0 - 3, n = (int)wcslen(lab); if (n > room) n = room;
                    for (int j = 0; j < n; j++) if (x0 + 2 + j >= 0 && x0 + 2 + j < tw) scr_put(r, x0 + 2 + j, lab[j], COL[C_DOT]);
                }
            }
            if (flagc >= 0) scr_put(r, flagc, 'G', COL[C_FLAG]);
            continue;
        }
        Para *p = &d->p[l->p];
        short *cols;
        int len = line_cols(d, l, &cols);
        ParaStyle *ps = &g_pstyles[p->style < g_npstyles ? p->style : 0];
        uint8_t fx = l->fx0;
        int isdot = (l->flags & LF_DOT) != 0;
        for (int k = 0; k < len; k++) {
            wch c = p->t[l->s + k];
            int col = cols[k] - w->hscroll;
            int inb = g_colblock ? (li >= rl0 && li <= rl1 && cols[k] >= rc0 && cols[k] < rc1) : in_block(d, l->p, l->s + k);
            if (IS_TAG(c)) {
                if (tags_shown() || isdot) {
                    wchar_t lab[64]; int n = tag_label(c, lab, 64);
                    for (int j = 0; j < n; j++) if (col + j >= 0 && col + j < tw) scr_put(r, col + j, lab[j], inb ? COL[C_BLOCK] : COL[C_TAG]);
                }
                if (c == TG_BOLD) fx ^= FX_BOLD; else if (c == TG_ITALIC) fx ^= FX_ITALIC;
                else if (c == TG_UNDER) fx ^= FX_UNDER; else if (c == TG_STRIKE) fx ^= FX_STRIKE;
                else if (c == TG_SUPER) fx ^= FX_SUPER; else if (c == TG_SUB) fx ^= FX_SUB;
                else if (c == TG_DOUBLE) fx ^= FX_DOUBLE;
                continue;
            }
            uint8_t efx = fx | (ps->bold ? FX_BOLD : 0) | (ps->italic ? FX_ITALIC : 0);
            uint8_t at = isdot ? COL[C_DOT] : fx_color(efx);
            if (inb) at = COL[C_BLOCK];
            if (d->nondoc) efx = 0;
            if (IS_PNUM(c)) {
                const wchar_t *lb = p->pnlab[0] ? p->pnlab : L"#";
                for (int j = 0; lb[j]; j++) if (col + j >= 0 && col + j < tw) scr_put(r, col + j, lb[j], inb ? COL[C_BLOCK] : COL[C_TAG]);
                continue;
            }
            if (c == SHY) {
                if (cols[k + 1] > cols[k] && col >= 0 && col < tw) scr_put(r, col, '-', inb ? COL[C_BLOCK] : (tags_shown() ? COL[C_TAG] : at));
                continue;
            }
            if (IS_NOTE(c)) {
                wchar_t nb[24]; int nl = note_label(d, c, nb, 24);
                for (int j = 0; j < nl; j++) if (col + j >= 0 && col + j < tw) scr_put(r, col + j, nb[j], inb ? COL[C_BLOCK] : COL[C_SUPSUB]);
                continue;
            }
            if (c == '\t') {
                for (int x = col; x < cols[k + 1] - w->hscroll; x++) if (x >= 0 && x < tw) scr_putfx(r, x, ' ', at, efx & (FX_UNDER | FX_STRIKE));
                continue;
            }
            wch shown = c == 0xA0 ? (tags_shown() ? 0xB7 : ' ') : c;
            if (col >= 0 && col < tw) scr_putfx(r, col, shown, at, isdot ? 0 : (efx & ~FX_DOUBLE));
            /* justification gap */
            for (int x = col + 1; x < cols[k + 1] - w->hscroll; x++)
                if (x >= 0 && x < tw) scr_putfx(r, x, ' ', inb ? COL[C_BLOCK] : COL[C_TEXT], (efx & FX_UNDER) && c == ' ' ? efx & FX_UNDER : 0);
        }
        if (l->flags & LF_HYPH) {
            int col = cols[len] - w->hscroll;
            if (col >= 0 && col < tw) scr_put(r, col, '-', COL[C_TEXT]);
        }
        /* a block that runs past the end of a paragraph: show the hard return as highlighted */
        if (!g_colblock && (l->flags & LF_LAST) && in_block(d, l->p, p->n)) {
            int col = cols[len] - w->hscroll;
            if (col >= 0 && col < tw) scr_put(r, col, ' ', COL[C_BLOCK]);
        }
        if (flagc >= 0) {
            wch fl = ' ';
            if (isdot) fl = (p->n >= 2 && p->t[1] == '.') ? '.' : ':';
            else if (l->flags & LF_LAST) fl = '<';
            if (cols[len] - w->hscroll > tw) fl = '+';
            scr_put(r, flagc, fl, COL[C_FLAG]);
        }
    }
    /* 1.5-type spacing: a line that starts half a row down is drawn lower, into the blank row below it */
    for (int i = 0; i < w->rows; i++) {
        int li = w->top + i;
        if (li >= d->nl) break;
        Line *l = &d->L[li];
#define HALFLINE(k) ((k) >= 0 && (k) + 1 < d->nl && d->L[k].type == LT_TEXT && !(d->L[k].flags & LF_DOT) && \
                     (d->L[k].y2 & 1) && d->L[(k) + 1].type == LT_SPACER)
        int span = tw + (flagc >= 0 ? 1 : 0);      /* the flag column moves with its line */
        if (HALFLINE(li)) scr_setlay(w->r0 + i, 0, span, LAY_HALF);
        else if (l->type == LT_SPACER && HALFLINE(li - 1)) {
            if (flagc >= 0) scr_put(w->r0 + i, flagc, ' ', COL[C_FLAG]);
            scr_setlay(w->r0 + i, 0, span, LAY_GAP);
        }
#undef HALFLINE
    }
    /* scroll bar */
    if (scrc >= 0) {
        int top = w->r0, h = w->rows;
        for (int i = 0; i < h; i++) scr_put(top + i, scrc, 0x2592, COL[C_SCROLL]);
        if (h >= 3) {
            scr_put(top, scrc, 0x25B2, COL[C_THUMB]);
            scr_put(top + h - 1, scrc, 0x25BC, COL[C_THUMB]);
            int li = doc_line_of(d, w->cur);
            int span = h - 2;
            int pos = d->nl > 1 ? (int)((long long)li * (span - 1) / (d->nl - 1)) : 0;
            scr_put(top + 1 + pos, scrc, 0x2588, COL[C_THUMB]);
        }
    }
}

static void draw_separator(void)
{
    if (g_seprow < 0) return;
    EdWin *w = &g_win[1];
    scr_fill(g_seprow, 0, SC, 0x2550, COL[C_TITLE]);
    wchar_t t[MAX_PATH + 8];
    if (w->doc->noteparent) {
        Doc *pd = w->doc->noteparent;
        Note *nt = w->doc->noteidx < pd->nnotes ? &pd->notes[w->doc->noteidx] : NULL;
        int num = nt ? nt->num : 0;
        if (nt && nt->kind == NOTE_ANNOT) swprintf(t, MAX_PATH + 8, L" Annotation %ls  -  F10 or ^KD returns to the document ", nt->mark[0] ? nt->mark : L"*");
        else swprintf(t, MAX_PATH + 8, L" %ls %d  -  F10 or ^KD returns to the document ", nt && nt->kind == NOTE_END ? L"Endnote" : L"Footnote", num);
    } else
    swprintf(t, MAX_PATH + 8, L" %ls ", w->doc->path[0] ? w->doc->path : L"UNTITLED");
    int n = (int)wcslen(t);
    if (n > SC - 4) n = SC - 4;
    scr_putsn(g_seprow, (SC - n) / 2, t, n, COL[C_TITLE]);
}

static void draw_status(void)
{
    if (g_statusrow < 0) return;
    int r = g_statusrow;
    EdWin *w = WIN; Doc *d = w->doc;
    scr_fill(r, 0, SC, ' ', COL[C_STATUS]);
    
    Line *l = cur_line(w);
    int col = line_col_of(d, l, w->cur.o) + 1;
    wchar_t s[128];
    if (d->nondoc) swprintf(s, 128, L"%-6ls  FC%d  L%d  C%d", g_insert ? L"Insert" : L"", w->cur.o + 1, w->cur.p + 1, col);
    else {
        Fmt f = d->base;
        int pg = l->page + (f.pn > 0 ? f.pn - 1 : 0);
        double v = (f.mt + l->y2 / 2.0) / 6.0, hh = (col - 1) / 10.0;
        swprintf(s, 128, L"%-6ls  P%d  L%d  V%.2f\"  C%d  H%.2f\"", g_insert ? L"Insert" : L"", pg, l->pline, v, col, hh);
    }
    int n = (int)wcslen(s);
    int c0 = SC - n - 4;
    if (c0 < 36) c0 = 36;
    scr_puts(r, c0, s, COL[C_STATUS]);
    if (block_ok(d) && d->bhide) scr_puts(r, c0 - 7, L"<B><K>", COL[C_STATUS]);
    if (g_msg[0]) {
        /* a short message sits left of the position; a long one uses the whole line until the next key */
        int len = (int)wcslen(g_msg);
        if (len <= c0 - 10) { scr_fill(r, 0, c0 - 8, ' ', COL[C_STATUS]); scr_putsn(r, 1, g_msg, c0 - 10, COL[C_STATUSHI]); }
        else { scr_fill(r, 0, SC, ' ', COL[C_STATUS]); scr_putsn(r, 1, g_msg, SC - 2, COL[C_STATUSHI]); }
    }
}

static void draw_all(void)
{
    scr_bigclear();
    layout_windows();
    for (int i = 0; i < g_nwin; i++) ensure_visible(&g_win[i]);
    draw_title();
    if (S.helplevel >= 4) draw_menubar(g_editmenus, g_neditmenus, -1);
    else if (S.helplevel == 3)
        for (int i = 0; i < EDITMENU_ROWS; i++) {
            scr_fill(1 + i, 0, SC, ' ', COL[C_MENUBOX]);
            scr_putsn(1 + i, 1, g_editmenu[i], SC - 2, i < 2 ? COL[C_MENUBOXHOT] : COL[C_MENUBOX]);
        }
    draw_stylebar();
    for (int i = 0; i < g_nwin; i++) {
        draw_ruler(&g_win[i], g_rulerrow[i]);
        draw_window(i);
    }
    draw_separator();
    draw_status();
    EdWin *w = WIN;
    int li = doc_line_of(w->doc, w->cur);
    int col = line_col_of(w->doc, &w->doc->L[li], w->cur.o) - w->hscroll;
    scr_cursor(w->r0 + li - w->top, col, 1, g_insert ? S.cursorblock : !S.cursorblock);
}

/* ------------------------------------------------------------------ cursor movement */
static void mv_left(EdWin *w)
{
    Doc *d = w->doc; Pos *c = &w->cur;
    if (c->o > 0) {
        c->o--;
        if (!tags_shown()) while (c->o > 0 && IS_TAG(d->p[c->p].t[c->o])) c->o--;
    } else if (c->p > 0) {
        int k = c->p - 1;
        while (k > 0 && hidden_para(d, k)) k--;
        if (hidden_para(d, k)) return;
        c->p = k; c->o = d->p[c->p].n;
    }
}
static void mv_right(EdWin *w)
{
    Doc *d = w->doc; Pos *c = &w->cur; Para *p = &d->p[c->p];
    if (c->o < p->n) {
        if (!tags_shown()) while (c->o < p->n && IS_TAG(p->t[c->o])) c->o++;
        if (c->o < p->n) c->o++;
    } else if (c->p < d->np - 1) {
        int k = c->p + 1;
        while (k < d->np - 1 && hidden_para(d, k)) k++;
        if (hidden_para(d, k)) return;
        c->p = k; c->o = 0;
    }
}

static int text_line_step(Doc *d, int li, int dir)
{
    for (int i = li + dir; i >= 0 && i < d->nl; i += dir)
        if (d->L[i].type == LT_TEXT) return i;
    return -1;
}

static void goto_line_col(EdWin *w, int li, int col)
{
    Doc *d = w->doc;
    if (li < 0) li = 0;
    if (li >= d->nl) li = d->nl - 1;
    while (li > 0 && d->L[li].type != LT_TEXT) li--;
    while (li < d->nl - 1 && d->L[li].type != LT_TEXT) li++;
    Line *l = &d->L[li];
    w->cur.p = l->p;
    w->cur.o = line_off_at_col(d, l, col);
}

static void mv_vert(EdWin *w, int n)
{
    Doc *d = w->doc;
    doc_layout(d, 0);
    int li = doc_line_of(d, w->cur);
    int dir = n > 0 ? 1 : -1;
    for (int k = 0; k < abs(n); k++) {
        int nx = text_line_step(d, li, dir);
        if (nx < 0) break;
        li = nx;
    }
    goto_line_col(w, li, w->wantcol);
}

static void mv_wordright(EdWin *w)
{
    Doc *d = w->doc; Pos *c = &w->cur; Para *p = &d->p[c->p];
    if (c->o >= p->n) {
        if (c->p < d->np - 1) { c->p++; c->o = 0; p = &d->p[c->p]; while (c->o < p->n && !wch_isword(p->t[c->o])) c->o++; }
        return;
    }
    while (c->o < p->n && wch_isword(p->t[c->o])) c->o++;
    while (c->o < p->n && !wch_isword(p->t[c->o])) c->o++;
}
static void mv_wordleft(EdWin *w)
{
    Doc *d = w->doc; Pos *c = &w->cur;
    if (c->o == 0) { if (c->p > 0) { c->p--; c->o = d->p[c->p].n; } return; }
    Para *p = &d->p[c->p];
    c->o--;
    while (c->o > 0 && !wch_isword(p->t[c->o])) c->o--;
    while (c->o > 0 && wch_isword(p->t[c->o - 1])) c->o--;
}

static void jump_to(EdWin *w, Pos p)
{
    w->doc->prevpos = w->cur;
    w->cur = p;
    clamp_cur(w);
    set_wantcol(w);
}

static void center_view(EdWin *w)
{
    Doc *d = w->doc; doc_layout(d, 0);
    int li = doc_line_of(d, w->cur);
    if (li < w->top || li >= w->top + w->rows) { w->top = li - w->rows / 3; if (w->top < 0) w->top = 0; }
}

/* ------------------------------------------------------------------ editing helpers */
static void ins_wch(EdWin *w, const wch *s, int n)
{
    doc_insert(w->doc, w->cur, s, n);
}

static void ins_str(EdWin *w, const wchar_t *s)
{
    int n = (int)wcslen(s);
    wch *b = xmalloc((n + 1) * sizeof(wch));
    int k = 0;
    for (int i = 0; i < n; i++) {
        wchar_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) { b[k++] = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
        else if (c == '\r') continue;
        else b[k++] = c;
    }
    undo_push(w->doc, w->cur, 0);
    doc_insert(w->doc, w->cur, b, k);
    free(b);
}

static void type_char(EdWin *w, wch c)
{
    Doc *d = w->doc;
    undo_push(d, w->cur, UK_TYPE);
    if (!g_insert) {
        Para *p = &d->p[w->cur.p];
        int o = w->cur.o;
        if (!tags_shown()) while (o < p->n && IS_TAG(p->t[o])) o++;
        if (o < p->n && !IS_TAG(p->t[o])) { para_del(d, w->cur.p, o, 1); }
    }
    ins_wch(w, &c, 1);
}

/* insert a dot-command line above the current paragraph */
static void insert_dot_line(EdWin *w, const wchar_t *s)
{
    Doc *d = w->doc;
    if (d->noteparent) { flash(L"Dot commands can't be used inside a footnote"); return; }
    undo_push(d, w->cur, 0);
    int n = (int)wcslen(s);
    wch *b = xmalloc((n + 2) * sizeof(wch));
    for (int i = 0; i < n; i++) b[i] = s[i];
    b[n] = '\n';
    Pos at = { w->cur.p, 0 };
    doc_insert(d, at, b, n + 1);
    free(b);
}

/* replace an existing identical dot command directly above, else insert */
static void set_dot(EdWin *w, const wchar_t *cmd, const wchar_t *arg)
{
    Doc *d = w->doc;
    if (d->noteparent) { flash(L"Dot commands can't be used inside a footnote"); return; }
    wchar_t line[300];
    swprintf(line, 300, L".%ls %ls", cmd, arg);
    for (int pi = w->cur.p - 1; pi >= 0 && para_is_dot(&d->p[pi]); pi--) {
        wchar_t c[8], a[256];
        dot_parse(d->p[pi].t, d->p[pi].n, c, 8, a, 256);
        if (!wcscmp(c, cmd)) {
            undo_push(d, w->cur, 0);
            para_del(d, pi, 0, d->p[pi].n);
            int n = (int)wcslen(line);
            wch *b = xmalloc(n * sizeof(wch));
            for (int i = 0; i < n; i++) b[i] = line[i];
            para_ins(d, pi, 0, b, n);
            free(b);
            return;
        }
    }
    insert_dot_line(w, line);
}

static wch *block_text(Doc *d, int *n) { return doc_extract(d, d->bb, d->bk, n); }

static void plain_from(const wch *s, int n, wchar_t **out)
{
    wchar_t *b = xmalloc((n * 2 + 2) * sizeof(wchar_t)); int k = 0;
    for (int i = 0; i < n; i++) {
        wch c = s[i];
        if (IS_TAG(c) || IS_SPECIAL(c) || c == SHY) continue;
        if (c == '\n') { b[k++] = '\r'; b[k++] = '\n'; continue; }
        if (c >= 0x10000) { c -= 0x10000; b[k++] = 0xD800 + (c >> 10); b[k++] = 0xDC00 + (c & 0x3FF); continue; }
        b[k++] = (wchar_t)(c == 0xA0 ? ' ' : c);
    }
    b[k] = 0;
    *out = b;
}

/* ------------------------------------------------------------------ files */
static void note_writeback(Doc *nd);
static int save_doc(EdWin *w, int askname)
{
    Doc *d = w->doc;
    if (d->noteparent) { note_writeback(d); flash(L"Footnote updated (save the document to keep it)"); return 1; }
    wchar_t path[MAX_PATH];
    wcscpy(path, d->path);
    if (d->fmt == FMT_WS && path[0] && !d->nondoc) {
        /* never overwrite the original DOS WordStar file: save beside it as .RTF */
        wchar_t *dot = wcsrchr(path, '.'), *bs = wcsrchr(path, '\\');
        if (dot && (!bs || dot > bs)) *dot = 0;
        wcscat(path, L".rtf");
        if (!askname && file_exists(path)) {
            wchar_t q[MAX_PATH + 64]; swprintf(q, MAX_PATH + 64, L"%ls already exists. Overwrite it?", path);
            if (ask_yn(L"Save", q) != 1) askname = 1;
        }
    }
    if (askname || !path[0]) {
        if (!file_dialog(L"Save As", path, 1, d->nondoc ? L"*.*" : L"*.rtf")) return 0;
        wchar_t ext[16]; path_ext(path, ext, 16);
        if (!ext[0] && !d->nondoc) wcscat(path, L".rtf");
        if (_wcsicmp(path, d->path) && file_exists(path)) {
            wchar_t q[MAX_PATH + 64]; swprintf(q, MAX_PATH + 64, L"%ls already exists. Overwrite it?", path);
            if (ask_yn(L"Save As", q) != 1) return 0;
        }
    }
    flash(L"Saving...");
    draw_status(); screen_update();
    if (!save_file(d, path)) {
        wchar_t m[MAX_PATH + 64]; swprintf(m, MAX_PATH + 64, L"Can't save %ls", path);
        msgbox(L"Error", m, L"OK");
        g_msg[0] = 0;
        return 0;
    }
    wcscpy(d->path, path);
    d->dirty = 0;
    if (d->fmt == FMT_WS) d->fmt = FMT_RTF;
    flash(L"Saved %ls", path);
    return 1;
}

static void close_window(int i)
{
    Doc *d = g_win[i].doc;
    doc_unregister_pos(d, &g_win[i].cur);
    if (--d->refs <= 0) doc_free(d);
    if (i == 0 && g_nwin == 2) g_win[0] = g_win[1];
    if (g_nwin == 2) {
        /* re-register the moved cursor */
        doc_unregister_pos(g_win[0].doc, &g_win[1].cur);
        doc_register_pos(g_win[0].doc, &g_win[0].cur);
    }
    g_nwin--;
    g_cw = 0;
}

static int confirm_close(EdWin *w)
{
    Doc *d = w->doc;
    if (d->noteparent) { note_writeback(d); return 1; }
    if (!d->dirty || d->refs > 1) return 1;
    wchar_t q[MAX_PATH + 80];
    const wchar_t *nm = wcsrchr(d->path, '\\');
    swprintf(q, MAX_PATH + 80, L"Save changes to %ls?", d->path[0] ? (nm ? nm + 1 : d->path) : L"UNTITLED");
    int r = msgbox(L"ScoopStar", q, L"&Yes|&No|&Cancel");
    if (r == 0) return save_doc(w, 0);
    if (r == 1) return 1;
    return 0;
}

static void open_second_path(const wchar_t *path);
static void open_second(void)
{
    if (g_nwin == 2) { g_cw = 1 - g_cw; return; }
    wchar_t path[MAX_PATH] = L"";
    if (!file_dialog(L"Open/Switch", path, 0, S.filespec)) return;
    open_second_path(path);
}
static int is_protected(const wchar_t *path)
{
    DWORD a = path[0] ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY);
}
static void open_second_path(const wchar_t *path)
{
    Doc *nd = NULL;
    if (!_wcsicmp(path, DOC->path)) nd = DOC;
    else {
        nd = doc_new();
        if (file_exists(path)) {
            if (!load_file(nd, path, 0)) { doc_free(nd); msgbox(L"Error", L"Can't open that file.", L"OK"); return; }
        } else wcscpy(nd->path, path);
    }
    nd->readonly = is_protected(nd->path);
    g_win[1].doc = nd; nd->refs++;
    g_win[1].cur.p = 0; g_win[1].cur.o = 0; g_win[1].top = 0; g_win[1].hscroll = 0; g_win[1].wantcol = 0;
    doc_register_pos(nd, &g_win[1].cur);
    g_nwin = 2; g_cw = 1; g_winsplit = 0;
}

/* ------------------------------------------------------------------ find / replace */
static int fold(wch c) { return F.icase ? (int)towlower((wint_t)c) : (int)c; }

static int match_in_para(Doc *d, int pi, int from, int dir, int *ms, int *me)
{
    Para *p = &d->p[pi];
    int m = (int)wcslen(F.find);
    if (!m) return 0;
    int *vis = xmalloc((p->n + 1) * sizeof(int)); int nv = 0;
    for (int i = 0; i < p->n; i++) if (!IS_TAG(p->t[i])) vis[nv++] = i;
    int vfrom = 0; while (vfrom < nv && vis[vfrom] < from) vfrom++;
    int found = -1;
    int start = dir > 0 ? vfrom : (vfrom - 1);
    for (int i = start; dir > 0 ? i <= nv - m : i >= 0; i += dir) {
        if (i + m > nv) continue;
        int ok = 1;
        for (int j = 0; j < m; j++) {
            wch a = p->t[vis[i + j]], b = F.find[j];
            if (b == '?' && 0) continue;
            if (fold(a) != fold(b)) { ok = 0; break; }
        }
        if (ok && F.whole) {
            if (i > 0 && wch_isword(p->t[vis[i - 1]])) ok = 0;
            if (i + m < nv && wch_isword(p->t[vis[i + m]])) ok = 0;
        }
        if (ok) { found = i; break; }
    }
    if (found >= 0) { *ms = vis[found]; *me = vis[found + m - 1] + 1; }
    free(vis);
    return found >= 0;
}

static int do_search(Doc *d, Pos from, int dir, Pos *ms, Pos *me)
{
    for (int pi = from.p; pi >= 0 && pi < d->np; pi += dir) {
        int start = (pi == from.p) ? from.o : (dir > 0 ? 0 : d->p[pi].n + 1);
        int a, b;
        if (match_in_para(d, pi, start, dir, &a, &b)) { ms->p = me->p = pi; ms->o = a; me->o = b; return 1; }
    }
    return 0;
}

static void find_dialog(int isrepl)
{
    int whole = F.whole, icase = F.icase, back = F.back, global = 0, noask = 0;
    wchar_t f[128], r[128];
    wcscpy(f, F.find); wcscpy(r, F.repl);
    /* seed with block text */
    DCtl c[] = {
        { DC_EDIT, 2, 3, 50, 1, L"&Find:", f, 128, NULL, 0, NULL, 0, 0, 0, 18 },
        { DC_EDIT, 4, 3, 50, 1, L"&Replace with:", r, 128, NULL, 0, NULL, 0, 0, 0, 18 },
        { DC_CHECK, 6, 5, 30, 1, L"Find &whole words only", NULL, 0, &whole },
        { DC_CHECK, 7, 5, 30, 1, L"&Ignore case", NULL, 0, &icase },
        { DC_CHECK, 8, 5, 30, 1, L"Search &backward", NULL, 0, &back },
        { DC_CHECK, 9, 5, 30, 1, L"Search entire &document", NULL, 0, &global },
        { DC_CHECK, 10, 5, 30, 1, L"Replace without &asking", NULL, 0, &noask },
        { DC_BUTTON, 6, 56, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 8, 56, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    DCtl cf[9]; int nc = 0;
    for (int i = 0; i < 9; i++) {
        if (!isrepl && (i == 1 || i == 6)) continue;
        cf[nc] = c[i];
        if (!isrepl && cf[nc].r > 4) cf[nc].r -= 2;
        nc++;
    }
    int res = dialog_run(isrepl ? L"Find and Replace" : L"Find", 4, 4, isrepl ? 13 : 11, 72, cf, nc, 0);
    if (res != 1 || !f[0]) return;
    wcscpy(F.find, f); wcscpy(F.repl, r);
    F.whole = whole; F.icase = icase; F.back = back; F.global = global; F.noask = noask;
    F.isrepl = isrepl; F.valid = 1;
}

static int ask_replace(void)
{
    /* status-line prompt */
    draw_all();
    int r = g_statusrow >= 0 ? g_statusrow : SR - 1;
    scr_fill(r, 0, SC, ' ', COL[C_STATUSHI]);
    scr_puts(r, 1, L"Replace (Y/N), A=all, Esc=stop?", COL[C_STATUSHI]);
    for (;;) {
        int k = getkey();
        if (k == K_ESC) return -1;
        if (k < 0x10000) {
            int K = towupper((wint_t)k);
            if (K == 'Y') return 1;
            if (K == 'N') return 0;
            if (K == 'A') return 2;
        }
    }
}

static void run_find(EdWin *w, int fromstart)
{
    Doc *d = w->doc;
    if (!F.valid) return;
    int dir = F.back ? -1 : 1;
    Pos from = w->cur, ms, me;
    if (fromstart) { from.p = dir > 0 ? 0 : d->np - 1; from.o = dir > 0 ? 0 : d->p[from.p].n + 1; }
    if (!F.isrepl) {
        Pos f2 = from;
        if (!fromstart && dir > 0 && d->lastfind.p == from.p && d->lastfind.o == from.o && 0) f2.o++;
        if (do_search(d, f2, dir, &ms, &me)) {
            Pos to = dir > 0 ? me : ms;
            jump_to(w, to);
            d->lastfind = ms;
            center_view(w);
        } else flash(L"Not found: %ls", F.find);
        return;
    }
    int count = 0, all = F.noask;
    undo_push(d, w->cur, 0);
    int rl = (int)wcslen(F.repl);
    wch *rb = xmalloc((rl + 1) * sizeof(wch));
    for (int i = 0; i < rl; i++) rb[i] = F.repl[i];
    Pos at = from;
    while (do_search(d, at, dir, &ms, &me)) {
        int ans = 1;
        if (!all) {
            w->cur = me;
            d->bb = ms; d->bk = me; d->bbset = d->bkset = 1; d->bhide = 0;
            center_view(w);
            ans = ask_replace();
            d->bbset = d->bkset = 0;
            if (ans < 0) break;
            if (ans == 2) all = 1;
        }
        if (ans) {
            doc_delete(d, ms, me);
            Pos e = doc_insert(d, ms, rb, rl);
            count++;
            at = dir > 0 ? e : ms;
        } else at = dir > 0 ? me : ms;
        d->lastfind = ms;
        w->cur = at;
    }
    free(rb);
    clamp_cur(w);
    set_wantcol(w);
    flash(L"%d replacement%ls made", count, count == 1 ? L"" : L"s");
}

/* ------------------------------------------------------------------ block utilities */
static void block_case(EdWin *w, int mode)
{
    Doc *d = w->doc;
    if (!block_ok(d)) { flash(L"No block marked"); return; }
    undo_push(d, w->cur, 0);
    int start = 1;
    for (int pi = d->bb.p; pi <= d->bk.p; pi++) {
        Para *p = &d->p[pi];
        int s = pi == d->bb.p ? d->bb.o : 0, e = pi == d->bk.p ? d->bk.o : p->n;
        for (int i = s; i < e; i++) {
            wch c = p->t[i];
            if (IS_TAG(c) || c > 0xFFFF) continue;
            if (mode == 0) p->t[i] = towupper((wint_t)c);
            else if (mode == 1) p->t[i] = towlower((wint_t)c);
            else {
                if (iswalpha((wint_t)c)) { p->t[i] = start ? towupper((wint_t)c) : towlower((wint_t)c); start = 0; }
                else if (c == '.' || c == '!' || c == '?') start = 1;
            }
        }
        start = 1;
    }
    d->dirty = 1; doc_touch(d);
}

static void word_count(EdWin *w)
{
    Doc *d = w->doc;
    Pos a = { 0, 0 }, b = { d->np - 1, d->p[d->np - 1].n };
    int isblk = block_ok(d);
    if (isblk) { a = d->bb; b = d->bk; }
    long words = 0, chars = 0, paras = 0;
    for (int pi = a.p; pi <= b.p; pi++) {
        Para *p = &d->p[pi];
        if (!d->nondoc && para_is_dot(p)) continue;
        int s = pi == a.p ? a.o : 0, e = pi == b.p ? b.o : p->n;
        int inw = 0, any = 0;
        for (int i = s; i < e; i++) {
            wch c = p->t[i];
            if (IS_TAG(c)) continue;
            chars++; any = 1;
            int wc = !(c == ' ' || c == '\t' || c == 0xA0);
            if (wc && !inw) words++;
            inw = wc;
        }
        if (any) paras++;
    }
    wchar_t m[256];
    doc_layout(d, 0);
    swprintf(m, 256, L"%ls\n\nWords:       %ld\nCharacters:  %ld\nParagraphs:  %ld%ls",
             isblk ? L"Word count of block" : L"Word count of document", words, chars, paras,
             isblk || d->nondoc ? L"" : L"");
    if (!isblk && !d->nondoc) { wchar_t pg[40]; swprintf(pg, 40, L"\nPages:       %d", d->npages); wcscat(m, pg); }
    msgbox(L"Word Count", m, L"OK");
}

static void block_math(EdWin *w)
{
    Doc *d = w->doc;
    if (!block_ok(d)) { flash(L"No block marked"); return; }
    int n; wch *s = block_text(d, &n);
    wchar_t *pl; plain_from(s, n, &pl); free(s);
    int err = 0;
    double v = calc_eval(pl, &err);
    if (err) {
        /* sum all numbers in the block */
        v = 0; wchar_t *q = pl;
        while (*q) {
            if (iswdigit(*q) || ((*q == '-' || *q == '.') && iswdigit(q[1]))) {
                wchar_t *e; double x = wcstod(q, &e);
                if (e > q) { v += x; q = e; continue; }
            }
            if (*q == ',' && q > pl && iswdigit(q[-1]) && iswdigit(q[1])) { memmove(q, q + 1, wcslen(q) * sizeof(wchar_t)); continue; }
            q++;
        }
        wcscpy(g_lastexpr, L"(sum of block)");
    } else { wcsncpy(g_lastexpr, pl, 255); g_lastexpr[255] = 0; }
    free(pl);
    g_lastmath = v; g_havemath = 1;
    wchar_t r[64]; fmt_number(v, r, 64, 0);
    wchar_t m[128]; swprintf(m, 128, L"Block math result: %ls\n\nUse ^M= to insert it.", r);
    msgbox(L"Block Math", m, L"OK");
}

static int cmp_dir = 1;
static int para_cmp(const void *a, const void *b)
{
    const Para *x = a, *y = b;
    int i = 0, j = 0;
    for (;;) {
        while (i < x->n && IS_TAG(x->t[i])) i++;
        while (j < y->n && IS_TAG(y->t[j])) j++;
        if (i >= x->n || j >= y->n) return cmp_dir * ((i < x->n) - (j < y->n));
        wint_t c1 = towlower((wint_t)x->t[i]), c2 = towlower((wint_t)y->t[j]);
        if (c1 != c2) return cmp_dir * (c1 < c2 ? -1 : 1);
        i++; j++;
    }
}

static void sort_block(EdWin *w)
{
    Doc *d = w->doc;
    if (!block_ok(d)) { flash(L"No block marked"); return; }
    int a = d->bb.p, b = d->bk.p;
    if (d->bk.o == 0 && b > a) b--;
    if (b <= a) { flash(L"Sort needs at least two lines in the block"); return; }
    int r = msgbox(L"Sort Block", L"Sort the lines in the block:", L"&Ascending|&Descending|&Cancel");
    if (r < 0 || r == 2) return;
    cmp_dir = r == 0 ? 1 : -1;
    undo_push(d, w->cur, 0);
    qsort(&d->p[a], b - a + 1, sizeof(Para), para_cmp);
    d->bb.p = a; d->bb.o = 0; d->bk.p = b; d->bk.o = d->p[b].n;
    d->dirty = 1; doc_touch(d);
    clamp_cur(w);
}

/* ------------------------------------------------------------------ clipboard */
static void clip_set_text(const wchar_t *u)
{
    size_t len = wcslen(u);
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (h) { memcpy(GlobalLock(h), u, (len + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    CloseClipboard();
}

static void clip_to(EdWin *w)
{
    Doc *d = w->doc;
    if (!block_ok(d)) { flash(L"No block marked"); return; }
    int n; wch *s = block_text(d, &n);
    wchar_t *pl; plain_from(s, n, &pl); free(s);
    size_t len = wcslen(pl);
    if (!OpenClipboard(g_hwnd)) { free(pl); return; }
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (h) { memcpy(GlobalLock(h), pl, (len + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    if (!d->nondoc) {
        size_t rl; char *rtf = rtf_build(d, &d->bb, &d->bk, &rl);
        if (rtf) {
            UINT cf = RegisterClipboardFormatW(L"Rich Text Format");
            HGLOBAL h2 = GlobalAlloc(GMEM_MOVEABLE, rl + 1);
            if (h2) { memcpy(GlobalLock(h2), rtf, rl + 1); GlobalUnlock(h2); SetClipboardData(cf, h2); }
            free(rtf);
        }
    }
    CloseClipboard();
    free(pl);
    flash(L"Block copied to the Windows Clipboard");
}

static void insert_doc_contents(EdWin *w, Doc *src)
{
    Doc *d = w->doc;
    Pos a = { 0, 0 }, b = { src->np - 1, src->p[src->np - 1].n };
    int n; wch *s = doc_extract(src, a, b, &n);
    if (d->nondoc || d->noteparent) { int k = 0; for (int i = 0; i < n; i++) if (!(IS_TAG(s[i]) && d->nondoc) && !IS_NOTE(s[i])) s[k++] = s[i]; n = k; }
    else notes_dup(d, src, s, n);
    undo_push(d, w->cur, 0);
    Pos start = w->cur;
    int sp = start.p;
    Pos end = doc_insert(d, start, s, n);
    /* paragraph styles of inserted paragraphs */
    if (!d->nondoc) for (int i = 1; i < src->np && sp + i < d->np; i++) d->p[sp + i].style = src->p[i].style;
    free(s);
    d->bb = start; d->bk = end; d->bbset = d->bkset = 1; d->bhide = 0;
    w->cur = start;
}

static void clip_from(EdWin *w)
{
    if (!OpenClipboard(g_hwnd)) return;
    UINT cf = RegisterClipboardFormatW(L"Rich Text Format");
    Doc *tmp = NULL;
    if (!w->doc->nondoc && IsClipboardFormatAvailable(cf)) {
        HGLOBAL h = GetClipboardData(cf);
        if (h) {
            char *p = GlobalLock(h);
            size_t n = GlobalSize(h);
            if (p) {
                size_t real = strnlen(p, n);
                tmp = doc_new();
                if (!load_rtf_mem(tmp, p, real)) { doc_free(tmp); tmp = NULL; }
                GlobalUnlock(h);
            }
        }
    }
    if (!tmp && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HGLOBAL h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            wchar_t *p = GlobalLock(h);
            if (p) {
                tmp = doc_new();
                tmp->np = 0;
                int n = (int)wcslen(p);
                wch *b = xmalloc((n + 1) * sizeof(wch)); int k = 0, s = 0;
                for (int i = 0; i <= n; i++) {
                    if (i == n || p[i] == '\n') {
                        doc_append_para(tmp, b + s, k - s);
                        s = k;
                        continue;
                    }
                    if (p[i] == '\r') continue;
                    wchar_t c = p[i];
                    if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) { b[k++] = 0x10000 + ((c - 0xD800) << 10) + (p[i + 1] - 0xDC00); i++; }
                    else b[k++] = c;
                }
                free(b);
                if (tmp->np == 0) doc_append_para(tmp, NULL, 0);
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    if (!tmp) { flash(L"Nothing on the Clipboard to paste"); return; }
    insert_doc_contents(w, tmp);
    doc_free(tmp);
    flash(L"Pasted from the Windows Clipboard (marked as a block)");
}

/* ------------------------------------------------------------------ dialogs for layout */
static void margins_dialog(EdWin *w, int which)
{
    Doc *d = w->doc;
    Fmt f = doc_fmt_at(d, w->cur.p);
    wchar_t lm[16], rm[16], pm[16], tabs[200];
    swprintf(lm, 16, L"%d", f.lm); swprintf(rm, 16, L"%d", f.rm); swprintf(pm, 16, L"%d", f.pm);
    tabs[0] = 0;
    for (int i = 0; i < f.ntabs && f.tabs[i] < f.rm; i++) { wchar_t t[12]; swprintf(t, 12, i ? L",%d" : L"%d", f.tabs[i]); wcscat(tabs, t); }
    if (which == 1) { if (!input_box(L"Left Margin", L"Left margin column (or inches, e.g. 1\"):", lm, 16)) return; }
    else if (which == 2) { if (!input_box(L"Right Margin", L"Right margin column (or inches, e.g. 6.5\"):", rm, 16)) return; }
    else {
        DCtl c[] = {
            { DC_EDIT, 2, 3, 8, 1, L"&Left margin:", lm, 16, NULL, 0, NULL, 0, 0, 0, 22 },
            { DC_EDIT, 3, 3, 8, 1, L"&Right margin:", rm, 16, NULL, 0, NULL, 0, 0, 0, 22 },
            { DC_EDIT, 4, 3, 8, 1, L"&Paragraph margin:", pm, 16, NULL, 0, NULL, 0, 0, 0, 22 },
            { DC_EDIT, 6, 3, 40, 1, L"&Tab stops:", tabs, 200, NULL, 0, NULL, 0, 0, 0, 22 },
            { DC_LABEL, 8, 3, 60, 1, L"Columns are numbered from 1. Inches may be typed as 1.5\"." },
            { DC_BUTTON, 2, 52, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
            { DC_BUTTON, 4, 52, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
        };
        if (dialog_run(L"Ruler Line", 5, 6, 11, 66, c, 7, 0) != 1) return;
    }
    int v;
    if (parse_measure(lm, 1, &v) && v != f.lm) set_dot(w, L"lm", lm);
    if (parse_measure(rm, 1, &v) && v != f.rm) set_dot(w, L"rm", rm);
    if (which == 0) {
        if (parse_measure(pm, 1, &v) && v != f.pm && wcscmp(pm, lm)) set_dot(w, L"pm", pm);
        wchar_t old[200] = L"";
        for (int i = 0; i < f.ntabs && f.tabs[i] < f.rm; i++) { wchar_t t[12]; swprintf(t, 12, i ? L",%d" : L"%d", f.tabs[i]); wcscat(old, t); }
        if (wcscmp(old, tabs) && tabs[0]) set_dot(w, L"tb", tabs);
    }
}

static void align_dialog(EdWin *w)
{
    Doc *d = w->doc;
    Fmt f = doc_fmt_at(d, w->cur.p);
    int al = f.align == 'C' ? 1 : f.align == 'R' ? 2 : f.align == 'J' ? 3 : 0;
    wchar_t ls[8]; ls_text(f.ls, ls);
    DCtl c[] = {
        { DC_LABEL, 2, 3, 20, 1, L"Alignment:" },
        { DC_RADIO, 3, 5, 20, 1, L"Flush &left", NULL, 0, &al, 0 },
        { DC_RADIO, 4, 5, 20, 1, L"&Centered", NULL, 0, &al, 1 },
        { DC_RADIO, 5, 5, 20, 1, L"Flush &right", NULL, 0, &al, 2 },
        { DC_RADIO, 6, 5, 20, 1, L"&Justified", NULL, 0, &al, 3 },
        { DC_EDIT, 8, 3, 5, 1, L"Line &spacing (1, 1.5 ... 9):", ls, 8, NULL, 0, NULL, 0, 0, 0, 31 },
        { DC_BUTTON, 2, 40, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 4, 40, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Alignment and Spacing", 5, 12, 11, 56, c, 8, 1) != 1) return;
    char na = "LCRJ"[al];
    if (na != f.align) set_dot(w, L"oj", na == 'L' ? L"off" : na == 'J' ? L"on" : na == 'C' ? L"c" : L"r");
    int v = ls_parse(ls);
    if (v && v != f.ls) { wchar_t t[8]; ls_text(v, t); set_dot(w, L"ls", t); }
}

static void fmt_inches(int v, int perinch, wchar_t *out)
{
    swprintf(out, 16, L"%.2f\"", v / (double)perinch);
}

static void page_dialog(EdWin *w)
{
    Doc *d = w->doc;
    Fmt f = doc_fmt_at(d, w->cur.p);
    wchar_t pl[16], mt[16], mb[16], hm[16], fm[16], po[16];
    fmt_inches(f.pl, 6, pl); fmt_inches(f.mt, 6, mt); fmt_inches(f.mb, 6, mb);
    fmt_inches(f.hm, 6, hm); fmt_inches(f.fm, 6, fm); fmt_inches(f.po, 10, po);
    DCtl c[] = {
        { DC_EDIT, 2, 3, 9, 1, L"Page &length:", pl, 16, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_EDIT, 3, 3, 9, 1, L"&Top margin:", mt, 16, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_EDIT, 4, 3, 9, 1, L"&Bottom margin:", mb, 16, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_EDIT, 5, 3, 9, 1, L"&Header margin:", hm, 16, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_EDIT, 6, 3, 9, 1, L"&Footer margin:", fm, 16, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_EDIT, 7, 3, 9, 1, L"Page &offset (left):", po, 16, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_LABEL, 9, 3, 50, 1, L"Measurements are in inches." },
        { DC_BUTTON, 2, 42, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 4, 42, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Page Layout", 5, 10, 12, 58, c, 9, 0) != 1) return;
    int v;
    if (parse_measure(pl, 0, &v) && v != f.pl) set_dot(w, L"pl", pl);
    if (parse_measure(mt, 0, &v) && v != f.mt) set_dot(w, L"mt", mt);
    if (parse_measure(mb, 0, &v) && v != f.mb) set_dot(w, L"mb", mb);
    if (parse_measure(hm, 0, &v) && v != f.hm) set_dot(w, L"hm", hm);
    if (parse_measure(fm, 0, &v) && v != f.fm) set_dot(w, L"fm", fm);
    if (parse_measure(po, 1, &v) && v != f.po) set_dot(w, L"po", po);
}

static void pagenum_dialog(EdWin *w)
{
    Doc *d = w->doc;
    Fmt f = doc_fmt_at(d, w->cur.p);
    int print = !f.op;
    wchar_t start[16]; swprintf(start, 16, L"%d", f.pn > 0 ? f.pn : 1);
    DCtl c[] = {
        { DC_CHECK, 2, 3, 30, 1, L"&Print page numbers", NULL, 0, &print },
        { DC_EDIT, 4, 3, 6, 1, L"&Starting page number:", start, 16, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_LABEL, 6, 3, 50, 1, L"Page numbers print centered at the bottom unless" },
        { DC_LABEL, 7, 3, 50, 1, L"a footer is set. Use # in a header or footer." },
        { DC_BUTTON, 2, 40, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 4, 40, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Page Numbering", 6, 12, 10, 56, c, 6, 0) != 1) return;
    if (print != !f.op) insert_dot_line(w, print ? L".pg" : L".op");
    int v = _wtoi(start);
    if (v > 0 && v != (f.pn > 0 ? f.pn : 1)) set_dot(w, L"pn", start);
}

static void hf_dialog(EdWin *w, int footer)
{
    wchar_t t[200] = L"";
    if (!input_box(footer ? L"Footer" : L"Header",
                   footer ? L"Footer text (# = page number):" : L"Header text (# = page number):", t, 200)) return;
    wchar_t line[220];
    swprintf(line, 220, L".%ls %ls", footer ? L"fo" : L"he", t);
    insert_dot_line(w, line);
}

static void font_dialog(EdWin *w)
{
    static const wchar_t *fonts[] = {
        L"Courier New", L"Times New Roman", L"Arial", L"Georgia", L"Garamond", L"Book Antiqua",
        L"Palatino Linotype", L"Cambria", L"Calibri", L"Century Schoolbook", L"Segoe UI", L"Verdana",
        L"Tahoma", L"Lucida Console", L"Consolas", L"SBL BibLit", L"SBL Greek", L"SBL Hebrew",
        L"Gentium Plus", L"Cardo", L"(Other...)" };
    int n = sizeof(fonts) / sizeof(fonts[0]);
    uint8_t fx; int cf; fx_at_cursor(w, &fx, &cf);
    int sel = 0;
    if (cf >= 0) for (int i = 0; i < n; i++) if (!_wcsicmp(fonts[i], g_fonts[cf].name)) sel = i;
    int r = list_pick(L"Font", fonts, n, sel, 30);
    if (r < 0) return;
    wchar_t name[40];
    if (r == n - 1) { name[0] = 0; if (!input_box(L"Font", L"Font name:", name, 40) || !name[0]) return; }
    else wcscpy(name, fonts[r]);
    wchar_t size[16] = L"12";
    if (cf >= 0) swprintf(size, 16, L"%g", g_fonts[cf].halfpts / 2.0);
    if (!input_box(L"Font", L"Point size:", size, 16)) return;
    double pt = _wtof(size);
    if (pt < 4 || pt > 144) pt = 12;
    int fi = font_find_or_add(name, (int)(pt * 2 + 0.5));
    wch tag = TG_FONT0 + fi;
    undo_push(w->doc, w->cur, 0);
    ins_wch(w, &tag, 1);
}

static void pstyle_dialog(EdWin *w)
{
    Doc *d = w->doc;
    if (d->nondoc) { flash(L"Paragraph styles are not available in nondocuments"); return; }
    const wchar_t *names[32];
    for (int i = 0; i < g_npstyles; i++) names[i] = g_pstyles[i].name;
    int r = list_pick(L"Select Paragraph Style", names, g_npstyles, d->p[w->cur.p].style, 30);
    if (r < 0) return;
    undo_push(d, w->cur, 0);
    int a = w->cur.p, b = w->cur.p;
    if (block_ok(d)) { a = d->bb.p; b = d->bk.p; }
    for (int i = a; i <= b; i++) d->p[i].style = (uint8_t)r;
    d->dirty = 1; doc_touch(d);
}

static void center_line(EdWin *w, int right)
{
    Doc *d = w->doc;
    Line *l = cur_line(w);
    Para *p = &d->p[l->p];
    if (l->s != 0 || !(l->flags & LF_LAST)) {
        /* multi-line paragraph: bracket it with alignment dot commands */
        int pi = w->cur.p;
        Fmt f = doc_fmt_at(d, pi);
        const wchar_t *rest = f.align == 'J' ? L"\n.oj on" : f.align == 'C' ? L"\n.oj c" : f.align == 'R' ? L"\n.oj r" : L"\n.oj off";
        undo_push(d, w->cur, 0);
        Pos save = w->cur;
        int n = (int)wcslen(rest);
        wch b[16]; for (int i = 0; i < n; i++) b[i] = rest[i];
        Pos e = { pi, d->p[pi].n };
        doc_insert(d, e, b, n);
        w->cur = save;
        insert_dot_line(w, right ? L".oj r" : L".oj c");
        return;
    }
    undo_push(d, w->cur, 0);
    int k = 0;
    while (k < p->n && (p->t[k] == ' ' || p->t[k] == '\t')) k++;
    if (k) para_del(d, l->p, 0, k);
    doc_layout(d, 0);
    l = cur_line(w);
    Fmt f = doc_fmt_at(d, w->cur.p);
    int width = l->width;
    int avail = f.rm - f.lm + 1;
    int pad = right ? avail - width : (avail - width) / 2;
    if (pad > 0) {
        wch *sp = xmalloc(pad * sizeof(wch));
        for (int i = 0; i < pad; i++) sp[i] = ' ';
        para_ins(d, l->p, 0, sp, pad);
        free(sp);
    }
}

static void insert_value(EdWin *w, int cmd)
{
    wchar_t buf[MAX_PATH + 32];
    buf[0] = 0;
    SYSTEMTIME st; GetLocalTime(&st);
    switch (cmd) {
    case CMD_DATE: GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"MMMM d, yyyy", buf, 64); break;
    case CMD_TIME: GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, L"h:mm tt", buf, 64); break;
    case CMD_MATHRES: if (!g_havemath) { flash(L"No math result yet (use ^QM or ^KM)"); return; } fmt_number(g_lastmath, buf, 64, 0); break;
    case CMD_MATHDOLLAR: if (!g_havemath) { flash(L"No math result yet (use ^QM or ^KM)"); return; } fmt_number(g_lastmath, buf, 64, 1); break;
    case CMD_MATHEXPR: if (!g_havemath) { flash(L"No math expression yet"); return; } wcscpy(buf, g_lastexpr); break;
    case CMD_FILENAME: { const wchar_t *p = wcsrchr(w->doc->path, '\\'); wcscpy(buf, p ? p + 1 : w->doc->path[0] ? w->doc->path : L"UNTITLED"); break; }
    case CMD_DRIVE: { wchar_t cd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cd); swprintf(buf, 8, L"%c:", cd[0]); break; }
    case CMD_DIRECTORY: GetCurrentDirectoryW(MAX_PATH, buf); break;
    case CMD_PATH: wcscpy(buf, w->doc->path); break;
    }
    if (buf[0]) ins_str(w, buf);
}

static void calc_cmd(void)
{
    static wchar_t expr[200];
    for (;;) {
        wchar_t lab[300];
        if (g_havemath) {
            wchar_t r[64]; fmt_number(g_lastmath, r, 64, 0);
            swprintf(lab, 300, L"Last result: %ls      Expression:", r);
        } else wcscpy(lab, L"Type a math expression, e.g. (12.5*3)+sqrt(16):");
        if (!input_box(L"Calculator", lab, expr, 200) || !expr[0]) return;
        int err = 0;
        double v = calc_eval(expr, &err);
        if (err) { msgbox(L"Calculator", L"That expression couldn't be evaluated.", L"OK"); continue; }
        g_lastmath = v; g_havemath = 1;
        wcsncpy(g_lastexpr, expr, 255);
    }
}

static void status_info(EdWin *w)
{
    Doc *d = w->doc;
    doc_layout(d, 0);
    long chars = 0, words = 0;
    for (int i = 0; i < d->np; i++) {
        int inw = 0;
        for (int k = 0; k < d->p[i].n; k++) {
            wch c = d->p[i].t[k]; if (IS_TAG(c)) continue;
            chars++; int wc = c != ' ' && c != '\t'; if (wc && !inw) words++; inw = wc;
        }
    }
    wchar_t m[1024], cd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cd);
    ULARGE_INTEGER fb; GetDiskFreeSpaceExW(NULL, &fb, NULL, NULL);
    MEMORYSTATUSEX ms = { sizeof(ms) }; GlobalMemoryStatusEx(&ms);
    swprintf(m, 1024, L"File:        %ls\nFormat:      %ls\nModified:    %ls\nParagraphs:  %d\nWords:       %ld\nCharacters:  %ld\nPages:       %d\n\nDirectory:   %ls\nDisk free:   %llu KB\nMemory free: %llu KB\nUndo levels: %d",
             d->path[0] ? d->path : L"UNTITLED",
             d->nondoc ? L"Nondocument (plain text)" : d->fmt == FMT_WS ? L"WordStar (will save as RTF)" : L"Document (RTF)",
             d->dirty ? L"Yes" : L"No", d->np, words, chars, d->npages, cd,
             (unsigned long long)(fb.QuadPart / 1024), (unsigned long long)(ms.ullAvailPhys / 1024), d->nundo);
    msgbox(L"Status", m, L"OK");
}


/* ------------------------------------------------------------------ spelling */
static int script_ok(wch c)
{
    /* Latin-script letters only; Greek, Hebrew, Cyrillic words are skipped */
    if (c >= 0x370 && c <= 0x3FF) return 0;
    if (c >= 0x1F00 && c <= 0x1FFF) return 0;
    if (c >= 0x590 && c <= 0x5FF) return 0;
    if (c >= 0x400 && c <= 0x4FF) return 0;
    if (c >= 0xFB1D && c <= 0xFB4F) return 0;
    return 1;
}
static int is_letter(wch c) { return !IS_TAG(c) && c < 0x10000 && (iswalpha((wint_t)c) || (c >= 0x300 && c <= 0x36F)); }
static int is_apos(wch c) { return c == '\'' || c == 0x2019; }

/* Finds the next word at or after offset 'from'. Returns 1 and fills [ws,we) and w (apostrophes
   normalised); *skip is set for words that should not be checked. */
static int next_word(Para *p, int from, int *ws, int *we, wchar_t *w, int *skip)
{
    int i = from;
    for (;;) {
        while (i < p->n && !is_letter(p->t[i]) && !(p->t[i] < 0x10000 && iswdigit((wint_t)p->t[i]))) i++;
        if (i >= p->n) return 0;
        int s = i, k = 0, digits = 0, foreign = 0, upper = 0, letters = 0;
        while (i < p->n) {
            wch c = p->t[i];
            if (IS_TAG(c) || c == SHY) { i++; continue; }
            if (is_letter(c) || (c < 0x10000 && iswdigit((wint_t)c))) {
                if (iswdigit((wint_t)c)) digits = 1;
                else { letters++; if (iswupper((wint_t)c)) upper++; if (!script_ok(c)) foreign = 1; }
                if (k < 62) w[k++] = (wchar_t)c;
                i++; continue;
            }
            if (is_apos(c) && i + 1 < p->n && is_letter(p->t[i + 1]) && k > 0) { if (k < 62) w[k++] = '\''; i++; continue; }
            break;
        }
        int e = i;
        while (e > s && IS_TAG(p->t[e - 1])) e--;
        w[k] = 0;
        *ws = s; *we = e;
        *skip = digits || foreign || letters < 2 || k >= 62 || (S.skipcaps && upper == letters);
        return 1;
    }
}

static void match_case(const wchar_t *orig, wchar_t *rep)
{
    int n = (int)wcslen(orig), up = 0, lo = 0;
    for (int i = 0; i < n; i++) { if (iswupper(orig[i])) up++; else if (iswlower(orig[i])) lo++; }
    if (up > 1 && !lo) { for (wchar_t *q = rep; *q; q++) *q = towupper(*q); return; }
    if (n && iswupper(orig[0]) && rep[0] && iswlower(rep[0])) rep[0] = towupper(rep[0]);
}

enum { SP_STOP, SP_IGNORE, SP_BYPASS, SP_ADD, SP_REPLACE };

/* the spelling dialog shown at the bottom of the screen */
static int spell_dialog(const wchar_t *word, int lang, wchar_t *rep)
{
    wchar_t sug[24][64];
    int ns = spell_suggest(word, lang, sug, 24);
    for (int i = 0; i < ns; i++) match_case(word, sug[i]);
    int sel = 0, focus = ns ? 0 : 1, epos, fresh = 1;
    wcscpy(rep, ns ? sug[0] : word);
    epos = (int)wcslen(rep);
    int h = 10, r = SR - h - 1, w = SC;
    int cols = 4, colw = (SC - 6) / cols, srows = 3;
    Cell *save = xmalloc(sizeof(scr)); scr_save(save);
    int result = SP_STOP;
    for (;;) {
        scr_restore(save);
        scr_rect(r, 0, h, w, ' ', COL[C_DLG]);
        scr_box(r, 0, h, w, COL[C_DLG], 0);
        scr_fill(r, 0, w, ' ', COL[C_DLGTITLE]);
        wchar_t t[160];
        swprintf(t, 160, L"Spelling Check - %ls", g_langnames[lang]);
        scr_puts(r, (w - (int)wcslen(t)) / 2, t, COL[C_DLGTITLE]);
        scr_puts(r + 1, 2, L"Not in dictionary:", COL[C_DLG]);
        scr_puts(r + 1, 21, word, COL[C_DLGFOCUS]);
        scr_puts(r + 2, 2, ns ? L"Suggestions:" : L"No suggestions.", COL[C_DLG]);
        for (int i = 0; i < ns && i < cols * srows; i++) {
            int rr = r + 3 + i / cols, cc = 3 + (i % cols) * colw;
            wchar_t it[80]; swprintf(it, 80, i < 9 ? L"%d %ls" : L"  %ls", i + 1, sug[i]);
            uint8_t at = (i == sel && focus == 0) ? COL[C_DLGFOCUS] : COL[C_DLG];
            scr_fill(rr, cc, colw - 1, ' ', at);
            scr_putsn(rr, cc, it, colw - 2, at);
        }
        scr_puthot(r + 6, 2, L"Replace &with:", COL[C_DLG], COL[C_DLGHOT]);
        uint8_t fa = focus == 1 ? COL[C_DLGFOCUS] : COL[C_DLGFIELD];
        scr_fill(r + 6, 17, 40, ' ', fa);
        scr_putsn(r + 6, 17, rep, 40, fa);
        scr_puts(r + 8, 2, L"Enter replace  1-9 pick  I ignore  B bypass all  A add to dictionary  Esc stop", COL[C_DLGHOT]);
        if (focus == 1) scr_cursor(r + 6, 17 + (epos < 40 ? epos : 39), 1, 0); else scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) break;
        if (e.type == EV_MOUSE) {
            if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
            int rr = e.mr - r - 3, cc = (e.mc - 3) / colw;
            if (rr >= 0 && rr < srows && cc >= 0 && cc < cols && rr * cols + cc < ns) {
                sel = rr * cols + cc; focus = 0; wcscpy(rep, sug[sel]); epos = (int)wcslen(rep);
                if (e.mact == MB_DBL) { result = SP_REPLACE; break; }
            } else if (e.mr == r + 6) { focus = 1; fresh = 0; }
            continue;
        }
        if (e.type != EV_KEY) continue;
        int k = e.key;
        if (k == K_ESC && focus == 1 && ns) { focus = 0; continue; }
        if (k == K_ESC) break;
        if (k == K_ENTER || k == K_F10) { if (rep[0]) result = SP_REPLACE; else continue; break; }
        if (k == K_TAB || k == K_BTAB) { focus = !focus; fresh = 1; continue; }
        if (focus == 0) {
            if (k == K_RIGHT || k == CTRL('D')) { if (sel < ns - 1) sel++; }
            else if (k == K_LEFT || k == CTRL('S')) { if (sel > 0) sel--; }
            else if (k == K_DOWN || k == CTRL('X')) { if (sel + cols < ns) sel += cols; else { focus = 1; continue; } }
            else if (k == K_UP || k == CTRL('E')) { if (sel >= cols) sel -= cols; }
            else if (k >= '1' && k <= '9' && k - '1' < ns) { sel = k - '1'; wcscpy(rep, sug[sel]); result = SP_REPLACE; break; }
            else if (k < 0x10000 && k >= 32) {
                int K = towupper((wint_t)k);
                if (K == 'I') { result = SP_IGNORE; break; }
                if (K == 'B') { result = SP_BYPASS; break; }
                if (K == 'A') { result = SP_ADD; break; }
                if (K == 'W' || K == 'E') { focus = 1; fresh = 0; continue; }
                continue;
            } else if (k == K_ALT + 'W') { focus = 1; fresh = 0; continue; }
            if (ns) { wcscpy(rep, sug[sel]); epos = (int)wcslen(rep); }
            continue;
        }
        /* editing the replacement */
        int len = (int)wcslen(rep);
        if (k == K_LEFT || k == CTRL('S')) { if (epos > 0) epos--; fresh = 0; }
        else if (k == K_RIGHT || k == CTRL('D')) { if (epos < len) epos++; fresh = 0; }
        else if (k == K_HOME) { epos = 0; fresh = 0; }
        else if (k == K_END) { epos = len; fresh = 0; }
        else if (k == K_UP && ns) { focus = 0; }
        else if (k == K_BS) { if (fresh) { rep[0] = 0; epos = 0; } else if (epos > 0) { memmove(rep + epos - 1, rep + epos, (len - epos + 1) * sizeof(wchar_t)); epos--; } fresh = 0; }
        else if (k == K_DEL || k == CTRL('G')) { if (epos < len) memmove(rep + epos, rep + epos + 1, (len - epos) * sizeof(wchar_t)); fresh = 0; }
        else if (k == CTRL('V') || k == K_SINS) field_paste(rep, 63, &epos, &fresh);
        else if (k >= 32 && k < 0x10000 && len < 62) {
            if (fresh) { rep[0] = 0; epos = 0; len = 0; }
            fresh = 0;
            memmove(rep + epos + 1, rep + epos, (len - epos + 1) * sizeof(wchar_t));
            rep[epos++] = (wchar_t)k;
        }
    }
    scr_restore(save); free(save);
    return result;
}

static void replace_range(EdWin *w, int pi, int s, int e, const wchar_t *rep)
{
    Doc *d = w->doc;
    undo_push(d, w->cur, 0);
    /* keep any tags that were inside the word */
    Para *p = &d->p[pi];
    int ntags = 0; wch tags[16];
    for (int i = s; i < e; i++) if (IS_TAG(p->t[i]) && ntags < 16) tags[ntags++] = p->t[i];
    para_del(d, pi, s, e - s);
    int n = (int)wcslen(rep);
    wch *b = xmalloc((n + ntags + 1) * sizeof(wch));
    for (int i = 0; i < n; i++) b[i] = rep[i];
    for (int i = 0; i < ntags; i++) b[n + i] = tags[i];
    para_ins(d, pi, s, b, n + ntags);
    free(b);
}

static int lang_at(Doc *d, int pi) { return doc_fmt_at(d, pi).lang; }

static void spell_run(EdWin *w, int fromstart)
{
    Doc *d = w->doc;
    Pos start = w->cur;
    if (fromstart) { start.p = 0; start.o = 0; }
    int lang = d->nondoc ? S.lang : lang_at(d, start.p);
    /* back up to the start of the word the cursor is in */
    { Para *p = &d->p[start.p]; while (start.o > 0 && (is_letter(p->t[start.o - 1]) || IS_TAG(p->t[start.o - 1]))) start.o--; }
    if (!spell_available(lang)) {
        wchar_t m[200]; swprintf(m, 200, L"No %ls dictionary was found.\nThe Dictionaries folder must be next to ScoopStar.exe.", g_langnames[lang]);
        msgbox(L"Spelling Check", m, L"OK");
        return;
    }
    int checked = 0, fixed = 0;
    Pos save = w->cur;
    for (int pi = start.p; pi < d->np; pi++) {
        Para *p = &d->p[pi];
        if (!d->nondoc && para_is_dot(p)) {
            wchar_t c[8], a[64]; dot_parse(p->t, p->n, c, 8, a, 64);
            if (!wcscmp(c, L"la")) { int l = lang_from_name(a); if (l >= 0) lang = l; }
            continue;
        }
        int o = pi == start.p ? start.o : 0, ws, we, skip;
        wchar_t word[64];
        while (next_word(&d->p[pi], o, &ws, &we, word, &skip)) {
            o = we;
            if (skip) continue;
            checked++;
            int r = spell_check(word, lang);
            if (r < 0) {
                wchar_t m[200]; swprintf(m, 200, L"No %ls dictionary was found.", g_langnames[lang]);
                msgbox(L"Spelling Check", m, L"OK");
                return;
            }
            if (r) continue;
            /* show the word */
            w->cur.p = pi; w->cur.o = ws;
            g_hl_on = 1; g_hl_p = pi; g_hl_s = ws; g_hl_e = we;
            doc_layout(d, 0);
            int li = doc_line_of(d, w->cur);
            w->top = li - 2; if (w->top < 0) w->top = 0;
            flash(L"Checking spelling...");
            draw_all();
            wchar_t rep[64];
            int act = spell_dialog(word, lang, rep);
            g_hl_on = 0;
            if (act == SP_STOP) { flash(L"Spelling check stopped"); set_wantcol(w); return; }
            if (act == SP_BYPASS) spell_ignore_all(word);
            else if (act == SP_ADD) spell_add(word, lang);
            else if (act == SP_REPLACE && wcscmp(rep, word)) {
                replace_range(w, pi, ws, we, rep);
                o = ws + (int)wcslen(rep);
                fixed++;
            }
        }
    }
    (void)save;
    g_hl_on = 0;
    flash(L"Spelling done: %d checked, %d fixed", checked, fixed);
}

static int word_at_cursor(EdWin *w, int *ws, int *we, wchar_t *word)
{
    Para *p = &w->doc->p[w->cur.p];
    int o = w->cur.o;
    if (o > 0 && (o >= p->n || !is_letter(p->t[o])) && is_letter(p->t[o - 1])) o--;
    if (o >= p->n || !(is_letter(p->t[o]) || IS_TAG(p->t[o]))) return 0;
    while (o > 0 && (is_letter(p->t[o - 1]) || IS_TAG(p->t[o - 1]) || (is_apos(p->t[o - 1]) && o > 1 && is_letter(p->t[o - 2])))) o--;
    int skip;
    if (!next_word(p, o, ws, we, word, &skip)) return 0;
    return *ws <= w->cur.o + 1;
}

static void spell_word_cmd(EdWin *w)
{
    Doc *d = w->doc;
    int ws, we; wchar_t word[64];
    if (!word_at_cursor(w, &ws, &we, word)) { flash(L"The cursor is not on a word"); return; }
    int lang = d->nondoc ? S.lang : lang_at(d, w->cur.p);
    int r = spell_check(word, lang);
    if (r < 0) { msgbox(L"Spelling Check", L"No dictionary was found for this language.", L"OK"); return; }
    if (r) { flash(L"\"%ls\" is spelled correctly (%ls)", word, g_langnames[lang]); return; }
    g_hl_on = 1; g_hl_p = w->cur.p; g_hl_s = ws; g_hl_e = we;
    draw_all();
    wchar_t rep[64];
    int act = spell_dialog(word, lang, rep);
    g_hl_on = 0;
    if (act == SP_BYPASS) spell_ignore_all(word);
    else if (act == SP_ADD) spell_add(word, lang);
    else if (act == SP_REPLACE && wcscmp(rep, word)) { replace_range(w, w->cur.p, ws, we, rep); w->cur.o = ws + (int)wcslen(rep); }
}

static void spell_type_cmd(EdWin *w)
{
    static wchar_t word[64];
    int lang = w->doc->nondoc ? S.lang : lang_at(w->doc, w->cur.p);
    wchar_t lab[80]; swprintf(lab, 80, L"Word to check (%ls):", g_langnames[lang]);
    if (!input_box(L"Check Spelling of a Word", lab, word, 64) || !word[0]) return;
    int r = spell_check(word, lang);
    if (r < 0) { msgbox(L"Spelling Check", L"No dictionary was found for this language.", L"OK"); return; }
    if (r) { wchar_t m[128]; swprintf(m, 128, L"\"%ls\" is spelled correctly.", word); msgbox(L"Spelling Check", m, L"OK"); return; }
    wchar_t sug[20][64]; int ns = spell_suggest(word, lang, sug, 20);
    if (!ns) { wchar_t m[128]; swprintf(m, 128, L"\"%ls\" is not in the dictionary.\nNo suggestions.", word); msgbox(L"Spelling Check", m, L"OK"); return; }
    const wchar_t *items[20]; for (int i = 0; i < ns; i++) items[i] = sug[i];
    wchar_t t[100]; swprintf(t, 100, L"Not found - Enter inserts a suggestion");
    int k = list_pick(t, items, ns, 0, 40);
    if (k >= 0) ins_str(w, sug[k]);
}

static void language_cmd(EdWin *w)
{
    Doc *d = w->doc;
    const wchar_t *items[NLANGS]; wchar_t buf[NLANGS][80];
    for (int i = 0; i < NLANGS; i++) { swprintf(buf[i], 80, L"%-8ls %ls", g_langnames[i], spell_engine_name(i)); items[i] = buf[i]; }
    int cur = d->nondoc ? S.lang : lang_at(d, w->cur.p);
    int k = list_pick(L"Spelling Language", items, NLANGS, cur, 40);
    if (k < 0) return;
    int r = d->nondoc ? 1 : msgbox(L"Language Change", L"Use this language for the text from here on (inserts a .la dot\ncommand), or make it the default for all documents?", L"From &here on|Make &default|&Cancel");
    static const wchar_t *codes[NLANGS] = { L"en", L"la", L"de" };
    if (r == 0) set_dot(w, L"la", codes[k]);
    else if (r == 1) { S.lang = k; settings_save(); doc_touch(d); flash(L"Default spelling language: %ls", g_langnames[k]); }
}

/* ------------------------------------------------------------------ thesaurus */
static void syn_clean(const wchar_t *s, wchar_t *out, int n)
{
    /* drop "(generic term)" style notes */
    wcsncpy(out, s, n - 1); out[n - 1] = 0;
    wchar_t *par = wcsstr(out, L" (");
    if (par) *par = 0;
}

static int split_syn(const wchar_t *m, wchar_t pos[32], const wchar_t **syn, wchar_t *buf, int max)
{
    /* "(noun)|a|b|c" -> pos and list */
    wcscpy(buf, m);
    int n = 0;
    wchar_t *ctx, *tok = wcstok_s(buf, L"|", &ctx);
    pos[0] = 0;
    if (tok && tok[0] == '(') { wcsncpy(pos, tok, 31); pos[31] = 0; tok = wcstok_s(NULL, L"|", &ctx); }
    while (tok && n < max) { syn[n++] = tok; tok = wcstok_s(NULL, L"|", &ctx); }
    return n;
}

static void thesaurus_cmd(EdWin *w)
{
    Doc *d = w->doc;
    int ws = -1, we = -1, wp = w->cur.p;
    wchar_t orig[64] = L"", look[64] = L"";
    if (word_at_cursor(w, &ws, &we, orig)) wcscpy(look, orig);
    else ws = -1;
    if (!thes_available()) {
        msgbox(L"Thesaurus", L"The thesaurus file (Dictionaries\\th_en_US_v2.dat)\nwas not found next to ScoopStar.exe.", L"OK");
        return;
    }
    if (!look[0] && (!input_box(L"Thesaurus", L"Word to look up:", look, 64) || !look[0])) return;
    wchar_t hist[16][64]; int nh = 0;
    for (;;) {
        wchar_t **m = NULL;
        wchar_t found[64]; wcscpy(found, look);
        int nm = thes_lookup(look, &m);
        if (!nm) {
            wchar_t st[8][64]; int ns = spell_stems(look, st, 8);
            for (int i = 0; i < ns && !nm; i++) { nm = thes_lookup(st[i], &m); if (nm) wcscpy(found, st[i]); }
        }
        if (!nm) {
            wchar_t q[160]; swprintf(q, 160, L"\"%ls\" is not in the thesaurus.\nLook up another word?", look);
            if (ask_yn(L"Thesaurus", q) != 1 || !input_box(L"Thesaurus", L"Word to look up:", look, 64) || !look[0]) return;
            continue;
        }
        int msel = 0, ssel = 0, pane = 0, h = SR - 5, r = 2, lw = 34;
        int mtop = 0, stop = 0;
        wchar_t sbuf[2048]; const wchar_t *syn[200]; wchar_t pos[32];
        int action = 0; wchar_t chosen[64] = L"";
        Cell *save = xmalloc(sizeof(scr)); scr_save(save);
        for (;;) {
            int nsyn = split_syn(m[msel], pos, syn, sbuf, 200);
            if (ssel >= nsyn) ssel = nsyn - 1;
            if (ssel < 0) ssel = 0;
            int body = h - 5;
            if (msel < mtop) mtop = msel;
            if (msel >= mtop + body) mtop = msel - body + 1;
            if (ssel < stop) stop = ssel;
            if (ssel >= stop + body) stop = ssel - body + 1;
            scr_restore(save);
            scr_rect(r, 0, h, SC, ' ', COL[C_DLG]);
            scr_box(r, 0, h, SC, COL[C_DLG], 0);
            scr_fill(r, 0, SC, ' ', COL[C_DLGTITLE]);
            scr_puts(r, (SC - 9) / 2, L"Thesaurus", COL[C_DLGTITLE]);
            wchar_t t[200]; swprintf(t, 200, L"Word: %ls%ls%ls", found, wcscmp(found, look) ? L"   (from " : L"", wcscmp(found, look) ? look : L"");
            if (wcscmp(found, look)) wcscat(t, L")");
            scr_puts(r + 1, 2, t, COL[C_DLGHOT]);
            scr_puts(r + 2, 2, L"Meanings:", COL[C_DLG]);
            scr_puts(r + 2, lw + 3, L"Synonyms:", COL[C_DLG]);
            for (int i = 0; i < body; i++) {
                int ix = mtop + i;
                if (ix < nm) {
                    wchar_t p2[32]; const wchar_t *s2[4]; wchar_t b2[2048];
                    int n2 = split_syn(m[ix], p2, s2, b2, 4);
                    wchar_t first[64] = L""; if (n2) syn_clean(s2[0], first, 64);
                    wchar_t line[120]; swprintf(line, 120, L"%-7ls %ls", p2, first);
                    uint8_t at = ix == msel ? (pane == 0 ? COL[C_DLGFOCUS] : COL[C_FILESEL]) : COL[C_DLG];
                    scr_fill(r + 3 + i, 2, lw, ' ', at);
                    scr_putsn(r + 3 + i, 2, line, lw - 1, at);
                }
                int jx = stop + i;
                if (jx < nsyn) {
                    uint8_t at = jx == ssel ? (pane == 1 ? COL[C_DLGFOCUS] : COL[C_FILESEL]) : COL[C_DLG];
                    scr_fill(r + 3 + i, lw + 3, SC - lw - 5, ' ', at);
                    scr_putsn(r + 3 + i, lw + 4, syn[jx], SC - lw - 7, at);
                }
            }
            for (int i = 0; i < h - 4; i++) scr_put(r + 2 + i, lw + 2, 0x2502, COL[C_DLG]);
            scr_puts(r + h - 2, 2, ws >= 0 ? L"Enter replace word   L look up synonym   Bksp back   W new word   Tab switch   Esc"
                                           : L"Enter insert word   L look up synonym   Bksp back   W new word   Tab switch   Esc", COL[C_DLGHOT]);
            scr_cursor(0, 0, 0, 0);
            Event e; ev_get(&e, -1);
            if (e.type == EV_CLOSE) { action = 0; break; }
            if (e.type == EV_MOUSE) {
                if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
                int row = e.mr - r - 3;
                if (row < 0 || row >= body) continue;
                if (e.mc < lw + 2) { if (mtop + row < nm) { msel = mtop + row; pane = 0; ssel = 0; stop = 0; } }
                else if (stop + row < nsyn) { ssel = stop + row; pane = 1; if (e.mact == MB_DBL) { syn_clean(syn[ssel], chosen, 64); action = 1; break; } }
                continue;
            }
            if (e.type != EV_KEY) continue;
            int k = e.key;
            if (k == K_ESC) { action = 0; break; }
            if (k == K_TAB || k == K_BTAB || k == K_LEFT || k == K_RIGHT || k == CTRL('S') || k == CTRL('D')) { pane = !pane; continue; }
            if (k == K_UP || k == CTRL('E')) { if (pane == 0) { if (msel > 0) { msel--; ssel = 0; stop = 0; } } else if (ssel > 0) ssel--; continue; }
            if (k == K_DOWN || k == CTRL('X')) { if (pane == 0) { if (msel < nm - 1) { msel++; ssel = 0; stop = 0; } } else if (ssel < nsyn - 1) ssel++; continue; }
            if (k == K_ENTER) {
                if (pane == 0) { pane = 1; continue; }
                if (nsyn) { syn_clean(syn[ssel], chosen, 64); action = 1; }
                break;
            }
            if (k == K_BS) { if (nh > 0) { wcscpy(look, hist[--nh]); action = 2; break; } continue; }
            if (k < 0x10000 && (towupper((wint_t)k) == 'L')) {
                if (nsyn) { if (nh < 16) wcscpy(hist[nh++], look); syn_clean(syn[pane == 1 ? ssel : 0], look, 64); action = 2; }
                break;
            }
            if (k < 0x10000 && towupper((wint_t)k) == 'W') {
                wchar_t nw[64] = L"";
                if (input_box(L"Thesaurus", L"Word to look up:", nw, 64) && nw[0]) { if (nh < 16) wcscpy(hist[nh++], look); wcscpy(look, nw); action = 2; break; }
                continue;
            }
        }
        scr_restore(save); free(save);
        thes_free(m, nm);
        if (action == 2) continue;
        if (action == 1 && chosen[0]) {
            if (ws >= 0) {
                match_case(orig, chosen);
                replace_range(w, wp, ws, we, chosen);
                w->cur.p = wp; w->cur.o = ws + (int)wcslen(chosen);
            } else ins_str(w, chosen);
            (void)d;
        }
        return;
    }
}


/* ------------------------------------------------------------------ footnotes */
static void note_writeback(Doc *nd)
{
    Doc *pd = nd->noteparent;
    if (!pd || nd->noteidx >= pd->nnotes) return;
    Pos a = { 0, 0 }, b = { nd->np - 1, nd->p[nd->np - 1].n };
    int n; wch *t = doc_extract(nd, a, b, &n);
    while (n > 0 && (t[n - 1] == '\n' || t[n - 1] == ' ')) n--;
    Note *nt = &pd->notes[nd->noteidx];
    free(nt->t); nt->t = t; nt->n = n;
    pd->dirty = 1; doc_touch(pd);
    nd->dirty = 0;
}

/* remove the reference to a footnote that was left empty */
static void drop_empty_note(Doc *pd, int idx)
{
    if (idx >= pd->nnotes || pd->notes[idx].n) return;
    for (int i = 0; i < pd->np; i++)
        for (int k = 0; k < pd->p[i].n; k++)
            if (pd->p[i].t[k] == NOTEBASE + (wch)idx) { para_del(pd, i, k, 1); return; }
}

static void close_note_window(int writeback)
{
    if (g_nwin < 2 || !g_win[1].doc->noteparent) return;
    Doc *nd = g_win[1].doc, *pd = nd->noteparent;
    int idx = nd->noteidx;
    if (writeback) note_writeback(nd);
    close_window(1);
    drop_empty_note(pd, idx);
    g_cw = 0;
}

static void open_note(EdWin *w, int idx)
{
    Doc *d = w->doc;
    if (d->noteparent) { flash(L"A footnote can't contain another footnote"); return; }
    if (g_nwin == 2) { flash(L"Close the second window first (^OK to switch, ^KD to close)"); return; }
    Doc *nd = doc_new();
    free(nd->p[0].t); nd->np = 0;
    Note *nt = &d->notes[idx];
    int s0 = 0;
    for (int i = 0; i <= nt->n; i++)
        if (i == nt->n || nt->t[i] == '\n') { doc_append_para(nd, nt->t + s0, i - s0); s0 = i + 1; }
    if (nd->np == 0) doc_append_para(nd, NULL, 0);
    nd->noteparent = d; nd->noteidx = idx;
    nd->base = d->base;
    nd->base.align = 'L';
    nd->dirty = 0;
    memset(&g_win[1], 0, sizeof(EdWin));
    g_win[1].doc = nd; nd->refs++;
    g_win[1].cur.p = nd->np - 1; g_win[1].cur.o = nd->p[nd->np - 1].n;
    doc_register_pos(nd, &g_win[1].cur);
    g_nwin = 2; g_cw = 1;
    int row = menu_top() + (S.show_stylebar && S.helplevel > 0 ? 1 : 0), bottom = S.show_status ? SR - 1 : SR;
    g_winsplit = bottom - row - 9;
    flash(L"Type the footnote; F10 or ^KD returns to the document");
}

static int note_at(Doc *d, Pos c, int forward_search, Pos *where)
{
    Para *p = &d->p[c.p];
    if (c.o < p->n && IS_NOTE(p->t[c.o])) { *where = c; return (int)(p->t[c.o] - NOTEBASE); }
    if (c.o > 0 && c.o <= p->n && IS_NOTE(p->t[c.o - 1])) { where->p = c.p; where->o = c.o - 1; return (int)(p->t[c.o - 1] - NOTEBASE); }
    if (!forward_search) return -1;
    for (int pi = c.p; pi < d->np; pi++)
        for (int k = (pi == c.p ? c.o : 0); k < d->p[pi].n; k++)
            if (IS_NOTE(d->p[pi].t[k])) { where->p = pi; where->o = k; return (int)(d->p[pi].t[k] - NOTEBASE); }
    return -1;
}

/* ------------------------------------------------------------------ commands */
static int exec_cmd(int cmd, int arg);

static void mouse_text(Event *e)
{
    for (int i = 0; i < g_nwin; i++) {
        EdWin *w = &g_win[i];
        if (e->mr < w->r0 || e->mr >= w->r0 + w->rows) continue;
        int tw = textw();
        if (S.show_scroll && e->mc == SC - 1) {
            if (e->mact != MB_DOWN && e->mact != MB_MOVE) return;
            g_cw = i;
            Doc *d = w->doc; doc_layout(d, 0);
            if (e->mr == w->r0 && e->mact == MB_DOWN) exec_cmd(CMD_SCRUP, 0);
            else if (e->mr == w->r0 + w->rows - 1 && e->mact == MB_DOWN) exec_cmd(CMD_SCRDN, 0);
            else {
                int span = w->rows - 2; if (span < 1) span = 1;
                int pos = e->mr - w->r0 - 1; if (pos < 0) pos = 0;
                if (pos >= span) pos = span - 1;
                int li = span > 1 ? (int)((long long)pos * (d->nl - 1) / (span - 1)) : 0;
                goto_line_col(w, li, w->wantcol);
                w->top = li - w->rows / 2; if (w->top < 0) w->top = 0;
            }
            return;
        }
        if (e->mc >= tw) return;
        if (e->mact == MB_DOWN && g_cw != i) { g_cw = i; }
        if (g_cw != i) return;
        Doc *d = w->doc; doc_layout(d, 0);
        int li = w->top + (e->mr - w->r0);
        if (li >= d->nl) li = d->nl - 1;
        int col = e->mc + w->hscroll;
        if (e->mact == MB_DOWN) {
            goto_line_col(w, li, col); w->wantcol = col;
            g_drag = 1; g_dragged = 0; g_anchor = w->cur;
        } else if (e->mact == MB_MOVE && g_drag) {
            goto_line_col(w, li, col); w->wantcol = col;
            if (pos_cmp(w->cur, g_anchor)) {
                g_dragged = 1;
                if (pos_cmp(w->cur, g_anchor) < 0) { d->bb = w->cur; d->bk = g_anchor; }
                else { d->bb = g_anchor; d->bk = w->cur; }
                d->bbset = d->bkset = 1; d->bhide = 0;
            }
        } else if (e->mact == MB_UP) {
            g_drag = 0;
        } else if (e->mact == MB_DBL) {
            goto_line_col(w, li, col);
            Para *p = &d->p[w->cur.p];
            int a = w->cur.o, b = w->cur.o;
            while (a > 0 && wch_isword(p->t[a - 1])) a--;
            while (b < p->n && wch_isword(p->t[b])) b++;
            if (b > a) { d->bb.p = d->bk.p = w->cur.p; d->bb.o = a; d->bk.o = b; d->bbset = d->bkset = 1; d->bhide = 0; }
        }
        return;
    }
}

static void mouse_stylebar(Event *e)
{
    if (e->mact != MB_DOWN) return;
    int bcol = SC - 37;
    if (e->mc < bcol) {
        if (e->mc < bcol / 2) exec_cmd(CMD_PSTYLE, 0); else exec_cmd(CMD_FONT, 0);
        return;
    }
    static const int cmds[9] = { CMD_BOLD, CMD_ITALIC, CMD_UNDER, CMD_TAGS, CMD_ALIGN_L, CMD_ALIGN_C, CMD_ALIGN_R, CMD_ALIGN_J, CMD_LINESPACE };
    for (int i = 0; i < 9; i++) if (e->mc >= sb_btncol[i] && e->mc < sb_btncol[i] + 3) { exec_cmd(cmds[i], 0); return; }
}

static int close_current(int save)
{
    EdWin *w = WIN;
    if (save) { if (!save_doc(w, 0)) return 0; }
    close_window(g_cw);
    return 1;
}

static int exit_all(int save)
{
    for (int i = g_nwin - 1; i >= 0; i--) {
        g_cw = i;
        if (save) { if (g_win[i].doc->dirty && !save_doc(&g_win[i], 0)) return 0; }
        else if (!confirm_close(&g_win[i])) return 0;
        close_window(i);
    }
    g_quit = 1;
    return 1;
}


/* ------------------------------------------------------------------ new WordStar 7 features */
static int modifies(int cmd)
{
    switch (cmd) {
    case CMD_BS: case CMD_DEL: case CMD_DELWORD: case CMD_DELLINE: case CMD_DELEOL: case CMD_DELBOL: case CMD_DELTOCHAR:
    case CMD_ENTER: case CMD_SPLIT: case CMD_TAB: case CMD_UNDO: case CMD_HARD2SOFT:
    case CMD_BCOPY: case CMD_BMOVE: case CMD_BDEL: case CMD_BREAD: case CMD_BCOPYWIN: case CMD_BMOVEWIN: case CMD_CLIPFROM:
    case CMD_UPPER: case CMD_LOWER: case CMD_SENTENCE: case CMD_SORT: case CMD_REPLACE:
    case CMD_PAGEBREAK: case CMD_DATE: case CMD_TIME: case CMD_MATHRES: case CMD_MATHEXPR: case CMD_MATHDOLLAR:
    case CMD_FILENAME: case CMD_DRIVE: case CMD_DIRECTORY: case CMD_PATH: case CMD_EXTCHAR: case CMD_COMMENT: case CMD_BINDSPACE:
    case CMD_CONDBREAK: case CMD_BOLD: case CMD_ITALIC: case CMD_UNDER: case CMD_STRIKE: case CMD_SUB: case CMD_SUPER:
    case CMD_DOUBLE: case CMD_FONT: case CMD_PSTYLE: case CMD_PSTYLEPREV: case CMD_CENTER: case CMD_RIGHTALIGN:
    case CMD_RULER: case CMD_LMARGIN: case CMD_RMARGIN: case CMD_PAGELAYOUT: case CMD_HEADER: case CMD_FOOTER:
    case CMD_PAGENUM: case CMD_ALIGNSPACE: case CMD_JUSTIFY: case CMD_LINESPACE: case CMD_COLUMNS:
    case CMD_FOOTNOTE: case CMD_ENDNOTE: case CMD_ANNOT: case CMD_COLBREAK: case CMD_SOFTHYPH: case CMD_AUTOHYPH:
    case CMD_VCENTER: case CMD_PARANUM: case CMD_PNUMSTYLE: case CMD_GRAPHIC: case CMD_INDEXMARK: case CMD_INDEXENTRY:
    case CMD_TOCENTRY: case CMD_EVENODD: case CMD_ALIGN_L: case CMD_ALIGN_C: case CMD_ALIGN_R: case CMD_ALIGN_J:
    case CMD_SPELL: case CMD_SPELLREST: case CMD_SPELLWORD: case CMD_THESAURUS: case CMD_LANGUAGE: case CMD_REFORMAT: case CMD_REFORMATREST:
        return 1;
    }
    return 0;
}

static int new_note(EdWin *w, int kind)
{
    Doc *d = w->doc;
    static const wchar_t *what[] = { L"Footnotes", L"Endnotes", L"Annotations" };
    if (d->nondoc) { flash(L"%ls can't be used in nondocuments", what[kind]); return 0; }
    if (d->noteparent) { flash(L"A note can't contain another note"); return 0; }
    if (g_nwin == 2) { flash(L"Close the second window first (^OK to switch, ^KD to close)"); return 0; }
    wchar_t mark[12] = L"";
    if (kind == NOTE_ANNOT) {
        if (!input_box(L"Annotation", L"Annotation mark (for example a, *, or a word):", mark, 12) || !mark[0]) return 0;
    }
    undo_push(d, w->cur, 0);
    int idx = note_add_kind(d, NULL, 0, kind, mark);
    wch tag = NOTEBASE + (wch)idx;
    ins_wch(w, &tag, 1);
    doc_layout(d, 0);
    open_note(w, idx);
    return 1;
}

static void columns_dialog(EdWin *w)
{
    Doc *d = w->doc;
    if (d->nondoc) { flash(L"Columns are not available in nondocuments"); return; }
    Fmt f = doc_fmt_at(d, w->cur.p);
    wchar_t n[8], g[16];
    swprintf(n, 8, L"%d", f.cols); swprintf(g, 16, L"%.2f\"", f.gutter / 10.0);
    DCtl c[] = {
        { DC_EDIT, 2, 3, 6, 1, L"&Number of columns (1-8):", n, 8, NULL, 0, NULL, 0, 0, 0, 30 },
        { DC_EDIT, 3, 3, 9, 1, L"&Space between columns:", g, 16, NULL, 0, NULL, 0, 0, 0, 30 },
        { DC_LABEL, 5, 3, 52, 1, L"Text snakes from column to column on the printed" },
        { DC_LABEL, 6, 3, 52, 1, L"page. The screen shows one column's width. Changing" },
        { DC_LABEL, 7, 3, 52, 1, L"the number of columns starts a new page. Use" },
        { DC_LABEL, 8, 3, 52, 1, L"Layout > Column Break (.cb) to end a column early." },
        { DC_BUTTON, 10, 12, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 10, 28, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Newspaper-Style Columns", 5, 10, 13, 58, c, sizeof(c) / sizeof(c[0]), 0) != 1) return;
    int nc = _wtoi(n); if (nc < 1) nc = 1; if (nc > 8) nc = 8;
    int gv = f.gutter; if (!parse_measure(g, 1, &gv) || gv < 0 || gv > 29) gv = f.gutter;
    wchar_t arg[32]; swprintf(arg, 32, L"%d,%.2f\"", nc, gv / 10.0);
    set_dot(w, L"co", arg);
    flash(nc > 1 ? L"%d columns" : L"Single column", nc);
}

static void evenodd_dialog(EdWin *w)
{
    if (w->doc->nondoc) return;
    int kind = 0, pages = 0;
    wchar_t ln[4] = L"1", text[160] = L"";
    DCtl c[] = {
        { DC_RADIO, 2, 4, 12, 1, L"&Header", NULL, 0, &kind, 0 },
        { DC_RADIO, 3, 4, 12, 1, L"&Footer", NULL, 0, &kind, 1 },
        { DC_RADIO, 2, 22, 16, 1, L"&All pages", NULL, 0, &pages, 0 },
        { DC_RADIO, 3, 22, 16, 1, L"&Even pages", NULL, 0, &pages, 1 },
        { DC_RADIO, 4, 22, 16, 1, L"&Odd pages", NULL, 0, &pages, 2 },
        { DC_EDIT, 6, 3, 4, 1, L"&Line (1-5):", ln, 4, NULL, 0, NULL, 0, 0, 0, 14 },
        { DC_EDIT, 7, 3, 44, 1, L"&Text:", text, 160, NULL, 0, NULL, 0, 0, 0, 9 },
        { DC_LABEL, 9, 3, 56, 1, L"# prints the page number. Leave the text empty to" },
        { DC_LABEL, 10, 3, 56, 1, L"turn a header or footer line off from here on." },
        { DC_BUTTON, 12, 14, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 12, 30, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Headers and Footers", 4, 8, 15, 62, c, sizeof(c) / sizeof(c[0]), 6) != 1) return;
    int l = _wtoi(ln); if (l < 1) l = 1; if (l > 5) l = 5;
    wchar_t cmd[8]; swprintf(cmd, 8, L"%c%d%ls", kind ? 'f' : 'h', l, pages == 1 ? L"e" : pages == 2 ? L"o" : L"");
    set_dot(w, cmd, text);
}

static void paranum_cmd(EdWin *w)
{
    static int lastlv = 1;
    Doc *d = w->doc;
    if (d->nondoc || d->noteparent) { flash(L"Paragraph numbers can only be used in document text"); return; }
    wchar_t lv[8]; swprintf(lv, 8, L"%d", lastlv);
    if (!input_box(L"Paragraph Number", L"Level (1-8):", lv, 8)) return;
    int n = _wtoi(lv); if (n < 1) n = 1; if (n > 8) n = 8;
    lastlv = n;
    undo_push(d, w->cur, 0);
    wch t[2] = { PNUMBASE + (wch)n, ' ' };
    Pos at = w->cur;
    doc_insert(d, at, t, 2);
}

static void pnumstyle_cmd(EdWin *w)
{
    static const wchar_t *items[] = { L"Compound   1.  1.1.  1.1.1.", L"Outline    I.  A.  1.  a.  (1)  (a)", L"Restart numbering at..." };
    int r = list_pick(L"Paragraph Numbering", items, 3, 0, 40);
    if (r < 0) return;
    if (r == 0) set_dot(w, L"p#", L"compound");
    else if (r == 1) set_dot(w, L"p#", L"outline");
    else {
        wchar_t n[8] = L"1";
        if (!input_box(L"Paragraph Numbering", L"Next level-1 number:", n, 8)) return;
        if (_wtoi(n) < 1) return;
        wchar_t line[24]; swprintf(line, 24, L".p# %d", _wtoi(n));
        insert_dot_line(w, line);
    }
}

static void graphic_cmd(EdWin *w)
{
    Doc *d = w->doc;
    if (d->nondoc || d->noteparent) { flash(L"Graphics can only be placed in document text"); return; }
    wchar_t path[MAX_PATH] = L"";
    if (!file_dialog(L"Insert Graphic", path, 0, L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff")) return;
    wchar_t full[MAX_PATH]; if (!_wfullpath(full, path, MAX_PATH)) wcscpy(full, path);
    int pw = 0, ph = 0;
    if (!image_size(full, &pw, &ph)) { msgbox(L"Insert Graphic", L"That file could not be read as a picture.", L"OK"); return; }
    double wi = pw / 96.0, maxw = (doc_fmt_at(d, w->cur.p).rm - doc_fmt_at(d, w->cur.p).lm + 1) / 10.0;
    if (wi > maxw) wi = maxw;
    wchar_t ws[16]; swprintf(ws, 16, L"%.2f", wi);
    if (!input_box(L"Insert Graphic", L"Width in inches (height keeps the proportions):", ws, 16)) return;
    double gw = _wtof(ws); if (gw <= 0.1) gw = wi; if (gw > 12) gw = 12;
    double gh = gw * ph / pw;
    wchar_t line[MAX_PATH + 40];
    swprintf(line, MAX_PATH + 40, L".gr %.2f %.2f %ls", gw, gh, full);
    insert_dot_line(w, line);
    flash(L"Graphic placed; ^OP previews it");
}

static void protect_cmd(EdWin *w)
{
    Doc *d = w->doc;
    if (d->noteparent) d = d->noteparent;
    if (!d->path[0] || !file_exists(d->path)) { flash(L"Save the file before protecting it"); return; }
    DWORD a = GetFileAttributesW(d->path);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_READONLY) {
        if (ask_yn(L"Protect/Unprotect", L"This file is protected. Unprotect it so it can be changed?") != 1) return;
        if (!SetFileAttributesW(d->path, a & ~FILE_ATTRIBUTE_READONLY)) { msgbox(L"Error", L"The file's protection could not be removed.", L"OK"); return; }
        d->readonly = 0; flash(L"File unprotected");
    } else {
        if (d->dirty) {
            int r = ask_yn(L"Protect/Unprotect", L"Save your changes before protecting the file?");
            if (r < 0) return;
            if (r == 1 && !save_doc(w, 0)) return;
        }
        if (!SetFileAttributesW(d->path, a | FILE_ATTRIBUTE_READONLY)) { msgbox(L"Error", L"The file could not be protected.", L"OK"); return; }
        d->readonly = 1; flash(L"File protected");
    }
}

static void helplevel_cmd(void)
{
    static const wchar_t *items[] = {
        L"4  All menus: pull-down menu bar",
        L"3  Classic menus and the Edit Menu",
        L"2  Classic command menus only",
        L"1  No menus (commands still work)",
        L"0  No menus, no style bar",
    };
    int r = list_pick(L"Change Help Level", items, 5, 4 - S.helplevel, 40);
    if (r < 0) return;
    S.helplevel = 4 - r;
    settings_save();
    flash(L"Help level %d", S.helplevel);
}

static void gen_cmd(EdWin *w, int toc)
{
    Doc *d = w->doc;
    if (d->noteparent) d = d->noteparent;
    if (d->nondoc) { flash(L"Only documents can have an index or contents"); return; }
    wchar_t out[MAX_PATH];
    int n = generate_list(d, toc, out);
    if (n == 0) {
        msgbox(toc ? L"Table of Contents" : L"Index",
               toc ? L"No entries found. Mark contents entries with .tc lines\n(\".tc Chapter One #1\") or use Heading 1-3 paragraph styles."
                   : L"No entries found. Mark index words with ^PK (on and off)\nor add \".ix entry\" lines (\".ix main;sub\" for subentries).", L"OK");
        return;
    }
    if (n < 0) { msgbox(L"Error", L"The file could not be written.", L"OK"); return; }
    wchar_t m[MAX_PATH + 120];
    const wchar_t *nm = wcsrchr(out, '\\'); nm = nm ? nm + 1 : out;
    swprintf(m, MAX_PATH + 120, L"%d %ls written to\n%ls\n\nOpen it in a second window?", n, n == 1 ? L"entry" : L"entries", nm);
    if (g_nwin == 1 && msgbox(toc ? L"Table of Contents" : L"Index", m, L"&Yes|&No") == 0) open_second_path(out);
}

/* ---- column blocks: rectangle between the block corners, in display columns */
typedef struct { int p, s, e, pad; } RRow;
static int rect_geom(Doc *d, int *l0, int *l1, int *c0, int *c1)
{
    if (!block_ok(d)) return 0;
    doc_layout(d, 0);
    *l0 = doc_line_of(d, d->bb); *l1 = doc_line_of(d, d->bk);
    int a = line_col_of(d, &d->L[*l0], d->bb.o), b = line_col_of(d, &d->L[*l1], d->bk.o);
    *c0 = a < b ? a : b; *c1 = a < b ? b : a;
    return *c1 > *c0;
}
/* rows of the rectangle as offsets into paragraphs, bottom row last */
static int rect_rows(Doc *d, RRow **out, int *width)
{
    int l0, l1, c0, c1;
    if (!rect_geom(d, &l0, &l1, &c0, &c1)) return 0;
    RRow *r = xmalloc((l1 - l0 + 1) * sizeof(RRow)); int n = 0;
    for (int li = l0; li <= l1; li++) {
        Line *l = &d->L[li];
        if (l->type != LT_TEXT || (l->flags & LF_DOT)) continue;
        short *cols; int len = line_cols(d, l, &cols);
        int s = -1, e = -1;
        for (int k = 0; k < len; k++) {
            if (cols[k] >= c0 && cols[k] < c1) { if (s < 0) s = l->s + k; e = l->s + k + 1; }
        }
        if (s < 0) { s = e = l->s + len; }
        /* don't take the soft-wrap space at the end of a line */
        r[n].p = l->p; r[n].s = s; r[n].e = e; r[n].pad = 0; n++;
    }
    *out = r; *width = c1 - c0;
    return n;
}
static wch *rect_text(Doc *d, int *nrows, int **rowlen, int *width)
{
    RRow *r; int n = rect_rows(d, &r, width);
    if (!n) return NULL;
    int tot = 0; for (int i = 0; i < n; i++) tot += r[i].e - r[i].s + *width + 1;
    wch *t = xmalloc((tot + 1) * sizeof(wch)); int k = 0;
    *rowlen = xmalloc(n * sizeof(int));
    for (int i = 0; i < n; i++) {
        int st = k;
        Para *p = &d->p[r[i].p];
        for (int j = r[i].s; j < r[i].e; j++) { wch c = p->t[j]; if (IS_TAG(c) || IS_SPECIAL(c)) continue; t[k++] = c == '\t' ? ' ' : c; }
        (*rowlen)[i] = k - st;
    }
    *nrows = n; free(r);
    return t;
}
static void rect_delete(Doc *d)
{
    RRow *r; int w0, n = rect_rows(d, &r, &w0);
    for (int i = n - 1; i >= 0; i--) if (r[i].e > r[i].s) para_del(d, r[i].p, r[i].s, r[i].e - r[i].s);
    free(r);
    doc_touch(d);
}
/* insert rows at the cursor column, one per display line, overwriting in column replace mode */
static void rect_insert(EdWin *w, const wch *t, int nrows, const int *rowlen, int width)
{
    Doc *d = w->doc;
    doc_layout(d, 0);
    int li0 = doc_line_of(d, w->cur);
    int col = line_col_of(d, &d->L[li0], w->cur.o);
    /* enough text lines below? add empty paragraphs at the end */
    for (;;) {
        int have = 0;
        for (int li = li0; li < d->nl; li++) if (d->L[li].type == LT_TEXT && !(d->L[li].flags & LF_DOT)) have++;
        if (have >= nrows) break;
        Pos end = { d->np - 1, d->p[d->np - 1].n };
        wch nl = '\n'; doc_insert(d, end, &nl, 1);
        doc_layout(d, 0);
    }
    typedef struct { int p, off, pad, del; } Tgt;
    Tgt *tg = xmalloc(nrows * sizeof(Tgt)); int nt = 0;
    for (int li = li0; li < d->nl && nt < nrows; li++) {
        Line *l = &d->L[li];
        if (l->type != LT_TEXT || (l->flags & LF_DOT)) continue;
        short *cols; int len = line_cols(d, l, &cols);
        int lastcol = cols[len];
        int k = 0; while (k < len && cols[k] < col) k++;
        tg[nt].p = l->p; tg[nt].off = l->s + k; tg[nt].pad = col > lastcol ? col - lastcol : 0; tg[nt].del = 0;
        if (!(l->flags & LF_LAST) && k >= len) { tg[nt].off = l->e; }
        if (g_colrepl) {
            int e = k; while (e < len && cols[e] < col + rowlen[nt]) e++;
            tg[nt].del = e - k;
        }
        nt++;
    }
    /* offsets of each row's text in t */
    int *ro = xmalloc((nrows + 1) * sizeof(int)); ro[0] = 0;
    for (int i = 0; i < nrows; i++) ro[i + 1] = ro[i] + rowlen[i];
    for (int i = nt - 1; i >= 0; i--) {
        Para *p = &d->p[tg[i].p];
        int after = tg[i].off < p->n;      /* text continues after the insertion point */
        int n = rowlen[i] + tg[i].pad + ((after && !g_colrepl) ? width - rowlen[i] : 0);
        wch *b = xmalloc((n + 1) * sizeof(wch)); int k = 0;
        for (int j = 0; j < tg[i].pad; j++) b[k++] = ' ';
        for (int j = 0; j < rowlen[i]; j++) b[k++] = t[ro[i] + j];
        while (k < n) b[k++] = ' ';
        if (tg[i].del) para_del(d, tg[i].p, tg[i].off, tg[i].del);
        para_ins(d, tg[i].p, tg[i].off, b, k);
        free(b);
    }
    free(ro); free(tg);
    doc_touch(d);
}
static int colblock_op(EdWin *w, int cmd)
{
    Doc *d = w->doc;
    if (!block_ok(d)) { flash(L"No block marked"); return 1; }
    int l0, l1, c0, c1;
    if (!rect_geom(d, &l0, &l1, &c0, &c1)) { flash(L"Mark the column block's corners with ^KB and ^KK"); return 1; }
    if (cmd == CMD_BDEL) { undo_push(d, w->cur, 0); rect_delete(d); d->bkset = 0; return 1; }
    int nrows, *rl, width;
    wch *t = rect_text(d, &nrows, &rl, &width);
    if (!t) return 1;
    if (cmd == CMD_CLIPTO) {
        int tot = 0; for (int i = 0; i < nrows; i++) tot += rl[i] + 2;
        wchar_t *u = xmalloc((tot * 2 + 1) * sizeof(wchar_t)); int k = 0, o = 0;
        for (int i = 0; i < nrows; i++) {
            for (int j = 0; j < rl[i]; j++) { wch c = t[o + j]; if (c >= 0x10000) { c -= 0x10000; u[k++] = 0xD800 + (c >> 10); u[k++] = 0xDC00 + (c & 0x3FF); } else u[k++] = (wchar_t)c; }
            o += rl[i];
            if (i < nrows - 1) { u[k++] = '\r'; u[k++] = '\n'; }
        }
        u[k] = 0;
        clip_set_text(u);
        free(u); free(t); free(rl);
        flash(L"Column block copied to the Windows clipboard");
        return 1;
    }
    doc_layout(d, 0);
    int cl = doc_line_of(d, w->cur), cc = line_col_of(d, &d->L[cl], w->cur.o);
    if (cmd == CMD_BMOVE && cl >= l0 && cl <= l1 && cc >= c0 && cc < c1) { flash(L"Can't move a block into itself"); free(t); free(rl); return 1; }
    undo_push(d, w->cur, 0);
    if (cmd == CMD_BMOVE) { rect_delete(d); d->bkset = 0; }
    rect_insert(w, t, nrows, rl, width);
    free(t); free(rl);
    return 1;
}

static int exec_cmd(int cmd, int arg)
{
    EdWin *w = WIN; Doc *d = w->doc;
    Para *p = &d->p[w->cur.p];
    int keepwant = 0;
    if (cmd != CMD_UP && cmd != CMD_DOWN && cmd != CMD_TAB) d->lastcmd = (cmd == CMD_DEL || cmd == CMD_BS) ? d->lastcmd : 0;
    {
        Doc *rd = d->noteparent ? d->noteparent : d;
        if (rd->readonly && modifies(cmd)) { flash(L"File is protected"); return 0; }
    }
    if (g_colblock && (cmd == CMD_BCOPY || cmd == CMD_BMOVE || cmd == CMD_BDEL || cmd == CMD_CLIPTO)) return colblock_op(w, cmd);
    switch (cmd) {
    case CMD_UP: mv_vert(w, -1); keepwant = 1; break;
    case CMD_DOWN: mv_vert(w, 1); keepwant = 1; break;
    case CMD_LEFT: mv_left(w); break;
    case CMD_RIGHT: mv_right(w); break;
    case CMD_WORDL: mv_wordleft(w); break;
    case CMD_WORDR: mv_wordright(w); break;
    case CMD_PGUP: case CMD_PGDN: {
        doc_layout(d, 0);
        int n = w->rows - 2; if (n < 1) n = 1;
        int dir = cmd == CMD_PGUP ? -1 : 1;
        w->top += dir * n;
        if (w->top < 0) w->top = 0;
        if (w->top > d->nl - 1) w->top = d->nl - 1;
        mv_vert(w, dir * n); keepwant = 1;
        break; }
    case CMD_SCRUP: case CMD_SCRDN: {
        doc_layout(d, 0);
        int dir = cmd == CMD_SCRUP ? -1 : 1;
        int nt = w->top + dir;
        if (nt < 0 || nt > d->nl - 1) break;
        w->top = nt;
        int li = doc_line_of(d, w->cur);
        if (li < w->top) goto_line_col(w, w->top, w->wantcol);
        else if (li >= w->top + w->rows) goto_line_col(w, w->top + w->rows - 1, w->wantcol);
        keepwant = 1;
        break; }
    case CMD_SCROLLUPC: case CMD_SCROLLDNC: {
        flash(L"Scrolling... press any key to stop (0-9 = speed)");
        int speed = 3;
        for (;;) {
            exec_cmd(cmd == CMD_SCROLLUPC ? CMD_SCRUP : CMD_SCRDN, 0);
            draw_all();
            Event e;
            if (ev_get(&e, 60 * (10 - speed) + 20)) {
                if (e.type == EV_KEY && e.key >= '0' && e.key <= '9') { speed = e.key - '0'; continue; }
                break;
            }
            doc_layout(d, 0);
            if (cmd == CMD_SCROLLUPC && w->top == 0) break;
            if (cmd == CMD_SCROLLDNC && w->top >= d->nl - 1) break;
        }
        g_msg[0] = 0;
        keepwant = 1;
        break; }
    case CMD_LINEBEG: { Line *l = cur_line(w); w->cur.o = l->s; break; }
    case CMD_LINEEND: {
        Line *l = cur_line(w);
        if (l->flags & LF_LAST) w->cur.o = l->e;
        else { int k = l->e; while (k > l->s + 1 && (p->t[k - 1] == ' ')) k--; w->cur.o = k < l->e ? k : l->e - 1; }
        break; }
    case CMD_SCRTOP: goto_line_col(w, w->top, 0); break;
    case CMD_SCRBOT: { doc_layout(d, 0); int li = w->top + w->rows - 1; if (li >= d->nl) li = d->nl - 1; goto_line_col(w, li, 0); break; }
    case CMD_DOCBEG: { Pos z = { 0, 0 }; jump_to(w, z); break; }
    case CMD_DOCEND: { Pos z = { d->np - 1, d->p[d->np - 1].n }; jump_to(w, z); break; }
    case CMD_PREVPOS: { Pos t = d->prevpos; d->prevpos = w->cur; w->cur = t; clamp_cur(w); break; }
    case CMD_BLOCKBEG: if (d->bbset) jump_to(w, d->bb); else flash(L"Block beginning not marked"); break;
    case CMD_BLOCKEND: if (d->bkset) jump_to(w, d->bk); else flash(L"Block end not marked"); break;
    case CMD_LASTFIND: jump_to(w, d->lastfind); break;
    case CMD_GOTOMARK:
        if (d->markset[arg]) jump_to(w, d->mark[arg]); else flash(L"Marker %d is not set", arg);
        center_view(w);
        break;
    case CMD_SETMARK:
        if (d->markset[arg] && !pos_cmp(d->mark[arg], w->cur)) { d->markset[arg] = 0; flash(L"Marker %d removed", arg); }
        else { d->mark[arg] = w->cur; d->markset[arg] = 1; flash(L"Marker %d set", arg); }
        break;
    case CMD_GOTOPAGE: {
        wchar_t b[16] = L"";
        if (!input_box(L"Go to Page", L"Page number (or -n / +n):", b, 16) || !b[0]) break;
        doc_layout(d, 0);
        int cur = cur_line(w)->page, n;
        if (b[0] == '+' || b[0] == '-') n = cur + _wtoi(b); else n = _wtoi(b);
        if (n < 1) n = 1;
        for (int i = 0; i < d->nl; i++)
            if (d->L[i].type == LT_TEXT && (d->L[i].page >= n || i == d->nl - 1)) {
                Pos z = { d->L[i].p, d->L[i].s }; jump_to(w, z); w->top = i; break;
            }
        break; }
    case CMD_GOTOCHAR: case CMD_GOTOCHARBACK: {
        flash(L"Go to character: press the character");
        draw_all();
        int k = getkey(); g_msg[0] = 0;
        if (k == K_ESC || k >= 0x110000) break;
        wch target = k == K_ENTER ? '\n' : (wch)k;
        Pos c = w->cur;
        int dir = cmd == CMD_GOTOCHAR ? 1 : -1;
        for (;;) {
            if (dir > 0) {
                c.o++;
                if (c.o > d->p[c.p].n) { if (c.p + 1 >= d->np) break; c.p++; c.o = 0; }
            } else {
                c.o--;
                if (c.o < 0) { if (c.p == 0) break; c.p--; c.o = d->p[c.p].n; }
            }
            wch ch = c.o < d->p[c.p].n ? d->p[c.p].t[c.o] : '\n';
            if (ch == target || (target < 0x10000 && ch < 0x10000 && towlower((wint_t)ch) == towlower((wint_t)target))) { jump_to(w, c); break; }
        }
        break; }
    case CMD_GOTOFONT: case CMD_GOTOSTYLE: {
        Pos c = w->cur;
        int found = 0;
        for (int pi = c.p; pi < d->np && !found; pi++) {
            Para *q = &d->p[pi];
            if (cmd == CMD_GOTOSTYLE) { if (pi > c.p && q->style != d->p[c.p].style) { Pos z = { pi, 0 }; jump_to(w, z); found = 1; } continue; }
            for (int k = (pi == c.p ? c.o + 1 : 0); k < q->n; k++) if (IS_FONTTAG(q->t[k])) { Pos z = { pi, k }; jump_to(w, z); found = 1; break; }
        }
        if (!found) flash(cmd == CMD_GOTOFONT ? L"No more font tags" : L"No more style changes");
        break; }

    /* ---- editing ---- */
    case CMD_BS:
        if (w->cur.o > 0) {
            int o = w->cur.o - 1;
            if (!tags_shown()) while (o > 0 && IS_TAG(p->t[o])) o--;
            if (!tags_shown() && IS_TAG(p->t[o])) break;
            undo_push(d, w->cur, UK_BS);
            para_del(d, w->cur.p, o, 1);
        } else if (w->cur.p > 0) {
            if (hidden_para(d, w->cur.p - 1)) { flash(L"A hidden dot command is above; ^OD shows it"); break; }
            undo_push(d, w->cur, 0);
            doc_join(d, w->cur.p - 1);
        }
        break;
    case CMD_DEL:
        if (w->cur.o < p->n) {
            int o = w->cur.o;
            if (!tags_shown()) while (o < p->n - 1 && IS_TAG(p->t[o])) o++;
            if (!tags_shown() && IS_TAG(p->t[o])) break;
            undo_push(d, w->cur, UK_DEL);
            para_del(d, w->cur.p, o, 1);
        } else if (w->cur.p < d->np - 1) {
            if (hidden_para(d, w->cur.p + 1)) { flash(L"A hidden dot command follows; ^OD shows it"); break; }
            undo_push(d, w->cur, 0);
            doc_join(d, w->cur.p);
        }
        break;
    case CMD_DELWORD: {
        undo_push(d, w->cur, 0);
        if (w->cur.o >= p->n) { if (w->cur.p < d->np - 1 && !hidden_para(d, w->cur.p + 1)) doc_join(d, w->cur.p); break; }
        int e = w->cur.o;
        if (wch_isword(p->t[e])) { while (e < p->n && wch_isword(p->t[e])) e++; while (e < p->n && p->t[e] == ' ') e++; }
        else if (p->t[e] == ' ') { while (e < p->n && p->t[e] == ' ') e++; }
        else e++;
        para_del(d, w->cur.p, w->cur.o, e - w->cur.o);
        break; }
    case CMD_DELLINE: {
        undo_push(d, w->cur, 0);
        Line *l = cur_line(w);
        if (l->s == 0 && (l->flags & LF_LAST)) {
            if (w->cur.p < d->np - 1) { Pos a = { w->cur.p, 0 }, b = { w->cur.p + 1, 0 }; doc_delete(d, a, b); }
            else para_del(d, w->cur.p, 0, p->n);
            w->cur.o = 0;
        } else {
            para_del(d, l->p, l->s, l->e - l->s);
            w->cur.o = l->s;
        }
        keepwant = 1;
        goto_line_col(w, doc_line_of(d, w->cur), w->wantcol);
        break; }
    case CMD_DELEOL: {
        undo_push(d, w->cur, 0);
        Line *l = cur_line(w);
        int e = (l->flags & LF_LAST) ? p->n : l->e;
        para_del(d, w->cur.p, w->cur.o, e - w->cur.o);
        break; }
    case CMD_DELBOL: {
        undo_push(d, w->cur, 0);
        Line *l = cur_line(w);
        para_del(d, w->cur.p, l->s, w->cur.o - l->s);
        break; }
    case CMD_DELTOCHAR: {
        flash(L"Delete to character: press the character (Enter = end of paragraph)");
        draw_all();
        int k = getkey(); g_msg[0] = 0;
        if (k == K_ESC || k >= 0x110000) break;
        undo_push(d, w->cur, 0);
        if (k == K_ENTER) { para_del(d, w->cur.p, w->cur.o, p->n - w->cur.o); break; }
        Pos c = w->cur; int found = 0;
        for (Pos s = c; s.p < d->np && !found; s.p++, s.o = 0)
            for (int o = (s.p == c.p ? c.o + 1 : 0); o < d->p[s.p].n; o++)
                if (d->p[s.p].t[o] == (wch)k) { Pos e = { s.p, o }; doc_delete(d, c, e); found = 1; break; }
        if (!found) flash(L"Character not found");
        break; }
    case CMD_ENTER:
        if (!g_insert) {
            if (w->cur.p < d->np - 1) { w->cur.p++; w->cur.o = 0; }
            else { undo_push(d, w->cur, 0); Pos e = { w->cur.p, p->n }; doc_split(d, e); w->cur.p = d->np - 1; w->cur.o = 0; }
            break;
        }
        undo_push(d, w->cur, 0);
        {
            int st = p->style;
            doc_split(d, w->cur);
            if (st >= 1 && st <= 4) d->p[w->cur.p].style = 0;
        }
        break;
    case CMD_SPLIT: { undo_push(d, w->cur, 0); Pos save = w->cur; doc_split(d, w->cur); w->cur = save; break; }
    case CMD_TAB:
        if (!g_insert && !d->nondoc) {
            Line *l = cur_line(w);
            int col = line_col_of(d, l, w->cur.o) + 1;
            Fmt f = doc_fmt_at(d, w->cur.p);
            w->wantcol = tab_next(&f, col) - 1;
            goto_line_col(w, doc_line_of(d, w->cur), w->wantcol);
            keepwant = 1;
            break;
        }
        type_char(w, '\t');
        break;
    case CMD_INSERT: g_insert = !g_insert; break;
    case CMD_UNDO:
        if (!undo_pop(d, &w->cur)) flash(L"Nothing to undo");
        break;
    case CMD_REFORMAT: {
        if (w->cur.p < d->np - 1) { w->cur.p++; w->cur.o = 0; } else w->cur.o = p->n;
        break; }
    case CMD_REFORMATREST: flash(L"Document aligned"); { Pos z = { d->np - 1, d->p[d->np - 1].n }; jump_to(w, z); } break;
    case CMD_REPAINT: InvalidateRect(g_hwnd, NULL, FALSE); break;

    /* ---- blocks ---- */
    case CMD_BB: g_prevbb = d->bb; g_prevbk = d->bk; g_prevset = d->bbset && d->bkset;
        d->bb = w->cur; d->bbset = 1; d->bhide = 0; break;
    case CMD_BK: g_prevbb = d->bb; g_prevbk = d->bk; g_prevset = d->bbset && d->bkset;
        d->bk = w->cur; d->bkset = 1; d->bhide = 0; break;
    case CMD_BHIDE: d->bhide = !d->bhide; break;
    case CMD_BPREV: if (g_prevset) { Pos a = d->bb, b = d->bk; d->bb = g_prevbb; d->bk = g_prevbk; g_prevbb = a; g_prevbk = b; d->bbset = d->bkset = 1; d->bhide = 0; } break;
    case CMD_BCOPY: case CMD_BMOVE: {
        if (!block_ok(d)) { flash(L"No block marked"); break; }
        if (cmd == CMD_BMOVE && pos_cmp(w->cur, d->bb) > 0 && pos_cmp(w->cur, d->bk) < 0) { flash(L"Can't move a block into itself"); break; }
        undo_push(d, w->cur, 0);
        int n; wch *s = block_text(d, &n);
        if (cmd == CMD_BCOPY) notes_dup(d, d, s, n);
        int sp0 = d->bb.p;
        uint8_t *sty = xmalloc(d->bk.p - d->bb.p + 2);
        for (int i = d->bb.p; i <= d->bk.p; i++) sty[i - d->bb.p] = d->p[i].style;
        int nsty = d->bk.p - d->bb.p + 1;
        if (cmd == CMD_BMOVE) doc_delete(d, d->bb, d->bk);
        Pos start = w->cur;
        Pos end = doc_insert(d, start, s, n);
        for (int i = 1; i < nsty && start.p + i < d->np; i++) d->p[start.p + i].style = sty[i];
        (void)sp0;
        free(sty); free(s);
        d->bb = start; d->bk = end; d->bbset = d->bkset = 1; d->bhide = 0;
        w->cur = start;
        break; }
    case CMD_BDEL:
        if (!block_ok(d)) { flash(L"No block marked"); break; }
        undo_push(d, w->cur, 0);
        doc_delete(d, d->bb, d->bk);
        d->bkset = 0;
        break;
    case CMD_BCOPYWIN: case CMD_BMOVEWIN: {
        if (g_nwin < 2) { flash(L"Only one window is open"); break; }
        EdWin *o = &g_win[1 - g_cw]; Doc *od = o->doc;
        if (!block_ok(od)) { flash(L"No block marked in the other window"); break; }
        if (od == d) { exec_cmd(cmd == CMD_BCOPYWIN ? CMD_BCOPY : CMD_BMOVE, 0); break; }
        undo_push(d, w->cur, 0);
        int n; wch *s = doc_extract(od, od->bb, od->bk, &n);
        if (d->noteparent) { int k = 0; for (int i = 0; i < n; i++) if (!IS_NOTE(s[i])) s[k++] = s[i]; n = k; }
        else notes_dup(d, od, s, n);
        if (cmd == CMD_BMOVEWIN) { undo_push(od, o->cur, 0); doc_delete(od, od->bb, od->bk); od->bkset = 0; }
        Pos start = w->cur;
        Pos end = doc_insert(d, start, s, n);
        free(s);
        d->bb = start; d->bk = end; d->bbset = d->bkset = 1; d->bhide = 0;
        w->cur = start;
        break; }
    case CMD_CLIPTO: clip_to(w); break;
    case CMD_CLIPFROM: clip_from(w); break;
    case CMD_BWRITE: {
        if (!block_ok(d)) { flash(L"No block marked"); break; }
        wchar_t path[MAX_PATH] = L"";
        if (!file_dialog(L"Copy Block to Another File", path, 1, L"*.*")) break;
        if (file_exists(path)) {
            wchar_t q[MAX_PATH + 64]; swprintf(q, MAX_PATH + 64, L"%ls exists. Overwrite?", path);
            if (ask_yn(L"Write Block", q) != 1) break;
        }
        wchar_t ext[16]; path_ext(path, ext, 16);
        int ok = (!_wcsicmp(ext, L"rtf") && !d->nondoc) ? save_rtf(d, path, &d->bb, &d->bk) : save_text(d, path, &d->bb, &d->bk);
        flash(ok ? L"Block written to %ls" : L"Couldn't write %ls", path);
        break; }
    case CMD_BREAD: {
        wchar_t path[MAX_PATH] = L"";
        if (!file_dialog(L"Insert File", path, 0, S.filespec)) break;
        Doc *tmp = doc_new();
        if (!load_file(tmp, path, d->nondoc)) { doc_free(tmp); flash(L"Couldn't read %ls", path); break; }
        insert_doc_contents(w, tmp);
        doc_free(tmp);
        break; }
    case CMD_COLBLOCK: g_colblock = !g_colblock; if (!g_colblock) g_colrepl = 0;
        flash(g_colblock ? L"Column block mode on" : L"Column block mode off"); break;
    case CMD_COLREPLACE:
        if (!g_colblock) { flash(L"Turn on ^KN column blocks first"); break; }
        g_colrepl = !g_colrepl; flash(g_colrepl ? L"Column replace mode on" : L"Column replace mode off"); break;
    case CMD_UPPER: block_case(w, 0); break;
    case CMD_LOWER: block_case(w, 1); break;
    case CMD_SENTENCE: block_case(w, 2); break;
    case CMD_WORDCOUNT: word_count(w); break;
    case CMD_BLOCKMATH: block_math(w); break;
    case CMD_SORT: sort_block(w); break;

    /* ---- files ---- */
    case CMD_SAVE: save_doc(w, 0); break;
    case CMD_SAVEAS: save_doc(w, 1); break;
    case CMD_EXPORTTXT: {
        wchar_t path[MAX_PATH]; wcscpy(path, d->path);
        wchar_t *dot = wcsrchr(path, '.'); if (dot && dot > wcsrchr(path, '\\')) *dot = 0;
        wcscat(path, L".txt");
        if (!file_dialog(L"Save as Text File", path, 1, L"*.txt")) break;
        flash(save_text(d, path, NULL, NULL) ? L"Text copy saved as %ls" : L"Couldn't save %ls", path);
        break; }
    case CMD_SAVECLOSE:
        if (d->noteparent) { close_note_window(1); flash(L"Footnote updated"); return 1; }
        close_note_window(1);
        close_current(1); return 1;
    case CMD_SAVEEXIT: exit_all(1); return 1;
    case CMD_ABANDON:
        if (d->noteparent) {
            if (d->dirty && ask_yn(L"Footnote", L"Discard the changes to this footnote?") != 1) break;
            close_note_window(0);
            return 1;
        }
        if (d->dirty && d->refs <= 1) {
            if (ask_yn(L"Close", L"Abandon changes to this document?") != 1) break;
        }
        close_note_window(1);
        close_window(g_cw);
        return 1;
    case CMD_EXIT: exit_all(0); return 1;
    case CMD_PRINT: if (d->noteparent) note_writeback(d); print_document(d->noteparent ? d->noteparent : d); break;
    case CMD_PREVIEW:
        if (d->noteparent) { note_writeback(d); doc_layout(d->noteparent, 0); preview_document(d->noteparent, 1); break; }
        doc_layout(d, 0); preview_document(d, cur_line(w)->page); break;
    case CMD_OPENSWITCH: open_second(); break;
    case CMD_WINSIZE: {
        if (g_nwin < 2) { flash(L"Only one window is open"); break; }
        wchar_t b[8]; swprintf(b, 8, L"%d", g_win[g_cw].rows);
        if (!input_box(L"Window Size", L"Number of lines for this window:", b, 8)) break;
        int n = _wtoi(b);
        int total = g_win[0].rows + g_win[1].rows;
        if (n < 2) n = 2;
        if (n > total - 2) n = total - 2;
        int r0 = g_cw == 0 ? n : total - n;
        g_winsplit = r0 + (S.show_ruler ? 1 : 0);
        break; }
    case CMD_FCOPY: fileop_copy(); break;
    case CMD_FDELETE: fileop_delete(); break;
    case CMD_FRENAME: fileop_rename(); break;
    case CMD_CHDIR: fileop_chdir(); break;
    case CMD_MKDIR: { wchar_t made[MAX_PATH]; if (fileop_mkdir(made)) flash(L"Directory %ls made", made); break; }
    case CMD_RUNDOS: fileop_rundos(); break;
    case CMD_STATUS: status_info(w); break;

    /* ---- find ---- */
    case CMD_FIND: find_dialog(0); run_find(w, F.global); break;
    case CMD_REPLACE: find_dialog(1); run_find(w, F.global); break;
    case CMD_FINDNEXT:
        if (!F.valid) { flash(L"No previous find"); break; }
        if (!F.isrepl && !F.back) { Pos t = w->cur; mv_right(w); run_find(w, 0); if (!pos_cmp(w->cur, t)) {} }
        else run_find(w, 0);
        break;

    /* ---- view ---- */
    case CMD_TAGS:
        S.show_tags = !S.show_tags; doc_touch(d); if (g_nwin == 2) doc_touch(g_win[1 - g_cw].doc);
        flash(S.show_tags ? L"Command tags and dot commands shown" : (S.dots_always ? L"Command tags hidden" : L"Command tags and dot commands hidden"));
        break;
    case CMD_SCREENSET: screen_settings_dialog(); for (int i = 0; i < g_nwin; i++) doc_touch(g_win[i].doc); break;

    /* ---- insert ---- */
    case CMD_PAGEBREAK:
        if (d->nondoc) break;
        if (w->cur.o > 0) { undo_push(d, w->cur, 0); doc_split(d, w->cur); }
        insert_dot_line(w, L".pa");
        break;
    case CMD_CONDBREAK: {
        if (d->nondoc) break;
        wchar_t b[8] = L"4";
        if (!input_box(L"Keep Lines Together", L"Keep this many lines together on a page:", b, 8)) break;
        wchar_t l[20]; swprintf(l, 20, L".cp %d", _wtoi(b));
        insert_dot_line(w, l);
        break; }
    case CMD_DATE: case CMD_TIME: case CMD_MATHRES: case CMD_MATHEXPR: case CMD_MATHDOLLAR:
    case CMD_FILENAME: case CMD_DRIVE: case CMD_DIRECTORY: case CMD_PATH:
        insert_value(w, cmd); break;
    case CMD_EXTCHAR: { wch c = extchar_dialog(); if (c) type_char(w, c); break; }
    case CMD_BINDSPACE: type_char(w, d->nondoc ? ' ' : 0xA0); break;
    case CMD_COMMENT:
        if (d->nondoc) break;
        insert_dot_line(w, L".. ");
        if (!S.show_tags && !S.dots_always) { S.show_tags = 1; doc_touch(d); }
        w->cur.p--; w->cur.o = 3;
        break;

    /* ---- style ---- */
    case CMD_BOLD: case CMD_ITALIC: case CMD_UNDER: case CMD_STRIKE: case CMD_SUB: case CMD_SUPER: case CMD_DOUBLE: {
        if (d->nondoc) { flash(L"Print controls are not used in nondocuments"); break; }
        static const wch tg[] = { TG_BOLD, TG_ITALIC, TG_UNDER, TG_STRIKE, TG_SUB, TG_SUPER, TG_DOUBLE };
        wch t = tg[cmd - CMD_BOLD];
        undo_push(d, w->cur, 0);
        if (block_vis(d) && w->cur.p >= 0 && (!pos_cmp(w->cur, d->bb) || !pos_cmp(w->cur, d->bk))) {
            /* cursor at block edge: wrap the whole block */
            Pos a = d->bb, b = d->bk;
            doc_insert(d, b, &t, 1);
            doc_insert(d, a, &t, 1);
            break;
        }
        ins_wch(w, &t, 1);
        break; }
    case CMD_FONT: if (d->nondoc) break; font_dialog(w); break;
    case CMD_PSTYLE: pstyle_dialog(w); break;
    case CMD_PSTYLEPREV: if (!d->nondoc) { undo_push(d, w->cur, 0); p->style = 0; d->dirty = 1; doc_touch(d); } break;

    /* ---- layout ---- */
    case CMD_CENTER: center_line(w, 0); break;
    case CMD_RIGHTALIGN: center_line(w, 1); break;
    case CMD_RULER: if (!d->nondoc) margins_dialog(w, 0); break;
    case CMD_LMARGIN: if (!d->nondoc) margins_dialog(w, 1); break;
    case CMD_RMARGIN: if (!d->nondoc) margins_dialog(w, 2); break;
    case CMD_PAGELAYOUT: if (!d->nondoc) page_dialog(w); break;
    case CMD_HEADER: if (!d->nondoc) hf_dialog(w, 0); break;
    case CMD_FOOTER: if (!d->nondoc) hf_dialog(w, 1); break;
    case CMD_PAGENUM: if (!d->nondoc) pagenum_dialog(w); break;
    case CMD_ALIGNSPACE: case CMD_LINESPACE: if (!d->nondoc) align_dialog(w); break;
    case CMD_JUSTIFY: {
        if (d->nondoc) break;
        Fmt f = doc_fmt_at(d, w->cur.p);
        set_dot(w, L"oj", f.align == 'J' ? L"off" : L"on");
        flash(f.align == 'J' ? L"Justification off" : L"Justification on");
        break; }
    case CMD_ALIGN_L: case CMD_ALIGN_C: case CMD_ALIGN_R: case CMD_ALIGN_J: {
        if (d->nondoc) break;
        static const wchar_t *v[] = { L"off", L"c", L"r", L"on" };
        set_dot(w, L"oj", v[cmd - CMD_ALIGN_L]);
        break; }
    case CMD_WORDWRAP: {
        if (d->nondoc) break;
        Fmt f = doc_fmt_at(d, w->cur.p);
        set_dot(w, L"aw", f.ww ? L"off" : L"on");
        flash(f.ww ? L"Word wrap off" : L"Word wrap on");
        break; }
    case CMD_COLUMNS: columns_dialog(w); break;
    case CMD_COLBREAK:
        if (d->nondoc) break;
        if (doc_fmt_at(d, w->cur.p).cols < 2) { flash(L"Set columns first (^OU)"); break; }
        insert_dot_line(w, L".cb"); break;
    case CMD_EVENODD: evenodd_dialog(w); break;
    case CMD_ENDNOTE: if (new_note(w, NOTE_END)) return 0; break;
    case CMD_ANNOT: if (new_note(w, NOTE_ANNOT)) return 0; break;
    case CMD_PROTECT: protect_cmd(w); break;
    case CMD_SOFTHYPH: { if (d->nondoc) break; undo_push(d, w->cur, 0); wch h = SHY; ins_wch(w, &h, 1); break; }
    case CMD_AUTOHYPH: {
        if (d->nondoc || d->noteparent) break;
        int on = doc_fmt_at(d, w->cur.p).hy;
        set_dot(w, L"hy", on ? L"off" : L"on");
        flash(on ? L"Hyphenation off" : L"Hyphenation on");
        break; }
    case CMD_VCENTER:
        if (d->nondoc || d->noteparent) break;
        insert_dot_line(w, L".vc");
        flash(L"Page centered vertically"); break;
    case CMD_PARANUM: paranum_cmd(w); break;
    case CMD_PNUMSTYLE: if (!d->nondoc && !d->noteparent) pnumstyle_cmd(w); break;
    case CMD_GRAPHIC: graphic_cmd(w); break;
    case CMD_HELPLEVEL: helplevel_cmd(); break;
    case CMD_INDEXMARK: {
        if (d->nondoc) break;
        undo_push(d, w->cur, 0);
        wch t = TG_INDEX; ins_wch(w, &t, 1);
        if (!tags_shown()) flash(L"Index mark (^OD shows <K>)");
        break; }
    case CMD_INDEXENTRY: case CMD_TOCENTRY: {
        if (d->nondoc || d->noteparent) break;
        wchar_t t[160] = L"";
        if (!input_box(cmd == CMD_TOCENTRY ? L"Table of Contents Entry" : L"Index Entry",
                       cmd == CMD_TOCENTRY ? L"Entry text (add #2 or #3 for a lower level):" : L"Entry (main;sub for a subentry):", t, 160) || !t[0]) break;
        wchar_t line[180]; swprintf(line, 180, L".%ls %ls", cmd == CMD_TOCENTRY ? L"tc" : L"ix", t);
        insert_dot_line(w, line); break; }
    case CMD_GENINDEX: gen_cmd(w, 0); break;
    case CMD_GENTOC: gen_cmd(w, 1); break;

    /* ---- utilities / help ---- */
    case CMD_CALC: calc_cmd(); break;
    case CMD_SPELL: spell_run(w, 1); break;
    case CMD_SPELLREST: spell_run(w, 0); break;
    case CMD_SPELLWORD: spell_word_cmd(w); break;
    case CMD_SPELLTYPE: spell_type_cmd(w); break;
    case CMD_THESAURUS: thesaurus_cmd(w); break;
    case CMD_LANGUAGE: language_cmd(w); break;
    case CMD_FOOTNOTE: {
        if (d->nondoc) { flash(L"Footnotes can't be used in nondocuments"); break; }
        if (d->noteparent) { flash(L"A footnote can't contain another footnote"); break; }
        if (g_nwin == 2) { flash(L"Close the second window first (^OK to switch, ^KD to close)"); break; }
        undo_push(d, w->cur, 0);
        int idx = note_add(d, NULL, 0);
        wch tag = NOTEBASE + (wch)idx;
        ins_wch(w, &tag, 1);
        doc_layout(d, 0);
        open_note(w, idx);
        return 0; }
    case CMD_EDITNOTE: {
        if (d->noteparent) { flash(L"You are already editing a footnote"); break; }
        Pos at; int idx = note_at(d, w->cur, 1, &at);
        if (idx < 0 || idx >= d->nnotes) { flash(L"No footnote at or after the cursor"); break; }
        jump_to(w, at);
        doc_layout(d, 0);
        open_note(w, idx);
        return 0; }
    case CMD_GOTONOTE: {
        Pos from = w->cur; from.o++;
        if (from.o > d->p[from.p].n) { if (from.p + 1 < d->np) { from.p++; from.o = 0; } }
        Pos at; int idx = note_at(d, from, 1, &at);
        if (idx < 0) flash(L"No more footnotes"); else { jump_to(w, at); center_view(w); }
        break; }
    case CMD_NOTEPREFIX: {
        flash(L"^ON:  F footnote  E endnote  A annotation  C comment  D edit note  G go to next note");
        draw_all();
        int k = getkey(); g_msg[0] = 0;
        int K = k < 0x10000 ? towupper((wint_t)k) : 0;
        if (k >= 1 && k <= 26) K = 'A' + k - 1;
        if (K == 'F') return exec_cmd(CMD_FOOTNOTE, 0);
        if (K == 'D') return exec_cmd(CMD_EDITNOTE, 0);
        if (K == 'G') return exec_cmd(CMD_GOTONOTE, 0);
        if (K == 'C') return exec_cmd(CMD_COMMENT, 0);
        if (K == 'E') return exec_cmd(CMD_ENDNOTE, 0);
        if (K == 'A') return exec_cmd(CMD_ANNOT, 0);
        break; }
    case CMD_DOCDEFAULTS:
        if (doc_defaults_dialog()) {
            for (int i = 0; i < g_nwin; i++) { fmt_default(&g_win[i].doc->base); doc_touch(g_win[i].doc); }
            flash(L"Document defaults saved");
        }
        break;
    case CMD_HELP: show_help(L"edit"); break;
    case CMD_KEYS: show_help(L"keys"); break;
    case CMD_DOTHELP: show_help(L"dot"); break;
    case CMD_ABOUT: about_box(); break;
    default: break;
    }
    if (g_nwin && g_cw < g_nwin) {
        clamp_cur(&g_win[g_cw]);
        if (!keepwant) set_wantcol(&g_win[g_cw]);
    }
    return 0;
}

static int key_to_cmd(int k, int *arg)
{
    *arg = 0;
    switch (k) {
    case K_UP: case CTRL('E'): return CMD_UP;
    case K_DOWN: case CTRL('X'): return CMD_DOWN;
    case K_LEFT: case CTRL('S'): return CMD_LEFT;
    case K_RIGHT: case CTRL('D'): return CMD_RIGHT;
    case K_CLEFT: case CTRL('A'): return CMD_WORDL;
    case K_CRIGHT: case CTRL('F'): return CMD_WORDR;
    case K_PGUP: case CTRL('R'): return CMD_PGUP;
    case K_PGDN: case CTRL('C'): return CMD_PGDN;
    case K_CPGUP: case K_CUP: case CTRL('W'): return CMD_SCRUP;
    case K_CPGDN: case K_CDOWN: case CTRL('Z'): return CMD_SCRDN;
    case K_HOME: return CMD_LINEBEG;
    case K_END: return CMD_LINEEND;
    case K_CHOME: return CMD_DOCBEG;
    case K_CEND: return CMD_DOCEND;
    case K_BS: return CMD_BS;
    case K_DEL: case CTRL('G'): return CMD_DEL;
    case CTRL('T'): case K_F6: case K_CDEL: return CMD_DELWORD;
    case CTRL('Y'): case K_F5: return CMD_DELLINE;
    case K_ENTER: return CMD_ENTER;
    case CTRL('N'): return CMD_SPLIT;
    case K_TAB: return CMD_TAB;
    case K_INS: case CTRL('V'): return CMD_INSERT;
    case CTRL('U'): case K_F2: return CMD_UNDO;
    case CTRL('B'): case K_F7: return CMD_REFORMAT;
    case CTRL('L'): return CMD_FINDNEXT;
    case 0x1C: return CMD_REPAINT;
    case K_F1: case CTRL('J'): return CMD_HELP;
    case K_F3: return CMD_UNDER;
    case K_F4: return CMD_BOLD;
    case K_F9: return CMD_SAVE;
    case K_F10: return CMD_SAVECLOSE;
    case K_SF1: return CMD_TAGS;
    case K_SF9: return CMD_PRINT;
    case K_SINS: return CMD_CLIPFROM;
    case K_CINS: return CMD_CLIPTO;
    }
    return 0;
}

int edit_document(Doc *d)
{
    g_nwin = 1; g_cw = 0;
    memset(&g_win[0], 0, sizeof(EdWin));
    g_win[0].doc = d; d->refs++;
    d->readonly = is_protected(d->path);
    doc_register_pos(d, &g_win[0].cur);
    g_insert = S.insert;
    g_quit = 0;
    g_msg[0] = 0;
    if (d->fmt == FMT_WS) flash(L"WordStar file converted; it will be saved as RTF");
    while (g_nwin > 0) {
        draw_all();
        Event e;
        ev_get(&e, -1);
        if (e.type == EV_KEY || (e.type == EV_MOUSE && e.mact == MB_DOWN)) g_msg[0] = 0;
        if (e.type == EV_CLOSE) { exit_all(0); continue; }
        if (e.type == EV_MOUSE) {
            if (e.mact == MB_WHEEL) {
                for (int i = 0; i < abs(e.wheel); i++) exec_cmd(e.wheel < 0 ? CMD_SCRUP : CMD_SCRDN, 0);
                continue;
            }
            if (e.mr == 1 && e.mact == MB_DOWN && S.helplevel >= 4) {
                int mi = menubar_hit(g_editmenus, g_neditmenus, e.mc);
                if (mi >= 0) { int arg = 0; int cmd = pulldown(g_editmenus, g_neditmenus, mi, &arg); if (cmd) exec_cmd(cmd, arg); }
                continue;
            }
            if (e.mr == 0 && e.mact == MB_DBL && e.mc <= 2) { exec_cmd(CMD_SAVECLOSE, 0); continue; }
            if (e.mr == g_stylerow) { mouse_stylebar(&e); continue; }
            if (e.mr == g_statusrow && e.mact == MB_DOWN) { exec_cmd(CMD_GOTOPAGE, 0); continue; }
            if (e.mr == g_seprow && e.mact == MB_DOWN) { g_cw = 1; continue; }
            mouse_text(&e);
            continue;
        }
        if (e.type != EV_KEY) continue;
        int k = e.key, arg = 0, cmd = 0;
        /* shift+arrows extend a block (a convenience beyond WordStar) */
        if (k == K_SUP || k == K_SDOWN || k == K_SLEFT || k == K_SRIGHT || k == K_SHOME || k == K_SEND) {
            Doc *dd = DOC; EdWin *w = WIN;
            int fresh = !(block_vis(dd) && (!pos_cmp(w->cur, dd->bb) || !pos_cmp(w->cur, dd->bk)));
            if (fresh) g_anchor = w->cur;
            else g_anchor = !pos_cmp(w->cur, dd->bb) ? dd->bk : dd->bb;
            int c2 = k == K_SUP ? CMD_UP : k == K_SDOWN ? CMD_DOWN : k == K_SLEFT ? CMD_LEFT : k == K_SRIGHT ? CMD_RIGHT : k == K_SHOME ? CMD_LINEBEG : CMD_LINEEND;
            exec_cmd(c2, 0);
            if (pos_cmp(w->cur, g_anchor) < 0) { dd->bb = w->cur; dd->bk = g_anchor; } else { dd->bb = g_anchor; dd->bk = w->cur; }
            dd->bbset = dd->bkset = 1; dd->bhide = 0;
            continue;
        }
        if (k >= K_ALT && k < K_ALT + 0x100) {
            int ch = k - K_ALT, mi = -1;
            for (int i = 0; i < g_neditmenus; i++) {
                const wchar_t *amp = wcschr(g_editmenus[i].label, '&');
                if (amp && towupper(amp[1]) == ch) mi = i;
            }
            if (mi >= 0) { cmd = pulldown(g_editmenus, g_neditmenus, mi, &arg); if (cmd) exec_cmd(cmd, arg); }
            continue;
        }
        if (k == K_F10 && 0) continue;
        if (k == CTRL('K')) cmd = classic_prefix('K', &arg);
        else if (k == CTRL('Q')) cmd = classic_prefix('Q', &arg);
        else if (k == CTRL('O')) cmd = classic_prefix('O', &arg);
        else if (k == CTRL('P')) cmd = classic_prefix('P', &arg);
        else if (k == K_CTRLM) cmd = classic_prefix('M', &arg);
        else cmd = key_to_cmd(k, &arg);
        if (cmd) { exec_cmd(cmd, arg); continue; }
        if (k == K_ESC) continue;
        if ((k >= 32 && k < 0x110000 && k != 127)) {
            Doc *rd = DOC->noteparent ? DOC->noteparent : DOC;
            if (rd->readonly) { flash(L"File is protected"); continue; }
            if (k == '.' && WIN->cur.o == 0 && !DOC->nondoc && !DOC->noteparent && !S.show_tags && !S.dots_always) {
                S.show_tags = 1;      /* show dot commands while one is typed */
                flash(L"Dot commands shown (^OD hides)");
            }
            type_char(WIN, (wch)k);
            set_wantcol(WIN);
        }
    }
    return g_quit;
}
