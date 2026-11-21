/*
 * ScoopStar 7.0 - ui.c
 * Dialog boxes, message boxes, file selection, help, the Opening screen,
 * file operations, screen settings, extended characters and the calculator.
 */
#include "scoopstar.h"
#include "cmds.h"
#include <math.h>
#include <shellapi.h>

double g_lastmath; wchar_t g_lastexpr[256]; int g_havemath;

/* ------------------------------------------------------------------ small helpers */
static int hot_of(const wchar_t *s)
{
    const wchar_t *p = s ? wcschr(s, '&') : NULL;
    return p && p[1] ? towupper(p[1]) : 0;
}
static int hlen(const wchar_t *s) { int n = 0; for (; s && *s; s++) if (*s != '&') n++; return n; }

static void dlg_frame(const wchar_t *title, int r, int c, int h, int w)
{
    scr_rect(r, c, h, w, ' ', COL[C_DLG]);
    scr_box(r, c, h, w, COL[C_DLG], 0);
    scr_fill(r, c, w, ' ', COL[C_DLGTITLE]);
    int tl = (int)wcslen(title);
    scr_puts(r, c + (w - tl) / 2, title, COL[C_DLGTITLE]);
    scr_shadow(r, c, h, w);
}

/* ------------------------------------------------------------------ dialog framework */
static int is_focusable(DCtl *c) { return c->type != DC_LABEL; }

static void draw_ctl(int br, int bc, DCtl *c, int focused, int *curr, int *curc, int epos, int escroll)
{
    int r = br + c->r, col = bc + c->c;
    uint8_t at = COL[C_DLG], hot = COL[C_DLGHOT];
    switch (c->type) {
    case DC_LABEL:
        scr_putsn(r, col, c->text, c->w, at);
        break;
    case DC_EDIT: {
        scr_puthot(r, col, c->text, at, hot);
        int fc = col + c->lcol;
        uint8_t fa = focused ? COL[C_DLGFOCUS] : COL[C_DLGFIELD];
        scr_fill(r, fc, c->w, ' ', fa);
        int len = (int)wcslen(c->buf);
        for (int i = 0; i < c->w && escroll + i < len; i++) scr_put(r, fc + i, c->buf[escroll + i], fa);
        if (focused) { *curr = r; *curc = fc + epos - escroll; }
        break; }
    case DC_CHECK:
        scr_puts(r, col, *c->val ? L"[X] " : L"[ ] ", at);
        scr_puthot(r, col + 4, c->text, at, hot);
        if (focused) { *curr = r; *curc = col + 1; }
        break;
    case DC_RADIO:
        scr_puts(r, col, *c->val == c->radioval ? L"\x25C6 " : L"o ", at);
        scr_puthot(r, col + 2, c->text, at, hot);
        if (focused) { *curr = r; *curc = col; }
        break;
    case DC_BUTTON: {
        uint8_t ba = focused ? COL[C_DLGBTNF] : COL[C_DLGBTN];
        scr_put(r, col, 0x2592, COL[C_DLG]);
        scr_fill(r, col + 1, c->w - 2, ' ', ba);
        int tl = hlen(c->text);
        scr_puthot(r, col + 1 + (c->w - 2 - tl) / 2, c->text, ba, focused ? ba : hot);
        scr_put(r, col + c->w - 1, 0x2592, COL[C_DLG]);
        if (focused) { *curr = -1; }
        break; }
    case DC_LIST: {
        scr_box(r, col, c->h + 2, c->w, at, 0);
        if (c->text) scr_puthot(r, col + 2, c->text, at, hot);
        int sel = *c->val;
        if (sel < c->ltop) c->ltop = sel;
        if (sel >= c->ltop + c->h) c->ltop = sel - c->h + 1;
        for (int i = 0; i < c->h; i++) {
            int ix = c->ltop + i;
            uint8_t la = (ix == sel) ? (focused ? COL[C_DLGFOCUS] : COL[C_FILESEL]) : at;
            scr_fill(r + 1 + i, col + 1, c->w - 2, ' ', la);
            if (ix < c->nitems) scr_putsn(r + 1 + i, col + 2, c->items[ix], c->w - 4, la);
        }
        if (focused) { *curr = -1; }
        break; }
    }
}

int dialog_run(const wchar_t *title, int r, int c, int h, int w, DCtl *ctl, int n, int focus)
{
    Cell *save = xmalloc(sizeof(scr));
    scr_save(save);
    if (r + h > SR) r = SR - h;
    if (r < 0) r = 0;
    if (c + w > SC) c = SC - w;
    if (c < 0) c = 0;
    while (focus < n && !is_focusable(&ctl[focus])) focus++;
    if (focus >= n) focus = 0;
    int epos = 0, escroll = 0, fresh = 1;
    wchar_t orig[512] = L"";
    if (ctl[focus].type == DC_EDIT) { epos = (int)wcslen(ctl[focus].buf); wcsncpy(orig, ctl[focus].buf, 511); }
    int result = 0;
    for (;;) {
        DCtl *f = &ctl[focus];
        if (f->type == DC_EDIT) {
            if (epos < escroll) escroll = epos;
            if (epos >= escroll + f->w) escroll = epos - f->w + 1;
        }
        dlg_frame(title, r, c, h, w);
        int cr = -1, cc = 0;
        for (int i = 0; i < n; i++) draw_ctl(r, c, &ctl[i], i == focus, &cr, &cc, epos, i == focus ? escroll : 0);
        if (cr >= 0) scr_cursor(cr, cc, 1, 0); else scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) { result = 0; break; }
        int newfocus = -1, activate = 0;
        if (e.type == EV_MOUSE) {
            if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
            int mr = e.mr - r, mc = e.mc - c;
            if (mr < 0 || mr >= h || mc < 0 || mc >= w) continue;
            for (int i = 0; i < n; i++) {
                DCtl *k = &ctl[i];
                if (!is_focusable(k)) continue;
                int x0 = k->c, x1 = k->c + (k->type == DC_EDIT ? k->lcol + k->w : k->type == DC_BUTTON ? k->w : k->type == DC_LIST ? k->w : hlen(k->text) + 4);
                int y0 = k->r, y1 = k->r + (k->type == DC_LIST ? k->h + 2 : 1);
                if (mr >= y0 && mr < y1 && mc >= x0 && mc < x1) {
                    newfocus = i;
                    if (k->type == DC_LIST && mr > y0 && mr < y1 - 1) {
                        int ix = k->ltop + mr - y0 - 1;
                        if (ix < k->nitems) *k->val = ix;
                        if (e.mact == MB_DBL) { result = 1; goto done; }
                    } else if (k->type == DC_EDIT) {
                        int pos = escroll + mc - k->c - k->lcol;
                        int len = (int)wcslen(k->buf);
                        if (i != focus) { escroll = 0; }
                        epos = pos < 0 ? 0 : pos > len ? len : pos;
                        fresh = 0;
                        if (i != focus) wcsncpy(orig, k->buf, 511);
                        focus = i;
                        newfocus = -1;
                    } else activate = 1;
                    break;
                }
            }
        } else if (e.type == EV_KEY) {
            int k = e.key;
            if (k == K_ESC || k == CTRL('U')) { result = 0; break; }
            if (k == K_F10 || k == CTRL('K')) { result = 1; break; }
            if (k == K_TAB || k == K_BTAB) {
                int dir = k == K_TAB ? 1 : -1, i = focus;
                do { i = (i + dir + n) % n; } while (!is_focusable(&ctl[i]));
                newfocus = i;
            } else if (k == K_ENTER) {
                if (f->type == DC_BUTTON) { result = f->id; break; }
                if (f->type == DC_CHECK && 0) { *f->val = !*f->val; continue; }
                result = 1; break;
            } else if (f->type == DC_EDIT) {
                int len = (int)wcslen(f->buf);
                if (k == K_LEFT || k == CTRL('S')) { if (epos > 0) epos--; fresh = 0; }
                else if (k == K_RIGHT || k == CTRL('D')) { if (epos < len) epos++; fresh = 0; }
                else if (k == K_HOME) { epos = 0; fresh = 0; }
                else if (k == K_END) { epos = len; fresh = 0; }
                else if (k == K_BS || k == CTRL('H')) { if (fresh) { f->buf[0] = 0; epos = 0; } else if (epos > 0) { memmove(f->buf + epos - 1, f->buf + epos, (len - epos + 1) * sizeof(wchar_t)); epos--; } fresh = 0; }
                else if (k == K_DEL || k == CTRL('G')) { if (epos < len) memmove(f->buf + epos, f->buf + epos + 1, (len - epos) * sizeof(wchar_t)); fresh = 0; }
                else if (k == CTRL('Y')) { f->buf[0] = 0; epos = 0; fresh = 0; }
                else if (k == CTRL('R')) { wcsncpy(f->buf, orig, f->bufsz - 1); f->buf[f->bufsz - 1] = 0; epos = (int)wcslen(f->buf); fresh = 0; }
                else if (k == K_UP || k == K_DOWN) {
                    int dir = k == K_DOWN ? 1 : -1, i = focus;
                    do { i = (i + dir + n) % n; } while (!is_focusable(&ctl[i]));
                    newfocus = i;
                }
                else if (k >= K_ALT && k < K_ALT + 0x100) {
                    for (int i = 0; i < n; i++) if (is_focusable(&ctl[i]) && hot_of(ctl[i].text) == k - K_ALT) { newfocus = i; activate = ctl[i].type != DC_EDIT; }
                }
                else if (k >= 32 && k < 0x10000) {
                    if (fresh) { f->buf[0] = 0; epos = 0; len = 0; }
                    fresh = 0;
                    if (len < f->bufsz - 1) {
                        memmove(f->buf + epos + 1, f->buf + epos, (len - epos + 1) * sizeof(wchar_t));
                        f->buf[epos++] = (wchar_t)k;
                    }
                }
            } else if (f->type == DC_LIST) {
                int *v = f->val;
                if (k == K_UP || k == CTRL('E')) { if (*v > 0) (*v)--; }
                else if (k == K_DOWN || k == CTRL('X')) { if (*v < f->nitems - 1) (*v)++; }
                else if (k == K_PGUP) { *v -= f->h; if (*v < 0) *v = 0; }
                else if (k == K_PGDN) { *v += f->h; if (*v > f->nitems - 1) *v = f->nitems - 1; }
                else if (k == K_HOME) *v = 0;
                else if (k == K_END) *v = f->nitems - 1;
                else if (k >= 32 && k < 0x10000) {
                    for (int i = 1; i <= f->nitems; i++) {
                        int ix = (*v + i) % f->nitems;
                        if (towupper(f->items[ix][0]) == towupper((wint_t)k)) { *v = ix; break; }
                    }
                }
            } else {
                if (k == ' ' && (f->type == DC_CHECK)) *f->val = !*f->val;
                else if (k == ' ' && f->type == DC_RADIO) *f->val = f->radioval;
                else if (k == ' ' && f->type == DC_BUTTON) { result = f->id; break; }
                else if ((k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT) ) {
                    int dir = (k == K_DOWN || k == K_RIGHT) ? 1 : -1, i = focus;
                    if (f->type == DC_RADIO) {
                        /* move within the radio group */
                        int j = focus;
                        for (int t = 0; t < n; t++) {
                            j = (j + dir + n) % n;
                            if (ctl[j].type == DC_RADIO && ctl[j].val == f->val) { newfocus = j; *f->val = ctl[j].radioval; break; }
                        }
                    } else {
                        do { i = (i + dir + n) % n; } while (!is_focusable(&ctl[i]));
                        newfocus = i;
                    }
                } else {
                    int K = (k >= K_ALT && k < K_ALT + 0x100) ? k - K_ALT : (k < 0x10000 ? (int)towupper((wint_t)k) : 0);
                    for (int i = 0; i < n; i++) if (is_focusable(&ctl[i]) && hot_of(ctl[i].text) == K && K) { newfocus = i; activate = ctl[i].type != DC_EDIT && ctl[i].type != DC_LIST; break; }
                }
            }
        }
        if (newfocus >= 0) {
            focus = newfocus;
            if (ctl[focus].type == DC_EDIT) { epos = (int)wcslen(ctl[focus].buf); escroll = 0; fresh = 1; wcsncpy(orig, ctl[focus].buf, 511); }
            if (activate) {
                DCtl *a = &ctl[focus];
                if (a->type == DC_CHECK) *a->val = !*a->val;
                else if (a->type == DC_RADIO) *a->val = a->radioval;
                else if (a->type == DC_BUTTON) { result = a->id; break; }
            }
        }
    }
