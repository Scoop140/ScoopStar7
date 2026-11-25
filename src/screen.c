/*
 * ScoopStar 7.0 - screen.c
 * Text-mode screen emulation on a Win32 window, event queue, settings, WinMain.
 */
#include "scoopstar.h"
#include "font8x16.h"

Cell scr[MAXR][MAXC];
int SR = 25, SC = 80;
HWND g_hwnd;
int g_mouse_down;
Settings S;

void (*g_overlay)(HDC hdc, int w, int h);
static int cur_r, cur_c, cur_vis, cur_block, blink_on = 1;
static short glyph_ix[65536];
static int scr_dirty = 1;
static int fullscreen;
static WINDOWPLACEMENT prev_wp = { sizeof(prev_wp) };


extern int g_scheme;
/* the 16 text-mode colours; the amber and green schemes turn them into shades of one phosphor */
static uint32_t rgb32(int i)
{
    static const uint32_t pal[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    uint32_t c = pal[i & 15];
    if (g_scheme == 3 || g_scheme == 4) {
        if (!c) return 0;
        double lum = (0.30 * (c >> 16) + 0.59 * ((c >> 8) & 255) + 0.11 * (c & 255)) / 255.0;
        double k = 0.30 + 0.70 * lum;
        int r = g_scheme == 3 ? 255 : 0x33, g = g_scheme == 3 ? 0xB0 : 255, b = g_scheme == 3 ? 0 : 0x33;
        return ((uint32_t)(r * k) << 16) | ((uint32_t)(g * k) << 8) | (uint32_t)(b * k);
    }
    return c;
}
uint32_t scheme_rgb(int i) { return rgb32(i); }

/* ------------------------------------------------------------------ colours */
uint8_t COL[C_NROLES];
int g_scheme;

/* role order must match enum in scoopstar.h */
static const uint8_t schemes[NSCHEMES][C_NROLES] = {
    /* 0: WordStar blue */
    { 0x70, 0x30, 0x3F, 0x0F, 0x0E,
      0x30, 0x3F, 0x0F, 0x38, 0x31,
      0x30, 0x38, 0x1F,
      0x1B, 0x10,
      0x17, 0x1F, 0x1B, 0x1A, 0x71, 0x1E, 0x1A, 0x1B, 0x13,
      0x13, 0x1B, 0x30, 0x3F,
      0x70, 0x1F, 0x74, 0x1F, 0x0F, 0x70, 0x0F, 0x08,
      0x07, 0x07, 0x17, 0x70, 0x1B,
      0x4F, 0x70, 0x74 },
    /* 1: black background */
    { 0x70, 0x70, 0x74, 0x0F, 0x0E,
      0x70, 0x74, 0x0F, 0x78, 0x71,
      0x70, 0x78, 0x1F,
      0x03, 0x00,
      0x07, 0x0F, 0x0B, 0x0A, 0x70, 0x0E, 0x0A, 0x03, 0x08,
      0x08, 0x07, 0x70, 0x7F,
      0x70, 0x1F, 0x74, 0x1F, 0x0F, 0x70, 0x0F, 0x08,
      0x07, 0x07, 0x07, 0x70, 0x0B,
      0x4F, 0x70, 0x74 },
    /* 2: monochrome */
    { 0x70, 0x70, 0x7F, 0x0F, 0x0F,
      0x70, 0x7F, 0x0F, 0x78, 0x70,
      0x70, 0x78, 0x0F,
      0x07, 0x00,
      0x07, 0x0F, 0x07, 0x07, 0x70, 0x0F, 0x07, 0x07, 0x08,
      0x08, 0x07, 0x70, 0x7F,
      0x70, 0x0F, 0x7F, 0x0F, 0x70, 0x70, 0x0F, 0x08,
      0x07, 0x07, 0x07, 0x70, 0x0F,
      0x70, 0x70, 0x7F },
    /* 3: amber and 4: green phosphor on black (shades come from rgb32) */
#define PHOSPHOR \
    { 0x70, 0x07, 0x0F, 0x70, 0x70, \
      0x07, 0x0F, 0x70, 0x08, 0x07, \
      0x07, 0x08, 0x70, \
      0x07, 0x08, \
      0x07, 0x0F, 0x07, 0x07, 0x70, 0x0F, 0x08, 0x07, 0x08, \
      0x08, 0x07, 0x70, 0x70, \
      0x07, 0x70, 0x0F, 0x8F, 0x70, 0x07, 0x70, 0x08, \
      0x07, 0x07, 0x07, 0x70, 0x0F, \
      0x70, 0x07, 0x0F }
    PHOSPHOR,
    PHOSPHOR,
};

void set_scheme(int s)
{
    if (s < 0 || s >= NSCHEMES) s = 0;
    g_scheme = s;
    memcpy(COL, schemes[s], C_NROLES);
    scr_dirty = 1;
}

/* ------------------------------------------------------------------ cells */
void scr_putfx(int r, int c, wch ch, uint8_t at, uint8_t fx)
{
    if (r < 0 || r >= SR || c < 0 || c >= SC) return;
    scr[r][c].ch = ch; scr[r][c].at = at; scr[r][c].fx = fx; scr[r][c].lay = 0;
    scr_dirty = 1;
}
void scr_setlay(int r, int c, int n, uint8_t v)
{
    if (r < 0 || r >= SR) return;
    for (int i = c; i < c + n && i < SC; i++) if (i >= 0) scr[r][i].lay = v;
    scr_dirty = 1;
}
void scr_put(int r, int c, wch ch, uint8_t at) { scr_putfx(r, c, ch, at, 0); }

int scr_putsn(int r, int c, const wchar_t *s, int n, uint8_t at)
{
    int i;
    for (i = 0; i < n && s[i]; i++) scr_put(r, c + i, s[i], at);
    return i;
}
int scr_puts(int r, int c, const wchar_t *s, uint8_t at) { return scr_putsn(r, c, s, 1 << 20, at); }

int scr_puthot(int r, int c, const wchar_t *s, uint8_t at, uint8_t hot)
{
    int x = c, h = 0;
    for (; *s; s++) {
        if (*s == '&' && s[1] && !h) { h = 1; s++; scr_put(r, x++, *s, hot); continue; }
        scr_put(r, x++, *s, at);
    }
    return x - c;
}

void scr_fill(int r, int c, int w, wch ch, uint8_t at)
{
    for (int i = 0; i < w; i++) scr_put(r, c + i, ch, at);
}
void scr_rect(int r, int c, int h, int w, wch ch, uint8_t at)
{
    for (int i = 0; i < h; i++) scr_fill(r + i, c, w, ch, at);
}
void scr_box(int r, int c, int h, int w, uint8_t at, int dbl)
{
    wch tl = dbl ? 0x2554 : 0x250C, tr = dbl ? 0x2557 : 0x2510, bl = dbl ? 0x255A : 0x2514,
        br = dbl ? 0x255D : 0x2518, hz = dbl ? 0x2550 : 0x2500, vt = dbl ? 0x2551 : 0x2502;
    scr_put(r, c, tl, at); scr_put(r, c + w - 1, tr, at);
    scr_put(r + h - 1, c, bl, at); scr_put(r + h - 1, c + w - 1, br, at);
    for (int i = 1; i < w - 1; i++) { scr_put(r, c + i, hz, at); scr_put(r + h - 1, c + i, hz, at); }
    for (int i = 1; i < h - 1; i++) { scr_put(r + i, c, vt, at); scr_put(r + i, c + w - 1, vt, at); }
}
void scr_shadow(int r, int c, int h, int w)
{
    for (int i = 1; i <= h; i++) {
        for (int k = 0; k < 2; k++) {
            int rr = r + i, cc = c + w + k;
            if (rr < SR && cc < SC && i < h) { scr[rr][cc].at = COL[C_DLGSHADE]; scr[rr][cc].fx = 0; scr[rr][cc].lay = 0; }
        }
    }
    for (int k = 2; k < w + 2; k++) {
        int rr = r + h, cc = c + k;
        if (rr < SR && cc < SC) { scr[rr][cc].at = COL[C_DLGSHADE]; scr[rr][cc].fx = 0; scr[rr][cc].lay = 0; }
    }
    scr_dirty = 1;
}
void scr_cursor(int r, int c, int visible, int block)
{
    cur_r = r; cur_c = c; cur_vis = visible; cur_block = block; blink_on = 1;
    scr_dirty = 1;
}
void scr_save(Cell *buf) { memcpy(buf, scr, sizeof(scr)); }
void scr_restore(const Cell *buf) { memcpy(scr, buf, sizeof(scr)); scr_dirty = 1; }
/* large TrueType text drawn over cells holding the BIGCELL placeholder */
#define BIGCELL 0xE000
typedef struct { int r, c, rows, cols; wchar_t text[64]; uint8_t at; } BigText;
static BigText bigs[4]; static int nbig;
void scr_bigclear(void) { nbig = 0; scr_dirty = 1; }
void scr_bigtext(int r, int c, int rows, int cols, const wchar_t *t, uint8_t at)
{
    if (nbig >= 4) return;
    BigText *b = &bigs[nbig++];
    b->r = r; b->c = c; b->rows = rows; b->cols = cols; b->at = at;
    wcsncpy(b->text, t, 63); b->text[63] = 0;
    for (int i = 0; i < rows; i++) for (int k = 0; k < cols; k++) scr_put(r + i, c + k, BIGCELL, at);
}
void scr_clear(uint8_t at) { nbig = 0; scr_rect(0, 0, SR, SC, ' ', at); }

/* ------------------------------------------------------------------ rendering */
static HBITMAP dib; static uint32_t *dibbits; static int dibw, dibh;
static HFONT ttf; static int ttf_h, ttf_mode;

static void ensure_dib(HDC hdc, int w, int h)
{
    if (dib && dibw == w && dibh == h) return;
    if (dib) DeleteObject(dib);
    BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&dibbits, NULL, 0);
    dibw = w; dibh = h;
}

