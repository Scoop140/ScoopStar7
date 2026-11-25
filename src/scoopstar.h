/*
 * ScoopStar 7.0 - a WordStar 7.0 for DOS work-alike for Windows
 * Shared declarations.
 */
#ifndef SCOOPSTAR_H
#define SCOOPSTAR_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APPNAME   L"ScoopStar"
#define APPVER    L"7.1.2"

typedef uint32_t wch;               /* one document character (Unicode code point or tag) */

/* ---- Embedded command tags (WordStar print controls) ---- */
#define TAGBASE    0x110000u
#define TG_BOLD    (TAGBASE + 1)    /* ^PB */
#define TG_ITALIC  (TAGBASE + 2)    /* ^PY */
#define TG_UNDER   (TAGBASE + 3)    /* ^PS */
#define TG_STRIKE  (TAGBASE + 4)    /* ^PX */
#define TG_SUPER   (TAGBASE + 5)    /* ^PT */
#define TG_SUB     (TAGBASE + 6)    /* ^PV */
#define TG_DOUBLE  (TAGBASE + 7)    /* ^PD */
#define TG_INDEX   (TAGBASE + 8)    /* ^PK index entry on/off */
#define TG_FONT0   (TAGBASE + 0x100) /* ^P= font tag: TG_FONT0 + index into font table */
#define NOTEBASE   0x120000u        /* footnote reference: NOTEBASE + index into doc->notes */
#define IS_TAG(c)  ((c) >= TAGBASE && (c) < NOTEBASE)
#define IS_NOTE(c) ((c) >= NOTEBASE && (c) < NOTEBASE + 0x10000)
#define PNUMBASE   0x130000u        /* paragraph number: PNUMBASE + level (1-8) */
#define IS_PNUM(c) ((c) >= PNUMBASE && (c) < PNUMBASE + 16)
#define IS_SPECIAL(c) (IS_NOTE(c) || IS_PNUM(c))
#define SHY        0xADu            /* soft (optional) hyphen */
#define IS_FONTTAG(c) ((c) >= TG_FONT0 && (c) < TG_FONT0 + 0x1000)

/* effect bits (also used for screen cells) */
#define FX_BOLD    0x01
#define FX_ITALIC  0x02
#define FX_UNDER   0x04
#define FX_STRIKE  0x08
#define FX_SUPER   0x10
#define FX_SUB     0x20
#define FX_DOUBLE  0x40

/* ---- Fonts referenced by font tags ---- */
typedef struct { wchar_t name[40]; int halfpts; } FontDef;
extern FontDef g_fonts[256];
extern int g_nfonts;
int font_find_or_add(const wchar_t *name, int halfpts);

/* ---- Screen ---- */
#define MAXR 60
#define MAXC 132
typedef struct { wch ch; uint8_t at; uint8_t fx; uint8_t lay; } Cell;   /* lay: LAY_* below */
#define LAY_HALF 1   /* text drawn half a row lower (1.5-type line spacing) */
#define LAY_GAP  2   /* blank spacer row that the half-lowered row above may draw into */
void scr_setlay(int r, int c, int n, uint8_t v);
extern Cell scr[MAXR][MAXC];
extern int SR, SC;                  /* screen rows / cols */

/* colour roles */
enum {
    C_TITLE, C_MENUBAR, C_MENUHOT, C_MENUSEL, C_MENUSELHOT,
    C_MENUBOX, C_MENUBOXHOT, C_MENUBOXSEL, C_MENUDIS, C_MENUKEY,
    C_STYLEBAR, C_STYLESEP, C_STYLEON,
    C_RULER, C_RULERX,
    C_TEXT, C_BOLD, C_ITALIC, C_SUPSUB, C_BLOCK, C_TAG, C_DOT, C_FLAG, C_PAGEBRK,
    C_SCROLL, C_THUMB, C_STATUS, C_STATUSHI,
    C_DLG, C_DLGTITLE, C_DLGHOT, C_DLGFIELD, C_DLGFOCUS, C_DLGBTN, C_DLGBTNF, C_DLGSHADE,
    C_OPENBG, C_OPENHELP, C_FILES, C_FILESEL, C_FILEHDR,
    C_MSG, C_HELP, C_HELPHOT,
    C_NROLES
};
extern uint8_t COL[C_NROLES];
extern int g_scheme;
void set_scheme(int s);
#define NSCHEMES 5
uint32_t scheme_rgb(int i);