done:
    scr_restore(save);
    free(save);
    return result;
}

/* ------------------------------------------------------------------ message boxes */
/* breaks lines that are too wide for the screen (at a backslash or space where possible) */
static wchar_t *wrap_text(const wchar_t *msg, int maxw)
{
    size_t n = wcslen(msg);
    wchar_t *out = xmalloc((n * 2 + 2) * sizeof(wchar_t));
    int k = 0, col = 0, lastbrk = -1;
    for (size_t i = 0; i < n; i++) {
        wchar_t c = msg[i];
        out[k++] = c;
        if (c == '\n') { col = 0; lastbrk = -1; continue; }
        col++;
        if (c == '\\' || c == ' ') lastbrk = k;
        if (col >= maxw) {
            if (lastbrk > 0 && k - lastbrk < maxw - 10) {
                /* move the tail after the break point to a new line, indented */
                int tail = k - lastbrk;
                memmove(out + lastbrk + 1 + 6, out + lastbrk, tail * sizeof(wchar_t));
                out[lastbrk] = '\n';
                for (int q = 0; q < 6; q++) out[lastbrk + 1 + q] = ' ';
                k += 7; col = tail + 6;
            } else { out[k++] = '\n'; col = 0; }
            lastbrk = -1;
        }
    }
    out[k] = 0;
    return out;
}

int msgbox(const wchar_t *title, const wchar_t *msg0, const wchar_t *buttons)
{
    wchar_t *msg = wrap_text(msg0, SC - 12);
    /* split message into lines */
    const wchar_t *lines[40]; int lens[40], nl = 0, maxw = (int)wcslen(title) + 4;
    const wchar_t *s = msg;
    while (*s && nl < 40) {
        const wchar_t *e = wcschr(s, '\n');
        int len = e ? (int)(e - s) : (int)wcslen(s);
        lines[nl] = s; lens[nl] = len; nl++;
        if (len > maxw) maxw = len;
        if (!e) break;
        s = e + 1;
    }
    wchar_t bt[8][24]; int nb = 0;
    const wchar_t *b = buttons;
    while (*b && nb < 8) {
        const wchar_t *e = wcschr(b, '|');
        int len = e ? (int)(e - b) : (int)wcslen(b);
        if (len > 22) len = 22;
        wcsncpy(bt[nb], b, len); bt[nb][len] = 0; nb++;
        if (!e) break;
        b = e + 1;
    }
    int bw = 0; for (int i = 0; i < nb; i++) bw += hlen(bt[i]) + 6;
    if (bw > maxw) maxw = bw;
    int w = maxw + 6; if (w > SC - 2) w = SC - 2;
    int h = nl + 5; if (h > SR - 2) h = SR - 2;
    int r = (SR - h) / 2, c = (SC - w) / 2;
    Cell *save = xmalloc(sizeof(scr));
    scr_save(save);
    int sel = 0, result = -1;
    for (;;) {
        dlg_frame(title, r, c, h, w);
        for (int i = 0; i < nl && i < h - 5; i++) scr_putsn(r + 2 + i, c + 3, lines[i], lens[i] < w - 4 ? lens[i] : w - 4, COL[C_DLG]);
        int x = c + (w - bw) / 2 + 1, by = r + h - 2;
        int bx[8];
        for (int i = 0; i < nb; i++) {
            int bwid = hlen(bt[i]) + 4;
            bx[i] = x;
            uint8_t ba = i == sel ? COL[C_DLGBTNF] : COL[C_DLGBTN];
            scr_put(by, x, 0x2592, COL[C_DLG]);
            scr_fill(by, x + 1, bwid - 2, ' ', ba);
            scr_puthot(by, x + 2, bt[i], ba, i == sel ? ba : COL[C_DLGHOT]);
            scr_put(by, x + bwid - 1, 0x2592, COL[C_DLG]);
            x += bwid + 2;
        }
        scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) { result = -1; break; }
        if (e.type == EV_MOUSE && e.mact == MB_DOWN) {
            if (e.mr == by) for (int i = 0; i < nb; i++) if (e.mc >= bx[i] && e.mc < bx[i] + hlen(bt[i]) + 4) { result = i; goto out; }
            continue;
        }
        if (e.type != EV_KEY) continue;
        int k = e.key;
        if (k == K_ESC) { result = -1; break; }
        if (k == K_ENTER || k == K_F10) { result = sel; break; }
        if (k == K_LEFT || k == K_BTAB || k == CTRL('S')) sel = (sel + nb - 1) % nb;
        else if (k == K_RIGHT || k == K_TAB || k == CTRL('D')) sel = (sel + 1) % nb;
        else {
            int K = (k >= K_ALT && k < K_ALT + 0x100) ? k - K_ALT : (k < 0x10000 ? (int)towupper((wint_t)k) : 0);
            for (int i = 0; i < nb; i++) if (hot_of(bt[i]) == K && K) { result = i; goto out; }
        }
    }
out:
    scr_restore(save);
    free(save);
    free(msg);
    return result;
}

void message(const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    va_list ap; va_start(ap, fmt); vswprintf(buf, 1024, fmt, ap); va_end(ap);
    msgbox(L"ScoopStar", buf, L"OK");
}

int ask_yn(const wchar_t *title, const wchar_t *q)
{
    int r = msgbox(title, q, L"&Yes|&No");
    return r == 0 ? 1 : r == 1 ? 0 : -1;
}

int input_box(const wchar_t *title, const wchar_t *label, wchar_t *buf, int bufsz)
{
    wchar_t *tmp = xmalloc(bufsz * sizeof(wchar_t));
    wcscpy(tmp, buf);
    int lw = (int)wcslen(label);
    int w = lw + 6; if (w < 60) w = 60; if (w > SC - 4) w = SC - 4;
    DCtl c[] = {
        { DC_LABEL, 2, 3, w - 6, 1, label },
        { DC_EDIT, 3, 3, w - 6, 1, L"", tmp, bufsz, NULL, 0, NULL, 0, 0, 0, 0 },
        { DC_BUTTON, 5, w / 2 - 12, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 5, w / 2 + 2, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    int r = dialog_run(title, (SR - 7) / 2 - 2, (SC - w) / 2, 7, w, c, 4, 1);
    if (r == 1) wcscpy(buf, tmp);
    free(tmp);
    return r == 1;
}

int list_pick(const wchar_t *title, const wchar_t **items, int n, int sel, int width)
{
    int h = n < SR - 8 ? n : SR - 8;
    int w = width + 6;
    if (w < (int)wcslen(title) + 6) w = (int)wcslen(title) + 6;
    int r = (SR - h - 2) / 2, c = (SC - w) / 2, top = 0;
    if (sel < 0 || sel >= n) sel = 0;
    Cell *save = xmalloc(sizeof(scr)); scr_save(save);
    int result = -1;
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + h) top = sel - h + 1;
        dlg_frame(title, r, c, h + 2, w);
        for (int i = 0; i < h; i++) {
            int ix = top + i;
            uint8_t at = ix == sel ? COL[C_DLGFOCUS] : COL[C_DLG];
            scr_fill(r + 1 + i, c + 1, w - 2, ' ', at);
            if (ix < n) scr_putsn(r + 1 + i, c + 3, items[ix], w - 5, at);
        }
        if (top > 0) scr_put(r + 1, c + w - 1, 0x25B2, COL[C_DLG]);
        if (top + h < n) scr_put(r + h, c + w - 1, 0x25BC, COL[C_DLG]);
        scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) break;
        if (e.type == EV_MOUSE) {
            if (e.mact == MB_WHEEL) { sel += e.wheel; if (sel < 0) sel = 0; if (sel >= n) sel = n - 1; continue; }
            if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
            if (e.mr > r && e.mr <= r + h && e.mc > c && e.mc < c + w - 1) {
                int ix = top + e.mr - r - 1;
                if (ix < n) { if (ix == sel || e.mact == MB_DBL) { result = ix; break; } sel = ix; }
            } else if (e.mact == MB_DOWN) break;
            continue;
        }
        if (e.type != EV_KEY) continue;
        int k = e.key;
        if (k == K_ESC) break;
        if (k == K_ENTER || k == K_F10) { result = sel; break; }
        if (k == K_UP || k == CTRL('E')) { if (sel > 0) sel--; }
        else if (k == K_DOWN || k == CTRL('X')) { if (sel < n - 1) sel++; }
        else if (k == K_PGUP || k == CTRL('R')) { sel -= h; if (sel < 0) sel = 0; }
        else if (k == K_PGDN || k == CTRL('C')) { sel += h; if (sel >= n) sel = n - 1; }
        else if (k == K_HOME) sel = 0;
        else if (k == K_END) sel = n - 1;
        else if (k >= 32 && k < 0x10000) {
            for (int i = 1; i <= n; i++) { int ix = (sel + i) % n; if (towupper(items[ix][0]) == towupper((wint_t)k)) { sel = ix; break; } }
        }
    }
    scr_restore(save); free(save);
    return result;
}