static int is_boxchar(wch c) { return (c >= 0x2500 && c <= 0x259F) || (c >= 0x25A0 && c <= 0x25FF); }

static void render(HDC hdc)
{
    RECT rc; GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W < 8 || H < 8) return;
    int cw = W / SC, ch = H / SR;
    if (cw < 4) cw = 4;
    if (ch < 8) ch = 8;
    int ox = (W - cw * SC) / 2, oy = (H - ch * SR) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    ensure_dib(hdc, W, H);
    if (!dibbits) return;
    for (int i = 0; i < W * H; i++) dibbits[i] = 0;

    int needtt = 0;
    /* a row drawn half a row lower (1.5-type spacing) needs a blank spacer row below it */
#define SHIFTED(r, c) (scr[r][c].lay == LAY_HALF && (r) + 1 < SR && scr[(r) + 1][c].lay == LAY_GAP)
    for (int pass = 0; pass < 2; pass++)
    for (int r = 0; r < SR; r++) {
        for (int c = 0; c < SC; c++) {
            Cell *cl = &scr[r][c];
            int shifted = SHIFTED(r, c);
            if (pass == 1 && !shifted) continue;
            uint32_t fg = rgb32(cl->at & 15), bg = rgb32(cl->at >> 4);
            int x0 = ox + c * cw, y0 = oy + r * ch;
            if (x0 + cw > W || y0 + ch > H) continue;
            if (pass == 0 && shifted) {
                /* the gap above the lowered text */
                for (int py = 0; py < ch; py++) { uint32_t *row = dibbits + (y0 + py) * W + x0; for (int px = 0; px < cw; px++) row[px] = bg; }
                continue;
            }
            if (shifted) { y0 += ch / 2; if (y0 + ch > H) continue; }
            wch code = cl->ch;
            if (code == 0 || IS_TAG(code) || code == BIGCELL) code = ' ';
            int gi = (code < 65536) ? glyph_ix[code] : -1;
            int useTT = (S.fontmode == 2 && !is_boxchar(code)) || (gi < 0 && code != ' ');
            const unsigned char *g = (gi >= 0 && !useTT) ? font_bits[gi] : NULL;
            int embold = (S.fontmode == 0 && !is_boxchar(code)) || (cl->fx & FX_BOLD);
            int italic = (cl->fx & FX_ITALIC) != 0;
            int under = S.realfx && (cl->fx & FX_UNDER);
            int strike = S.realfx && (cl->fx & FX_STRIKE);
            int curhere = cur_vis && blink_on && r == cur_r && c == cur_c && GetFocus() == g_hwnd;
            for (int py = 0; py < ch; py++) {
                int gy = py * 16 / ch;
                unsigned bits = g ? g[gy] : 0;
                if (g && embold) bits |= bits >> 1;
                if (g && italic) { int sh = (11 - gy) / 4; bits = sh > 0 ? (bits >> sh) : (bits << -sh); bits &= 0xFF; }
                if (under && (gy == 15)) bits = 0xFF;
                if (strike && (gy == 8)) bits = 0xFF;
                int cursorrow = curhere && (cur_block ? (gy >= 8) : (gy >= 14));
                uint32_t *row = dibbits + (y0 + py) * W + x0;
                for (int px = 0; px < cw; px++) {
                    int gx = px * 8 / cw;
                    int on = (bits >> (7 - gx)) & 1;
                    if (cursorrow) on = !on;
                    row[px] = on ? fg : bg;
                }
            }
            if (useTT) needtt = 1;
        }
    }

    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP old = SelectObject(mdc, dib);
    if (needtt) {
        if (!ttf || ttf_h != ch || ttf_mode != S.fontmode) {
            if (ttf) DeleteObject(ttf);
            ttf = CreateFontW(-(ch * 13 / 16), cw, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH,
                              S.ttfont[0] ? S.ttfont : L"Consolas");
            ttf_h = ch; ttf_mode = S.fontmode;
        }
        HFONT of = SelectObject(mdc, ttf);
        SetBkMode(mdc, TRANSPARENT);
        for (int r = 0; r < SR; r++)
            for (int c = 0; c < SC; c++) {
                Cell *cl = &scr[r][c];
                wch code = cl->ch;
                if (code == 0 || code == ' ' || IS_TAG(code) || code == BIGCELL) continue;
                int gi = (code < 65536) ? glyph_ix[code] : -1;
                int useTT = (S.fontmode == 2 && !is_boxchar(code)) || gi < 0;
                if (!useTT) continue;
                uint32_t f = rgb32(cl->at & 15);
                SetTextColor(mdc, RGB((f >> 16) & 255, (f >> 8) & 255, f & 255));
                wchar_t buf[3]; int n = 1;
                if (code >= 0x10000) { code -= 0x10000; buf[0] = 0xD800 + (code >> 10); buf[1] = 0xDC00 + (code & 0x3FF); n = 2; }
                else buf[0] = (wchar_t)code;
                RECT cr = { ox + c * cw, oy + r * ch, ox + c * cw + cw, oy + r * ch + ch };
                if (SHIFTED(r, c)) OffsetRect(&cr, 0, ch / 2);
                DrawTextW(mdc, buf, n, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
                if (S.fontmode == 2 && (cl->fx & FX_BOLD)) { OffsetRect(&cr, 1, 0); DrawTextW(mdc, buf, n, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP); }
            }
        SelectObject(mdc, of);
        GdiFlush();
        /* redraw cursor over TrueType cells */
        if (cur_vis && blink_on && GetFocus() == g_hwnd && cur_r < SR && cur_c < SC) {
            uint32_t fg = rgb32(scr[cur_r][cur_c].at & 15);
            int x0 = ox + cur_c * cw, y0 = oy + cur_r * ch;
            if (SHIFTED(cur_r, cur_c)) y0 += ch / 2;
            int from = cur_block ? ch / 2 : ch - ch / 8 - 1;
            wch code = scr[cur_r][cur_c].ch;
            int gi = (code < 65536) ? glyph_ix[code] : -1;
            if ((S.fontmode == 2 && !is_boxchar(code)) || gi < 0)
                for (int py = from; py < ch; py++)
                    for (int px = 0; px < cw; px++) dibbits[(y0 + py) * W + x0 + px] = fg;
        }
    }
    for (int bi = 0; bi < nbig; bi++) {
        BigText *b = &bigs[bi];
        /* clip to the placeholder cells that are still visible (dialogs may cover them) */
        HRGN rg = CreateRectRgn(0, 0, 0, 0);
        for (int i = 0; i < b->rows; i++) for (int k = 0; k < b->cols; k++) {
            int rr = b->r + i, cc = b->c + k;
            if (rr >= SR || cc >= SC || scr[rr][cc].ch != BIGCELL) continue;
            HRGN cr = CreateRectRgn(ox + cc * cw, oy + rr * ch, ox + cc * cw + cw, oy + rr * ch + ch);
            CombineRgn(rg, rg, cr, RGN_OR);
            DeleteObject(cr);
        }
        SelectClipRgn(mdc, rg);
        HFONT bf = CreateFontW(-(b->rows * ch * 72 / 100), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, S.ttfont[0] ? S.ttfont : L"Consolas");
        HFONT of = SelectObject(mdc, bf);
        SetBkMode(mdc, TRANSPARENT);
        uint32_t f = rgb32(b->at & 15);
        SetTextColor(mdc, RGB((f >> 16) & 255, (f >> 8) & 255, f & 255));
        RECT tr = { ox + b->c * cw, oy + b->r * ch, ox + (b->c + b->cols) * cw, oy + (b->r + b->rows) * ch };
        DrawTextW(mdc, b->text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mdc, of); DeleteObject(bf);
        SelectClipRgn(mdc, NULL);
        DeleteObject(rg);
    }
    BitBlt(hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteDC(mdc);
}

void screen_update(void)
{
    if (scr_dirty) {
        scr_dirty = 0;
        InvalidateRect(g_hwnd, NULL, FALSE);
        UpdateWindow(g_hwnd);
    }
}

/* ------------------------------------------------------------------ events */
#define QSZ 512
static Event q[QSZ]; static int qh, qt;
static void ev_push(Event e) { int n = (qt + 1) % QSZ; if (n == qh) return; q[qt] = e; qt = n; }
static void push_key(int k) { Event e = { EV_KEY, k }; ev_push(e); }
int ev_pending(void) { return qh != qt; }
void ev_flush(void) { qh = qt = 0; }

static void pump(DWORD wait)
{
    MSG m;
    MsgWaitForMultipleObjects(0, NULL, FALSE, wait, QS_ALLINPUT);
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { ExitProcess(0); }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

int ev_get(Event *e, int timeout)
{
    DWORD start = GetTickCount();
    for (;;) {
        if (qh != qt) { *e = q[qh]; qh = (qh + 1) % QSZ; return 1; }
        screen_update();
        DWORD el = GetTickCount() - start;
        if (timeout >= 0 && (int)el >= timeout) { e->type = EV_NONE; return 0; }
        pump(timeout < 0 ? INFINITE : (DWORD)(timeout - el));
    }
}

int getkey(void)
{
    Event e;
    for (;;) {
        ev_get(&e, -1);
        if (e.type == EV_KEY) return e.key;
        if (e.type == EV_CLOSE) return K_ESC;
    }
}

static void mouse_cell(LPARAM lp, int *r, int *c)
{
    RECT rc; GetClientRect(g_hwnd, &rc);
    int cw = rc.right / SC, ch = rc.bottom / SR;
    if (cw < 4) cw = 4;
    if (ch < 8) ch = 8;
    int ox = (rc.right - cw * SC) / 2, oy = (rc.bottom - ch * SR) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    int x = (short)LOWORD(lp) - ox, y = (short)HIWORD(lp) - oy;
    *c = x < 0 ? -1 : x / cw; *r = y < 0 ? -1 : y / ch;
}

static void toggle_fullscreen(void)
{
    DWORD st = GetWindowLongW(g_hwnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        GetWindowPlacement(g_hwnd, &prev_wp);
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(g_hwnd, GWL_STYLE, st & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen = 1;
    } else {
        SetWindowLongW(g_hwnd, GWL_STYLE, st | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &prev_wp);
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen = 0;
    }
}

static int fkey(int vk, int shift, int ctrl, int alt)
{
    int n = vk - VK_F1;
    if (n >= 10) return K_F11 + (n - 10);
    if (alt) return K_AF1 + n;
    if (ctrl) return K_CF1 + n;
    if (shift) return K_SF1 + n;
    return K_F1 + n;
}

static WCHAR hi_surrogate;

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        if (g_overlay) { RECT rc; GetClientRect(h, &rc); g_overlay(hdc, rc.right, rc.bottom); }
        else render(hdc);
        EndPaint(h, &ps);
        return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = 400; mm->ptMinTrackSize.y = 260; return 0; }
    case WM_TIMER: if (g_overlay) return 0; blink_on = !blink_on; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(h, NULL, FALSE); break;
    case WM_CLOSE: { Event e = { EV_CLOSE }; ev_push(e); return 0; }
    case WM_SYSCHAR: return 0;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        int vk = (int)wp;
        int ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        int alt = GetKeyState(VK_MENU) < 0;   /* F10 arrives as WM_SYSKEYDOWN without Alt */
        int k = 0;
        blink_on = 1;
        if (alt && vk == VK_F4) return DefWindowProcW(h, msg, wp, lp);
        if (alt && vk == VK_RETURN) { toggle_fullscreen(); return 0; }
        if (ctrl && alt && !(vk >= VK_F1 && vk <= VK_F12)) break;    /* AltGr: let WM_CHAR handle */
        switch (vk) {
        case VK_UP: k = ctrl ? K_CUP : shift ? K_SUP : K_UP; break;
        case VK_DOWN: k = ctrl ? K_CDOWN : shift ? K_SDOWN : K_DOWN; break;
        case VK_LEFT: k = ctrl ? K_CLEFT : shift ? K_SLEFT : K_LEFT; break;
        case VK_RIGHT: k = ctrl ? K_CRIGHT : shift ? K_SRIGHT : K_RIGHT; break;
        case VK_HOME: k = ctrl ? K_CHOME : shift ? K_SHOME : K_HOME; break;
        case VK_END: k = ctrl ? K_CEND : shift ? K_SEND : K_END; break;
        case VK_PRIOR: k = ctrl ? K_CPGUP : K_PGUP; break;
        case VK_NEXT: k = ctrl ? K_CPGDN : K_PGDN; break;
        case VK_INSERT: k = shift ? K_SINS : ctrl ? K_CINS : K_INS; break;
        case VK_DELETE: k = ctrl ? K_CDEL : K_DEL; break;
        case VK_RETURN: k = K_ENTER; break;
        case VK_BACK: k = ctrl ? K_CBS : K_BS; break;
        case VK_TAB: k = shift ? K_BTAB : K_TAB; break;
        case VK_ESCAPE: k = K_ESC; break;
        default:
            if (vk >= VK_F1 && vk <= VK_F12) { k = fkey(vk, shift, ctrl, alt); break; }
            if (ctrl && !alt) {
                if (vk >= 'A' && vk <= 'Z') k = (vk == 'M') ? K_CTRLM : (vk - 'A' + 1);
                else if (vk == VK_OEM_5) k = 0x1C;
                else if (vk == VK_OEM_6) k = 0x1D;
                else if (vk == VK_OEM_4) k = K_ESC;
                else if (vk == VK_OEM_MINUS) k = 0x1F;
                else if (vk == '6') k = 0x1E;
            } else if (alt && !ctrl) {
                if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) k = K_ALT + vk;
            }
        }
        if (k) { push_key(k); return 0; }
        if (msg == WM_SYSKEYDOWN && vk != VK_MENU) return 0;
        break; }
    case WM_SYSKEYUP:
        if (wp == VK_MENU || wp == VK_F10) return 0;
        break;
    case WM_CHAR: {
        WCHAR c = (WCHAR)wp;
        if (c >= 0xD800 && c < 0xDC00) { hi_surrogate = c; return 0; }
        if (c >= 0xDC00 && c < 0xE000) {
            if (hi_surrogate) push_key(0x10000 + ((hi_surrogate - 0xD800) << 10) + (c - 0xDC00));
            hi_surrogate = 0; return 0;
        }
        if (c >= 32 && c != 127) push_key(c);
        return 0; }
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_MOUSEMOVE: case WM_RBUTTONDOWN: {
        Event e = { EV_MOUSE };
        mouse_cell(lp, &e.mr, &e.mc);
        if (msg == WM_LBUTTONDOWN) { e.mact = MB_DOWN; SetCapture(h); g_mouse_down = 1; }
        else if (msg == WM_LBUTTONUP) { e.mact = MB_UP; ReleaseCapture(); g_mouse_down = 0; }
        else if (msg == WM_LBUTTONDBLCLK) { e.mact = MB_DBL; }
        else if (msg == WM_RBUTTONDOWN) { e.mact = MB_RDOWN; }
        else { if (!(wp & MK_LBUTTON)) return 0; e.mact = MB_MOVE; }
        ev_push(e);
        return 0; }
    case WM_MOUSEWHEEL: {
        Event e = { EV_MOUSE };
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(h, &pt);
        mouse_cell(MAKELPARAM(pt.x, pt.y), &e.mr, &e.mc);
        e.mact = MB_WHEEL; e.wheel = (short)HIWORD(wp) > 0 ? -3 : 3;
        ev_push(e);
        return 0; }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ settings */
