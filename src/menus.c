/*
 * ScoopStar 7.0 - menus.c
 * Pull-down menus (WordStar 7 layout) and the classic ^K ^Q ^O ^P ^M prefix menus.
 */
#include "scoopstar.h"
#include "cmds.h"

int g_cmdarg;

#define N(a) (int)(sizeof(a) / sizeof(a[0]))
#define SEP { NULL, NULL, 0, 0, NULL, 0 }

/* ---------------------------------------------------------------- edit screen menus */
static const MItem m_copy[] = {
    { L"&Block", L"^KC", CMD_BCOPY },
    { L"Block from &Other Window", L"^KA", CMD_BCOPYWIN },
    { L"&From Windows Clipboard", L"^K[", CMD_CLIPFROM },
    { L"&To Windows Clipboard", L"^K]", CMD_CLIPTO },
    { L"To &Another File...", L"^KW", CMD_BWRITE },
};
static const MItem m_move[] = {
    { L"&Block", L"^KV", CMD_BMOVE },
    { L"Block from &Other Window", L"^KG", CMD_BMOVEWIN },
};
static const MItem m_delete[] = {
    { L"&Block", L"^KY", CMD_BDEL },
    { L"&Word", L"^T", CMD_DELWORD },
    { L"&Line", L"^Y", CMD_DELLINE },
    { L"Line Le&ft of Cursor", L"^Q Del", CMD_DELBOL },
    { L"Line &Right of Cursor", L"^QY", CMD_DELEOL },
    { L"To &Character...", L"^QT", CMD_DELTOCHAR },
};
static const MItem m_marks[] = {
    { L"Marker &0", L"0", 0, 0 }, { L"Marker &1", L"1", 0, 1 }, { L"Marker &2", L"2", 0, 2 },
    { L"Marker &3", L"3", 0, 3 }, { L"Marker &4", L"4", 0, 4 }, { L"Marker &5", L"5", 0, 5 },
    { L"Marker &6", L"6", 0, 6 }, { L"Marker &7", L"7", 0, 7 }, { L"Marker &8", L"8", 0, 8 },
    { L"Marker &9", L"9", 0, 9 },
};
static MItem m_gomark[10], m_setmark[10];
static const MItem m_goother[] = {
    { L"&Font Tag", L"^Q=", CMD_GOTOFONT },
    { L"S&tyle Tag", L"^Q<", CMD_GOTOSTYLE },
    { L"&Note", L"^ONG", CMD_GOTONOTE },
    { L"&Previous Position", L"^QP", CMD_PREVPOS },
    { L"&Last Find/Replace", L"^QV", CMD_LASTFIND },
    { L"&Beginning of Block", L"^QB", CMD_BLOCKBEG },
    { L"&End of Block", L"^QK", CMD_BLOCKEND },
    { L"&Document Beginning", L"^QR", CMD_DOCBEG },
    { L"D&ocument End", L"^QC", CMD_DOCEND },
    { L"Scroll Continuously &Up", L"^QW", CMD_SCROLLUPC },
    { L"&Scroll Continuously Down", L"^QZ", CMD_SCROLLDNC },
};
static const MItem m_editset[] = {
    { L"&Column Block Mode", L"^KN", CMD_COLBLOCK },
    { L"Column &Replace Mode", L"^KI", CMD_COLREPLACE },
    { L"&Word Wrap", L"^OW", CMD_WORDWRAP },
};
static const MItem m_edit[] = {
    { L"&Undo", L"^U", CMD_UNDO },
    SEP,
    { L"Mark Block &Beginning", L"^KB", CMD_BB },
    { L"Mark Block &End", L"^KK", CMD_BK },
    { L"&Move", NULL, 0, 0, m_move, N(m_move) },
    { L"&Copy", NULL, 0, 0, m_copy, N(m_copy) },
    { L"&Delete", NULL, 0, 0, m_delete, N(m_delete) },
    { L"Mark Pre&vious Block", L"^KU", CMD_BPREV },
    SEP,
    { L"&Find...", L"^QF", CMD_FIND },
    { L"Find and &Replace...", L"^QA", CMD_REPLACE },
    { L"&Next Find", L"^L", CMD_FINDNEXT },
    { L"Go to C&haracter...", L"^QG", CMD_GOTOCHAR },
    { L"Go to &Page...", L"^QI", CMD_GOTOPAGE },
    { L"Go to M&arker", NULL, 0, 0, m_gomark, 10 },
    { L"Go to &Other", NULL, 0, 0, m_goother, N(m_goother) },
    { L"&Set Marker", NULL, 0, 0, m_setmark, 10 },
    { L"Ed&it Note", L"^OND", CMD_EDITNOTE },
    SEP,
    { L"Editing Se&ttings", NULL, 0, 0, m_editset, N(m_editset) },
};
static const MItem m_file[] = {
    { L"&Open/Switch...", L"^OK", CMD_OPENSWITCH },
    { L"&Close", L"^KQ", CMD_ABANDON },
    SEP,
    { L"&Save", L"^KS", CMD_SAVE },
    { L"Save &As...", L"^KT", CMD_SAVEAS },
    { L"Save and C&lose", L"^KD", CMD_SAVECLOSE },
    { L"Save as &Text File...", NULL, CMD_EXPORTTXT },
    SEP,
    { L"&Print...", L"^KP", CMD_PRINT },
    SEP,
    { L"Cop&y...", L"^KO", CMD_FCOPY },
    { L"&Delete...", L"^KJ", CMD_FDELETE },
    { L"Re&name...", L"^KE", CMD_FRENAME },
    { L"Change Dri&ve/Directory...", L"^KL", CMD_CHDIR },
    { L"&Make Directory...", NULL, CMD_MKDIR },
    { L"&Run DOS Command...", L"^KF", CMD_RUNDOS },
    SEP,
    { L"Prot&ect/Unprotect", NULL, CMD_PROTECT },
    { L"Stat&us", L"^O?", CMD_STATUS },
    { L"E&xit ScoopStar", L"^KX", CMD_EXIT },
};
static const MItem m_view[] = {
    { L"&Preview", L"^OP", CMD_PREVIEW },
    { L"&Command Tags", L"^OD", CMD_TAGS },
    { L"&Block Highlighting", L"^KH", CMD_BHIDE },
    SEP,
    { L"&Open/Switch Window...", L"^OK", CMD_OPENSWITCH },
    { L"Change &Window Size...", L"^OM", CMD_WINSIZE },
    SEP,
    { L"&Screen Settings...", L"^OB", CMD_SCREENSET },
};
static const MItem m_value[] = {
    { L"Current &Time", L"^M!", CMD_TIME },
    { L"Last &Math Result", L"^M=", CMD_MATHRES },
    { L"Last Math &Expression", L"^M#", CMD_MATHEXPR },
    { L"&Last Math, as Dollar", L"^M$", CMD_MATHDOLLAR },
    { L"Current &Filename", L"^M*", CMD_FILENAME },
    { L"Current &Drive", L"^M:", CMD_DRIVE },
    { L"Current Di&rectory", L"^M.", CMD_DIRECTORY },
    { L"Current &Path", L"^M\\", CMD_PATH },
};
static const MItem m_note[] = {
    { L"&Footnote", L"^ONF", CMD_FOOTNOTE },
    { L"&Endnote", L"^ONE", CMD_ENDNOTE },
    { L"&Annotation", L"^ONA", CMD_ANNOT },
    { L"&Comment (nonprinting)", L"^ONC", CMD_COMMENT },
};
static const MItem m_ixtoc[] = {
    { L"&Mark Index Word", L"^PK", CMD_INDEXMARK },
    { L"&Index Entry...", L".ix", CMD_INDEXENTRY },
    { L"&Contents Entry...", L".tc", CMD_TOCENTRY },
};
static const MItem m_insert[] = {
    { L"&Page Break", L".pa", CMD_PAGEBREAK },
    { L"&Conditional Page Break...", L".cp", CMD_CONDBREAK },
    { L"&Today's Date", L"^M@", CMD_DATE },
    { L"Other &Value", NULL, 0, 0, m_value, N(m_value) },
    SEP,
    { L"&Extended Character...", L"^P0", CMD_EXTCHAR },
    { L"&Binding Space", L"^PO", CMD_BINDSPACE },
    { L"&Soft Hyphen", L"^OE", CMD_SOFTHYPH },
    { L"Paragraph N&umber...", L"^OZ", CMD_PARANUM },
    SEP,
    { L"&File...", L"^KR", CMD_BREAD },
    { L"&Graphic...", L"^P*", CMD_GRAPHIC },
    { L"&Note", NULL, 0, 0, m_note, N(m_note) },
    { L"&Index/Contents Entry", NULL, 0, 0, m_ixtoc, N(m_ixtoc) },
};
static const MItem m_other[] = {
    { L"&Strikeout", L"^PX", CMD_STRIKE },
    { L"Su&bscript", L"^PV", CMD_SUB },
    { L"Su&perscript", L"^PT", CMD_SUPER },
    { L"&Double Strike", L"^PD", CMD_DOUBLE },
};
static const MItem m_case[] = {
    { L"&Uppercase", L"^K\"", CMD_UPPER },
    { L"&Lowercase", L"^K'", CMD_LOWER },
    { L"&Sentence Case", L"^K.", CMD_SENTENCE },
};
static const MItem m_style[] = {
    { L"&Bold", L"^PB", CMD_BOLD },
    { L"&Italic", L"^PY", CMD_ITALIC },
    { L"&Underline", L"^PS", CMD_UNDER },
    { L"&Font...", L"^P=", CMD_FONT },
    { L"&Other", NULL, 0, 0, m_other, N(m_other) },
    SEP,
    { L"Select &Paragraph Style...", L"^OFS", CMD_PSTYLE },
    { L"&Return to Body Text", L"^OFR", CMD_PSTYLEPREV },
    SEP,
    { L"&Convert Case", NULL, 0, 0, m_case, N(m_case) },
};
static const MItem m_hf[] = {
    { L"&Header...", L".he", CMD_HEADER },
    { L"&Footer...", L".fo", CMD_FOOTER },
    { L"&Even/Odd and Multiline...", L".h1e", CMD_EVENODD },
};
static const MItem m_effects[] = {
    { L"Keep &Words Together on Line", L"^PO", CMD_BINDSPACE },
    { L"Keep &Lines Together on Page...", L".cp", CMD_CONDBREAK },
};
static const MItem m_layout[] = {
    { L"&Center Line", L"^OC", CMD_CENTER },
    { L"&Right Align Line", L"^O]", CMD_RIGHTALIGN },
    { L"Ruler &Line...", L"^OL", CMD_RULER },
    { L"&Alignment and Spacing...", L"^OS", CMD_ALIGNSPACE },
    SEP,
    { L"Colu&mns...", L"^OU", CMD_COLUMNS },
    { L"Column &Break", L".cb", CMD_COLBREAK },
    SEP,
    { L"&Page...", L"^OY", CMD_PAGELAYOUT },
    { L"Center Page &Vertically", L"^OV", CMD_VCENTER },
    { L"&Headers/Footers", NULL, 0, 0, m_hf, N(m_hf) },
    { L"Page &Numbering...", L"^O#", CMD_PAGENUM },
    { L"Paragraph Numbering &Style...", L".p#", CMD_PNUMSTYLE },
    { L"H&yphenation On/Off", L"^OH", CMD_AUTOHYPH },
    { L"Special &Effects", NULL, 0, 0, m_effects, N(m_effects) },
};
static const MItem m_reformat[] = {
    { L"&Rest of Document", L"^QU", CMD_REFORMATREST },
    { L"&Paragraph", L"^B", CMD_REFORMAT },
};
static const MItem m_spellother[] = {
    { L"&Rest of Document", L"^QL", CMD_SPELLREST },
    { L"&Word", L"^QN", CMD_SPELLWORD },
    { L"&Type Word...", L"^QO", CMD_SPELLTYPE },
};
static const MItem m_util[] = {
    { L"Spelling Check &Global", NULL, CMD_SPELL },
    { L"Spelling Check &Other", NULL, 0, 0, m_spellother, N(m_spellother) },
    { L"&Thesaurus...", L"^QJ", CMD_THESAURUS },
    { L"&Language Change...", L".la", CMD_LANGUAGE },
    SEP,
    { L"&Calculator...", L"^QM", CMD_CALC },
    { L"&Block Math", L"^KM", CMD_BLOCKMATH },
    { L"Sort Bloc&k...", L"^KZ", CMD_SORT },
    { L"&Word Count", L"^K?", CMD_WORDCOUNT },
    { L"&Reformat", NULL, 0, 0, m_reformat, N(m_reformat) },
    SEP,
    { L"Generate &Index", NULL, CMD_GENINDEX },
    { L"Generate Table of Co&ntents", NULL, CMD_GENTOC },
    SEP,
    { L"&Document Defaults...", NULL, CMD_DOCDEFAULTS },
};
static const MItem m_help[] = {
    { L"&Help...", L"F1", CMD_HELP },
    { L"&Keyboard Commands", NULL, CMD_KEYS },
    { L"&Dot Commands", NULL, CMD_DOTHELP },
    { L"Change Help &Level...", NULL, CMD_HELPLEVEL },
    SEP,
    { L"&About ScoopStar...", NULL, CMD_ABOUT },
};