void scr_put(int r, int c, wch ch, uint8_t at);
void scr_putfx(int r, int c, wch ch, uint8_t at, uint8_t fx);
int  scr_puts(int r, int c, const wchar_t *s, uint8_t at);
int  scr_putsn(int r, int c, const wchar_t *s, int n, uint8_t at);
int  scr_puthot(int r, int c, const wchar_t *s, uint8_t at, uint8_t hot); /* '&' marks hotkey */
void scr_fill(int r, int c, int w, wch ch, uint8_t at);
void scr_rect(int r, int c, int h, int w, wch ch, uint8_t at);
void scr_box(int r, int c, int h, int w, uint8_t at, int dbl);
void scr_shadow(int r, int c, int h, int w);
void scr_cursor(int r, int c, int visible, int block);
void scr_save(Cell *buf);
void scr_restore(const Cell *buf);
void scr_clear(uint8_t at);
void scr_bigtext(int r, int c, int rows, int cols, const wchar_t *t, uint8_t at);
void scr_bigclear(void);

/* ---- Events ---- */
#define K_BASE    0x200000
enum {
    K_UP = K_BASE + 1, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN,
    K_INS, K_DEL, K_CUP, K_CDOWN, K_CLEFT, K_CRIGHT, K_CHOME, K_CEND, K_CPGUP, K_CPGDN,
    K_SUP, K_SDOWN, K_SLEFT, K_SRIGHT, K_SHOME, K_SEND,
    K_BTAB, K_CTRLM, K_CDEL, K_CBS, K_SINS, K_CINS,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
    K_SF1, K_SF2, K_SF3, K_SF4, K_SF5, K_SF6, K_SF7, K_SF8, K_SF9, K_SF10,
    K_CF1, K_CF2, K_CF3, K_CF4, K_CF5, K_CF6, K_CF7, K_CF8, K_CF9, K_CF10,
    K_AF1, K_AF2, K_AF3, K_AF4, K_AF5, K_AF6, K_AF7, K_AF8, K_AF9, K_AF10,
    K_ALT = K_BASE + 0x1000      /* K_ALT + 'A'..'Z' / '0'..'9' */
};
#define CTRL(x) ((x) & 0x1F)
#define K_ENTER 13
#define K_TAB   9
#define K_BS    8
#define K_ESC   27

enum { EV_NONE, EV_KEY, EV_MOUSE, EV_CLOSE };
enum { MB_DOWN = 1, MB_UP, MB_MOVE, MB_DBL, MB_WHEEL, MB_RDOWN };
typedef struct { int type; int key; int mr, mc; int mact; int wheel; } Event;

int  ev_get(Event *e, int timeout_ms);   /* returns 0 on timeout */
int  ev_pending(void);
void ev_flush(void);
int  getkey(void);                        /* waits for a key (mouse ignored) */
void screen_update(void);
extern HWND g_hwnd;
extern int g_mouse_down;

/* ---- Settings ---- */
typedef struct {
    int scheme;
    int fontmode;           /* 0 = bitmap bold (VGA look), 1 = bitmap thin, 2 = TrueType */
    wchar_t ttfont[40];
    int rows;
    int show_tags;          /* ^OD */
    int dots_always;        /* show dot commands even when tags hidden */
    int show_ruler, show_stylebar, show_status, show_scroll, show_flags;
    int classic_on_menus;
    int menu_delay;         /* ms before classic prefix menu appears */
    int lm, rm, pm, tabw;   /* default margins (1-based columns) */
    int insert;             /* default insert mode */
    int autoalign;
    int backups;            /* make .BAK files */
    int realfx;             /* draw real underline/strike lines */
    int cursorblock;
    int lang;               /* default spelling language */
    int skipcaps;           /* spelling: ignore words in CAPITALS */
    int def_mt, def_mb, def_po, def_ls; char def_align;
    int helplevel;
    int startfull;      /* open in full screen */          /* 0-4 */  /* new-document page defaults */
    wchar_t dir[MAX_PATH];
    wchar_t filespec[64];
    int winx, winy, winw, winh, maximized;
} Settings;
extern Settings S;
void settings_load(void);
void settings_save(void);
void apply_rows(int rows);