/* ------------------------------------------------------------------ directory listing */
typedef struct { wchar_t name[MAX_PATH]; int isdir; unsigned long long size; } FEnt;

/* simple case-insensitive wildcard match (* and ?) */
static int PathMatchSpecW_(const wchar_t *s, const wchar_t *p)
{
    if (!wcscmp(p, L"*.*") || !wcscmp(p, L"*")) return 1;
    const wchar_t *star = NULL, *ss = NULL;
    while (*s) {
        if (*p == '?' || towlower(*p) == towlower(*s)) { s++; p++; continue; }
        if (*p == '*') { star = p++; ss = s; continue; }
        if (star) { p = star + 1; s = ++ss; continue; }
        return 0;
    }
    while (*p == '*') p++;
    return !*p;
}

static int fent_cmp(const void *a, const void *b)
{
    const FEnt *x = a, *y = b;
    if (x->isdir != y->isdir) return y->isdir - x->isdir;
    if (!wcscmp(x->name, L"..")) return -1;
    if (!wcscmp(y->name, L"..")) return 1;
    return _wcsicmp(x->name, y->name);
}

static int spec_match(const wchar_t *name, const wchar_t *spec)
{
    /* spec may be a list separated by ; or space */
    wchar_t buf[128]; wcsncpy(buf, spec, 127); buf[127] = 0;
    wchar_t *ctx, *tok = wcstok_s(buf, L"; ,", &ctx);
    if (!tok) return 1;
    while (tok) {
        if (PathMatchSpecW_(name, tok)) return 1;
        tok = wcstok_s(NULL, L"; ,", &ctx);
    }
    return 0;
}

static int list_dir(FEnt **out, const wchar_t *spec)
{
    int n = 0, cap = 64;
    FEnt *v = xmalloc(cap * sizeof(FEnt));
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".")) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!isdir && !spec_match(fd.cFileName, spec)) continue;
            if (n >= cap) { cap *= 2; v = xrealloc(v, cap * sizeof(FEnt)); }
            wcsncpy(v[n].name, fd.cFileName, MAX_PATH - 1); v[n].name[MAX_PATH - 1] = 0;
            v[n].isdir = isdir;
            v[n].size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    /* make sure ".." exists unless at a root */
    wchar_t cd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cd);
    int hasup = 0; for (int i = 0; i < n; i++) if (!wcscmp(v[i].name, L"..")) hasup = 1;
    if (!hasup && wcslen(cd) > 3) {
        if (n >= cap) { cap *= 2; v = xrealloc(v, cap * sizeof(FEnt)); }
        wcscpy(v[n].name, L".."); v[n].isdir = 1; v[n].size = 0; n++;
    }
    qsort(v, n, sizeof(FEnt), fent_cmp);
    *out = v;
    return n;
}

static void size_str(unsigned long long sz, wchar_t *b)
{
    if (sz < 1000) swprintf(b, 8, L".%dk", (int)((sz + 99) / 100) > 9 ? 9 : (int)((sz + 99) / 100));
    else if (sz < 10000) swprintf(b, 8, L"%.1fk", sz / 1000.0);
    else if (sz < 1000000) swprintf(b, 8, L"%dk", (int)(sz / 1000));
    else if (sz < 10000000) swprintf(b, 8, L"%.1fM", sz / 1000000.0);
    else swprintf(b, 8, L"%dM", (int)(sz / 1000000));
}

/* draws a 4-column directory listing; returns number of rows used */
static void draw_dir(int r0, int nrows, FEnt *v, int n, int sel, int top, int focused)
{
    int colw = SC / 4;
    for (int i = 0; i < nrows; i++) scr_fill(r0 + i, 0, SC, ' ', COL[C_FILES]);
    for (int i = 0; i < nrows * 4; i++) {
        int ix = top + i;
        if (ix >= n) break;
        int row = i / 4, col = (i % 4) * colw;
        uint8_t at = (ix == sel && focused) ? COL[C_FILESEL] : COL[C_FILES];
        wchar_t nm[64]; int nw = colw - 7;
        int len = (int)wcslen(v[ix].name);
        if (len > nw) { wcsncpy(nm, v[ix].name, nw - 1); nm[nw - 1] = '~'; nm[nw] = 0; }
        else wcscpy(nm, v[ix].name);
        scr_fill(r0 + row, col, colw - 1, ' ', at);
        scr_puts(r0 + row, col, nm, at);
        if (v[ix].isdir) scr_put(r0 + row, col + colw - 3, '\\', at);
        else { wchar_t sz[8]; size_str(v[ix].size, sz); scr_puts(r0 + row, col + colw - 1 - (int)wcslen(sz) - 1, sz, at); }
        if ((i % 4) < 3) scr_put(r0 + row, col + colw - 1, 0x2502, COL[C_FILES]);
    }
}

int file_exists(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void path_ext(const wchar_t *path, wchar_t *ext, int n)
{
    const wchar_t *s = wcsrchr(path, '\\'), *d = wcsrchr(path, '.');
    if (d && (!s || d > s)) { wcsncpy(ext, d + 1, n - 1); ext[n - 1] = 0; }
    else ext[0] = 0;
}

int file_dialog(const wchar_t *title, wchar_t *path, int forsave, const wchar_t *spec)
{
    (void)forsave;
    wchar_t name[MAX_PATH];
    const wchar_t *bs = wcsrchr(path, '\\');
    wcscpy(name, path);
    if (bs) {
        /* switch to that directory if it exists */
        wchar_t dir[MAX_PATH]; int dl = (int)(bs - path);
        wcsncpy(dir, path, dl); dir[dl] = 0;
        if (dl == 2 && dir[1] == ':') wcscat(dir, L"\\");
        if (SetCurrentDirectoryW(dir)) wcscpy(name, bs + 1);
    }
    FEnt *v = NULL; int n = list_dir(&v, spec && spec[0] ? spec : L"*.*");
    int sel = 0, top = 0, focus = 0, epos = (int)wcslen(name), fresh = 1;
    int r = 3, h = SR - 6, w = SC;
    int listr = r + 6, listh = h - 7;
    Cell *save = xmalloc(sizeof(scr)); scr_save(save);
    int result = 0;
    for (;;) {
        if (sel < top) top = (sel / 4) * 4;
        if (sel >= top + listh * 4) top = (sel / 4 - listh + 1) * 4;
        dlg_frame(title, r, 0, h, w);
        wchar_t cd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cd);
        scr_puthot(r + 2, 3, L"&Filename:", COL[C_DLG], COL[C_DLGHOT]);
        int fw = SC - 30;
        uint8_t fa = focus == 0 ? COL[C_DLGFOCUS] : COL[C_DLGFIELD];
        scr_fill(r + 2, 14, fw, ' ', fa);
        int scroll = epos > fw - 1 ? epos - fw + 1 : 0;
        scr_putsn(r + 2, 14, name + scroll, fw, fa);
        int bx = SC - 13;
        scr_put(r + 2, bx, 0x2592, COL[C_DLG]); scr_puts(r + 2, bx + 1, L"   OK   ", COL[C_DLGBTN]); scr_put(r + 2, bx + 9, 0x2592, COL[C_DLG]);
        scr_put(r + 4, bx, 0x2592, COL[C_DLG]); scr_puts(r + 4, bx + 1, L" Cancel ", COL[C_DLGBTN]); scr_put(r + 4, bx + 9, 0x2592, COL[C_DLG]);
        wchar_t info[MAX_PATH + 40];
        swprintf(info, MAX_PATH + 40, L"Path: %ls", cd);
        scr_putsn(r + 4, 3, info, SC - 20, COL[C_DLG]);
        scr_puts(r + 5, 1, L"Files:", COL[C_DLGHOT]);
        scr_fill(r + 5, 8, SC - 9, 0x2500, COL[C_DLG]);
        /* list */
        int colw = SC / 4;
        for (int i = 0; i < listh; i++) scr_fill(listr + i, 1, SC - 2, ' ', COL[C_DLG]);
        for (int i = 0; i < listh * 4 && top + i < n; i++) {
            int ix = top + i, row = i / 4, col = 1 + (i % 4) * colw;
            uint8_t at = (ix == sel && focus == 1) ? COL[C_DLGFOCUS] : COL[C_DLG];
            wchar_t nm[64]; int nw = colw - 8;
            int len = (int)wcslen(v[ix].name);
            if (len > nw) { wcsncpy(nm, v[ix].name, nw - 1); nm[nw - 1] = '~'; nm[nw] = 0; } else wcscpy(nm, v[ix].name);
            scr_fill(listr + row, col, colw - 2, ' ', at);
            scr_puts(listr + row, col, nm, at);
            if (v[ix].isdir) scr_put(listr + row, col + colw - 4, '\\', at);
            else { wchar_t sz[8]; size_str(v[ix].size, sz); scr_puts(listr + row, col + colw - 2 - (int)wcslen(sz) - 1, sz, at); }
        }
        if (focus == 0) scr_cursor(r + 2, 14 + epos - scroll, 1, 0); else scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) break;
        int open_sel = 0;
        if (e.type == EV_MOUSE) {
            if (e.mact == MB_WHEEL) { sel += e.wheel * 4; if (sel < 0) sel = 0; if (sel >= n) sel = n - 1; focus = 1; continue; }
            if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
            if (e.mr == r + 2 && e.mc >= bx) { result = 1; goto accept; }
            if (e.mr == r + 4 && e.mc >= bx) break;
            if (e.mr == r + 2) { focus = 0; fresh = 0; continue; }
            if (e.mr >= listr && e.mr < listr + listh) {
                int ix = top + (e.mr - listr) * 4 + (e.mc - 1) / colw;
                if (ix < n) {
                    focus = 1;
                    if (e.mact == MB_DBL || ix == sel) open_sel = 1;
                    sel = ix;
                    if (!v[ix].isdir) { wcscpy(name, v[ix].name); epos = (int)wcslen(name); }
                }
            }
            if (!open_sel) continue;
        }
        if (e.type == EV_KEY || open_sel) {
            int k = open_sel ? K_ENTER : e.key;
            if (k == K_ESC) break;
            if (k == K_TAB || k == K_BTAB) { focus = !focus; fresh = 1; continue; }
            if (k == K_F10 || k == CTRL('K')) { result = 1; goto accept; }
            if (focus == 1) {
                if (k == K_LEFT || k == CTRL('S')) { if (sel > 0) sel--; }
                else if (k == K_RIGHT || k == CTRL('D')) { if (sel < n - 1) sel++; }
                else if (k == K_UP || k == CTRL('E')) { if (sel >= 4) sel -= 4; else { focus = 0; continue; } }
                else if (k == K_DOWN || k == CTRL('X')) { if (sel + 4 < n) sel += 4; else sel = n - 1; }
                else if (k == K_PGUP) { sel -= listh * 4; if (sel < 0) sel = 0; }
                else if (k == K_PGDN) { sel += listh * 4; if (sel >= n) sel = n - 1; }
                else if (k == K_HOME) sel = 0;
                else if (k == K_END) sel = n - 1;
                else if (k == K_ENTER) {
                    if (n == 0) continue;
                    if (v[sel].isdir) {
                        SetCurrentDirectoryW(v[sel].name);
                        free(v); n = list_dir(&v, spec && spec[0] ? spec : L"*.*"); sel = 0; top = 0;
                        continue;
                    }
                    wcscpy(name, v[sel].name); result = 1; goto accept;
                } else if (k >= 32 && k < 0x10000) {
                    for (int i = 1; i <= n; i++) { int ix = (sel + i) % n; if (towupper(v[ix].name[0]) == towupper((wint_t)k)) { sel = ix; break; } }
                }
                if (n && !v[sel].isdir) { wcscpy(name, v[sel].name); epos = (int)wcslen(name); }
                continue;
            }
            int len = (int)wcslen(name);
            if (k == K_ENTER) { result = 1; goto accept; }
            if (k == K_DOWN || k == CTRL('X')) { focus = 1; continue; }
            if (k == K_LEFT || k == CTRL('S')) { if (epos > 0) epos--; fresh = 0; }
            else if (k == K_RIGHT || k == CTRL('D')) { if (epos < len) epos++; fresh = 0; }
            else if (k == K_HOME) { epos = 0; fresh = 0; }
            else if (k == K_END) { epos = len; fresh = 0; }
            else if (k == K_BS) { if (fresh) { name[0] = 0; epos = 0; } else if (epos > 0) { memmove(name + epos - 1, name + epos, (len - epos + 1) * sizeof(wchar_t)); epos--; } fresh = 0; }
            else if (k == K_DEL || k == CTRL('G')) { if (epos < len) memmove(name + epos, name + epos + 1, (len - epos) * sizeof(wchar_t)); fresh = 0; }
            else if (k == CTRL('Y')) { name[0] = 0; epos = 0; }
            else if (k >= 32 && k < 0x10000 && len < MAX_PATH - 2) {
                if (fresh) { name[0] = 0; epos = 0; len = 0; }
                fresh = 0;
                memmove(name + epos + 1, name + epos, (len - epos + 1) * sizeof(wchar_t));
                name[epos++] = (wchar_t)k;
            }
        }
        continue;
    accept:
        if (!name[0]) { result = 0; continue; }
        {
            /* a directory or wildcard typed? */
            if (wcschr(name, '*') || wcschr(name, '?')) {
                wchar_t sp[64]; wcsncpy(sp, name, 63); sp[63] = 0;
                free(v); n = list_dir(&v, sp); sel = 0; top = 0; name[0] = 0; epos = 0; result = 0; continue;
            }
            DWORD a = GetFileAttributesW(name);
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
                SetCurrentDirectoryW(name);
                free(v); n = list_dir(&v, spec && spec[0] ? spec : L"*.*"); sel = 0; top = 0; name[0] = 0; epos = 0; result = 0; continue;
            }
            GetFullPathNameW(name, MAX_PATH, path, NULL);
            result = 1;
            break;
        }
    }
    free(v);
    scr_restore(save); free(save);
    return result;
}