const TopMenu g_editmenus[] = {
    { L"&File", m_file, N(m_file), 1 },
    { L"&Edit", m_edit, N(m_edit), 8 },
    { L"&View", m_view, N(m_view), 15 },
    { L"&Insert", m_insert, N(m_insert), 22 },
    { L"&Style", m_style, N(m_style), 31 },
    { L"&Layout", m_layout, N(m_layout), 39 },
    { L"&Utilities", m_util, N(m_util), 48 },
    { L"&Help", m_help, N(m_help), -5 },
};
const int g_neditmenus = N(g_editmenus);

/* ---------------------------------------------------------------- opening screen menus */
static const MItem o_file[] = {
    { L"&New", L"S", OCMD_NEW },
    { L"Open &Document...", L"D", OCMD_OPENDOC },
    { L"Open Nondocu&ment...", L"N", OCMD_OPENNON },
    SEP,
    { L"&Print...", L"P", OCMD_PRINT },
    SEP,
    { L"C&opy...", L"O", OCMD_COPY },
    { L"Dele&te...", L"Y", OCMD_DELETE },
    { L"R&ename...", L"E", OCMD_RENAME },
    { L"Change Dri&ve/Directory...", L"L", OCMD_CHDIR },
    { L"Make Director&y...", L"M", OCMD_MKDIR },
    { L"&Change Filename Display...", L"F", OCMD_FILESPEC },
    { L"&Run DOS Command...", L"R", OCMD_RUNDOS },
    { L"Protect/Unprotect &File...", L"C", OCMD_PROTECT },
    SEP,
    { L"Stat&us", L"?", OCMD_STATUS },
    { L"E&xit ScoopStar", L"X", OCMD_EXIT },
};
static const MItem o_util[] = {
    { L"&Index...", L"I", OCMD_GENINDEX },
    { L"&Table of Contents...", L"T", OCMD_GENTOC },
    SEP,
    { L"&Screen Settings...", NULL, OCMD_SETTINGS },
    { L"&Document Defaults...", NULL, OCMD_DOCDEFAULTS },
};
static const MItem o_help[] = {
    { L"&Help...", L"F1", CMD_HELP },
    { L"&Keyboard Commands", NULL, CMD_KEYS },
    { L"Change Help &Level...", L"J", OCMD_HELPLEVEL },
    SEP,
    { L"&About ScoopStar...", NULL, CMD_ABOUT },
};
const TopMenu g_openmenus[] = {
    { L"&File", o_file, N(o_file), 1 },
    { L"&Utilities", o_util, N(o_util), 8 },
    { L"&Help", o_help, N(o_help), -5 },
};
const int g_nopenmenus = N(g_openmenus);