static wchar_t inipath[MAX_PATH];

static int geti(const wchar_t *k, int def) { return GetPrivateProfileIntW(L"ScoopStar", k, def, inipath); }
static void puti(const wchar_t *k, int v) { wchar_t b[32]; swprintf(b, 32, L"%d", v); WritePrivateProfileStringW(L"ScoopStar", k, b, inipath); }

void settings_load(void)
{
    wchar_t app[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH)) {
        swprintf(inipath, MAX_PATH, L"%ls\\ScoopStar", app);
        CreateDirectoryW(inipath, NULL);
        wcscat(inipath, L"\\ScoopStar.ini");
    } else wcscpy(inipath, L"ScoopStar.ini");
    S.scheme = geti(L"Scheme", 0);
    S.fontmode = geti(L"FontMode", 2);
    GetPrivateProfileStringW(L"ScoopStar", L"TrueTypeFont", L"Consolas", S.ttfont, 40, inipath);
    S.rows = geti(L"Rows", 25);
    S.show_tags = geti(L"CommandTags", 0);
    S.dots_always = geti(L"DotsWhenTagsOff", 0);
    S.show_ruler = geti(L"Ruler", 1);
    S.show_stylebar = geti(L"StyleBar", 1);
    S.show_status = geti(L"StatusLine", 1);
    S.show_scroll = geti(L"ScrollBar", 1);
    S.show_flags = geti(L"FlagColumn", 1);
    S.classic_on_menus = geti(L"ClassicOnMenus", 1);
    S.menu_delay = geti(L"MenuDelay", 900);
    S.lm = geti(L"LeftMargin", 1);
    S.rm = geti(L"RightMargin", 65);
    S.pm = geti(L"ParaMargin", 1);
    S.tabw = geti(L"TabWidth", 5);
    S.insert = geti(L"Insert", 1);
    S.autoalign = geti(L"AutoAlign", 1);
    S.backups = geti(L"Backups", 1);
    S.realfx = geti(L"RealEffects", 1);
    S.cursorblock = geti(L"BlockCursor", 0);
    S.lang = geti(L"Language", 0); if (S.lang < 0 || S.lang >= NLANGS) S.lang = 0;
    S.skipcaps = geti(L"IgnoreCapitals", 1);
    S.def_mt = geti(L"DefTopMargin", 6);        /* lines: 6 = 1.00" */
    S.def_mb = geti(L"DefBottomMargin", 8);     /* 8 = 1.33" */
    S.def_po = geti(L"DefPageOffset", 10);      /* columns: 10 = 1.00" */
    S.def_ls = geti(L"DefLineSpacingHalves", 0);
    if (S.def_ls < 2 || S.def_ls > 18) { S.def_ls = geti(L"DefLineSpacing", 1) * 2; if (S.def_ls < 2 || S.def_ls > 18) S.def_ls = 2; }
    S.def_align = (char)geti(L"DefAlign", 'J');
    if (!strchr("LCRJ", S.def_align)) S.def_align = 'J';
    S.startfull = geti(L"StartFullScreen", 1);
    S.helplevel = geti(L"HelpLevel", 4);
    if (S.helplevel < 0 || S.helplevel > 4) S.helplevel = 4;
    S.winx = geti(L"WinX", CW_USEDEFAULT);
    S.winy = geti(L"WinY", CW_USEDEFAULT);
    S.winw = geti(L"WinW", 0);
    S.winh = geti(L"WinH", 0);
    S.maximized = geti(L"Maximized", 0);
    GetPrivateProfileStringW(L"ScoopStar", L"Directory", L"", S.dir, MAX_PATH, inipath);
    GetPrivateProfileStringW(L"ScoopStar", L"FileSpec", L"*.*", S.filespec, 64, inipath);
    if (S.rm <= S.lm) S.rm = 65;
    if (S.tabw < 1) S.tabw = 5;
    if (S.fontmode < 0 || S.fontmode > 2) S.fontmode = 0;
}