/* ------------------------------------------------------------------ file operations */
static int ask_file(const wchar_t *title, wchar_t *buf)
{
    buf[0] = 0;
    return file_dialog(title, buf, 0, L"*.*");
}

void fileop_copy(void)
{
    wchar_t src[MAX_PATH], dst[MAX_PATH] = L"";
    if (!ask_file(L"Copy - file to copy", src)) return;
    if (!input_box(L"Copy", L"Name of the copy:", dst, MAX_PATH) || !dst[0]) return;
    wchar_t full[MAX_PATH]; GetFullPathNameW(dst, MAX_PATH, full, NULL);
    DWORD a = GetFileAttributesW(full);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
        const wchar_t *b = wcsrchr(src, '\\'); wcscat(full, L"\\"); wcscat(full, b ? b + 1 : src);
    }
    if (file_exists(full) && ask_yn(L"Copy", L"That file exists. Overwrite it?") != 1) return;
    if (!CopyFileW(src, full, FALSE)) message(L"Couldn't copy the file.");
}

void fileop_delete(void)
{
    wchar_t f[MAX_PATH];
    if (!ask_file(L"Delete", f)) return;
    wchar_t q[MAX_PATH + 40]; swprintf(q, MAX_PATH + 40, L"Delete %ls?", f);
    if (ask_yn(L"Delete", q) != 1) return;
    if (!DeleteFileW(f)) message(L"Couldn't delete the file.");
}

void fileop_rename(void)
{
    wchar_t f[MAX_PATH], to[MAX_PATH] = L"";
    if (!ask_file(L"Rename - file to rename", f)) return;
    const wchar_t *b = wcsrchr(f, '\\'); wcscpy(to, b ? b + 1 : f);
    if (!input_box(L"Rename", L"New name:", to, MAX_PATH) || !to[0]) return;
    if (!MoveFileW(f, to)) message(L"Couldn't rename the file.");
}

void fileop_chdir(void)
{
    wchar_t d[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, d);
    if (!input_box(L"Change Drive/Directory", L"New drive and/or directory:", d, MAX_PATH) || !d[0]) return;
    if (wcslen(d) == 2 && d[1] == ':') wcscat(d, L"\\");
    if (!SetCurrentDirectoryW(d)) message(L"Can't find that drive or directory.");
    else GetCurrentDirectoryW(MAX_PATH, S.dir);
}

void fileop_rundos(void)
{
    static wchar_t cmd[512];
    if (!input_box(L"Run DOS Command", L"Command (leave blank for a Command Prompt; type EXIT to return):", cmd, 512)) return;
    wchar_t line[700];
    if (cmd[0]) swprintf(line, 700, L"cmd.exe /c \"%ls & echo. & pause\"", cmd);
    else wcscpy(line, L"cmd.exe /k title ScoopStar - type EXIT to return");
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) { message(L"Couldn't run the command."); return; }
    CloseHandle(pi.hThread);
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_KEYDOWN || m.message == WM_CHAR || m.message == WM_SYSKEYDOWN || m.message == WM_LBUTTONDOWN) continue;
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    CloseHandle(pi.hProcess);
    ev_flush();
    SetForegroundWindow(g_hwnd);
}