/* ---------------------------------------------------------------- drawing */
static int menu_col(const TopMenu *m) { return m->col >= 0 ? m->col : SC + m->col; }

void draw_menubar(const TopMenu *menus, int n, int sel)
{
    scr_fill(1, 0, SC, ' ', COL[C_MENUBAR]);
    for (int i = 0; i < n; i++) {
        int c = menu_col(&menus[i]);
        int len = 0; for (const wchar_t *s = menus[i].label; *s; s++) if (*s != '&') len++;
        if (i == sel) {
            scr_fill(1, c - 1, len + 2, ' ', COL[C_MENUSEL]);
            scr_puthot(1, c, menus[i].label, COL[C_MENUSEL], COL[C_MENUSELHOT]);
        } else scr_puthot(1, c, menus[i].label, COL[C_MENUBAR], COL[C_MENUHOT]);
    }
}

int menubar_hit(const TopMenu *menus, int n, int col)
{
    for (int i = 0; i < n; i++) {
        int c = menu_col(&menus[i]);
        int len = 0; for (const wchar_t *s = menus[i].label; *s; s++) if (*s != '&') len++;
        if (col >= c - 1 && col <= c + len) return i;
    }
    return -1;
}

static int hotkey_of(const wchar_t *label)
{
    const wchar_t *p = wcschr(label, '&');
    return p && p[1] ? towupper(p[1]) : 0;
}