void settings_save(void)
{
    puti(L"Scheme", S.scheme); puti(L"FontMode", S.fontmode);
    WritePrivateProfileStringW(L"ScoopStar", L"TrueTypeFont", S.ttfont, inipath);
    puti(L"Rows", S.rows); puti(L"CommandTags", S.show_tags); puti(L"DotsWhenTagsOff", S.dots_always);
    puti(L"Ruler", S.show_ruler); puti(L"StyleBar", S.show_stylebar); puti(L"StatusLine", S.show_status);
    puti(L"ScrollBar", S.show_scroll); puti(L"FlagColumn", S.show_flags);
    puti(L"ClassicOnMenus", S.classic_on_menus); puti(L"MenuDelay", S.menu_delay);
    puti(L"LeftMargin", S.lm); puti(L"RightMargin", S.rm); puti(L"ParaMargin", S.pm); puti(L"TabWidth", S.tabw);
    puti(L"Insert", S.insert); puti(L"AutoAlign", S.autoalign); puti(L"Backups", S.backups);
    puti(L"RealEffects", S.realfx); puti(L"BlockCursor", S.cursorblock);
    puti(L"Language", S.lang); puti(L"IgnoreCapitals", S.skipcaps);
    puti(L"DefTopMargin", S.def_mt); puti(L"DefBottomMargin", S.def_mb); puti(L"DefPageOffset", S.def_po);
    puti(L"DefLineSpacingHalves", S.def_ls); puti(L"DefLineSpacing", S.def_ls / 2); puti(L"DefAlign", S.def_align); puti(L"HelpLevel", S.helplevel); puti(L"StartFullScreen", S.startfull);
    if (g_hwnd && !fullscreen) {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        GetWindowPlacement(g_hwnd, &wp);
        RECT r = wp.rcNormalPosition;
        puti(L"WinX", r.left); puti(L"WinY", r.top); puti(L"WinW", r.right - r.left); puti(L"WinH", r.bottom - r.top);
        puti(L"Maximized", wp.showCmd == SW_SHOWMAXIMIZED);
    }
    WritePrivateProfileStringW(L"ScoopStar", L"Directory", S.dir, inipath);
    WritePrivateProfileStringW(L"ScoopStar", L"FileSpec", S.filespec, inipath);
}