/* ---- Document ---- */
typedef struct { int p, o; } Pos;

typedef struct {
    wch *t; int n, cap;
    uint8_t style;
    wchar_t pnlab[24];      /* paragraph number label computed by layout */
} Para;

/* layout line types */
enum { LT_TEXT, LT_PAGEBRK, LT_SPACER, LT_COLBRK, LT_GRAPHIC };
#define LF_LAST   1     /* last line of paragraph (hard return) */
#define LF_DOT    2     /* dot command line */
#define LF_JUST   4     /* justify this line */
#define LF_HIDDEN 8     /* (unused) */
#define LF_FORCED 16    /* page break forced by .pa */
#define LF_HYPH   32    /* line broken by automatic hyphenation */

typedef struct {
    int p, s, e;        /* paragraph, start, end offset (exclusive) */
    short col;          /* starting screen column (0-based, before hscroll) */
    short width;        /* visible width in columns (w/o justification) */
    uint8_t type, flags;
    uint8_t fx0;        /* effect state at line start */
    uint8_t align;
    short font0;        /* font index at start, -1 = default */
    short rm;           /* right margin for justification */
    int page, pline;    /* page number, line on page (1-based) */
    short xoff;         /* column offset (characters) for newspaper columns */
    short y2;           /* exact position below the top margin, in half lines */
    uint8_t colno;
} Line;

#define MAXTABS 32
typedef struct {
    short lm, rm, pm;       /* 1-based columns */
    char align;             /* 'L','C','R','J' */
    char ls;                /* line spacing in half lines: 2 single, 3 one and a half, 4 double ... 18 */
    short pl, mt, mb, hm, fm, po;  /* in lines / columns */
    short tabs[MAXTABS]; int ntabs;
    int pn;                 /* page number override (-1 none) */
    int op;                 /* omit page number */
    int ww;                 /* word wrap (.aw) */
    int lang;               /* spelling language (.la) */
    int cols, gutter;       /* newspaper columns (.co) */
    int hy;                 /* automatic hyphenation (.hy) */
    short pw;               /* paper width in columns (tenths of an inch), .pw */
    char land;              /* landscape (.pr or=l) */
} Fmt;

enum { NOTE_FOOT, NOTE_END, NOTE_ANNOT };
typedef struct { wch *t; int n; int num; int kind; wchar_t mark[12]; } Note;   /* footnote text ('\n' between paragraphs) */

typedef struct Doc {
    Para *p; int np, cap;
    Note *notes; int nnotes, notecap;
    struct Doc *noteparent; int noteidx;   /* set when this doc is a footnote being edited */
    wchar_t path[MAX_PATH];
    int nondoc;             /* plain text, no wrapping */
    int fmt;                /* FMT_RTF / FMT_TXT / FMT_WS */
    int dirty;
    int refs;               /* number of windows showing it */
    Pos bb, bk; int bbset, bkset, bhide, colblock;
    Pos mark[10]; int markset[10];
    Pos prevpos, lastfind;
    Fmt base;               /* defaults at top of document */
    uint32_t ver, layver;
    Line *L; int nl, lcap;
    int npages;
    wchar_t header[3][256], footer[3][256];  /* last .he/.fo seen (for print) */
    int textenc;            /* for nondoc: 0 utf8, 1 ansi, 2 utf16 */
    int crlf;
    /* undo */
    struct UndoRec *undo; int nundo, undocap;
    int lastcmd, groupn;
    Pos *regs[24]; int nregs;
    int hidedots;           /* dot command lines hidden in the current layout */
    int readonly;           /* protected file */
} Doc;