static int label_len(const wchar_t *s) { int n = 0; for (; *s; s++) if (*s != '&') n++; return n; }

static void box_geom(const MItem *it, int n, int *w)
{
    int lw = 0, kw = 0;
    for (int i = 0; i < n; i++) {
        if (!it[i].label) continue;
        int l = label_len(it[i].label) + (it[i].sub ? 2 : 0);
        if (l > lw) lw = l;
        if (S.classic_on_menus && it[i].key) { int k = (int)wcslen(it[i].key); if (k > kw) kw = k; }
    }
    *w = lw + (kw ? kw + 3 : 0) + 4;
}

static void draw_box(const MItem *it, int n, int r, int c, int w, int sel)
{
    scr_rect(r, c, n + 2, w, ' ', COL[C_MENUBOX]);
    scr_box(r, c, n + 2, w, COL[C_MENUBOX], 0);
    for (int i = 0; i < n; i++) {
        int rr = r + 1 + i;
        if (!it[i].label) {
            scr_put(rr, c, 0x251C, COL[C_MENUBOX]);
            scr_fill(rr, c + 1, w - 2, 0x2500, COL[C_MENUBOX]);
            scr_put(rr, c + w - 1, 0x2524, COL[C_MENUBOX]);
            continue;
        }
        int dis = (it[i].cmd == CMD_NOTAVAIL);
        uint8_t at = i == sel ? COL[C_MENUBOXSEL] : dis ? COL[C_MENUDIS] : COL[C_MENUBOX];
        uint8_t hot = i == sel ? COL[C_MENUBOXSEL] : dis ? COL[C_MENUDIS] : COL[C_MENUBOXHOT];
        scr_fill(rr, c + 1, w - 2, ' ', at);
        scr_puthot(rr, c + 2, it[i].label, at, hot);
        if (it[i].sub) scr_put(rr, c + w - 3, 0x25BA, at);
        else if (S.classic_on_menus && it[i].key) {
            int k = (int)wcslen(it[i].key);
            scr_puts(rr, c + w - 2 - k, it[i].key, i == sel ? at : (dis ? at : COL[C_MENUKEY]));
        }
    }
    scr_shadow(r, c, n + 2, w);
}