void apply_rows(int rows)
{
    if (rows < 25) rows = 25;
    if (rows > MAXR) rows = MAXR;
    SR = rows;
    scr_dirty = 1;
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ------------------------------------------------------------------ misc */
void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) { MessageBoxW(NULL, L"Out of memory", APPNAME, MB_ICONSTOP); ExitProcess(1); } return p; }
void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) { MessageBoxW(NULL, L"Out of memory", APPNAME, MB_ICONSTOP); ExitProcess(1); } return p; }
wchar_t *wcsdup_(const wchar_t *s) { size_t n = wcslen(s) + 1; wchar_t *d = xmalloc(n * sizeof(wchar_t)); memcpy(d, s, n * sizeof(wchar_t)); return d; }

/* ------------------------------------------------------------------ main */
/* ------------------------------------------------------------------ crash report */
/* If ScoopStar crashes, write %APPDATA%\ScoopStar\crash.txt (what failed and where,
   for fixing the bug) and save every changed document beside the original as
   "<name> (recovered).rtf", then tell the user. */
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    static volatile LONG busy;
    if (InterlockedExchange(&busy, 1)) return EXCEPTION_EXECUTE_HANDLER;
    HMODULE mod = GetModuleHandleW(NULL);
    uintptr_t base = (uintptr_t)mod, size = 0;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)mod + dos->e_lfanew);
    size = nt->OptionalHeader.SizeOfImage;
    wchar_t dir[MAX_PATH], log[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) wcscpy(dir, L".");
    wcscat(dir, L"\\ScoopStar"); CreateDirectoryW(dir, NULL);
    swprintf(log, MAX_PATH, L"%ls\\crash.txt", dir);
    FILE *f = _wfopen(log, L"a");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *cx = ep->ContextRecord;
    if (f) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "ScoopStar %ls crash %04d-%02d-%02d %02d:%02d:%02d\n", APPVER, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        fprintf(f, "code %08lX at +%llX", (unsigned long)er->ExceptionCode, (unsigned long long)((uintptr_t)er->ExceptionAddress - base));
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            fprintf(f, " (%s %llX)", er->ExceptionInformation[0] ? "write" : "read", (unsigned long long)er->ExceptionInformation[1]);
        fprintf(f, "\nstack:");
        /* return addresses into ScoopStar found on the stack, nearest first */
        uintptr_t *sp = (uintptr_t *)cx->Rsp;
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t top = (uintptr_t)sp + 64 * 1024;
        if (VirtualQuery(sp, &mbi, sizeof mbi)) { uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize; if (end < top) top = end; }
        int n = 0;
        for (uintptr_t *q = sp; (uintptr_t)(q + 1) <= top && n < 48; q++)
            if (*q > base + 0x1000 && *q < base + size) { fprintf(f, " %llX", (unsigned long long)(*q - base)); n++; }
        fprintf(f, "\n\n");
        fclose(f);
    }
    /* rescue changed documents */
    wchar_t saved[1024] = L"";
    for (int i = 0; i < g_nwin && i < 2; i++) {
        Doc *d = g_win[i].doc;
        if (!d || !d->dirty || (i == 1 && d == g_win[0].doc)) continue;
        wchar_t out[MAX_PATH + 32];
        if (d->path[0]) {
            wcsncpy(out, d->path, MAX_PATH); out[MAX_PATH - 1] = 0;
            wchar_t *dot = wcsrchr(out, '.'), *sl = wcsrchr(out, '\\');
            if (dot && (!sl || dot > sl)) *dot = 0;
            wcscat(out, L" (recovered).rtf");
        } else swprintf(out, MAX_PATH + 32, L"%ls\\Untitled (recovered).rtf", dir);
        int ok = d->nondoc ? save_text(d, out, NULL, NULL) : save_rtf(d, out, NULL, NULL);
        if (ok && wcslen(saved) + wcslen(out) < 1000) { wcscat(saved, L"\n  "); wcscat(saved, out); }
    }
    wchar_t m[1400];
    swprintf(m, 1400, L"ScoopStar has stopped because of an error.\n\n%ls%ls\n\nA report was written to:\n  %ls\nPlease send it so the error can be fixed.",
             saved[0] ? L"Your unsaved work was saved as:" : L"No documents had unsaved changes.", saved, log);
    MessageBoxW(NULL, m, L"ScoopStar", MB_OK | MB_ICONERROR | MB_TOPMOST);
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show)
{
    (void)hp; (void)cmd;
    SetUnhandledExceptionFilter(crash_filter);
    /* Per-monitor DPI awareness so the text grid is crisp on Windows 10/11 */
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *SetCtx)(HANDLE);
    SetCtx sdc = (SetCtx)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (sdc) sdc((HANDLE)-4); else SetProcessDPIAware();

    for (int i = 0; i < 65536; i++) glyph_ix[i] = -1;
    for (int i = 0; i < FONT_GLYPHS; i++) glyph_ix[font_cp[i]] = (short)i;
    /* a few useful aliases */
    if (glyph_ix[0xA0] < 0) glyph_ix[0xA0] = glyph_ix[' '];

    settings_load();
    set_scheme(S.scheme);
    apply_rows(S.rows);
    if (S.dir[0]) SetCurrentDirectoryW(S.dir);
    else {
        wchar_t docs[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", docs, MAX_PATH)) {
            wcscat(docs, L"\\Documents");
            SetCurrentDirectoryW(docs);
        }
    }

    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"ScoopStar7";
    RegisterClassW(&wc);

    /* pick an initial size: cells 2:1, fitting ~85% of the work area */
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int ah = (wa.bottom - wa.top) * 85 / 100;
    int chh = ah / SR; if (chh > 32) chh = 32; if (chh < 12) chh = 12;
    chh &= ~1;
    int cww = chh / 2;
    RECT r = { 0, 0, cww * SC, chh * SR };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT;
    if (S.winw > 300 && S.winh > 200) { ww = S.winw; wh = S.winh; wx = S.winx; wy = S.winy; }
    g_hwnd = CreateWindowW(L"ScoopStar7", L"ScoopStar 7.1.2", WS_OVERLAPPEDWINDOW, wx, wy, ww, wh, NULL, NULL, hi, NULL);
    ShowWindow(g_hwnd, S.maximized ? SW_SHOWMAXIMIZED : show);
    if (S.startfull) toggle_fullscreen();
    UpdateWindow(g_hwnd);
    SetTimer(g_hwnd, 1, 530, NULL);

    /* command line: a file to edit */
    int argc; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int edited = 0;
    if (argc > 1) {
        int nondoc = 0; int ai = 1;
        if (!_wcsicmp(argv[1], L"/n") || !_wcsicmp(argv[1], L"-n")) { nondoc = 1; ai = 2; }
        if (ai < argc) {
            Doc *d = doc_new();
            wchar_t full[MAX_PATH];
            GetFullPathNameW(argv[ai], MAX_PATH, full, NULL);
            if (file_exists(full)) load_file(d, full, nondoc);
            else { wcscpy(d->path, full); d->nondoc = nondoc; d->fmt = nondoc ? FMT_TXT : FMT_RTF; }
            if (edit_document(d)) edited = 2; else edited = 1;
        }
    }
    LocalFree(argv);
    if (edited != 2) opening_screen();
    GetCurrentDirectoryW(MAX_PATH, S.dir);
    settings_save();
    DestroyWindow(g_hwnd);
    return 0;
}