enum { FMT_RTF, FMT_TXT, FMT_WS };

Doc *doc_new(void);
void doc_free(Doc *d);
void doc_clear(Doc *d);
Para *doc_para(Doc *d, int i);
void para_ins(Doc *d, int pi, int at, const wch *s, int n);
void para_del(Doc *d, int pi, int at, int n);
void doc_split(Doc *d, Pos at);
void doc_join(Doc *d, int pi);          /* join para pi with pi+1 */
Pos  doc_insert(Doc *d, Pos at, const wch *s, int n);  /* s may contain '\n' para breaks; returns end */
void doc_delete(Doc *d, Pos a, Pos b);  /* delete [a,b) */
wch *doc_extract(Doc *d, Pos a, Pos b, int *outn);  /* '\n' separates paras */
int  pos_cmp(Pos a, Pos b);
void doc_append_para(Doc *d, const wch *s, int n);
void doc_touch(Doc *d);
void doc_register_pos(Doc *d, Pos *p);
void doc_unregister_pos(Doc *d, Pos *p);
int  para_is_dot(Para *pp);
void doc_layout(Doc *d, int textwidth_hint);
int  doc_line_of(Doc *d, Pos p);        /* layout line index containing p */
int  line_col_of(Doc *d, Line *l, int off);   /* screen column (0-based) of offset in line */
int  line_off_at_col(Doc *d, Line *l, int col);
int  line_cols(Doc *d, Line *l, short **cols);  /* column of each offset s..e */
int  tag_label(wch c, wchar_t *buf, int n);
int  note_add(Doc *d, const wch *t, int n);
int  note_add_kind(Doc *d, const wch *t, int n, int kind, const wchar_t *mark);
int  hyph_points(const wchar_t *word, int len, int lang, char *out);
int  special_width(Doc *d, Para *p, wch c);
int  graphic_parse(const wch *t, int n, double *w, double *h, wchar_t *path, int pathsz);
void notes_dup(Doc *dst, Doc *src, wch *s, int n);
int  note_label(Doc *d, wch c, wchar_t *buf, int n);     /* superscript number; returns width */
int  note_wrap_lines(Doc *d, int idx, int width);

/* paragraph styles */
typedef struct { const wchar_t *name; int bold, italic; char align; int lmadd, rmsub, pmadd; int spacebefore; } ParaStyle;
extern ParaStyle g_pstyles[];
extern int g_npstyles;
Fmt  doc_fmt_at(Doc *d, int pi);
void fmt_default(Fmt *f);
int  parse_measure(const wchar_t *s, int horiz, int *out);
int  tab_next(const Fmt *f, int col);    /* 1-based col -> next tab stop col */
int  pw_parse(const wchar_t *s);               /* paper width: inches unless c, p or r follows; 0 if invalid */
int  ls_parse(const wchar_t *s);               /* "1.5" -> 3 half lines; 0 if invalid */
void ls_text(int halves, wchar_t *out);         /* 3 -> "1.5" */
void dot_parse(const wch *t, int n, wchar_t *cmd, int cmdsz, wchar_t *arg, int argsz);
int  wch_isword(wch c);

/* undo */
void undo_push(Doc *d, Pos cur, int kind);
int  undo_pop(Doc *d, Pos *cur);
void undo_clear(Doc *d);

/* ---- Editor windows ---- */
typedef struct EdWin {
    Doc *doc;
    Pos cur;
    int top;            /* first layout line shown */
    int hscroll;
    int wantcol;        /* desired column for up/down */
    int r0, rows;       /* screen rows for text area */
} EdWin;

extern EdWin g_win[2];
extern int g_nwin, g_cw;
extern int g_insert;
extern int g_quit;

int  edit_document(Doc *d);       /* returns 1 if user chose exit WordStar */
void opening_screen(void);