static int next_item(const MItem *it, int n, int i, int dir)
{
    for (int k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        if (it[i].label) return i;
    }
    return i;
}

/* returns: >0 cmd chosen, 0 escape, -1 left, -2 right, -3 mouse on menubar (g_cmdarg=menu index) */
static int run_box(const TopMenu *menus, int nmenus, const MItem *it, int n, int r, int c, int level, int *arg)
{
    static Cell save[MAXR][MAXC];
    Cell *buf = xmalloc(sizeof(save));
    scr_save(buf);
    int w; box_geom(it, n, &w);
    if (c + w + 2 > SC) c = SC - w - 2;
    if (c < 0) c = 0;
    if (r + n + 2 > SR) r = SR - n - 2;
    int sel = it[0].label ? 0 : next_item(it, n, 0, 1);
    int result = 0;
    for (;;) {
        scr_restore(buf);
        draw_box(it, n, r, c, w, sel);
        scr_cursor(0, 0, 0, 0);
        Event e; ev_get(&e, -1);
        int choose = 0;
        if (e.type == EV_CLOSE) { result = 0; break; }
        if (e.type == EV_MOUSE) {
            if (e.mact == MB_DOWN || e.mact == MB_DBL || e.mact == MB_UP || e.mact == MB_MOVE) {
                if (e.mr > r && e.mr < r + n + 1 && e.mc > c && e.mc < c + w - 1) {
                    int i = e.mr - r - 1;
                    if (it[i].label) { sel = i; if (e.mact == MB_UP || e.mact == MB_DBL) choose = 1; else if (e.mact == MB_DOWN && it[i].sub) choose = 1; }
                } else if (e.mact == MB_DOWN) {
                    if (e.mr == 1 && level == 0 && menus) {
                        int mi = menubar_hit(menus, nmenus, e.mc);
                        if (mi >= 0) { g_cmdarg = mi; result = -3; break; }
                    }
                    result = 0; break;
                }
            }
            if (!choose) continue;
        }
        if (e.type == EV_KEY) {
            int k = e.key;
            if (k == K_ESC) { result = 0; break; }
            if (k == K_UP || k == CTRL('E')) { sel = next_item(it, n, sel, -1); continue; }
            if (k == K_DOWN || k == CTRL('X')) { sel = next_item(it, n, sel, 1); continue; }
            if (k == K_HOME) { sel = next_item(it, n, n - 1, 1); continue; }
            if (k == K_END) { sel = next_item(it, n, 0, -1); continue; }
            if (k == K_LEFT || k == CTRL('S')) { result = -1; break; }
            if (k == K_RIGHT || k == CTRL('D')) {
                if (it[sel].sub) choose = 1; else { result = -2; break; }
            }
            if (k == K_ENTER) choose = 1;
            if (!choose && k >= K_ALT && k < K_ALT + 0x100 && level == 0) { result = -100 - (k - K_ALT); break; }
            if (!choose && k < 0x10000 && k >= 32) {
                int K = towupper((wint_t)k);
                for (int i = 0; i < n; i++) if (it[i].label && hotkey_of(it[i].label) == K) { sel = i; choose = 1; break; }
                if (!choose) continue;
            }
            if (!choose) continue;
        }
        if (choose) {
            if (it[sel].sub) {
                scr_restore(buf);
                draw_box(it, n, r, c, w, sel);
                int rr = run_box(NULL, 0, it[sel].sub, it[sel].nsub, r + 1 + sel, c + w - 2, level + 1, arg);
                if (rr > 0) { result = rr; break; }
                if (rr == 0 && 0) { result = 0; break; }
                if (rr == -2) { result = -2; break; }
                continue;
            }
            if (it[sel].cmd == CMD_NOTAVAIL) { result = CMD_NOTAVAIL; break; }
            *arg = it[sel].arg;
            result = it[sel].cmd;
            if (!result) result = CMD_NOTAVAIL;
            break;
        }
    }
    scr_restore(buf);
    free(buf);
    return result;
}