/* ------------------------------------------------------------------ screen settings */
int screen_settings_dialog(void)
{
    int tags = S.show_tags, sty = S.show_stylebar, rul = S.show_ruler, sta = S.show_status, scb = S.show_scroll;
    int flg = S.show_flags, cls = S.classic_on_menus, rfx = S.realfx, ins = S.insert, bak = S.backups;
    int sch = S.scheme, fm = S.fontmode, blk = S.cursorblock, dots = S.dots_always;
    wchar_t ttf[40], rows[8], delay[8];
    wcscpy(ttf, S.ttfont); swprintf(rows, 8, L"%d", S.rows);
    swprintf(delay, 8, L"%d", S.menu_delay);
    DCtl c[] = {
        { DC_CHECK, 2, 3, 30, 1, L"&Command tags", NULL, 0, &tags },
        { DC_CHECK, 3, 3, 30, 1, L"St&yle bar", NULL, 0, &sty },
        { DC_CHECK, 4, 3, 30, 1, L"&Ruler line", NULL, 0, &rul },
        { DC_CHECK, 5, 3, 30, 1, L"Status &line", NULL, 0, &sta },
        { DC_CHECK, 6, 3, 30, 1, L"Scroll &bar", NULL, 0, &scb },
        { DC_CHECK, 7, 3, 30, 1, L"&Flag column", NULL, 0, &flg },
        { DC_CHECK, 8, 3, 34, 1, L"Classic commands on &menus", NULL, 0, &cls },
        { DC_CHECK, 9, 3, 34, 1, L"Show &underline/strikeout", NULL, 0, &rfx },
        { DC_CHECK, 10, 3, 34, 1, L"&Insert mode at start", NULL, 0, &ins },
        { DC_CHECK, 11, 3, 34, 1, L"Make .BA&K backup files", NULL, 0, &bak },
        { DC_CHECK, 12, 3, 34, 1, L"Bloc&k cursor in insert mode", NULL, 0, &blk },
        { DC_LABEL, 2, 40, 20, 1, L"Colors:" },
        { DC_RADIO, 3, 42, 22, 1, L"Blue (&WordStar)", NULL, 0, &sch, 0 },
        { DC_RADIO, 4, 42, 22, 1, L"Blac&k background", NULL, 0, &sch, 1 },
        { DC_RADIO, 5, 42, 22, 1, L"Mon&ochrome", NULL, 0, &sch, 2 },
        { DC_RADIO, 6, 42, 22, 1, L"&Amber", NULL, 0, &sch, 3 },
        { DC_RADIO, 7, 42, 22, 1, L"&Green", NULL, 0, &sch, 4 },
        { DC_LABEL, 8, 40, 20, 1, L"Screen font:" },
        { DC_RADIO, 9, 42, 22, 1, L"&VGA (heavy)", NULL, 0, &fm, 0 },
        { DC_RADIO, 10, 42, 22, 1, L"VGA (&thin)", NULL, 0, &fm, 1 },
        { DC_RADIO, 11, 42, 22, 1, L"Tr&ueType:", NULL, 0, &fm, 2 },
        { DC_EDIT, 12, 44, 18, 1, L"", ttf, 40, NULL, 0, NULL, 0, 0, 0, 0 },
        { DC_EDIT, 14, 3, 4, 1, L"Screen ro&ws (25-60):", rows, 8, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_CHECK, 15, 3, 36, 1, L"Dot commands shown with tags &off", NULL, 0, &dots },
        { DC_EDIT, 17, 3, 6, 1, L"Menu delay (ms):", delay, 8, NULL, 0, NULL, 0, 0, 0, 24 },
        { DC_BUTTON, 14, 48, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 16, 48, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Screen Settings", 2, 6, 20, 68, c, sizeof(c) / sizeof(c[0]), 0) != 1) return 0;
    S.show_tags = tags; S.show_stylebar = sty; S.show_ruler = rul; S.show_status = sta; S.show_scroll = scb;
    S.show_flags = flg; S.classic_on_menus = cls; S.realfx = rfx; S.insert = ins; S.backups = bak; S.cursorblock = blk;
    S.scheme = sch; S.fontmode = fm; wcscpy(S.ttfont, ttf[0] ? ttf : L"Consolas");
    int rr = _wtoi(rows); if (rr < 25) rr = 25; if (rr > MAXR) rr = MAXR; S.rows = rr;
    S.dots_always = dots;
    int dl = _wtoi(delay); if (dl >= 0 && dl < 10000) S.menu_delay = dl;
    set_scheme(S.scheme);
    apply_rows(S.rows);
    settings_save();
    InvalidateRect(g_hwnd, NULL, FALSE);
    return 1;
}

/* ------------------------------------------------------------------ document defaults */
int doc_defaults_dialog(void)
{
    wchar_t lm[16], rm[16], mt[16], mb[16], po[16], ls[8];
    swprintf(lm, 16, L"%d", S.lm); swprintf(rm, 16, L"%d", S.rm);
    swprintf(mt, 16, L"%.2f\"", S.def_mt / 6.0); swprintf(mb, 16, L"%.2f\"", S.def_mb / 6.0);
    swprintf(po, 16, L"%.2f\"", S.def_po / 10.0); swprintf(ls, 8, L"%d", S.def_ls);
    int al = S.def_align == 'C' ? 1 : S.def_align == 'R' ? 2 : S.def_align == 'J' ? 3 : 0;
    DCtl c[] = {
        { DC_EDIT, 2, 3, 9, 1, L"&Left margin (column):", lm, 16, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_EDIT, 3, 3, 9, 1, L"&Right margin (column):", rm, 16, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_EDIT, 4, 3, 9, 1, L"&Top margin:", mt, 16, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_EDIT, 5, 3, 9, 1, L"&Bottom margin:", mb, 16, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_EDIT, 6, 3, 9, 1, L"Page &offset (left edge):", po, 16, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_EDIT, 7, 3, 4, 1, L"Line &spacing (1-9):", ls, 8, NULL, 0, NULL, 0, 0, 0, 26 },
        { DC_LABEL, 9, 3, 20, 1, L"Alignment:" },
        { DC_RADIO, 10, 5, 18, 1, L"Flush l&eft", NULL, 0, &al, 0 },
        { DC_RADIO, 11, 5, 18, 1, L"&Centered", NULL, 0, &al, 1 },
        { DC_RADIO, 12, 5, 18, 1, L"Flush ri&ght", NULL, 0, &al, 2 },
        { DC_RADIO, 13, 5, 18, 1, L"&Justified", NULL, 0, &al, 3 },
        { DC_LABEL, 15, 3, 60, 1, L"These apply to new documents and to documents without" },
        { DC_LABEL, 16, 3, 60, 1, L"their own dot commands. Inches may be typed as 1.5\"." },
        { DC_BUTTON, 2, 48, 10, 1, L"OK", NULL, 0, NULL, 0, NULL, 0, 0, 1 },
        { DC_BUTTON, 4, 48, 10, 1, L"Cancel", NULL, 0, NULL, 0, NULL, 0, 0, 2 },
    };
    if (dialog_run(L"Document Defaults", 2, 8, 19, 64, c, sizeof(c) / sizeof(c[0]), 0) != 1) return 0;
    int v, l = S.lm, r = S.rm;
    if (parse_measure(lm, 1, &v) && v >= 1) l = wcschr(lm, '"') ? v + 1 : v;
    if (parse_measure(rm, 1, &v) && v > l + 5 && v < 250) r = v;
    if (r > l + 5) { S.lm = l; S.rm = r; S.pm = l; }
    if (parse_measure(mt, 0, &v) && v >= 1 && v < 40) S.def_mt = v;
    if (parse_measure(mb, 0, &v) && v >= 1 && v < 40) S.def_mb = v;
    if (parse_measure(po, 1, &v) && v >= 0 && v < 60) S.def_po = v;
    v = _wtoi(ls); if (v >= 1 && v <= 9) S.def_ls = v;
    S.def_align = "LCRJ"[al];
    settings_save();
    return 1;
}

/* ------------------------------------------------------------------ extended characters */
typedef struct { const wchar_t *name; wch first, last; } CharPage;
static const CharPage cpages[] = {
    { L"Latin-1 Supplement", 0xA0, 0xFF },
    { L"Latin Extended-A", 0x100, 0x17F },
    { L"Greek and Coptic", 0x370, 0x3FF },
    { L"Greek Extended (polytonic)", 0x1F00, 0x1FFF },
    { L"Hebrew", 0x591, 0x5F4 },
    { L"Punctuation", 0x2010, 0x205E },
    { L"Currency, Letterlike, Numbers", 0x20A0, 0x218F },
    { L"Arrows", 0x2190, 0x21FF },
    { L"Mathematical Operators", 0x2200, 0x22FF },
    { L"Box Drawing and Blocks", 0x2500, 0x259F },
    { L"Geometric Shapes", 0x25A0, 0x25FF },
    { L"Miscellaneous Symbols", 0x2600, 0x26FF },
    { L"Cyrillic", 0x400, 0x4FF },
    { L"Latin Extended-B", 0x180, 0x24F },
};

wch extchar_dialog(void)
{
    static int page = 0, sel = 0;
    int np = sizeof(cpages) / sizeof(cpages[0]);
    int perrow = 32, rows = 8;
    int w = perrow * 2 + 6, h = rows + 9;
    int r = 3, c = (SC - w) / 2;
    Cell *save = xmalloc(sizeof(scr)); scr_save(save);
    wch result = 0;
    for (;;) {
        const CharPage *pg = &cpages[page];
        int count = (int)(pg->last - pg->first + 1);
        if (count > perrow * rows) count = perrow * rows;
        if (sel >= count) sel = count - 1;
        if (sel < 0) sel = 0;
        dlg_frame(L"Extended Character", r, c, h, w);
        wchar_t t[80]; swprintf(t, 80, L"%ls  (PgUp/PgDn = %d of %d)", pg->name, page + 1, np);
        scr_puts(r + 1, c + 2, t, COL[C_DLGHOT]);
        for (int i = 0; i < count; i++) {
            int rr = r + 3 + i / perrow, cc = c + 3 + (i % perrow) * 2;
            wch ch = pg->first + i;
            scr_put(rr, cc, ch, i == sel ? COL[C_DLGFOCUS] : COL[C_DLG]);
        }
        wch cur = pg->first + sel;
        wchar_t info[80]; swprintf(info, 80, L"Character U+%04X:  ", (unsigned)cur);
        scr_fill(r + rows + 4, c + 2, w - 4, ' ', COL[C_DLG]);
        scr_puts(r + rows + 4, c + 3, info, COL[C_DLG]);
        scr_put(r + rows + 4, c + 3 + (int)wcslen(info), cur, COL[C_DLGFOCUS]);
        scr_puts(r + rows + 6, c + 3, L"Arrows select, Enter inserts, H types a hex code, Esc cancels", COL[C_DLG]);
        scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) break;
        if (e.type == EV_MOUSE) {
            if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
            int rr = e.mr - r - 3, cc = (e.mc - c - 3) / 2;
            if (rr >= 0 && rr < rows && cc >= 0 && cc < perrow && rr * perrow + cc < count) {
                int ix = rr * perrow + cc;
                if (ix == sel || e.mact == MB_DBL) { result = pg->first + ix; break; }
                sel = ix;
            }
            continue;
        }
        if (e.type != EV_KEY) continue;
        int k = e.key;
        if (k == K_ESC) break;
        if (k == K_ENTER) { result = cur; break; }
        if (k == K_LEFT || k == CTRL('S')) sel--;
        else if (k == K_RIGHT || k == CTRL('D')) sel++;
        else if (k == K_UP || k == CTRL('E')) { if (sel >= perrow) sel -= perrow; }
        else if (k == K_DOWN || k == CTRL('X')) { if (sel + perrow < count) sel += perrow; }
        else if (k == K_PGDN || k == CTRL('C')) { page = (page + 1) % np; sel = 0; }
        else if (k == K_PGUP || k == CTRL('R')) { page = (page + np - 1) % np; sel = 0; }
        else if (k == 'h' || k == 'H') {
            wchar_t hex[12] = L"";
            if (input_box(L"Extended Character", L"Hexadecimal character code (e.g. 03B1):", hex, 12) && hex[0]) {
                unsigned long v = wcstoul(hex, NULL, 16);
                if (v >= 32 && v < 0x110000) { result = (wch)v; break; }
            }
        }
    }
    scr_restore(save); free(save);
    return result;
}

/* ------------------------------------------------------------------ help */
static const wchar_t *help_keys =
L"CURSOR MOVEMENT                         SCROLLING\n"
L"  ^E / Up      up a line                  ^W   scroll up a line\n"
L"  ^X / Down    down a line                ^Z   scroll down a line\n"
L"  ^S / Left    left a character           ^R / PgUp   up a screen\n"
L"  ^D / Right   right a character          ^C / PgDn   down a screen\n"
L"  ^A / ^Left   left a word                ^QW / ^QZ   scroll continuously\n"
L"  ^F / ^Right  right a word\n"
L"  ^QS / Home   beginning of line        DELETING\n"
L"  ^QD / End    end of line                ^G / Del    character at cursor\n"
L"  ^QE / ^QX    top / bottom of screen     ^H / Bksp   character left\n"
L"  ^QR / ^Home  beginning of document      ^T          word\n"
L"  ^QC / ^End   end of document            ^Y          line\n"
L"  ^QI          go to page                 ^QY         to end of line\n"
L"  ^QP          previous position          ^Q Del      to start of line\n"
L"  ^Q0-9        go to marker               ^QT         to a character\n"
L"  ^QG / ^QH    go to character            ^U          undo\n"
L"\n"
L"BLOCKS                                  FILES\n"
L"  ^KB / ^KK    mark beginning / end       ^KS   save and continue\n"
L"  ^KH          hide / show block          ^KD   save and close\n"
L"  ^KC / ^KV    copy / move block          ^KX   save and exit ScoopStar\n"
L"  ^KY          delete block               ^KQ   abandon (close without saving)\n"
L"  ^KW / ^KR    write block / read file    ^KT   save as\n"
L"  ^K] / ^K[    copy to / paste from       ^KP   print\n"
L"               the Windows Clipboard      ^OP   preview\n"
L"  ^K\" ^K' ^K.  upper / lower / sentence   ^KO ^KJ ^KE  copy, delete, rename\n"
L"  ^K?  word count   ^KM  block math       ^KL   change directory\n"
L"  ^KZ  sort block   ^K0-9  set marker     ^KF   run a DOS command\n"
L"  ^KN  column block mode on/off: ^KB and ^KK mark opposite corners of a\n"
L"       rectangle; ^KC ^KV ^KY ^K] then copy, move, delete or clip it.\n"
L"  ^KI  column replace mode: copied columns write over existing text.\n"
L"  Mouse drag or Shift+arrows also mark a block.\n"
L"\n"
L"PRINT CONTROLS (^P)                     FORMATTING (^O)\n"
L"  ^PB bold        ^PY italic              ^OL / ^OR   left / right margin\n"
L"  ^PS underline   ^PX strikeout           ^OI         ruler and tabs\n"
L"  ^PT superscript ^PV subscript           ^OC / ^O]   center / right-align line\n"
L"  ^PD double      ^P= font                ^OS         alignment and spacing\n"
L"  ^P0 extended character (Greek,          ^OJ         justification on/off\n"
L"      Hebrew, symbols)                    ^OY         page layout\n"
L"  ^PO binding space                       ^O#         page numbering\n"
L"  ^P* insert a graphic                    ^OF         paragraph style\n"
L"  ^PK index mark (on / off)               ^OU         newspaper columns\n"
L"                                          ^OH / ^OE   hyphenation, soft hyphen\n"
L"                                          ^OV         center page vertically\n"
L"                                          ^OZ         paragraph number\n"
L"OTHER                                     ^OD         command tags on/off\n"
L"  ^V / Ins   insert on/off                ^OB         screen settings\n"
L"  ^N  split line    ^B  align paragraph   ^OK / ^OM   open/switch, size window\n"
L"  ^QF find   ^QA find & replace   ^L again\n"
L"  ^QM calculator    ^M@ date   ^M! time   ^M= last math result\n"
L"  ^QL spell check rest of document   ^QN check word   ^QO type a word\n"
L"  ^QJ thesaurus     .la en / .la la / .la de  spelling language\n"
L"  ^ONF footnote   ^ONE endnote   ^ONA annotation   ^ONC comment\n"
L"  ^OND edit note  ^ONG next note  (in the note window, F10 or ^KD returns)\n"
L"  Utilities > Generate Index / Table of Contents (I and T at the Opening)\n"
L"  Help > Change Help Level (J at the Opening screen)\n"
L"\n"
L"FUNCTION KEYS\n"
L"  F1 help   F2 undo   F3 underline   F4 bold   F5 delete line\n"
L"  F6 delete word   F7 align paragraph   F9 save   F10 save & close\n"
L"  Shift+F1 command tags   Alt+Enter full screen   Alt+letter pull-down menus\n"
L"\n"
L"Hold Ctrl while pressing the first key of a two-key command (^K, ^Q, ^O,\n"
L"^P, ^M). The second key may be pressed with or without Ctrl. Pause after\n"
L"the first key and a menu of the choices appears.\n";

static const wchar_t *help_dot =
L"DOT COMMANDS\n"
L"A dot command is a line that begins with a period in column 1. It does not\n"
L"print; it changes the layout from that point on. ScoopStar understands:\n"
L"\n"
L"  .lm n     left margin (column, or inches: .lm 1\")\n"
L"  .rm n     right margin (column, or inches: .rm 6.5\")\n"
L"  .pm n     paragraph margin (first-line indent)\n"
L"  .oj on    justify both margins     .oj off  flush left\n"
L"  .oj c     center                   .oj r    flush right\n"
L"  .oc on    center lines             .oc off\n"
L"  .ls n     line spacing (1-9)\n"
L"  .tb list  tab stops, e.g. .tb 6,11,21\n"
L"  .aw off   word wrap off (lines kept as typed)   .aw on\n"
L"  .pa       page break\n"
L"  .cp n     conditional page break (keep n lines together)\n"
L"  .pl n     page length in lines (6 per inch) or inches: .pl 11\"\n"
L"  .mt n     top margin          .mb n   bottom margin\n"
L"  .hm n     header margin       .fm n   footer margin\n"
L"  .po n     page offset (left edge), columns or inches\n"
L"  .he text  header line 1        .h1 - .h5   header lines 1 to 5\n"
L"  .fo text  footer line 1        .f1 - .f5   footer lines 1 to 5\n"
L"  .h1e .h1o  header line for even / odd pages only (.f1e .f1o likewise)\n"
L"            In headers and footers # prints the page number.\n"
L"  .pn n     starting page number\n"
L"  .la xx    spelling language: en (English), la (Latin), de (German)\n"
L"  .f# n     number the next footnote n     .e# n  next endnote number\n"
L"  .co n,g   n newspaper columns, g apart (.co 2,0.5\")  .co 1  back to one\n"
L"  .cb       column break\n"
L"  .vc       center this page's text vertically\n"
L"  .hy on    automatic hyphenation (English, German)   .hy off\n"
L"  .gr w h file   graphic, w by h inches (Insert > Graphic writes it)\n"
L"  .p# outline / compound / n   paragraph number style, or restart at n\n"
L"  .ix entry index entry (main;sub for a subentry)\n"
L"  .tc text #n   table of contents entry at level n\n"
L"  .op       omit page numbers    .pg   print page numbers again\n"
L"  .. text   nonprinting comment  (.ig also accepted)\n"
L"\n"
L"Standard WordStar page: 66 lines (11\"), top margin 3 lines (0.5\"),\n"
L"bottom margin 8 lines (1.33\"), page offset 0.8\", margins 1 to 65.\n"
L"Unless a footer is set or .op is used, page numbers print at the\n"
L"bottom center of each page, as in WordStar.\n";

static const wchar_t *help_edit =
L"SCOOPSTAR 7.0 HELP\n"
L"\n"
L"ScoopStar works like WordStar 7.0 for DOS. You can use the classic\n"
L"control-key commands or the pull-down menus (Alt + the highlighted letter,\n"
L"or click with the mouse).\n"
L"\n"
L"THE EDITING SCREEN\n"
L"  Title bar      the file you are editing (double-click the box at the\n"
L"                 left to save and close).\n"
L"  Menu bar       File Edit View Insert Style Layout Utilities Help.\n"
L"  Style bar      paragraph style, font, and buttons for bold, italic,\n"
L"                 underline, command tags <*>, alignment and spacing.\n"
L"  Ruler line     margins (\x251C \x2524) and tab stops (\x25BA).\n"
L"  Flag column    <  end of paragraph   P  page break   ^  past end of file\n"
L"                 :  dot command        .  comment       +  line too long\n"
L"  Status line    Insert mode, page, line, vertical position, column and\n"
L"                 horizontal position. Click it to go to a page.\n"
L"\n"
L"DOCUMENTS AND NONDOCUMENTS\n"
L"  Documents are saved as Rich Text Format (.RTF), which Microsoft Word,\n"
L"  LibreOffice and WordPad open directly. Bold, italic, fonts, margins,\n"
L"  headers, footers and page breaks carry over. ScoopStar also stores its\n"
L"  own copy of your dot commands inside the RTF file so that nothing is\n"
L"  lost when you reopen it here.\n"
L"  Nondocuments are plain text files (programs, batch files, notes) with\n"
L"  no word wrap. Open them with File > Open Nondocument (N at the Opening\n"
L"  screen).\n"
L"  WordStar files from DOS (.WS, .DOC and others) can be opened; they are\n"
L"  converted and saved as .RTF beside the original, which is left alone.\n"
L"\n"
L"COMMAND TAGS\n"
L"  Bold, italic and the other print controls are on/off codes placed in\n"
L"  your text, just as in WordStar. Press ^OD (or Shift+F1, or click <*>)\n"
L"  to see them as <B>, <I>, <U> and so on. With a block marked and the\n"
L"  cursor at either end of it, ^PB (and the other print controls) put\n"
L"  the codes around the whole block.\n"
L"\n"
L"Press PgDn for the keyboard command list and dot commands.\n"
L"\n";

void show_help(const wchar_t *topic)
{
    size_t total = wcslen(help_edit) + wcslen(help_keys) + wcslen(help_dot) + 16;
    wchar_t *all = xmalloc(total * sizeof(wchar_t));
    wcscpy(all, help_edit); wcscat(all, help_keys); wcscat(all, L"\n"); wcscat(all, help_dot);
    const wchar_t *lines[400]; int lens[400], nl = 0;
    const wchar_t *s = all;
    while (*s && nl < 400) {
        const wchar_t *e = wcschr(s, '\n');
        lines[nl] = s; lens[nl] = e ? (int)(e - s) : (int)wcslen(s); nl++;
        if (!e) break;
        s = e + 1;
    }
    int top = 0;
    if (!wcscmp(topic, L"keys")) { for (int i = 0; i < nl; i++) if (!wcsncmp(lines[i], L"CURSOR MOVEMENT", 15)) { top = i; break; } }
    if (!wcscmp(topic, L"dot")) { for (int i = 0; i < nl; i++) if (!wcsncmp(lines[i], L"DOT COMMANDS", 12)) { top = i; break; } }
    int r = 2, h = SR - 3, w = SC;
    int body = h - 3;
    Cell *save = xmalloc(sizeof(scr)); scr_save(save);
    for (;;) {
        if (top > nl - body) top = nl - body;
        if (top < 0) top = 0;
        dlg_frame(L"ScoopStar Help", r, 0, h, w);
        for (int i = 0; i < body; i++) {
            int ix = top + i;
            scr_fill(r + 1 + i, 1, w - 2, ' ', COL[C_HELP]);
            if (ix < nl) {
                int isHead = lens[ix] > 0 && iswupper(lines[ix][0]) && iswupper(lines[ix][1 < lens[ix] ? 1 : 0]);
                scr_putsn(r + 1 + i, 2, lines[ix], lens[ix] < w - 4 ? lens[ix] : w - 4, isHead ? COL[C_HELPHOT] : COL[C_HELP]);
            }
        }
        scr_fill(r + h - 2, 1, w - 2, ' ', COL[C_DLG]);
        scr_puts(r + h - 2, 2, L"PgUp/PgDn, arrows or wheel to scroll    Esc to return", COL[C_DLGHOT]);
        scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        if (e.type == EV_CLOSE) break;
        if (e.type == EV_MOUSE) { if (e.mact == MB_WHEEL) top += e.wheel; else if (e.mact == MB_RDOWN) break; continue; }
        if (e.type != EV_KEY) continue;
        int k = e.key;
        if (k == K_UP || k == CTRL('E') || k == CTRL('W')) top--;
        else if (k == K_DOWN || k == CTRL('X') || k == CTRL('Z')) top++;
        else if (k == K_PGUP || k == CTRL('R')) top -= body - 1;
        else if (k == K_PGDN || k == CTRL('C') || k == ' ') top += body - 1;
        else if (k == K_HOME || k == K_CHOME) top = 0;
        else if (k == K_END || k == K_CEND) top = nl;
        else break;
    }
    scr_restore(save); free(save); free(all);
}

void about_box(void)
{
    msgbox(L"About ScoopStar",
           L"ScoopStar 7.0\n"
           L"A WordStar 7.0 work-alike for Windows\n"
           L"\n"
           L"Part of the Scoop Suite.\n"
           L"Documents are stored as Rich Text Format (.RTF).\n"
           L"\n"
           L"WordStar is a trademark of its owners; ScoopStar is an\n"
           L"independent program and is not affiliated with them.\n"
           L"Screen font glyphs from GNU Unifont (SIL Open Font License).",
           L"OK");
}

/* ------------------------------------------------------------------ calculator */
static const wchar_t *cp;
static int cerr;
static double c_expr(void);
static void sk(void) { while (*cp == ' ' || *cp == '\t' || *cp == '\r' || *cp == '\n') cp++; }
static double c_prim(void)
{
    sk();
    if (*cp == '(') { cp++; double v = c_expr(); sk(); if (*cp == ')') cp++; else cerr = 1; return v; }
    if (*cp == '-') { cp++; return -c_prim(); }
    if (*cp == '+') { cp++; return c_prim(); }
    if (*cp == '$') cp++;
    if (iswdigit(*cp) || *cp == '.') {
        wchar_t buf[64]; int k = 0;
        while ((iswdigit(*cp) || *cp == '.' || *cp == ',') && k < 63) { if (*cp != ',') buf[k++] = *cp; cp++; }
        buf[k] = 0;
        if (*cp == 'e' || *cp == 'E') { /* exponent */
            wchar_t *e; double v = wcstod(buf, NULL);
            double ex = wcstod(cp + 1, &e); if (e > cp + 1) { cp = e; v *= pow(10, ex); }
            return v;
        }
        double v = wcstod(buf, NULL);
        sk();
        if (*cp == '%') { cp++; v /= 100; }
        return v;
    }
    if (iswalpha(*cp)) {
        wchar_t name[16]; int k = 0;
        while (iswalpha(*cp) && k < 15) name[k++] = (wchar_t)towlower(*cp++);
        name[k] = 0;
        if (!wcscmp(name, L"pi")) return 3.14159265358979323846;
        double a = c_prim();
        if (!wcscmp(name, L"sqrt")) return a >= 0 ? sqrt(a) : (cerr = 1, 0);
        if (!wcscmp(name, L"int")) return a < 0 ? ceil(a) : floor(a);
        if (!wcscmp(name, L"abs")) return fabs(a);
        if (!wcscmp(name, L"log")) return a > 0 ? log10(a) : (cerr = 1, 0);
        if (!wcscmp(name, L"ln")) return a > 0 ? log(a) : (cerr = 1, 0);
        if (!wcscmp(name, L"exp")) return exp(a);
        if (!wcscmp(name, L"sin")) return sin(a * 3.14159265358979323846 / 180);
        if (!wcscmp(name, L"cos")) return cos(a * 3.14159265358979323846 / 180);
        if (!wcscmp(name, L"tan")) return tan(a * 3.14159265358979323846 / 180);
        if (!wcscmp(name, L"atn") || !wcscmp(name, L"atan")) return atan(a) * 180 / 3.14159265358979323846;
        cerr = 1; return 0;
    }
    cerr = 1;
    return 0;
}
static double c_pow(void)
{
    double v = c_prim(); sk();
    if (*cp == '^') { cp++; double e = c_pow(); v = pow(v, e); }
    return v;
}
static double c_term(void)
{
    double v = c_pow();
    for (;;) {
        sk();
        if (*cp == '*' || *cp == 'x' || *cp == 'X') { cp++; v *= c_pow(); }
        else if (*cp == '/') { cp++; double d = c_pow(); if (d == 0) { cerr = 1; return 0; } v /= d; }
        else return v;
    }
}
static double c_expr(void)
{
    double v = c_term();
    for (;;) {
        sk();
        if (*cp == '+') { cp++; v += c_term(); }
        else if (*cp == '-') { cp++; v -= c_term(); }
        else return v;
    }
}
double calc_eval(const wchar_t *s, int *err)
{
    cp = s; cerr = 0;
    sk();
    if (!*cp) { *err = 1; return 0; }
    double v = c_expr();
    sk();
    if (*cp && *cp != '=') cerr = 1;
    *err = cerr;
    return v;
}

void fmt_number(double v, wchar_t *out, int n, int dollar)
{
    if (dollar) {
        wchar_t tmp[64]; swprintf(tmp, 64, L"%.2f", fabs(v));
        wchar_t *dot = wcschr(tmp, '.');
        int il = dot ? (int)(dot - tmp) : (int)wcslen(tmp);
        wchar_t res[80]; int k = 0;
        if (v < 0) res[k++] = '-';
        res[k++] = '$';
        for (int i = 0; i < il; i++) { res[k++] = tmp[i]; if ((il - i - 1) % 3 == 0 && i < il - 1) res[k++] = ','; }
        wcscpy(res + k, tmp + il);
        wcsncpy(out, res, n - 1); out[n - 1] = 0;
        return;
    }
    swprintf(out, n, L"%.10g", v);
}

/* ------------------------------------------------------------------ opening screen */
static void open_and_edit(const wchar_t *path, int nondoc, int isnew)
{
    Doc *d = doc_new();
    if (!isnew) {
        wchar_t full[MAX_PATH]; GetFullPathNameW(path, MAX_PATH, full, NULL);
        if (file_exists(full)) {
            if (!load_file(d, full, nondoc)) { doc_free(d); message(L"Can't open %ls", full); return; }
        } else {
            wchar_t q[MAX_PATH + 60]; swprintf(q, MAX_PATH + 60, L"Can't find %ls.\nCreate a new file?", full);
            if (ask_yn(L"Open", q) != 1) { doc_free(d); return; }
            wcscpy(d->path, full);
            d->nondoc = nondoc; d->fmt = nondoc ? FMT_TXT : FMT_RTF;
            wchar_t ext[16]; path_ext(full, ext, 16);
            if (!nondoc && !ext[0]) wcscat(d->path, L".rtf");
        }
    } else { d->nondoc = nondoc; d->fmt = nondoc ? FMT_TXT : FMT_RTF; }
    if (nondoc) d->nondoc = 1;
    edit_document(d);
}

static void opening_print(void)
{
    wchar_t path[MAX_PATH] = L"";
    if (!file_dialog(L"Print", path, 0, S.filespec)) return;
    Doc *d = doc_new();
    if (load_file(d, path, 0)) print_document(d);
    else message(L"Can't open %ls", path);
    doc_free(d);
}

void status_dialog(void)
{
    wchar_t m[600], cd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cd);
    ULARGE_INTEGER fb; GetDiskFreeSpaceExW(NULL, &fb, NULL, NULL);
    MEMORYSTATUSEX ms = { sizeof(ms) }; GlobalMemoryStatusEx(&ms);
    swprintf(m, 600, L"ScoopStar 7.0\n\nCurrent directory: %ls\nDisk space free:   %llu KB\nMemory free:       %llu KB\nFiles displayed:   %ls",
             cd, (unsigned long long)(fb.QuadPart / 1024), (unsigned long long)(ms.ullAvailPhys / 1024), S.filespec);
    msgbox(L"Status", m, L"OK");
}