/* ---- UI ---- */
int  msgbox(const wchar_t *title, const wchar_t *msg, const wchar_t *buttons); /* returns index or -1 */
void message(const wchar_t *fmt, ...);
int  ask_yn(const wchar_t *title, const wchar_t *q);   /* 1 yes, 0 no, -1 cancel */
int  input_box(const wchar_t *title, const wchar_t *label, wchar_t *buf, int bufsz);
int  file_dialog(const wchar_t *title, wchar_t *path, int forsave, const wchar_t *spec);
void show_help(const wchar_t *topic);
void status_dialog(void);
int  list_pick(const wchar_t *title, const wchar_t **items, int n, int sel, int width);

/* dialog framework */
enum { DC_LABEL, DC_EDIT, DC_CHECK, DC_RADIO, DC_BUTTON, DC_LIST };
typedef struct {
    int type, r, c, w, h;
    const wchar_t *text;        /* label with & hotkey */
    wchar_t *buf; int bufsz;    /* edit */
    int *val;                   /* check/radio value pointer; list selection */
    int radioval;               /* value for radio */
    const wchar_t **items; int nitems; int ltop;  /* list */
    int id;                     /* button id returned */
    int lcol;                   /* column for edit field start (after label) */
} DCtl;
int  field_paste(wchar_t *buf, int bufsz, int *epos, int *fresh);   /* Ctrl+V / Shift+Ins in a typing box */
int dialog_run(const wchar_t *title, int r, int c, int h, int w, DCtl *ctl, int nctl, int focus);

/* ---- Files ---- */
int  load_file(Doc *d, const wchar_t *path, int nondoc);
int  save_file(Doc *d, const wchar_t *path);
int  save_rtf(Doc *d, const wchar_t *path, Pos *a, Pos *b);
int  load_rtf_mem(Doc *d, const char *buf, size_t n);
int  save_text(Doc *d, const wchar_t *path, Pos *a, Pos *b);
int  load_wordstar_mem(Doc *d, const unsigned char *buf, size_t n);
int  file_exists(const wchar_t *path);
void path_ext(const wchar_t *path, wchar_t *ext, int n);

char *rtf_build(Doc *d, Pos *a, Pos *b, size_t *len);
int  load_rtf_file_into(Doc *d, const wchar_t *path);

/* file operations shared by opening screen and editor */
void fileop_copy(void);
void fileop_delete(void);
void fileop_rename(void);
void fileop_chdir(void);
int  fileop_mkdir(wchar_t *made);
void fileop_rundos(void);
int  screen_settings_dialog(void);
int  doc_defaults_dialog(void);
wch  extchar_dialog(void);
void about_box(void);

#define DEFAULT_FONT L"Courier New"
#define DEFAULT_HALFPTS 24
#define UK_TYPE 1
#define UK_DEL  2
#define UK_BS   3

/* ---- Spelling and thesaurus ---- */
enum { LANG_EN, LANG_LA, LANG_DE, NLANGS };
extern const wchar_t *g_langnames[NLANGS];
int  lang_from_name(const wchar_t *a);
int  spell_available(int lang);
const wchar_t *spell_engine_name(int lang);
int  spell_check(const wchar_t *w, int lang);
int  spell_suggest(const wchar_t *w, int lang, wchar_t out[][64], int max);
void spell_add(const wchar_t *w, int lang);
void spell_ignore_all(const wchar_t *w);
int  spell_stems(const wchar_t *w, wchar_t out[][64], int max);
int  thes_available(void);
int  thes_lookup(const wchar_t *word, wchar_t ***meanings);
void thes_free(wchar_t **m, int n);

/* ---- Printing ---- */
int  print_document(Doc *d);
void preview_document(Doc *d, int startpage);
int  image_size(const wchar_t *path, int *w, int *h);
int  generate_list(Doc *d, int toc, wchar_t *outpath);

/* ---- misc ---- */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
wchar_t *wcsdup_(const wchar_t *s);
double calc_eval(const wchar_t *s, int *err);
void fmt_number(double v, wchar_t *out, int n, int dollar);
extern double g_lastmath; extern wchar_t g_lastexpr[256]; extern int g_havemath;

#endif