static void init_markmenus(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (int i = 0; i < 10; i++) {
        m_gomark[i] = m_marks[i]; m_gomark[i].cmd = CMD_GOTOMARK;
        m_setmark[i] = m_marks[i]; m_setmark[i].cmd = CMD_SETMARK;
        static wchar_t kg[10][6], ks[10][6];
        swprintf(kg[i], 6, L"^Q%d", i); swprintf(ks[i], 6, L"^K%d", i);
        m_gomark[i].key = kg[i]; m_setmark[i].key = ks[i];
    }
}

int pulldown(const TopMenu *menus, int n, int start, int *arg)
{
    init_markmenus();
    int cur = start;
    Cell *buf = xmalloc(sizeof(scr));
    scr_save(buf);
    int result = 0;
    for (;;) {
        scr_restore(buf);
        draw_menubar(menus, n, cur);
        int c = menu_col(&menus[cur]) - 1;
        int r = run_box(menus, n, menus[cur].items, menus[cur].n, 2, c, 0, arg);
        if (r == -1) { cur = (cur + n - 1) % n; continue; }
        if (r == -2) { cur = (cur + 1) % n; continue; }
        if (r == -3) { cur = g_cmdarg; continue; }
        if (r <= -100) {
            int k = -(r + 100), found = -1;
            for (int i = 0; i < n; i++) if (hotkey_of(menus[i].label) == k) found = i;
            if (found >= 0) { cur = found; continue; }
            continue;
        }
        result = r;
        break;
    }
    scr_restore(buf);
    free(buf);
    return result;
}

/* ---------------------------------------------------------------- classic prefix menus */
typedef struct { int key; int cmd; int arg; } CKey;