static void opening_protect(void)
{
    wchar_t path[MAX_PATH] = L"";
    if (!file_dialog(L"Protect/Unprotect a File", path, 0, S.filespec) || !file_exists(path)) return;
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    int prot = (a & FILE_ATTRIBUTE_READONLY) != 0;
    wchar_t q[MAX_PATH + 80];
    swprintf(q, MAX_PATH + 80, prot ? L"%ls is protected.\nUnprotect it?" : L"Protect %ls\nso it can be read but not changed?", path);
    if (ask_yn(L"Protect/Unprotect", q) != 1) return;
    if (!SetFileAttributesW(path, prot ? (a & ~FILE_ATTRIBUTE_READONLY) : (a | FILE_ATTRIBUTE_READONLY)))
        msgbox(L"Error", L"The file's protection could not be changed.", L"OK");
}

static void opening_generate(int toc)
{
    wchar_t path[MAX_PATH] = L"";
    if (!file_dialog(toc ? L"Table of Contents for" : L"Index for", path, 0, S.filespec) || !file_exists(path)) return;
    Doc *d = doc_new();
    if (!load_file(d, path, 0)) { doc_free(d); msgbox(L"Error", L"Can't open that file.", L"OK"); return; }
    wchar_t out[MAX_PATH], m[MAX_PATH + 160];
    int n = generate_list(d, toc, out);
    doc_free(d);
    const wchar_t *nm = wcsrchr(out, '\\'); nm = nm ? nm + 1 : out;
    if (n > 0) swprintf(m, MAX_PATH + 160, L"%d %ls written to\n%ls", n, n == 1 ? L"entry" : L"entries", nm);
    else if (n == 0) swprintf(m, MAX_PATH + 160, toc ? L"No contents entries found (.tc lines or Heading styles)." : L"No index entries found (^PK marks or .ix lines).");
    else swprintf(m, MAX_PATH + 160, L"The file could not be written.");
    msgbox(toc ? L"Table of Contents" : L"Index", m, L"OK");
}

static void opening_helplevel(void)
{
    static const wchar_t *items[] = {
        L"4  All menus: pull-down menu bar",
        L"3  Classic menus and the Edit Menu",
        L"2  Classic command menus only",
        L"1  No menus (commands still work)",
        L"0  No menus, no style bar",
    };
    int r = list_pick(L"Change Help Level", items, 5, 4 - S.helplevel, 40);
    if (r >= 0) { S.helplevel = 4 - r; settings_save(); }
}

void opening_screen(void)
{
    FEnt *v = NULL; int n = 0, sel = 0, top = 0, reload = 1, showdir = 1;
    for (;;) {
        if (reload) { free(v); n = list_dir(&v, S.filespec); if (sel >= n) sel = n - 1; if (sel < 0) sel = 0; reload = 0; }
        /* draw */
        scr_clear(COL[C_OPENBG]);
        scr_fill(0, 0, SC, ' ', COL[C_TITLE]);
        scr_puts(0, (SC - 9) / 2, L"ScoopStar", COL[C_TITLE]);
        draw_menubar(g_openmenus, g_nopenmenus, -1);
        /* banner in the classic help area */
        static const wchar_t *banner[] = {
            L"   \x2584\x2580\x2580 \x2584\x2580\x2580 \x2584\x2580\x2584 \x2584\x2580\x2584 \x2588\x2580\x2584  \x2584\x2580\x2580 \x2580\x2588\x2580 \x2584\x2580\x2584 \x2588\x2580\x2584    \x2580\x2580\x2588   \x2584\x2580\x2584",
            L"    \x2580\x2584 \x2588   \x2588 \x2588 \x2588 \x2588 \x2588\x2580    \x2580\x2584  \x2588  \x2588\x2580\x2588 \x2588\x2580\x2584     \x2588    \x2588 \x2588",
            L"   \x2584\x2584\x2580  \x2580\x2580  \x2580   \x2580  \x2580    \x2584\x2584\x2580  \x2580  \x2580 \x2580 \x2580 \x2580    \x2580  \x2584  \x2580 ",
        };
        int hr = 3;
        (void)banner;
        scr_bigtext(hr, 10, 3, SC - 20, L"ScoopStar 7.0", (COL[C_OPENBG] & 0xF0) | 0x0E);
        scr_puts(hr + 4, (SC - 52) / 2, L"A WordStar 7.0 work-alike for Windows - Scoop Suite", COL[C_OPENHELP]);
        scr_puts(hr + 6, 4, L"D open a document   S new document   N open a nondocument   P print", COL[C_OPENHELP]);
        scr_puts(hr + 7, 4, L"L change directory  F files displayed  O copy  Y delete  E rename  X exit", COL[C_OPENHELP]);
        scr_puts(hr + 8, 4, L"C protect a file    I index   T table of contents   J help level", COL[C_OPENHELP]);
        int lr = hr + 10;
        wchar_t cd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cd);
        ULARGE_INTEGER fb; GetDiskFreeSpaceExW(NULL, &fb, NULL, NULL);
        wchar_t hdr[MAX_PATH + 80];
        unsigned long long mb = fb.QuadPart / 1000000ULL;
        if (mb >= 10000) swprintf(hdr, MAX_PATH + 80, L"Path: %ls      %lluG free", cd, mb / 1000);
        else swprintf(hdr, MAX_PATH + 80, L"Path: %ls      %lluM free", cd, mb);
        scr_fill(lr, 0, SC, ' ', COL[C_FILES]);
        scr_puts(lr, 0, L"Filenames:", COL[C_FILEHDR]);
        scr_putsn(lr, 18, hdr, SC - 19, COL[C_FILES]);
        int listr = lr + 1, listh = SR - listr;
        if (showdir) {
            if (sel < top) top = (sel / 4) * 4;
            if (sel >= top + listh * 4) top = (sel / 4 - listh + 1) * 4;
            draw_dir(listr, listh, v, n, sel, top, 1);
        } else scr_rect(listr, 0, listh, SC, ' ', COL[C_FILES]);
        scr_cursor(0, 0, 0, 0);

        Event e; ev_get(&e, -1);
        int cmd = 0, arg = 0;
        if (e.type == EV_CLOSE) cmd = OCMD_EXIT;
        else if (e.type == EV_MOUSE) {
            if (e.mact == MB_WHEEL) { sel += e.wheel * 4; if (sel < 0) sel = 0; if (sel >= n) sel = n - 1; continue; }
            if (e.mact != MB_DOWN && e.mact != MB_DBL) continue;
            if (e.mr == 1) { int mi = menubar_hit(g_openmenus, g_nopenmenus, e.mc); if (mi >= 0) cmd = pulldown(g_openmenus, g_nopenmenus, mi, &arg); }
            else if (e.mr >= listr && showdir) {
                int ix = top + (e.mr - listr) * 4 + e.mc / (SC / 4);
                if (ix < n) { if (e.mact == MB_DBL) { sel = ix; cmd = -1; } else sel = ix; }
            }
            if (!cmd) continue;
        } else if (e.type == EV_KEY) {
            int k = e.key;
            if (k >= K_ALT && k < K_ALT + 0x100) {
                int ch = k - K_ALT, mi = -1;
                for (int i = 0; i < g_nopenmenus; i++) { const wchar_t *a = wcschr(g_openmenus[i].label, '&'); if (a && towupper(a[1]) == ch) mi = i; }
                if (mi >= 0) cmd = pulldown(g_openmenus, g_nopenmenus, mi, &arg);
            }
            else if (k == K_LEFT || k == CTRL('S')) { if (sel > 0) sel--; }
            else if (k == K_RIGHT || k == CTRL('D')) { if (sel < n - 1) sel++; }
            else if (k == K_UP || k == CTRL('E')) { if (sel >= 4) sel -= 4; }
            else if (k == K_DOWN || k == CTRL('X')) { if (sel + 4 < n) sel += 4; }
            else if (k == K_PGUP || k == CTRL('R')) { sel -= listh * 4; if (sel < 0) sel = 0; }
            else if (k == K_PGDN || k == CTRL('C')) { sel += listh * 4; if (sel >= n) sel = n - 1; }
            else if (k == K_HOME) sel = 0;
            else if (k == K_END) sel = n - 1;
            else if (k == K_ENTER) cmd = -1;
            else if (k == K_F1) cmd = CMD_HELP;
            else if (k == K_F10) cmd = 0;
            else if (k == K_ESC) ;
            else if (k < 0x10000) {
                switch (towupper((wint_t)k)) {
                case 'D': cmd = OCMD_OPENDOC; break;
                case 'S': cmd = OCMD_NEW; break;
                case 'N': cmd = OCMD_OPENNON; break;
                case 'P': cmd = OCMD_PRINT; break;
                case 'O': cmd = OCMD_COPY; break;
                case 'Y': cmd = OCMD_DELETE; break;
                case 'E': cmd = OCMD_RENAME; break;
                case 'L': cmd = OCMD_CHDIR; break;
                case 'F': cmd = OCMD_FILESPEC; break;
                case 'R': cmd = OCMD_RUNDOS; break;
                case 'X': cmd = OCMD_EXIT; break;
                case '?': cmd = OCMD_STATUS; break;
                case 'B': cmd = OCMD_SETTINGS; break;
                case 'C': cmd = OCMD_PROTECT; break;
                case 'I': cmd = OCMD_GENINDEX; break;
                case 'T': cmd = OCMD_GENTOC; break;
                case 'J': cmd = OCMD_HELPLEVEL; break;
                }
            }
        }
        if (!cmd) continue;
        if (cmd == -1) {
            if (n == 0) continue;
            if (v[sel].isdir) { SetCurrentDirectoryW(v[sel].name); sel = 0; top = 0; reload = 1; continue; }
            open_and_edit(v[sel].name, 0, 0);
            if (g_quit) break;
            reload = 1;
            continue;
        }
        switch (cmd) {
        case OCMD_NEW: open_and_edit(NULL, 0, 1); break;
        case OCMD_OPENDOC: case OCMD_OPENNON: {
            wchar_t path[MAX_PATH] = L"";
            if (n && !v[sel].isdir) wcscpy(path, v[sel].name);
            if (file_dialog(cmd == OCMD_OPENDOC ? L"Open Document" : L"Open Nondocument", path, 0, S.filespec))
                open_and_edit(path, cmd == OCMD_OPENNON, 0);
            break; }
        case OCMD_PRINT: opening_print(); break;
        case OCMD_COPY: fileop_copy(); break;
        case OCMD_DELETE: fileop_delete(); break;
        case OCMD_RENAME: fileop_rename(); break;
        case OCMD_CHDIR: fileop_chdir(); sel = 0; top = 0; break;
        case OCMD_FILESPEC: {
            wchar_t sp[64]; wcscpy(sp, S.filespec);
            int r = msgbox(L"Change Filename Display", L"Show the list of files?", L"&Yes|&No");
            if (r == 1) { showdir = 0; break; }
            if (r < 0) break;
            showdir = 1;
            if (input_box(L"Change Filename Display", L"Files to display (e.g. *.* or *.rtf;*.txt):", sp, 64) && sp[0]) wcscpy(S.filespec, sp);
            break; }
        case OCMD_RUNDOS: fileop_rundos(); break;
        case OCMD_STATUS: status_dialog(); break;
        case OCMD_SETTINGS: screen_settings_dialog(); break;
        case OCMD_DOCDEFAULTS: doc_defaults_dialog(); break;
        case CMD_HELP: show_help(L"edit"); break;
        case CMD_KEYS: show_help(L"keys"); break;
        case CMD_ABOUT: about_box(); break;
        case OCMD_PROTECT: opening_protect(); break;
        case OCMD_GENINDEX: opening_generate(0); break;
        case OCMD_GENTOC: opening_generate(1); break;
        case OCMD_HELPLEVEL: opening_helplevel(); break;
        case OCMD_EXIT: g_quit = 1; break;
        }
        if (g_quit) break;
        reload = 1;
    }
    free(v);
}