static const CKey ck_K[] = {
    { 'B', CMD_BB }, { 'K', CMD_BK }, { 'H', CMD_BHIDE }, { 'C', CMD_BCOPY }, { 'V', CMD_BMOVE },
    { 'Y', CMD_BDEL }, { 'W', CMD_BWRITE }, { 'R', CMD_BREAD }, { 'A', CMD_BCOPYWIN }, { 'G', CMD_BMOVEWIN },
    { ']', CMD_CLIPTO }, { '[', CMD_CLIPFROM }, { 'U', CMD_BPREV }, { 'N', CMD_COLBLOCK }, { 'I', CMD_COLREPLACE },
    { '"', CMD_UPPER }, { '\'', CMD_LOWER }, { '.', CMD_SENTENCE }, { '?', CMD_WORDCOUNT }, { 'M', CMD_BLOCKMATH },
    { 'Z', CMD_SORT }, { 'S', CMD_SAVE }, { 'T', CMD_SAVEAS }, { 'D', CMD_SAVECLOSE }, { 'X', CMD_SAVEEXIT },
    { 'Q', CMD_ABANDON }, { 'P', CMD_PRINT }, { 'O', CMD_FCOPY }, { 'J', CMD_FDELETE }, { 'E', CMD_FRENAME },
    { 'L', CMD_CHDIR }, { 'F', CMD_RUNDOS },
    { '0', CMD_SETMARK, 0 }, { '1', CMD_SETMARK, 1 }, { '2', CMD_SETMARK, 2 }, { '3', CMD_SETMARK, 3 }, { '4', CMD_SETMARK, 4 },
    { '5', CMD_SETMARK, 5 }, { '6', CMD_SETMARK, 6 }, { '7', CMD_SETMARK, 7 }, { '8', CMD_SETMARK, 8 }, { '9', CMD_SETMARK, 9 },
    { 0 }
};
static const CKey ck_Q[] = {
    { 'S', CMD_LINEBEG }, { 'D', CMD_LINEEND }, { 'E', CMD_SCRTOP }, { 'X', CMD_SCRBOT }, { 'R', CMD_DOCBEG },
    { 'C', CMD_DOCEND }, { 'B', CMD_BLOCKBEG }, { 'K', CMD_BLOCKEND }, { 'P', CMD_PREVPOS }, { 'V', CMD_LASTFIND },
    { 'F', CMD_FIND }, { 'A', CMD_REPLACE }, { 'I', CMD_GOTOPAGE }, { 'G', CMD_GOTOCHAR }, { 'H', CMD_GOTOCHARBACK },
    { 'Y', CMD_DELEOL }, { K_DEL, CMD_DELBOL }, { K_BS, CMD_DELBOL }, { 'T', CMD_DELTOCHAR }, { 'U', CMD_REFORMATREST },
    { 'M', CMD_CALC }, { 'L', CMD_SPELLREST }, { 'N', CMD_SPELLWORD }, { 'O', CMD_SPELLTYPE }, { 'J', CMD_THESAURUS },
    { 'W', CMD_SCROLLUPC }, { 'Z', CMD_SCROLLDNC }, { '=', CMD_GOTOFONT }, { '<', CMD_GOTOSTYLE },
    { '0', CMD_GOTOMARK, 0 }, { '1', CMD_GOTOMARK, 1 }, { '2', CMD_GOTOMARK, 2 }, { '3', CMD_GOTOMARK, 3 }, { '4', CMD_GOTOMARK, 4 },
    { '5', CMD_GOTOMARK, 5 }, { '6', CMD_GOTOMARK, 6 }, { '7', CMD_GOTOMARK, 7 }, { '8', CMD_GOTOMARK, 8 }, { '9', CMD_GOTOMARK, 9 },
    { 0 }
};
static const CKey ck_O[] = {
    { 'L', CMD_LMARGIN }, { 'R', CMD_RMARGIN }, { 'C', CMD_CENTER }, { ']', CMD_RIGHTALIGN }, { 'I', CMD_RULER },
    { 'S', CMD_ALIGNSPACE }, { 'J', CMD_JUSTIFY }, { 'W', CMD_WORDWRAP }, { 'D', CMD_TAGS },
    { 'B', CMD_SCREENSET }, { 'P', CMD_PREVIEW }, { 'K', CMD_OPENSWITCH }, { 'M', CMD_WINSIZE }, { 'Y', CMD_PAGELAYOUT },
    { '#', CMD_PAGENUM }, { '?', CMD_STATUS }, { 'F', CMD_PSTYLE }, { 'U', CMD_COLUMNS }, { 'N', CMD_NOTEPREFIX },
    { 'H', CMD_AUTOHYPH }, { 'E', CMD_SOFTHYPH }, { 'V', CMD_VCENTER }, { 'Z', CMD_PARANUM },
    { 0 }
};
static const CKey ck_P[] = {
    { 'B', CMD_BOLD }, { 'Y', CMD_ITALIC }, { 'S', CMD_UNDER }, { 'X', CMD_STRIKE }, { 'V', CMD_SUB },
    { 'T', CMD_SUPER }, { 'D', CMD_DOUBLE }, { '=', CMD_FONT }, { '0', CMD_EXTCHAR }, { 'O', CMD_BINDSPACE },
    { '*', CMD_GRAPHIC }, { 'K', CMD_INDEXMARK },
    { 0 }
};
static const CKey ck_M[] = {
    { '@', CMD_DATE }, { '!', CMD_TIME }, { '=', CMD_MATHRES }, { '#', CMD_MATHEXPR }, { '$', CMD_MATHDOLLAR },
    { '*', CMD_FILENAME }, { ':', CMD_DRIVE }, { '.', CMD_DIRECTORY }, { '\\', CMD_PATH },
    { 0 }
};

static const wchar_t *cm_K[] = {
    L"   SAVE             BLOCK                 FILE            WINDOWS",
    L"S save & resume  B begin    K end      O copy file     A copy from other",
    L"D save & close   H hide/display        J delete file   G move from other",
    L"T save as        C copy     V move     E rename file   [ paste clipboard",
    L"X save & exit    Y delete   W write    L change dir    ] copy clipboard",
    L"Q abandon file   R read     U previous P print file    F run DOS command",
    L"0-9 set marker   N column block mode   I column replace mode",
    L"\" uppercase  ' lowercase  . sentence  ? word count  M math  Z sort",
};
static const wchar_t *cm_Q[] = {
    L"   CURSOR                 FIND              DELETE         OTHER",
    L"S begin line  D end line  F find text       Y line right   M calculator",
    L"E top screen  X bottom    A find & replace  Del line left  U align rest",
    L"R begin file  C end file  G go to char      T to char      W scroll up",
    L"B begin block K end block H char backward                  Z scroll down",
    L"P previous position       I go to page      SPELLING       = font tag",
    L"V last find/replace       0-9 go to marker  L rest of doc  < style tag",
    L"                                            N word  O type J thesaurus",
};
static const wchar_t *cm_O[] = {
    L"   MARGINS & PAGE    PARAGRAPH          DISPLAY           OTHER",
    L"L left margin        C center line      D tags & dots     K open window",
    L"R right margin       ] right align      B screen settings M window size",
    L"I ruler / tabs       S align & spacing  P preview         ? status",
    L"Y page layout        J justify on/off   W word wrap       N notes",
    L"U columns            F paragraph style  H hyphenation     E soft hyphen",
    L"# page numbers       Z paragraph number V center page vertically",
};
static const wchar_t *cm_P[] = {
    L"   PRINT CONTROLS",
    L"B bold          Y italic        S underline     X strikeout",
    L"T superscript   V subscript     D double strike = font",
    L"0 extended character            O binding space",
    L"* graphic                       K index mark",
};
static const wchar_t *cm_M[] = {
    L"   INSERT VALUES",
    L"@ today's date     ! current time     = last math result",
    L"$ last math as dollar     # last math expression",
    L"* filename     : drive     . directory     \\ path",
};

int classic_prefix(int prefix, int *arg)
{
    const CKey *tab; const wchar_t **menu; int nlines; const wchar_t *title;
    switch (prefix) {
    case 'K': tab = ck_K; menu = cm_K; nlines = N(cm_K); title = L"B L O C K   &   S A V E"; break;
    case 'Q': tab = ck_Q; menu = cm_Q; nlines = N(cm_Q); title = L"Q U I C K"; break;
    case 'O': tab = ck_O; menu = cm_O; nlines = N(cm_O); title = L"O N S C R E E N   F O R M A T"; break;
    case 'P': tab = ck_P; menu = cm_P; nlines = N(cm_P); title = L"P R I N T   C O N T R O L S"; break;
    case 'M': tab = ck_M; menu = cm_M; nlines = N(cm_M); title = L"I N S E R T   V A L U E S"; break;
    default: return 0;
    }
    /* show ^X in the status line corner */
    wchar_t pf[4] = { '^', (wchar_t)prefix, 0 };
    int sr = SR - 1;
    Cell *buf = xmalloc(sizeof(scr));
    scr_save(buf);
    scr_fill(sr, 0, SC - 44, ' ', COL[C_STATUS]);
    scr_puts(sr, 1, pf, COL[C_STATUSHI]);
    Event e; int k = 0, shown = 0;
    for (;;) {
        int got = ev_get(&e, (shown || S.helplevel <= 1) ? -1 : S.menu_delay);
        if (!got) {
            int h = nlines + 2, r = 3, w = SC - 2;
            scr_rect(r, 1, h, w, ' ', COL[C_MENUBOX]);
            scr_box(r, 1, h, w, COL[C_MENUBOX], 0);
            int tl = (int)wcslen(title);
            scr_puts(r, 1 + (w - tl) / 2, title, COL[C_MENUBOXHOT]);
            for (int i = 0; i < nlines; i++) scr_putsn(r + 1 + i, 3, menu[i], w - 4, i == 0 ? COL[C_MENUBOXHOT] : COL[C_MENUBOX]);
            shown = 1;
            continue;
        }
        if (e.type == EV_CLOSE) { k = K_ESC; break; }
        if (e.type == EV_KEY) { k = e.key; break; }
        if (e.type == EV_MOUSE && e.mact == MB_DOWN) { k = K_ESC; break; }
    }
    scr_restore(buf);
    free(buf);
    if (k == K_ESC || k == ' ') return 0;
    int K = k;
    if (k >= 1 && k <= 26) K = 'A' + k - 1;
    else if (k == K_CTRLM) K = 'M';
    else if (k < 0x10000) K = towupper((wint_t)k);
    else if (k == K_CDEL) K = K_DEL;
    else if (k == K_CBS) K = K_BS;
    for (int i = 0; tab[i].key; i++) if (tab[i].key == K) { *arg = tab[i].arg; return tab[i].cmd; }
    return 0;
}
