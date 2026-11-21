/* ===== aniwatch - Step 1 v2: dango-style UI, anime + movies library ===== */
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/gxm.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <curl/curl.h>
#include <psp2/io/fcntl.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <curl/curl.h>
#include <malloc.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>

#define APP_VERSION "v1.0.0"
#define SCREEN_W 960
#define SCREEN_H 544
#define DATA_ROOT  "ux0:data/aniwatch"
#define ANIME_ROOT DATA_ROOT "/anime"
#define MOVIE_ROOT DATA_ROOT "/movies"
#define FONT_REG  "app0:font.ttf"
#define FONT_BOLD "app0:font_bold.ttf"
#define MAX_SHOWS 384
#define SEASON_SPECIAL 100
#define POSTER_W 200
#define POSTER_H 300
#define BANNER_W 480
#define BANNER_H 270

/* colours (dango style) */
#define C_BG      13, 13, 16
#define C_BAR     20, 21, 30
#define C_PANEL   28, 28, 36
#define C_PANEL2  42, 42, 54
#define C_LINE    52, 52, 66
#define C_TEXT    242, 242, 246
#define C_SOFT    200, 200, 212
#define C_DIM     140, 140, 156
#define C_ACCENT  108, 99, 255
#define C_ACCLT   152, 144, 255
#define C_TINT    34, 31, 74

/* layout */
#define BAR_H     60
#define HERO_H    340
#define SEC_H     308
#define CARD_W    128
#define CARD_H    192
#define CARD_GAP  18
#define GCOLS     6
#define GCARD_W   132
#define GCARD_H   198
#define GGAP      22
#define GROW_H    256
#define GRID_Y    150
#define DRAWER_W  300
#define D_LIST_Y  452
#define D_ROW_EP  66
#define D_ROW_HEAD 42
#define HINT_CY   (SCREEN_H - 18)

/* ===== Globals ===== */
static SDL_Window *win;
static SDL_Renderer *ren;

enum { F_SMALL, F_SMALLB, F_BODY, F_BODYB, F_SUB, F_LOGO, F_H2, F_HERO, F_COUNT };
static const int font_px[F_COUNT]   = { 15, 15, 18, 18, 22, 26, 30, 44 };
static const int font_bold[F_COUNT] = { 0, 1, 0, 1, 1, 1, 1, 1 };
static TTF_Font *fonts[F_COUNT];

enum { K_ANIME, K_MOVIE };

typedef struct {
    char path[512], file[256], label[128], sub[160];
    int season, number;
} Episode;

typedef struct {
    int kind;
    char name[128], card_name[128];
    char dir[512], cover_path[512], banner_path[512];
    char synopsis[1024], genres[160], year[16], rating[8], type[16], status[24];
    Episode *eps; int neps, cap, nseasons;
    SDL_Texture *cover; int cover_state;   /* 0 not tried, 1 loaded, 2 none */
    unsigned pg_gen; int pg_done; float pg_f;   /* watch progress cache (Step 3) */
} Show;

static Show shows[MAX_SHOWS];
static int nshows;
static int anime_ix[MAX_SHOWS], nanime, movie_ix[MAX_SHOWS], nmovie;

enum { PG_HOME, PG_ANIME, PG_MOVIES };
static int page = PG_HOME;
static float tab_x;

/* home */
static int   h_focus, h_btn, row_sel[3];   /* h_focus: 0 hero, else HR_ row + 1 */
static float row_scroll[3], row_target[3], h_scroll, h_target;
static int   feat; static float feat_t, hero_a = 1;
static char  hero_title[128], hero_meta[200], hero_genres[200];

/* grids */
static int   g_sel[2];
static float g_scroll[2], g_target[2];

/* drawer */
static int   drawer_open, drawer_sel;
static float drawer_t;

/* details */
typedef struct { int ep, season; } Row;
static int    in_details, d_show, d_focus, d_btn, d_has_list;
static Row   *rows; static int *row_y; static int nrows, rows_h, d_sel;
static float  d_scroll, d_target;
static char   d_title[128], d_meta[200], d_genres[200];

typedef struct { SDL_Texture *t; int w, h; } WTex;
static WTex hero_syn, d_syn;

/* misc */
static char  toast_msg[160];
static float toast_t;
static unsigned frame_no = 1;
static int cover_budget;
static SDL_Texture *circ_tex, *dots_tex;
#define CIRC 128

/* input */
static unsigned btn, btn_old;
static int st_up, st_down, st_left, st_right;
static float rep_t[4]; static int rep_on[4];
static int   t_down, t_axis, tap_pending;
static float t_x0, t_y0, t_lx, t_ly, drag_dx, drag_dy, tap_x, tap_y;

/* tap targets (drawn last frame, tested this frame) */
enum { H_NONE, H_MENU, H_TAB, H_REFRESH, H_SETTINGS, H_HPLAY, H_HDETAILS, H_HPREV, H_HNEXT,
       H_ROWCARD, H_SEEALL, H_GRIDCARD, H_DPLAY, H_DBACK, H_EP, H_DRAWER_ITEM, H_DRAWER_OUT,
       H_DFETCH, H_FRES, H_FSRC, H_FKB };
typedef struct { int x, y, w, h, id, arg; } Hit;
#define MAX_HITS 200
static Hit hits[MAX_HITS], hits_prev[MAX_HITS];
static int nhits, nhits_prev;

/* ===== Small helpers ===== */
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float lerpk(float dt) { return clampf(dt * 14.0f, 0, 1); }
static int ends_with_ci(const char *s, const char *ext) {
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && strcasecmp(s + a - b, ext) == 0;
}
static int file_exists(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }

static void toast(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(toast_msg, sizeof toast_msg, fmt, ap);
    va_end(ap);
    toast_t = 2.4f;
}

static void hit(int x, int y, int w, int h, int id, int arg, int content) {
    if (content) {
        int y0 = y < BAR_H ? BAR_H : y, y1 = y + h > SCREEN_H ? SCREEN_H : y + h;
        if (y1 <= y0) return;
        y = y0; h = y1 - y0;
    }
    if (nhits < MAX_HITS) { Hit t = { x, y, w, h, id, arg }; hits[nhits++] = t; }
}
static Hit *tap_hit(void) {
    if (!tap_pending) return NULL;
    for (int i = nhits_prev - 1; i >= 0; i--) {
        Hit *h = &hits_prev[i];
        if (tap_x >= h->x && tap_x < h->x + h->w && tap_y >= h->y && tap_y < h->y + h->h) return h;
    }
    return NULL;
}

/* ===== Drawing primitives ===== */
static void fill(int x, int y, int w, int h, int r, int g, int b, int a) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(ren, &rc);
}
static void frame_rect(int x, int y, int w, int h, int t, int r, int g, int b, int a) {
    fill(x, y, w, t, r, g, b, a); fill(x, y + h - t, w, t, r, g, b, a);
    fill(x, y + t, t, h - 2 * t, r, g, b, a); fill(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}
static void grad_v(int x, int y, int w, int h, int r, int g, int b, int a0, int a1) {
    const int steps = 16;
    for (int i = 0; i < steps; i++) {
        int y0 = y + h * i / steps, y1 = y + h * (i + 1) / steps;
        fill(x, y0, w, y1 - y0, r, g, b, a0 + (a1 - a0) * i / (steps - 1));
    }
}
static void grad_h(int x, int y, int w, int h, int r, int g, int b, int a0, int a1) {
    const int steps = 24;
    for (int i = 0; i < steps; i++) {
        int x0 = x + w * i / steps, x1 = x + w * (i + 1) / steps;
        fill(x0, y, x1 - x0, h, r, g, b, a0 + (a1 - a0) * i / (steps - 1));
    }
}
static void tex_col(SDL_Texture *t, int r, int g, int b, int a) {
    SDL_SetTextureColorMod(t, r, g, b);
    SDL_SetTextureAlphaMod(t, a);
}

/* anti-aliased disc/ring texture */
static SDL_Texture *make_ring_tex(int size, float r_out, float r_in) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    Uint32 *px = s->pixels; int p = s->pitch / 4; float c = size * 0.5f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c, d = sqrtf(dx * dx + dy * dy);
            float a = clampf(r_out - d + 0.5f, 0, 1);
            if (r_in > 0) a *= clampf(d - r_in + 0.5f, 0, 1);
            px[y * p + x] = ((Uint32)(a * 255 + 0.5f) << 24) | 0xFFFFFF;
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

typedef struct { int r, t; SDL_Texture *tex; } RingC;
static RingC rings[32]; static int nrings;
static SDL_Texture *ring_tex(int r, int t) {
    for (int i = 0; i < nrings; i++) if (rings[i].r == r && rings[i].t == t) return rings[i].tex;
    if (nrings >= 32) return NULL;
    SDL_Texture *tx = make_ring_tex(r * 2, (float)r, (float)(r - t));
    rings[nrings].r = r; rings[nrings].t = t; rings[nrings].tex = tx; nrings++;
    return tx;
}

static void rrect(int x, int y, int w, int h, int rad, int r, int g, int b, int a) {
    if (w <= 0 || h <= 0) return;
    if (rad * 2 > w) rad = w / 2;
    if (rad * 2 > h) rad = h / 2;
    if (rad < 1) { fill(x, y, w, h, r, g, b, a); return; }
    tex_col(circ_tex, r, g, b, a);
    int q = CIRC / 2;
    SDL_Rect s[4] = { {0,0,q,q}, {q,0,q,q}, {0,q,q,q}, {q,q,q,q} };
    SDL_Rect d[4] = { {x,y,rad,rad}, {x+w-rad,y,rad,rad}, {x,y+h-rad,rad,rad}, {x+w-rad,y+h-rad,rad,rad} };
    for (int i = 0; i < 4; i++) SDL_RenderCopy(ren, circ_tex, &s[i], &d[i]);
    fill(x + rad, y, w - 2 * rad, h, r, g, b, a);
    fill(x, y + rad, rad, h - 2 * rad, r, g, b, a);
    fill(x + w - rad, y + rad, rad, h - 2 * rad, r, g, b, a);
}
static void rrect_line(int x, int y, int w, int h, int rad, int t, int r, int g, int b, int a) {
    if (rad * 2 > w) rad = w / 2;
    if (rad * 2 > h) rad = h / 2;
    SDL_Texture *rt = rad > 0 ? ring_tex(rad, t) : NULL;
    if (!rt) { frame_rect(x, y, w, h, t, r, g, b, a); return; }
    tex_col(rt, r, g, b, a);
    SDL_Rect s[4] = { {0,0,rad,rad}, {rad,0,rad,rad}, {0,rad,rad,rad}, {rad,rad,rad,rad} };
    SDL_Rect d[4] = { {x,y,rad,rad}, {x+w-rad,y,rad,rad}, {x,y+h-rad,rad,rad}, {x+w-rad,y+h-rad,rad,rad} };
    for (int i = 0; i < 4; i++) SDL_RenderCopy(ren, rt, &s[i], &d[i]);
    fill(x + rad, y, w - 2 * rad, t, r, g, b, a);
    fill(x + rad, y + h - t, w - 2 * rad, t, r, g, b, a);
    fill(x, y + rad, t, h - 2 * rad, r, g, b, a);
    fill(x + w - t, y + rad, t, h - 2 * rad, r, g, b, a);
}
static void disc(float cx, float cy, float rad, int r, int g, int b, int a) {
    tex_col(circ_tex, r, g, b, a);
    SDL_Rect d = { (int)(cx - rad), (int)(cy - rad), (int)(rad * 2), (int)(rad * 2) };
    SDL_RenderCopy(ren, circ_tex, NULL, &d);
}

/* filled triangles via SDL_RenderGeometry */
static SDL_Color gcol;
static void gc(int r, int g, int b, int a) { gcol.r = r; gcol.g = g; gcol.b = b; gcol.a = a; }
static void tri(float x0, float y0, float x1, float y1, float x2, float y2) {
    SDL_Vertex v[3];
    memset(v, 0, sizeof v);
    v[0].position.x = x0; v[0].position.y = y0;
    v[1].position.x = x1; v[1].position.y = y1;
    v[2].position.x = x2; v[2].position.y = y2;
    v[0].color = v[1].color = v[2].color = gcol;
    SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
}
static void line_w(float x0, float y0, float x1, float y1, float w) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny);
}
static void fillg(float x, float y, float w, float h) { fill((int)x, (int)y, (int)(w + 0.5f), (int)(h + 0.5f), gcol.r, gcol.g, gcol.b, gcol.a); }
static void rrectg(float x, float y, float w, float h, int rad) { rrect((int)x, (int)y, (int)(w + 0.5f), (int)(h + 0.5f), rad, gcol.r, gcol.g, gcol.b, gcol.a); }
static void discg(float cx, float cy, float r) { disc(cx, cy, r, gcol.r, gcol.g, gcol.b, gcol.a); }

/* ===== Icons (fg colour = gcol, bg = colour for cut-outs) ===== */
enum { IC_MENU, IC_HOME, IC_TV, IC_FILM, IC_GEAR, IC_REFRESH, IC_PLAY, IC_INFO, IC_STAR, IC_LEFT, IC_RIGHT };
static void icon(int ic, float cx, float cy, float s, int br, int bg, int bb) {
    SDL_Color fg = gcol;
    switch (ic) {
    case IC_MENU:
        for (int i = -1; i <= 1; i++) rrectg(cx - 10 * s, cy + i * 6 * s - 1.5f * s, 20 * s, 3 * s, 1);
        break;
    case IC_HOME:
        tri(cx - 11 * s, cy - 1 * s, cx, cy - 11 * s, cx + 11 * s, cy - 1 * s);
        rrectg(cx - 7.5f * s, cy - 2 * s, 15 * s, 11 * s, 1);
        gc(br, bg, bb, 255); fillg(cx - 2.5f * s, cy + 3 * s, 5 * s, 6 * s);
        break;
    case IC_TV:
        rrectg(cx - 11 * s, cy - 8 * s, 22 * s, 15 * s, (int)(3 * s));
        gc(br, bg, bb, 255); rrectg(cx - 8 * s, cy - 5 * s, 16 * s, 9 * s, 1);
        gcol = fg; fillg(cx - 5 * s, cy + 8.5f * s, 10 * s, 2.5f * s);
        break;
    case IC_FILM:
        rrectg(cx - 9 * s, cy - 10 * s, 18 * s, 20 * s, 2);
        gc(br, bg, bb, 255);
        rrectg(cx - 4.5f * s, cy - 7 * s, 9 * s, 14 * s, 1);
        for (int i = 0; i < 4; i++) {
            fillg(cx - 7.5f * s, cy - 8 * s + i * 4.6f * s, 2 * s, 2 * s);
            fillg(cx + 5.5f * s, cy - 8 * s + i * 4.6f * s, 2 * s, 2 * s);
        }
        break;
    case IC_GEAR:
        for (int k = 0; k < 8; k++) {
            float a = k * 0.785398f, c = cosf(a), sn = sinf(a);
            line_w(cx + c * 5 * s, cy + sn * 5 * s, cx + c * 10.5f * s, cy + sn * 10.5f * s, 4.5f * s);
        }
        discg(cx, cy, 7.5f * s);
        gc(br, bg, bb, 255); discg(cx, cy, 3.2f * s);
        break;
    case IC_REFRESH: {
        float r = 8 * s, a0 = 0.7f, a1 = 5.5f, px = 0, py = 0;
        for (float a = a0; a <= a1 + 0.001f; a += 0.32f) {
            float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
            if (a > a0) line_w(px, py, x, y, 2.6f * s);
            px = x; py = y;
        }
        float ex = cx + cosf(a1) * r, ey = cy + sinf(a1) * r;
        float tx = -sinf(a1), ty = cosf(a1), nx = cosf(a1), ny = sinf(a1);
        tri(ex + tx * 5 * s, ey + ty * 5 * s, ex + nx * 4.5f * s, ey + ny * 4.5f * s, ex - nx * 4.5f * s, ey - ny * 4.5f * s);
        break; }
    case IC_PLAY:
        tri(cx - 6 * s, cy - 8 * s, cx + 9 * s, cy, cx - 6 * s, cy + 8 * s);
        break;
    case IC_INFO:
        discg(cx, cy, 9.5f * s);
        gc(br, bg, bb, 255);
        fillg(cx - 1.3f * s, cy - 1 * s, 2.6f * s, 6.5f * s);
        discg(cx, cy - 4.5f * s, 1.6f * s);
        break;
    case IC_STAR: {
        float px[11], py[11];
        for (int k = 0; k <= 10; k++) {
            float a = -1.570796f + k * 0.628318f, r = (k % 2) ? 4.2f * s : 9.5f * s;
            px[k] = cx + cosf(a) * r; py[k] = cy + sinf(a) * r;
        }
        for (int k = 0; k < 10; k++) tri(cx, cy, px[k], py[k], px[k + 1], py[k + 1]);
        break; }
    case IC_LEFT:
        line_w(cx + 3 * s, cy - 7 * s, cx - 4 * s, cy, 2.8f * s);
        line_w(cx - 4 * s, cy, cx + 3 * s, cy + 7 * s, 2.8f * s);
        discg(cx - 4 * s, cy, 1.4f * s);
        break;
    case IC_RIGHT:
        line_w(cx - 3 * s, cy - 7 * s, cx + 4 * s, cy, 2.8f * s);
        line_w(cx + 4 * s, cy, cx - 3 * s, cy + 7 * s, 2.8f * s);
        discg(cx + 4 * s, cy, 1.4f * s);
        break;
    }
    gcol = fg;
}

static void dango_icon(float cx, float cy, float s) {
    gc(160, 128, 96, 255);
    line_w(cx - 15 * s, cy + 11 * s, cx + 15 * s, cy - 11 * s, 2.4f * s);
    disc(cx - 8 * s, cy + 6 * s, 6.5f * s, 150, 140, 240, 255);
    disc(cx,         cy,         6.5f * s, 230, 150, 200, 255);
    disc(cx + 8 * s, cy - 6 * s, 6.5f * s, 236, 228, 200, 255);
}

/* ===== Text (cached white textures, tinted at draw time) ===== */
#define TC_MAX 448
typedef struct { char *s; unsigned hash; int font, w, h; unsigned used; SDL_Texture *t; } TCEntry;
static TCEntry tcache[TC_MAX];

static unsigned str_hash(const char *s) { unsigned h = 5381; while (*s) h = h * 33 + (unsigned char)*s++; return h; }

static TCEntry *text_get(int f, const char *s) {
    if (!s || !s[0]) return NULL;
    unsigned h = str_hash(s) ^ ((unsigned)f * 2654435761u);
    int slot = -1;
    for (int i = 0; i < TC_MAX; i++) {
        TCEntry *e = &tcache[i];
        if (!e->s) { if (slot < 0 || tcache[slot].s) slot = i; continue; }
        if (e->hash == h && e->font == f && !strcmp(e->s, s)) { e->used = frame_no; return e; }
        if (slot < 0 || (tcache[slot].s && e->used < tcache[slot].used)) slot = i;
    }
    TCEntry *e = &tcache[slot];
    if (e->s) { free(e->s); SDL_DestroyTexture(e->t); e->s = NULL; e->t = NULL; }
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *sf = TTF_RenderUTF8_Blended(fonts[f], s, white);
    if (!sf) return NULL;
    e->t = SDL_CreateTextureFromSurface(ren, sf);
    e->w = sf->w; e->h = sf->h;
    SDL_FreeSurface(sf);
    if (!e->t) return NULL;
    SDL_SetTextureBlendMode(e->t, SDL_BLENDMODE_BLEND);
    e->s = strdup(s); e->hash = h; e->font = f; e->used = frame_no;
    return e;
}

enum { AL_L, AL_C, AL_R };
static int text(int f, int x, int y, int al, const char *s, int r, int g, int b, int a) {
    TCEntry *e = text_get(f, s);
    if (!e) return 0;
    if (al == AL_C) x -= e->w / 2; else if (al == AL_R) x -= e->w;
    tex_col(e->t, r, g, b, a);
    SDL_Rect d = { x, y, e->w, e->h };
    SDL_RenderCopy(ren, e->t, NULL, &d);
    return e->w;
}
static int textc(int f, int x, int cy, int al, const char *s, int r, int g, int b, int a) {
    TCEntry *e = text_get(f, s);
    if (!e) return 0;
    return text(f, x, cy - e->h / 2, al, s, r, g, b, a);
}
static int text_w(int f, const char *s) { TCEntry *e = text_get(f, s); return e ? e->w : 0; }

static void fit_text(int f, const char *src, int maxw, char *out, int outsz) {
    int w = 0;
    snprintf(out, outsz, "%s", src);
    if (TTF_SizeUTF8(fonts[f], out, &w, NULL) == 0 && w <= maxw) return;
    int len = (int)strlen(out);
    if (w > 0) {
        int est = len * maxw / w + 2;
        if (est < len) {
            while (est > 0 && ((unsigned char)out[est] & 0xC0) == 0x80) est--;
            out[est] = 0; len = est;
        }
    }
    char tmp[512] = "...";
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80) len--;
        out[len] = 0;
        while (len > 0 && out[len - 1] == ' ') out[--len] = 0;
        snprintf(tmp, sizeof tmp, "%s...", out);
        if (TTF_SizeUTF8(fonts[f], tmp, &w, NULL) == 0 && w <= maxw) break;
    }
    snprintf(out, outsz, "%s", len > 0 ? tmp : "...");
}

static void first_char(const char *s, char *out) {
    unsigned char c = (unsigned char)s[0];
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    memcpy(out, s, n); out[n] = 0;
    if (n == 1) out[0] = (char)toupper(c);
}

static void wtex_free(WTex *w) { if (w->t) SDL_DestroyTexture(w->t); w->t = NULL; }
static void wtex_make(WTex *w, int f, const char *s, int maxw, int lines) {
    wtex_free(w);
    if (!s || !s[0]) return;
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface *sf = TTF_RenderUTF8_Blended_Wrapped(fonts[f], s, white, maxw);
    if (!sf) return;
    int lh = TTF_FontLineSkip(fonts[f]), h = sf->h;
    if (h > lh * lines) h = lh * lines;
    w->t = SDL_CreateTextureFromSurface(ren, sf);
    w->w = sf->w; w->h = h;
    SDL_FreeSurface(sf);
}
static void wtex_draw(WTex *w, int x, int y, int r, int g, int b, int a) {
    if (!w->t) return;
    tex_col(w->t, r, g, b, a);
    SDL_Rect s = { 0, 0, w->w, w->h }, d = { x, y, w->w, w->h };
    SDL_RenderCopy(ren, w->t, &s, &d);
}

/* ===== Button icons + hints ===== */
enum { BI_X, BI_O, BI_TRI, BI_SQ, BI_DLR };
static void btn_icon(int k, float cx, float cy) {
    switch (k) {
    case BI_X:   gc(124, 178, 232, 255); line_w(cx - 5, cy - 5, cx + 5, cy + 5, 2.2f); line_w(cx + 5, cy - 5, cx - 5, cy + 5, 2.2f); break;
    case BI_O:   rrect_line((int)cx - 7, (int)cy - 7, 14, 14, 7, 2, 240, 100, 100, 255); break;
    case BI_TRI: gc(64, 210, 170, 255); line_w(cx, cy - 6, cx + 6, cy + 5, 2); line_w(cx + 6, cy + 5, cx - 6, cy + 5, 2); line_w(cx - 6, cy + 5, cx, cy - 6, 2); break;
    case BI_SQ:  rrect_line((int)cx - 6, (int)cy - 6, 12, 12, 2, 2, 220, 130, 220, 255); break;
    case BI_DLR: gc(C_SOFT, 255); icon(IC_LEFT, cx - 6, cy, 0.7f, 0, 0, 0); icon(IC_RIGHT, cx + 6, cy, 0.7f, 0, 0, 0); break;
    }
}
static int hint(int x, int kind, const char *label) {
    btn_icon(kind, x + 8, HINT_CY);
    int w = textc(F_SMALL, x + 22, HINT_CY, AL_L, label, C_SOFT, 255);
    return x + 22 + w + 24;
}
static int hint_key(int x, const char *key, const char *label) {
    int kw = text_w(F_SMALLB, key) + 14;
    rrect_line(x, HINT_CY - 10, kw, 20, 6, 1, C_SOFT, 220);
    textc(F_SMALLB, x + kw / 2, HINT_CY, AL_C, key, C_SOFT, 255);
    int w = textc(F_SMALL, x + kw + 8, HINT_CY, AL_L, label, C_SOFT, 255);
    return x + kw + 8 + w + 24;
}

/* ===== Images ===== */
static void round_mask(SDL_Surface *s, int rad) {
    Uint32 *px = s->pixels; int p = s->pitch / 4;
    for (int y = 0; y < rad; y++)
        for (int x = 0; x < rad; x++) {
            float dx = rad - (x + 0.5f), dy = rad - (y + 0.5f), d = sqrtf(dx * dx + dy * dy);
            float a = clampf(rad - d + 0.5f, 0, 1);
            if (a >= 1) continue;
            int xs[2] = { x, s->w - 1 - x }, ys[2] = { y, s->h - 1 - y };
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++) {
                    Uint32 *q = &px[ys[j] * p + xs[i]];
                    Uint32 al = (Uint32)((*q >> 24) * a);
                    *q = (*q & 0xFFFFFF) | (al << 24);
                }
        }
}

static SDL_Texture *load_image_fit(const char *path, int W, int H, int round) {
    SDL_Surface *s = IMG_Load(path);
    if (!s) return NULL;
    SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!c) return NULL;
    SDL_Rect src = { 0, 0, c->w, c->h };
    if ((long)c->w * H > (long)c->h * W) { src.w = c->h * W / H; src.x = (c->w - src.w) / 2; }
    else                                 { src.h = c->w * H / W; src.y = (c->h - src.h) / 2; }
    SDL_Surface *o = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!o) { SDL_FreeSurface(c); return NULL; }
    SDL_SetSurfaceBlendMode(c, SDL_BLENDMODE_NONE);
    if (SDL_SoftStretchLinear(c, &src, o, NULL) < 0) SDL_BlitScaled(c, &src, o, NULL);
    SDL_FreeSurface(c);
    if (round > 0) round_mask(o, round);
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, o);
    SDL_FreeSurface(o);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static void ensure_cover(Show *sh) {
    if (sh->cover_state != 0) return;
    sh->cover = load_image_fit(sh->cover_path, POSTER_W, POSTER_H, 16);
    sh->cover_state = sh->cover ? 1 : 2;
}

typedef struct { int show; SDL_Texture *t; unsigned used; } BCache;
static BCache bcache[3] = { { -1, NULL, 0 }, { -1, NULL, 0 }, { -1, NULL, 0 } };
static void bcache_clear(void) {
    for (int i = 0; i < 3; i++) {
        if (bcache[i].t) SDL_DestroyTexture(bcache[i].t);
        bcache[i].t = NULL; bcache[i].show = -1; bcache[i].used = 0;
    }
}
static SDL_Texture *get_banner(int si) {
    if (!shows[si].banner_path[0]) return NULL;
    int lru = 0;
    for (int i = 0; i < 3; i++) {
        if (bcache[i].show == si) { bcache[i].used = frame_no; return bcache[i].t; }
        if (bcache[i].used < bcache[lru].used) lru = i;
    }
    if (bcache[lru].t) SDL_DestroyTexture(bcache[lru].t);
    bcache[lru].show = si;
    bcache[lru].used = frame_no;
    bcache[lru].t = load_image_fit(shows[si].banner_path, BANNER_W, BANNER_H, 0);
    return bcache[lru].t;
}

static SDL_Texture *make_dots(void) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 480, 272, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    Uint32 *px = s->pixels; int p = s->pitch / 4;
    for (int y = 0; y < 272; y++)
        for (int x = 0; x < 480; x++)
            px[y * p + x] = (x % 4 == 1 && y % 4 == 1) ? 0xFFFFFFFF : 0x00FFFFFF;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

/* backdrop for hero and details: banner (or blurred cover) + dark fades + dots */
static void draw_backdrop(int si, int y, int h, int a) {
    Show *sh = &shows[si];
    SDL_Texture *t = get_banner(si);
    SDL_Rect src;
    int dim;
    if (t) {
        src.w = BANNER_W; src.h = BANNER_W * h / SCREEN_W;
        if (src.h > BANNER_H) src.h = BANNER_H;
        src.x = 0; src.y = (BANNER_H - src.h) * 2 / 5;
        dim = 135;
    } else {
        ensure_cover(sh);
        t = sh->cover;
        src.w = POSTER_W; src.h = POSTER_W * h / SCREEN_W;
        src.x = 0; src.y = POSTER_H * 2 / 5 - src.h / 2;
        dim = 85;
    }
    if (t) {
        SDL_Rect d = { 0, y, SCREEN_W, h };
        tex_col(t, dim, dim, dim + 8, a);
        SDL_RenderCopy(ren, t, &src, &d);
        tex_col(t, 255, 255, 255, 255);
    }
    grad_h(0, y, 640, h, C_BG, 235 * a / 255, 0);
    grad_v(0, y + h - 160, SCREEN_W, 160, C_BG, 0, 255);
    if (dots_tex) {
        SDL_Rect s2 = { 0, 0, 480, h / 2 > 272 ? 272 : h / 2 }, d2 = { 0, y, SCREEN_W, s2.h * 2 };
        tex_col(dots_tex, 255, 255, 255, 20 * a / 255);
        SDL_RenderCopy(ren, dots_tex, &s2, &d2);
    }
}

/* ===== Library scan ===== */
static void parse_ep(const char *name, int *fs, int *num) {
    char c[256]; int n = 0, depth = 0;
    for (const char *p = name; *p && n < 255; p++) {
        if (*p == '[' || *p == '(' || *p == '{') { depth++; continue; }
        if (*p == ']' || *p == ')' || *p == '}') { if (depth) depth--; continue; }
        if (!depth) c[n++] = (*p == '_' || *p == '.') ? ' ' : (char)tolower((unsigned char)*p);
    }
    c[n] = 0;
    *fs = 0; *num = 0;
    for (int i = 0; c[i]; i++) {
        if (c[i] == 's' && isdigit((unsigned char)c[i + 1]) && (i == 0 || !isalpha((unsigned char)c[i - 1]))) {
            int j = i + 1, s = 0;
            while (isdigit((unsigned char)c[j])) s = s * 10 + (c[j++] - '0');
            if (c[j] == 'e' && isdigit((unsigned char)c[j + 1])) { *fs = s; *num = atoi(&c[j + 1]); return; }
        }
    }
    for (int i = 0; c[i]; i++) {
        if (i > 0 && isalpha((unsigned char)c[i - 1])) continue;
        int k;
        if (!strncmp(&c[i], "episode", 7)) k = 7;
        else if (!strncmp(&c[i], "ep", 2)) k = 2;
        else if (c[i] == 'e') k = 1;
        else continue;
        int j = i + k;
        while (c[j] == ' ') j++;
        if (isdigit((unsigned char)c[j])) { *num = atoi(&c[j]); return; }
    }
    for (char *d = strstr(c, " - "); d; d = strstr(d + 1, " - "))
        if (isdigit((unsigned char)d[3])) { *num = atoi(d + 3); return; }
    int last = 0, found = 0;
    for (int i = 0; c[i]; ) {
        if (isdigit((unsigned char)c[i])) {
            int j = i, v = 0;
            while (isdigit((unsigned char)c[j])) v = v * 10 + (c[j++] - '0');
            int res = (c[j] == 'p' || c[j] == 'x' || (i > 0 && c[i - 1] == 'x'));
            if (!res) { last = v; found = 1; }
            i = j;
        } else i++;
    }
    if (found) *num = last;
}

static int season_of_dir(const char *n) {
    if (!strncasecmp(n, "special", 7) || !strncasecmp(n, "ova", 3) || !strncasecmp(n, "extra", 5))
        return SEASON_SPECIAL;
    const char *p = n;
    if (!strncasecmp(p, "season", 6)) p += 6;
    else if (p[0] == 's' || p[0] == 'S') p += 1;
    else return 0;
    while (*p == ' ' || *p == '_' || *p == '-' || *p == '.') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    int v = atoi(p);
    return (v > 0 && v < SEASON_SPECIAL) ? v : 0;
}

static void add_ep(Show *sh, const char *dir, const char *file, int season) {
    if (sh->neps >= sh->cap) {
        int nc = sh->cap ? sh->cap * 2 : 32;
        Episode *n = realloc(sh->eps, nc * sizeof(Episode));
        if (!n) return;
        sh->eps = n; sh->cap = nc;
    }
    Episode *e = &sh->eps[sh->neps++];
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s/%s", dir, file);
    snprintf(e->file, sizeof e->file, "%s", file);
    char *dot = strrchr(e->file, '.');
    if (dot) *dot = 0;
    int fs = 0, num = 0;
    parse_ep(e->file, &fs, &num);
    e->season = season ? season : (fs ? fs : 1);
    e->number = num;
}

static void scan_dir_eps(Show *sh, const char *dir, int season, int seasons_ok) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent de;
    for (;;) {
        memset(&de, 0, sizeof de);
        if (sceIoDread(d, &de) <= 0) break;
        if (de.d_name[0] == '.') continue;
        if (SCE_S_ISDIR(de.d_stat.st_mode)) {
            if (seasons_ok) {
                int s = season_of_dir(de.d_name);
                if (s) {
                    char full[512];
                    snprintf(full, sizeof full, "%s/%s", dir, de.d_name);
                    scan_dir_eps(sh, full, s, 0);
                }
            }
        } else if (ends_with_ci(de.d_name, ".mp4")) {
            add_ep(sh, dir, de.d_name, season);
        }
    }
    sceIoDclose(d);
}

static int ep_cmp(const void *a, const void *b) {
    const Episode *x = a, *y = b;
    if (x->season != y->season) return x->season - y->season;
    if (x->number != y->number) return x->number - y->number;
    return strcasecmp(x->file, y->file);
}
static int show_cmp(const void *a, const void *b) {
    return strcasecmp(((const Show *)a)->name, ((const Show *)b)->name);
}

static void load_info(Show *sh, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1200];
    int first = 1;
    while (fgets(line, sizeof line, f)) {
        char *l = line;
        if (first && (unsigned char)l[0] == 0xEF && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF) l += 3;
        first = 0;
        l[strcspn(l, "\r\n")] = 0;
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = l, *v = eq + 1;
        if (!strcasecmp(k, "title") && v[0]) snprintf(sh->name, sizeof sh->name, "%s", v);
        else if (!strcasecmp(k, "synopsis")) snprintf(sh->synopsis, sizeof sh->synopsis, "%s", v);
        else if (!strcasecmp(k, "year"))     snprintf(sh->year, sizeof sh->year, "%s", v);
        else if (!strcasecmp(k, "genres"))   snprintf(sh->genres, sizeof sh->genres, "%s", v);
        else if (!strcasecmp(k, "rating"))   snprintf(sh->rating, sizeof sh->rating, "%s", v);
        else if (!strcasecmp(k, "type"))     snprintf(sh->type, sizeof sh->type, "%s", v);
        else if (!strcasecmp(k, "status"))   snprintf(sh->status, sizeof sh->status, "%s", v);
    }
    fclose(f);
}

static int find_img(char *out, int n, const char *dir, const char *const *names, int cnt) {
    char d[512];
    snprintf(d, sizeof d, "%s", dir);   /* local copy: dir and out can live in the same Show */
    for (int i = 0; i < cnt; i++) {
        snprintf(out, n, "%s/%s", d, names[i]);
        if (file_exists(out)) return 1;
    }
    out[0] = 0;
    return 0;
}

static void finish_show(Show *sh) {
    qsort(sh->eps, sh->neps, sizeof(Episode), ep_cmp);
    sh->nseasons = 0;
    for (int i = 0; i < sh->neps; i++) {
        Episode *e = &sh->eps[i];
        if (i == 0 || e->season != sh->eps[i - 1].season) sh->nseasons++;
        if (sh->kind == K_ANIME && e->number > 0) snprintf(e->label, sizeof e->label, "Episode %d", e->number);
        else fit_text(F_BODYB, e->file, 690, e->label, sizeof e->label);
        fit_text(F_SMALL, e->file, 690, e->sub, sizeof e->sub);
    }
    sh->cover_state = sh->cover_path[0] ? 0 : 2;
    fit_text(F_SMALLB, sh->name, CARD_W + 4, sh->card_name, sizeof sh->card_name);
}

static const char *const cover_names[]  = { "cover.jpg", "cover.png", "poster.jpg", "poster.png", "folder.jpg", "cover.jpeg" };
static const char *const banner_names[] = { "banner.jpg", "banner.png", "fanart.jpg", "fanart.png", "backdrop.jpg" };

static void scan_root(const char *root, int kind) {
    SceUID d = sceIoDopen(root);
    if (d < 0) return;
    SceIoDirent de;
    while (nshows < MAX_SHOWS) {
        memset(&de, 0, sizeof de);
        if (sceIoDread(d, &de) <= 0) break;
        if (de.d_name[0] == '.') continue;
        Show *sh = &shows[nshows];
        memset(sh, 0, sizeof *sh);
        sh->kind = kind;
        if (SCE_S_ISDIR(de.d_stat.st_mode)) {
            snprintf(sh->name, sizeof sh->name, "%s", de.d_name);
            snprintf(sh->dir, sizeof sh->dir, "%s/%s", root, de.d_name);
            scan_dir_eps(sh, sh->dir, kind == K_MOVIE ? 1 : 0, kind == K_ANIME);
            if (sh->neps == 0) { free(sh->eps); memset(sh, 0, sizeof *sh); continue; }
            find_img(sh->cover_path, sizeof sh->cover_path, sh->dir, cover_names, 6);
            find_img(sh->banner_path, sizeof sh->banner_path, sh->dir, banner_names, 5);
            char ip[600];
            snprintf(ip, sizeof ip, "%s/info.txt", sh->dir);
            load_info(sh, ip);
        } else if (kind == K_MOVIE && ends_with_ci(de.d_name, ".mp4")) {
            char base[256];
            snprintf(base, sizeof base, "%s", de.d_name);
            base[strlen(base) - 4] = 0;
            snprintf(sh->name, sizeof sh->name, "%s", base);
            snprintf(sh->dir, sizeof sh->dir, "%s", root);
            add_ep(sh, root, de.d_name, 1);
            char a[300], b[300];
            snprintf(a, sizeof a, "%s.jpg", base);
            snprintf(b, sizeof b, "%s.png", base);
            const char *nm[2] = { a, b };
            find_img(sh->cover_path, sizeof sh->cover_path, root, nm, 2);
            char bn[300], ipl[600];
            snprintf(bn, sizeof bn, "%s-banner.jpg", base);
            const char *bnm[1] = { bn };
            find_img(sh->banner_path, sizeof sh->banner_path, root, bnm, 1);
            snprintf(ipl, sizeof ipl, "%s/%s.txt", root, base);
            load_info(sh, ipl);
        } else continue;
        finish_show(sh);
        nshows++;
    }
    sceIoDclose(d);
}

static void free_library(void) {
    for (int i = 0; i < nshows; i++) {
        free(shows[i].eps);
        if (shows[i].cover) SDL_DestroyTexture(shows[i].cover);
    }
    memset(shows, 0, sizeof shows);
    nshows = nanime = nmovie = 0;
    bcache_clear();
}

static void scan_library(void) {
    free_library();
    scan_root(ANIME_ROOT, K_ANIME);
    scan_root(MOVIE_ROOT, K_MOVIE);
    qsort(shows, nshows, sizeof(Show), show_cmp);
    for (int i = 0; i < nshows; i++) {
        if (shows[i].kind == K_ANIME) anime_ix[nanime++] = i;
        else movie_ix[nmovie++] = i;
    }
}

/* ===== Show text helpers ===== */
static void add_part(char *o, int n, const char *p) {
    if (!p || !p[0]) return;
    if (o[0]) strncat(o, "  \xE2\x80\xA2  ", n - strlen(o) - 1);
    strncat(o, p, n - strlen(o) - 1);
}
static void show_meta(Show *sh, char *o, int n) {
    char t[48];
    o[0] = 0;
    add_part(o, n, sh->type[0] ? sh->type : (sh->kind == K_MOVIE ? "MOVIE" : "TV"));
    add_part(o, n, sh->status);
    add_part(o, n, sh->year);
    if (sh->kind == K_ANIME) {
        if (sh->nseasons > 1) { snprintf(t, sizeof t, "%d SEASONS", sh->nseasons); add_part(o, n, t); }
        snprintf(t, sizeof t, sh->neps == 1 ? "%d EPISODE" : "%d EPISODES", sh->neps); add_part(o, n, t);
    } else if (sh->neps > 1) {
        snprintf(t, sizeof t, "%d PARTS", sh->neps); add_part(o, n, t);
    }
}
static void genres_fmt(const char *src, char *o, int n) {
    char tmp[200], *save = NULL;
    o[0] = 0;
    snprintf(tmp, sizeof tmp, "%s", src);
    for (char *t = strtok_r(tmp, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        while (*t == ' ') t++;
        int l = (int)strlen(t);
        while (l > 0 && t[l - 1] == ' ') t[--l] = 0;
        if (!l) continue;
        if (o[0]) strncat(o, "  /  ", n - strlen(o) - 1);
        strncat(o, t, n - strlen(o) - 1);
    }
}
static const char *badge_of(Show *sh) {
    if (sh->type[0]) return sh->type;
    return sh->kind == K_MOVIE ? "MOVIE" : "TV";
}

/* ===== Shared widgets ===== */
static void sec_head(int x, int y, const char *small, const char *big) {
    rrect(x, y + 6, 28, 4, 2, C_ACCENT, 255);
    text(F_SMALLB, x + 40, y, AL_L, small, C_ACCLT, 255);
    text(F_H2, x, y + 20, AL_L, big, C_TEXT, 255);
}
static int pill_btn(int xr, int cy, const char *label, int chevron) {   /* right-aligned pill */
    int w = text_w(F_SMALLB, label) + (chevron ? 46 : 28), x = xr - w;
    rrect(x, cy - 18, w, 36, 14, C_PANEL, 255);
    rrect_line(x, cy - 18, w, 36, 14, 1, C_LINE, 255);
    textc(F_SMALLB, x + 14, cy, AL_L, label, C_SOFT, 255);
    if (chevron) { gc(C_SOFT, 255); icon(IC_RIGHT, xr - 20, cy, 0.6f, 0, 0, 0); }
    return x;
}
static int rating_pill(int x, int cy, const char *rating, int a) {
    int w = text_w(F_BODYB, rating) + 52;
    rrect(x, cy - 18, w, 36, 18, 14, 14, 20, a * 220 / 255);
    rrect_line(x, cy - 18, w, 36, 18, 1, C_LINE, a);
    gc(C_ACCENT, a); icon(IC_STAR, x + 20, cy, 0.85f, 0, 0, 0);
    textc(F_BODYB, x + 36, cy, AL_L, rating, C_TEXT, a);
    return w;
}
static void button(int x, int y, int w, int h, int primary, int ic, const char *label, int focus, int a) {
    if (primary) rrect(x, y, w, h, 14, C_ACCENT, a);
    else { rrect(x, y, w, h, 14, 16, 16, 22, a * 200 / 255); rrect_line(x, y, w, h, 14, 2, C_LINE, a); }
    if (focus) rrect_line(x - 5, y - 5, w + 10, h + 10, 18, 2, C_TEXT, a);
    int tw = text_w(F_BODYB, label), cx = x + (w - (tw + 30)) / 2;
    gc(C_TEXT, a);
    if (primary) icon(ic, cx + 9, y + h / 2, 0.8f, 108, 99, 255);
    else         icon(ic, cx + 9, y + h / 2, 0.8f, 16, 16, 22);
    textc(F_BODYB, cx + 30, y + h / 2, AL_L, label, C_TEXT, a);
}
static void circle_btn(int cx, int cy, int ic, int id, int content) {
    disc(cx, cy, 22, 16, 16, 22, 210);
    rrect_line(cx - 22, cy - 22, 44, 44, 22, 1, C_LINE, 255);
    gc(C_TEXT, 255); icon(ic, cx, cy, 0.85f, 16, 16, 22);
    hit(cx - 24, cy - 24, 48, 48, id, 0, content);
}

/* ===== Watch progress + settings (Step 3) ===== */
#define PROG_PATH   DATA_ROOT "/progress.txt"
#define PROG_TMP    DATA_ROOT "/progress.tmp"
#define CFG_PATH    DATA_ROOT "/settings.txt"
#define PROG_DONE   0.90f      /* past 90% = watched */
#define PROG_MIN_MS 30000      /* under 30 s = start from the beginning */
#define CW_MAX      10         /* Continue Watching cards */
#define H_ST_BACK   92         /* settings hit ids (the H_ enum stays below 90) */
#define H_ST_ROW    93

enum { HR_CW, HR_ANIME, HR_MOVIE };   /* home rows; h_focus = row + 1, 0 = hero */

typedef struct { char path[512]; unsigned hash, seq; int64_t pos, dur; int done; } Prog;
static Prog *prog;
static int nprog, prog_cap, prog_dirty;
static unsigned prog_seq, prog_gen = 1;

typedef struct { char key[512]; unsigned hash, seq; } Hide;   /* shows removed from Continue Watching */
static Hide *hide;
static int nhide, hide_cap;

/* settings (settings.txt) */
static int cfg_auto = 1, cfg_count = 10, cfg_resume = 0;   /* resume: 0 ask, 1 always resume */
static int cfg_skip = 1;                                   /* skip intro/outro: 0 auto, 1 button, 2 off */
static int in_settings, st_sel, st_confirm;

/* player resume + auto-next state */
static int64_t pl_resume_ms, pl_jump_ms;
static float pl_jump_t, pl_save_t;
static int an_on;
static float an_t;
static char an_line[160], an_sub[160];

/* "Resume watching?" box */
static int rs_open, rs_show, rs_ep, rs_sel;
static int64_t rs_ms, rs_dur;
static float rs_t;
static char rs_line[160];

/* Continue Watching */
typedef struct { int show, ep; unsigned seq; int64_t pos, dur; char tag[24], left[24]; } CWItem;
static CWItem cw[CW_MAX];
static int ncw;
static unsigned cw_gen;

static void fmt_time(char *o, int n, int64_t ms);
static void settings_open(void);

static Prog *prog_find(const char *path) {
    unsigned h = str_hash(path);
    for (int i = 0; i < nprog; i++)
        if (prog[i].hash == h && !strcmp(prog[i].path, path)) return &prog[i];
    return NULL;
}

static Prog *prog_get(const char *path) {   /* find or add (pointers die on the next add) */
    Prog *p = prog_find(path);
    if (p) return p;
    if (nprog == prog_cap) {
        int nc = prog_cap ? prog_cap * 2 : 256;
        Prog *np = realloc(prog, nc * sizeof(Prog));
        if (!np) return NULL;
        prog = np; prog_cap = nc;
    }
    p = &prog[nprog++];
    memset(p, 0, sizeof *p);
    snprintf(p->path, sizeof p->path, "%s", path);
    p->hash = str_hash(p->path);
    return p;
}

static void prog_set(const char *path, int64_t pos, int64_t dur) {
    if (dur <= 0) return;
    Prog *p = prog_get(path);
    if (!p) return;
    if (pos < 0) pos = 0;
    if (pos > dur) pos = dur;
    p->dur = dur;
    if ((float)pos >= PROG_DONE * (float)dur) { p->done = 1; p->pos = 0; }
    else p->pos = pos < PROG_MIN_MS ? 0 : pos;
    p->seq = ++prog_seq;
    prog_dirty = 1;
    prog_gen++;
}

static int64_t prog_resume_ms(const char *path) { Prog *p = prog_find(path); return p ? p->pos : 0; }

static Hide *hide_find(const char *key) {
    unsigned h = str_hash(key);
    for (int i = 0; i < nhide; i++)
        if (hide[i].hash == h && !strcmp(hide[i].key, key)) return &hide[i];
    return NULL;
}

static void hide_set(const char *key, unsigned seq) {
    Hide *h = hide_find(key);
    if (!h) {
        if (nhide == hide_cap) {
            int nc = hide_cap ? hide_cap * 2 : 32;
            Hide *nh = realloc(hide, nc * sizeof(Hide));
            if (!nh) return;
            hide = nh; hide_cap = nc;
        }
        h = &hide[nhide++];
        memset(h, 0, sizeof *h);
        snprintf(h->key, sizeof h->key, "%s", key);
        h->hash = str_hash(h->key);
    }
    h->seq = seq;
    prog_dirty = 1;
}

static void prog_load(void) {
    FILE *f = fopen(PROG_PATH, "r");
    if (!f) return;
    char l[700];
    while (fgets(l, sizeof l, f)) {
        l[strcspn(l, "\r\n")] = 0;
        char *s, *e;
        if (l[0] == 'H' && l[1] == '\t') {
            unsigned long hs = strtoul(l + 2, &e, 10);
            if (*e == '\t' && e[1]) hide_set(e + 1, (unsigned)hs);
            continue;
        }
        if (l[0] != 'P' || l[1] != '\t') continue;
        s = l + 2;
        long long pos = strtoll(s, &e, 10);      if (*e != '\t') continue; s = e + 1;
        long long dur = strtoll(s, &e, 10);      if (*e != '\t') continue; s = e + 1;
        long done = strtol(s, &e, 10);           if (*e != '\t') continue; s = e + 1;
        unsigned long seq = strtoul(s, &e, 10);  if (*e != '\t' || !e[1]) continue;
        Prog *p = prog_get(e + 1);
        if (!p) break;
        p->pos = pos; p->dur = dur; p->done = done != 0; p->seq = (unsigned)seq;
        if (p->seq > prog_seq) prog_seq = p->seq;
    }
    fclose(f);
    prog_dirty = 0;
    prog_gen++;
}

static void prog_save(void) {
    if (!prog_dirty) return;
    FILE *f = fopen(PROG_TMP, "w");
    if (!f) return;
    fprintf(f, "# aniwatch progress v1: P pos dur done seq path / H seq key\n");
    for (int i = 0; i < nprog; i++)
        fprintf(f, "P\t%lld\t%lld\t%d\t%u\t%s\n", (long long)prog[i].pos, (long long)prog[i].dur,
                prog[i].done, prog[i].seq, prog[i].path);
    for (int i = 0; i < nhide; i++)
        fprintf(f, "H\t%u\t%s\n", hide[i].seq, hide[i].key);
    if (fclose(f) != 0) return;
    remove(PROG_PATH);
    rename(PROG_TMP, PROG_PATH);
    prog_dirty = 0;
}

static void prog_clear(void) {
    free(prog); prog = NULL; nprog = prog_cap = 0;
    free(hide); hide = NULL; nhide = hide_cap = 0;
    prog_seq = 0;
    prog_dirty = 1;
    prog_save();
    prog_gen++;
}

static void cfg_load(void) {
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) return;
    char l[128];
    int v;
    while (fgets(l, sizeof l, f)) {
        if (sscanf(l, "auto_next=%d", &v) == 1) cfg_auto = v != 0;
        else if (sscanf(l, "countdown=%d", &v) == 1) cfg_count = v <= 5 ? 5 : (v >= 15 ? 15 : 10);
        else if (sscanf(l, "resume=%d", &v) == 1) cfg_resume = v != 0;
        else if (sscanf(l, "skip=%d", &v) == 1) cfg_skip = (v < 0 || v > 2) ? 1 : v;
    }
    fclose(f);
}

static void cfg_save(void) {
    FILE *f = fopen(CFG_PATH, "w");
    if (!f) return;
    fprintf(f, "auto_next=%d\ncountdown=%d\nresume=%d\nskip=%d\n", cfg_auto, cfg_count, cfg_resume, cfg_skip);
    fclose(f);
}

static void show_prog(Show *sh) {   /* cached per show, redone when prog_gen changes */
    if (sh->pg_gen == prog_gen) return;
    sh->pg_gen = prog_gen; sh->pg_done = 0; sh->pg_f = 0;
    if (sh->neps <= 0 || nprog == 0) return;
    float sum = 0;
    for (int i = 0; i < sh->neps; i++) {
        Prog *p = prog_find(sh->eps[i].path);
        if (!p) continue;
        if (p->done) { sh->pg_done++; sum += 1; }
        else if (p->dur > 0) sum += (float)p->pos / (float)p->dur;
    }
    sh->pg_f = sum / sh->neps;
}

static void check_mark(float cx, float cy, float s) {   /* uses the gc() colour */
    line_w(cx - 6 * s, cy, cx - 2 * s, cy + 4 * s, 2.6f * s);
    line_w(cx - 2 * s, cy + 4 * s, cx + 6 * s, cy - 4 * s, 2.6f * s);
}

static void cw_fill(CWItem *c, int s, int ep, unsigned seq, int64_t pos, int64_t dur) {
    Show *sh = &shows[s];
    Episode *e = &sh->eps[ep];
    c->show = s; c->ep = ep; c->seq = seq; c->pos = pos; c->dur = dur;
    if (sh->kind == K_ANIME) {
        if (e->season == SEASON_SPECIAL) {
            if (e->number > 0) snprintf(c->tag, sizeof c->tag, "SP %d", e->number);
            else snprintf(c->tag, sizeof c->tag, "SPECIAL");
        } else if (e->number > 0 && sh->nseasons > 1) snprintf(c->tag, sizeof c->tag, "S%d E%d", e->season, e->number);
        else if (e->number > 0) snprintf(c->tag, sizeof c->tag, "E%d", e->number);
        else snprintf(c->tag, sizeof c->tag, "EP %d", ep + 1);
    } else if (sh->neps > 1) snprintf(c->tag, sizeof c->tag, "PART %d", ep + 1);
    else snprintf(c->tag, sizeof c->tag, "MOVIE");
    if (pos > 0 && dur > pos) snprintf(c->left, sizeof c->left, "%d MIN LEFT", (int)((dur - pos + 59999) / 60000));
    else snprintf(c->left, sizeof c->left, "UP NEXT");
}

static void cw_refresh(void) {   /* newest first, max CW_MAX, redone when prog_gen changes */
    if (cw_gen == prog_gen) return;
    cw_gen = prog_gen;
    ncw = 0;
    for (int s = 0; s < nshows; s++) {
        Show *sh = &shows[s];
        int last = -1;
        unsigned best = 0;
        Prog *lp = NULL;
        for (int i = 0; i < sh->neps; i++) {
            Prog *p = prog_find(sh->eps[i].path);
            if (p && p->seq > best) { best = p->seq; last = i; lp = p; }
        }
        if (last < 0) continue;
        Hide *hd = hide_find(sh->eps[0].path);
        if (hd && hd->seq >= best) continue;
        int ep = last;
        int64_t pos = lp->pos, dur = lp->dur;
        if (lp->done) {   /* finished: point at the next unwatched episode */
            ep = -1;
            for (int i = last + 1; i < sh->neps; i++) {
                Prog *q = prog_find(sh->eps[i].path);
                if (!q || !q->done) { ep = i; pos = q ? q->pos : 0; dur = q ? q->dur : 0; break; }
            }
            if (ep < 0) continue;
        }
        if (ncw == CW_MAX && best <= cw[CW_MAX - 1].seq) continue;
        int at = ncw < CW_MAX ? ncw : CW_MAX - 1;
        while (at > 0 && cw[at - 1].seq < best) { cw[at] = cw[at - 1]; at--; }
        cw_fill(&cw[at], s, ep, best, pos, dur);
        if (ncw < CW_MAX) ncw++;
    }
}

static void cw_remove(int i) {
    if (i < 0 || i >= ncw) return;
    hide_set(shows[cw[i].show].eps[0].path, cw[i].seq);
    prog_save();
    prog_gen++;
    toast("Removed from Continue Watching");
}

static void draw_card(int si, int x, int y, int w, int h, int sel) {
    Show *sh = &shows[si];
    if (sh->cover_state == 0 && cover_budget > 0) { cover_budget--; ensure_cover(sh); }
    if (sh->cover) {
        SDL_Rect d = { x, y, w, h };
        tex_col(sh->cover, 255, 255, 255, 255);
        SDL_RenderCopy(ren, sh->cover, NULL, &d);
    } else {
        char fc[8]; first_char(sh->name, fc);
        rrect(x, y, w, h, 10, C_PANEL, 255);
        textc(F_H2, x + w / 2, y + h / 2, AL_C, fc, C_DIM, 255);
    }
    const char *b = badge_of(sh);
    int bw = text_w(F_SMALLB, b) + 16;
    rrect(x + 8, y + 8, bw, 26, 9, 18, 18, 24, 215);
    textc(F_SMALLB, x + 8 + bw / 2, y + 21, AL_C, b, C_TEXT, 255);
    if (sh->kind == K_ANIME) {
        char e[24];
        snprintf(e, sizeof e, "%d EP", sh->neps);
        int ew = text_w(F_SMALLB, e) + 14;
        rrect(x + w - ew - 8, y + h - 32, ew, 24, 8, 18, 18, 24, 215);
        textc(F_SMALLB, x + w - ew / 2 - 8, y + h - 20, AL_C, e, C_SOFT, 255);
    }
    show_prog(sh);   /* watch progress (Step 3) */
    if (sh->pg_f > 0.001f) {
        int bw2 = w - 16, fw = (int)(bw2 * clampf(sh->pg_f, 0, 1));
        rrect(x + 8, y + h - 6, bw2, 4, 2, 0, 0, 0, 190);
        if (fw >= 4) rrect(x + 8, y + h - 6, fw, 4, 2, C_ACCENT, 255);
    }
    if (sh->neps > 0 && sh->pg_done >= sh->neps) {
        disc(x + w - 21, y + 21, 13, C_ACCENT, 235);
        gc(C_TEXT, 255); check_mark(x + w - 21, y + 21, 0.75f);
    }

    if (sel) rrect_line(x - 5, y - 5, w + 10, h + 10, 15, 3, C_ACCENT, 255);
    if (sel) text(F_SMALLB, x, y + h + 10, AL_L, sh->card_name, C_ACCLT, 255);
    else     text(F_SMALLB, x, y + h + 10, AL_L, sh->card_name, C_TEXT, 255);
}

/* ===== Input ===== */
static int pressed(unsigned b) { return (btn & b) && !(btn_old & b); }

static int nav_rep(int i, int held, float dt) {
    if (!held) { rep_on[i] = 0; return 0; }
    if (!rep_on[i]) { rep_on[i] = 1; rep_t[i] = 0.35f; return 1; }
    rep_t[i] -= dt;
    if (rep_t[i] <= 0) { rep_t[i] += 0.07f; return 1; }
    return 0;
}
static int n_up(float dt)    { return nav_rep(0, (btn & SCE_CTRL_UP) || st_up, dt); }
static int n_down(float dt)  { return nav_rep(1, (btn & SCE_CTRL_DOWN) || st_down, dt); }
static int n_left(float dt)  { return nav_rep(2, (btn & SCE_CTRL_LEFT) || st_left, dt); }
static int n_right(float dt) { return nav_rep(3, (btn & SCE_CTRL_RIGHT) || st_right, dt); }

static void read_input(void) {
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    btn_old = btn;
    btn = pad.buttons;
    /* left OR right stick moves the selection */
    st_up    = pad.ly < 50  || pad.ry < 50;
    st_down  = pad.ly > 205 || pad.ry > 205;
    st_left  = pad.lx < 50  || pad.rx < 50;
    st_right = pad.lx > 205 || pad.rx > 205;
}

static SDL_TouchID touch_front_id(void) {   /* front panel ID: 0 on old SDL2, 1 since 2.30.7 */
    static int done;
    static SDL_TouchID id;
    if (done) return id;
    int n = SDL_GetNumTouchDevices();
    if (n <= 0) return 0;
    id = SDL_GetTouchDevice(0);              /* SDL adds the front panel first */
    for (int i = 0; i < n; i++) {
        const char *nm = SDL_GetTouchName(i);
        if (nm && !strcasecmp(nm, "Front")) { id = SDL_GetTouchDevice(i); break; }
    }
    done = 1;
    return id;
}

static void handle_touch_event(SDL_Event *ev) {
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP) return;
    if (ev->tfinger.touchId != touch_front_id()) return;   /* front panel only */
    float x = ev->tfinger.x * SCREEN_W, y = ev->tfinger.y * SCREEN_H;
    if (ev->type == SDL_FINGERDOWN) {
        t_down = 1; t_axis = 0; t_x0 = t_lx = x; t_y0 = t_ly = y;
    } else if (ev->type == SDL_FINGERMOTION && t_down) {
        if (!t_axis) {
            float dx = x - t_x0, dy = y - t_y0;
            if (fabsf(dx) > 12 || fabsf(dy) > 12) t_axis = fabsf(dx) > fabsf(dy) ? 2 : 1;
        }
        if (t_axis == 1) drag_dy += t_ly - y;
        if (t_axis == 2) drag_dx += t_lx - x;
        t_lx = x; t_ly = y;
    } else if (ev->type == SDL_FINGERUP && t_down) {
        if (!t_axis) { tap_pending = 1; tap_x = t_x0; tap_y = t_y0; }
        t_down = 0; t_axis = 0;
    }
}

/* ===== Intro/outro marks (Step 4) ===== */
#define SKIP_PATH DATA_ROOT "/skips.txt"
#define SKIP_TMP  DATA_ROOT "/skips.tmp"

typedef struct { char path[512]; unsigned hash; int64_t is, ie, os; } Skip;   /* ms, -1 = not set */
static Skip *skips;
static int nskip, skip_cap;

/* player-side state */
static int mk_open, mk_sel, mk_paused, pl_ended;
static int64_t sk_is = -1, sk_ie = -1, sk_os = -1;          /* marks of the playing episode */
static int sk_used[2], sk_vis, sk_kind = -1, sk_show_k, sk_px, sk_pw;
static float sk_t, sk_a;
static void sk_load_cur(void);

static Skip *skip_find(const char *path) {
    unsigned h = str_hash(path);
    for (int i = 0; i < nskip; i++)
        if (skips[i].hash == h && !strcmp(skips[i].path, path)) return &skips[i];
    return NULL;
}

static Skip *skip_get(const char *path) {   /* find or add (pointers die on the next add) */
    Skip *s = skip_find(path);
    if (s) return s;
    if (nskip == skip_cap) {
        int nc = skip_cap ? skip_cap * 2 : 64;
        Skip *ns = realloc(skips, nc * sizeof(Skip));
        if (!ns) return NULL;
        skips = ns; skip_cap = nc;
    }
    s = &skips[nskip++];
    memset(s, 0, sizeof *s);
    snprintf(s->path, sizeof s->path, "%s", path);
    s->hash = str_hash(s->path);
    s->is = s->ie = s->os = -1;
    return s;
}

static void skip_remove(const char *path) {
    Skip *s = skip_find(path);
    if (s) *s = skips[--nskip];
}

static void skip_load(void) {
    FILE *f = fopen(SKIP_PATH, "r");
    if (!f) return;
    char l[700];
    while (fgets(l, sizeof l, f)) {
        l[strcspn(l, "\r\n")] = 0;
        if (l[0] != 'S' || l[1] != '\t') continue;
        char *s = l + 2, *e;
        long long a = strtoll(s, &e, 10); if (*e != '\t') continue; s = e + 1;
        long long b = strtoll(s, &e, 10); if (*e != '\t') continue; s = e + 1;
        long long c = strtoll(s, &e, 10); if (*e != '\t' || !e[1]) continue;
        Skip *k = skip_get(e + 1);
        if (!k) break;
        k->is = a; k->ie = b; k->os = c;
    }
    fclose(f);
}

static void skip_save(void) {
    FILE *f = fopen(SKIP_TMP, "w");
    if (!f) return;
    fprintf(f, "# aniwatch skips v1: S intro_start intro_end outro_start path (ms, -1 = not set)\n");
    for (int i = 0; i < nskip; i++)
        fprintf(f, "S\t%lld\t%lld\t%lld\t%s\n", (long long)skips[i].is, (long long)skips[i].ie,
                (long long)skips[i].os, skips[i].path);
    if (fclose(f) != 0) return;
    remove(SKIP_PATH);
    rename(SKIP_TMP, SKIP_PATH);
}

/* ===== Player (Step 2) ===== */
#define PL_SEEK     10      /* seconds per seek step */
#define PL_NV21     0       /* set 1 if colours look wrong (blue skin, purple grass) */
#define PL_ALIGN_H  1       /* set 0 if there's a green strip or the colours shift at the bottom */
#define PL_PITCH(w) (((w) + 15) & ~15)   /* use (w) if the picture is slanted/striped */
#define PL_UI_TIME  3.0f
#define PB_X  40
#define PB_W  (SCREEN_W - 80)
#define PB_Y  (SCREEN_H - 66)

enum { H_PBACK = 100, H_PPLAY, H_PREW, H_PFWD, H_PPREV, H_PNEXT };

static int in_player, pl_show, pl_ep;
static SceAvPlayerHandle pl_h;
static int pl_have, pl_got_frame;
static volatile int pl_paused, pl_audio_run, pl_vol = 8;
static SceUID pl_thread = -1;
static SDL_Texture *pl_tex;
static int pl_tw, pl_th;
static uint64_t pl_dur, pl_pos;
static float pl_wait, pl_clock, pl_tick;
static int pl_ui;
static float pl_ui_a, pl_ui_t;
static int pl_pend, pl_scrub, pl_moved;
static int64_t pl_pend_ms, pl_pend_base, pl_after_ms;
static float pl_pend_t, pl_after_t, pl_vol_t, pl_rip_t, pl_tap_last = -9;
static int pl_rip_side, pl_tap_side;
static float pl_rep[4];
static int pl_rep_on[4];
static char pl_title[160], pl_sub[200];
static int pl_mod_res;

static void details_retarget(void);

/* --- memory for sceAvPlayer (frames go in CDRAM) --- */
static void *pl_alloc(void *arg, uint32_t align, uint32_t size) {
    (void)arg;
    return memalign(align < 16 ? 16 : align, size);
}
static void pl_free(void *arg, void *p) { (void)arg; free(p); }

/* video frames go in video memory (or plain contiguous memory if that's full) */
static void *pl_galloc(void *arg, uint32_t align, uint32_t size) {
    (void)arg; (void)align;
    void *base = NULL;
    uint32_t sz = (size + 0x3FFFF) & ~0x3FFFFu;                  /* 256 KB steps */
    SceUID b = sceKernelAllocMemBlock("aw_video", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, sz, NULL);
    if (b < 0) {
        sz = (size + 0xFFFFF) & ~0xFFFFFu;                       /* 1 MB steps */
        b = sceKernelAllocMemBlock("aw_video", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, sz, NULL);
    }
    if (b < 0) return NULL;
    sceKernelGetMemBlockBase(b, &base);
    return base;
}
static void pl_gfree(void *arg, void *p) {
    (void)arg;
    SceUID b = sceKernelFindMemBlockByAddr(p, 0);
    if (b >= 0) sceKernelFreeMemBlock(b);
}

/* --- audio thread --- */
static int pl_audio_main(SceSize args, void *argp) {
    (void)args; (void)argp;
    int port = -1, rate = 0, ch = 0, grain = 0, vol = -1;
    while (pl_audio_run) {
        SceAvPlayerFrameInfo fr;
        memset(&fr, 0, sizeof fr);
        if (pl_paused || !sceAvPlayerGetAudioData(pl_h, &fr) || !fr.pData) { sceKernelDelayThread(4000); continue; }
        int c = fr.details.audio.channelCount, r = (int)fr.details.audio.sampleRate;
        if (c < 1 || c > 2) c = 2;
        if (r <= 0) r = 48000;
        int g = (int)(fr.details.audio.size / (2 * c));
        if (g < 64 || g > 65472) continue;
        if (port < 0) {
            port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, g, r, c == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
            if (port < 0) { sceKernelDelayThread(10000); continue; }
            rate = r; ch = c; grain = g; vol = -1;
        } else if (r != rate || c != ch || g != grain) {
            sceAudioOutSetConfig(port, g, r, c == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
            rate = r; ch = c; grain = g;
        }
        if (vol != pl_vol) {
            vol = pl_vol;
            int v[2] = { 32768 * vol / 10, 32768 * vol / 10 };
            sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, v);
        }
        sceAudioOutOutput(port, fr.pData);
    }
    if (port >= 0) { sceAudioOutOutput(port, NULL); sceAudioOutReleasePort(port); }
    return 0;
}

static void pl_stop(void) {
    if (pl_thread >= 0) {
        pl_audio_run = 0;
        sceKernelWaitThreadEnd(pl_thread, NULL, NULL);
        sceKernelDeleteThread(pl_thread);
        pl_thread = -1;
    }
    if (pl_have) { sceAvPlayerStop(pl_h); sceAvPlayerClose(pl_h); pl_have = 0; }
    if (pl_tex) { SDL_DestroyTexture(pl_tex); pl_tex = NULL; }
    pl_tw = pl_th = 0;
}

static void pl_titles(void) {
    Show *sh = &shows[pl_show];
    Episode *e = &sh->eps[pl_ep];
    char s[400], se[24];
    fit_text(F_SUB, sh->name, SCREEN_W - 200, pl_title, sizeof pl_title);
    if (sh->kind == K_ANIME) {
        if (e->season == SEASON_SPECIAL) snprintf(se, sizeof se, "Specials");
        else snprintf(se, sizeof se, "S%d", e->season);
        if (e->number > 0) snprintf(s, sizeof s, "%s  \xE2\x80\xA2  E%d  \xE2\x80\xA2  %s", se, e->number, e->file);
        else snprintf(s, sizeof s, "%s  \xE2\x80\xA2  %s", se, e->file);
    } else if (sh->neps > 1) snprintf(s, sizeof s, "Part %d of %d  \xE2\x80\xA2  %s", pl_ep + 1, sh->neps, e->file);
    else snprintf(s, sizeof s, "Movie");
    fit_text(F_SMALL, s, SCREEN_W - 200, pl_sub, sizeof pl_sub);
}

static int player_open(int si, int ep) {
    pl_stop();
    Show *sh = &shows[si];
    SceAvPlayerInitData d;
    memset(&d, 0, sizeof d);
    d.memoryReplacement.allocate = pl_alloc;
    d.memoryReplacement.deallocate = pl_free;
    d.memoryReplacement.allocateTexture = pl_galloc;
    d.memoryReplacement.deallocateTexture = pl_gfree;
    d.basePriority = 0x60;
    d.numOutputVideoFrameBuffers = 2;
    d.autoStart = 1;
    pl_h = sceAvPlayerInit(&d);
    if (!pl_h) { pl_have = 0; in_player = 0; toast("Video player failed to start (mod 0x%08X)", (unsigned)pl_mod_res); return 0; }
    pl_have = 1;
    if (sceAvPlayerAddSource(pl_h, sh->eps[ep].path) < 0) {
        sceAvPlayerClose(pl_h);
        pl_h = 0; pl_have = 0; in_player = 0;
        toast("Can't open %s", sh->eps[ep].path);
        return 0;
    }
    sceAvPlayerSetLooping(pl_h, 0);
    pl_paused = 0;
    pl_audio_run = 1;
    pl_thread = sceKernelCreateThread("aw_audio", pl_audio_main, 0x10000100, 0x10000, 0, 0, NULL);
    if (pl_thread >= 0) sceKernelStartThread(pl_thread, 0, NULL);
    in_player = 1; pl_show = si; pl_ep = ep;
    pl_got_frame = 0; pl_dur = pl_pos = 0; pl_wait = 0; pl_tick = 0;
    pl_resume_ms = 0; pl_jump_t = 0; pl_save_t = 0; an_on = 0;
    pl_ended = 0; mk_open = 0; mk_paused = 0; sk_load_cur();
    pl_pend = pl_scrub = 0; pl_after_t = 0; pl_rip_t = 0;
    pl_ui = 1; pl_ui_t = PL_UI_TIME;
    pl_titles();
    return 1;
}

static void pl_save_prog(void) {
    if (!pl_got_frame || !pl_dur || pl_ended) return;   /* pl_ended: already saved as watched */
    int64_t pos = pl_jump_t > 0 ? pl_jump_ms : (int64_t)pl_pos;
    prog_set(shows[pl_show].eps[pl_ep].path, pos, (int64_t)pl_dur);
}

static void player_close(void) {
    pl_save_prog();
    prog_save();
    pl_stop();
    in_player = 0;
    an_on = 0;
    if (in_details && d_show == pl_show && d_has_list) {
        for (int i = 0; i < nrows; i++)
            if (rows[i].ep == pl_ep) { d_sel = i; d_focus = 1; details_retarget(); break; }
    }
}

/* --- helpers --- */
static uint64_t pl_query_dur(void) {
    uint64_t best = 0;
    for (int i = 0; i < 4; i++) {
        SceAvPlayerStreamInfo si;
        memset(&si, 0, sizeof si);
        if (sceAvPlayerGetStreamInfo(pl_h, i, &si) >= 0 && si.duration > best) best = si.duration;
    }
    return best;
}

static void pl_upload(SceAvPlayerFrameInfo *fr) {
    int w = (int)fr->details.video.width, h = (int)fr->details.video.height;
    if (w <= 0 || h <= 0 || w > 1920 || h > 1088) return;
    if (!pl_tex || w != pl_tw || h != pl_th) {
        if (pl_tex) SDL_DestroyTexture(pl_tex);
        pl_tex = SDL_CreateTexture(ren, PL_NV21 ? SDL_PIXELFORMAT_NV21 : SDL_PIXELFORMAT_NV12,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
        pl_tw = w; pl_th = h;
        pl_got_frame = 1;
        if (!pl_tex) { toast("Video texture failed: %s", SDL_GetError()); return; }
    }
    int pitch = PL_PITCH(w), ah = PL_ALIGN_H ? ((h + 15) & ~15) : h;
    const Uint8 *yp = fr->pData, *uvp = fr->pData + pitch * ah;
    if (SDL_UpdateNVTexture(pl_tex, NULL, yp, pitch, uvp, pitch) < 0) {
        void *px; int tp;
        if (SDL_LockTexture(pl_tex, NULL, &px, &tp) == 0) {
            Uint8 *dst = px;
            for (int y = 0; y < h; y++) memcpy(dst + y * tp, yp + y * pitch, w);
            dst += tp * h;
            for (int y = 0; y < (h + 1) / 2; y++) memcpy(dst + y * tp, uvp + y * pitch, (w + 1) & ~1);
            SDL_UnlockTexture(pl_tex);
        }
    }
    pl_got_frame = 1;
}

static void pl_show_ui(void) { pl_ui = 1; pl_ui_t = PL_UI_TIME; }

static int64_t pl_shown_ms(void) {
    if (pl_pend) return pl_pend_ms;
    if (pl_after_t > 0) return pl_after_ms;
    return (int64_t)pl_pos;
}
static int64_t pl_clamp_ms(int64_t ms) {
    int64_t mx = (int64_t)pl_dur - 1000;
    if (mx < 0) mx = 0;
    return ms < 0 ? 0 : (ms > mx ? mx : ms);
}
static void pl_seek_by(int sec) {
    if (!pl_got_frame || !pl_dur) return;
    if (!pl_pend) { pl_pend_base = pl_shown_ms(); pl_pend_ms = pl_pend_base; pl_pend = 1; }
    pl_pend_ms = pl_clamp_ms(pl_pend_ms + sec * 1000LL);
    pl_pend_t = 0.45f;
    pl_show_ui();
}
static void pl_commit(void) {
    if (!pl_pend) return;
    sceAvPlayerJumpToTime(pl_h, (uint64_t)pl_pend_ms);
    pl_after_ms = pl_pend_ms;
    pl_after_t = 1.0f;
    pl_pend = 0;
}
static void pl_toggle_pause(void) {
    if (!pl_got_frame) return;
    if (pl_paused) { sceAvPlayerResume(pl_h); pl_paused = 0; }
    else { sceAvPlayerPause(pl_h); pl_paused = 1; }
    pl_show_ui();
}
static void pl_set_vol(int v) {
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    pl_vol = v;
    pl_vol_t = 1.2f;
}
static void pl_prev_next(int d) {
    int ne = pl_ep + d;
    if (ne < 0 || ne >= shows[pl_show].neps) {
        toast(d < 0 ? "No previous episode" : "No next episode");
        return;
    }
    pl_save_prog();
    prog_save();
    int64_t ms = prog_resume_ms(shows[pl_show].eps[ne].path);
    if (player_open(pl_show, ne)) {
        pl_resume_ms = ms;
        toast("%s", shows[pl_show].eps[ne].label);
    }
}
static int pl_rep_btn(int i, int held, float dt) {
    if (!held) { pl_rep_on[i] = 0; return 0; }
    if (!pl_rep_on[i]) { pl_rep_on[i] = 1; pl_rep[i] = 0.4f; return 1; }
    pl_rep[i] -= dt;
    if (pl_rep[i] <= 0) { pl_rep[i] += 0.15f; return 1; }
    return 0;
}
static int pl_button(int id) {   /* returns 1 if the player closed or changed episode */
    switch (id) {
    case H_PBACK: player_close(); return 1;
    case H_PPLAY: pl_toggle_pause(); break;
    case H_PREW:  pl_seek_by(-PL_SEEK); break;
    case H_PFWD:  pl_seek_by(PL_SEEK); break;
    case H_PPREV: pl_prev_next(-1); return 1;
    case H_PNEXT: pl_prev_next(1); return 1;
    }
    pl_show_ui();
    return 0;
}

/* --- touch --- */
static int pl_bar_hit(float x, float y) {
    return pl_ui_a > 0.6f && pl_dur && pl_got_frame &&
           y > PB_Y - 26 && y < PB_Y + 22 && x > PB_X - 16 && x < PB_X + PB_W + 16;
}
static int64_t pl_ms_at(float x) {
    return pl_clamp_ms((int64_t)(clampf((x - PB_X) / PB_W, 0, 1) * (float)pl_dur));
}
static void pl_touch_event(SDL_Event *ev) {
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP) return;
    if (ev->tfinger.touchId != touch_front_id()) return;   /* front panel only */
    float x = ev->tfinger.x * SCREEN_W, y = ev->tfinger.y * SCREEN_H;
    if (ev->type == SDL_FINGERDOWN) {
        t_down = 1; pl_moved = 0; t_x0 = x; t_y0 = y;
        if (pl_bar_hit(x, y)) {
            pl_scrub = 1;
            if (!pl_pend) { pl_pend_base = pl_shown_ms(); pl_pend = 1; }
            pl_pend_ms = pl_ms_at(x);
            pl_show_ui();
        }
    } else if (ev->type == SDL_FINGERMOTION && t_down) {
        if (pl_scrub) { pl_pend_ms = pl_ms_at(x); pl_show_ui(); }
        else if (fabsf(x - t_x0) > 14 || fabsf(y - t_y0) > 14) pl_moved = 1;
    } else if (ev->type == SDL_FINGERUP && t_down) {
        if (pl_scrub) { pl_scrub = 0; pl_pend_t = 0; }
        else if (!pl_moved) { tap_pending = 1; tap_x = t_x0; tap_y = t_y0; }
        t_down = 0;
    }
}

/* --- auto-next "Up next" card (Step 3) --- */
#define AN_W 380
#define AN_H 150
#define AN_X (SCREEN_W - AN_W - 30)
#define AN_Y (SCREEN_H - AN_H - 56)

static void an_start(void) {
    Show *sh = &shows[pl_show];
    Episode *e = &sh->eps[pl_ep + 1];
    an_on = 1;
    an_t = (float)cfg_count;
    pl_pend = 0; pl_scrub = 0;
    pl_ui = 0; pl_ui_a = 0;    /* keeps the seek bar from catching taps on the card */
    prog_save();
    fit_text(F_BODYB, e->label, AN_W - 80, an_line, sizeof an_line);
    if (sh->kind == K_ANIME && e->season != sh->eps[pl_ep].season) {
        if (e->season == SEASON_SPECIAL) snprintf(an_sub, sizeof an_sub, "New: Specials");
        else snprintf(an_sub, sizeof an_sub, "New season: Season %d", e->season);
    } else fit_text(F_SMALL, sh->name, AN_W - 40, an_sub, sizeof an_sub);
}

static int an_btn_at(float x, float y) {   /* 0 Play now, 1 Cancel, -1 none */
    int by = AN_Y + AN_H - 62;
    if (y < by - 8 || y >= by + 52) return -1;
    if (x >= AN_X + 20 && x < AN_X + 220) return 0;
    if (x >= AN_X + 236 && x < AN_X + 360) return 1;
    return -1;
}

static void update_autonext(float dt) {
    an_t -= dt;
    int go = pressed(SCE_CTRL_CROSS) || an_t <= 0, stop = pressed(SCE_CTRL_CIRCLE);
    if (tap_pending) {
        int b = an_btn_at(tap_x, tap_y);
        tap_pending = 0;
        if (b == 0) go = 1;
        else if (b == 1) stop = 1;
    }
    if (stop) { an_on = 0; player_close(); return; }
    if (go) { an_on = 0; pl_prev_next(1); }
}

static void draw_autonext(void) {
    char n[8];
    float f = cfg_count > 0 ? clampf(an_t / (float)cfg_count, 0, 1) : 0;
    int by = AN_Y + AN_H - 62, fw = (int)((AN_W - 40) * f);
    fill(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 110);
    grad_v(0, SCREEN_H - 60, SCREEN_W, 60, 0, 0, 0, 0, 200);
    rrect(AN_X, AN_Y, AN_W, AN_H, 18, C_PANEL, 245);
    rrect_line(AN_X, AN_Y, AN_W, AN_H, 18, 1, C_LINE, 255);
    text(F_SMALLB, AN_X + 20, AN_Y + 14, AL_L, "UP NEXT", C_ACCLT, 255);
    snprintf(n, sizeof n, "%d", (int)ceilf(an_t > 0 ? an_t : 0));
    textc(F_SUB, AN_X + AN_W - 24, AN_Y + 26, AL_R, n, C_TEXT, 255);
    text(F_BODYB, AN_X + 20, AN_Y + 36, AL_L, an_line, C_TEXT, 255);
    text(F_SMALL, AN_X + 20, AN_Y + 60, AL_L, an_sub, C_DIM, 255);
    rrect(AN_X + 20, AN_Y + 80, AN_W - 40, 4, 2, 255, 255, 255, 40);
    if (fw >= 4) rrect(AN_X + 20, AN_Y + 80, fw, 4, 2, C_ACCENT, 255);
    button(AN_X + 20, by, 200, 44, 1, IC_PLAY, "Play now", 0, 255);
    button(AN_X + 236, by, 124, 44, 0, IC_LEFT, "Cancel", 0, 255);
    int x = hint(20, BI_X, "Play now");
    hint(x, BI_O, "Cancel");
}

/* --- intro/outro marks + skip (Step 4) --- */
#define MK_N    4
#define MK_W    320
#define MK_X    (SCREEN_W - MK_W - 30)
#define MK_Y    84
#define MK_ROW  48
#define MK_TOP  70
#define SK_Y    (PB_Y - 104)
#define SK_H    46
#define SK_TIME 6.0f    /* Button mode: the pill stays this long (longer while the overlay is up) */

static const char *const mk_names[MK_N] = { "Intro start", "Intro end", "Outro start", "Clear marks" };

static void sk_load_cur(void) {
    Skip *s = skip_find(shows[pl_show].eps[pl_ep].path);
    sk_is = s ? s->is : -1; sk_ie = s ? s->ie : -1; sk_os = s ? s->os : -1;
    sk_used[0] = sk_used[1] = 0; sk_vis = 0; sk_t = 0; sk_a = 0; sk_kind = -1;
}

static void sk_store(void) {
    const char *p = shows[pl_show].eps[pl_ep].path;
    if (sk_is < 0 && sk_ie < 0 && sk_os < 0) skip_remove(p);
    else {
        Skip *s = skip_get(p);
        if (!s) return;
        s->is = sk_is; s->ie = sk_ie; s->os = sk_os;
    }
    skip_save();
}

static void mk_open_menu(void) {
    mk_open = 1; mk_sel = 0; mk_paused = 0;
    if (!pl_paused) { sceAvPlayerPause(pl_h); pl_paused = 1; mk_paused = 1; }
    pl_show_ui();
}

static void mk_close(void) {
    mk_open = 0;
    if (mk_paused && pl_paused) { sceAvPlayerResume(pl_h); pl_paused = 0; }
    mk_paused = 0;
    pl_show_ui();
}

static void mk_set(int i) {
    int64_t t = pl_shown_ms();
    char ts[24];
    fmt_time(ts, sizeof ts, t);
    if (i == 0) {
        sk_is = t;
        if (sk_ie >= 0 && sk_ie <= t) { sk_ie = -1; toast("Intro start set at %s, now set Intro end", ts); }
        else toast("Intro start set at %s", ts);
        sk_used[0] = 1;
    } else if (i == 1) {
        int64_t st = sk_is < 0 ? 0 : sk_is;
        if (t <= st + 1000) { toast("Intro end must be after Intro start"); return; }
        if (sk_is < 0) sk_is = 0;   /* no start set = intro starts at 0:00 */
        sk_ie = t;
        toast("Intro end set at %s", ts);
        sk_used[0] = 1;
    } else if (i == 2) {
        if (pl_dur && t > (int64_t)pl_dur - 3000) { toast("Too close to the end for an outro"); return; }
        sk_os = t;
        toast("Outro start set at %s", ts);
        sk_used[1] = 1;   /* don't skip the outro you just marked */
    } else {
        if (sk_is < 0 && sk_ie < 0 && sk_os < 0) { toast("No marks on this episode"); return; }
        sk_is = sk_ie = sk_os = -1;
        sk_used[0] = sk_used[1] = 0;
        toast("Marks cleared");
    }
    sk_store();
}

static int mk_row_at(float x, float y) {   /* row index, MK_N = panel, -1 = outside */
    int h = MK_TOP + MK_N * MK_ROW + 12;
    if (x < MK_X || x >= MK_X + MK_W || y < MK_Y || y >= MK_Y + h) return -1;
    for (int i = 0; i < MK_N; i++) {
        int ry = MK_Y + MK_TOP + i * MK_ROW;
        if (y >= ry && y < ry + MK_ROW - 6) return i;
    }
    return MK_N;
}

static void update_mark(float dt) {
    pl_show_ui();
    if (tap_pending) {
        int r = mk_row_at(tap_x, tap_y);
        tap_pending = 0;
        if (r >= 0 && r < MK_N) { mk_sel = r; mk_set(r); }
        else if (r < 0) { mk_close(); return; }
    }
    if (pressed(SCE_CTRL_CIRCLE) || pressed(SCE_CTRL_SQUARE)) { mk_close(); return; }
    if (pl_rep_btn(2, (btn & SCE_CTRL_UP) != 0, dt) && mk_sel > 0) mk_sel--;
    if (pl_rep_btn(3, (btn & SCE_CTRL_DOWN) != 0, dt) && mk_sel < MK_N - 1) mk_sel++;
    if (pl_rep_btn(0, (btn & SCE_CTRL_LEFT) != 0, dt))  pl_seek_by(-1);
    if (pl_rep_btn(1, (btn & SCE_CTRL_RIGHT) != 0, dt)) pl_seek_by(1);
    if (pressed(SCE_CTRL_LTRIGGER)) pl_seek_by(-PL_SEEK);
    if (pressed(SCE_CTRL_RTRIGGER)) pl_seek_by(PL_SEEK);
    if (pressed(SCE_CTRL_CROSS)) mk_set(mk_sel);
}

static int sk_section(int64_t t) {   /* 0 intro, 1 outro, -1 none */
    if (sk_ie > 0 && t >= (sk_is < 0 ? 0 : sk_is) && t < sk_ie - 1000) return 0;
    if (sk_os >= 0 && pl_dur && t >= sk_os && t < (int64_t)pl_dur - 1500) return 1;
    return -1;
}

static void pl_finish(void) {   /* episode over: real end or outro skip */
    if (pl_dur) prog_set(shows[pl_show].eps[pl_ep].path, (int64_t)pl_dur, (int64_t)pl_dur);
    pl_ended = 1;
    if (cfg_auto && pl_ep + 1 < shows[pl_show].neps) { an_start(); return; }
    toast("Finished: %s", shows[pl_show].eps[pl_ep].label);
    player_close();
}

static void sk_do(int k) {
    sk_used[k] = 1; sk_vis = 0; sk_a = 0;
    if (k == 0) {
        pl_pend = 0; pl_scrub = 0;
        sceAvPlayerJumpToTime(pl_h, (uint64_t)sk_ie);
        pl_after_ms = sk_ie; pl_after_t = 1.0f; pl_pos = (uint64_t)sk_ie;
        toast("Skipped intro");
    } else pl_finish();
}

static int sk_update(float dt) {   /* returns 1 if the player closed or moved on */
    int k = -1;
    if (cfg_skip != 2 && pl_got_frame) k = sk_section(pl_shown_ms());
    if (k >= 0 && sk_used[k]) k = -1;
    if (k != sk_kind) { sk_kind = k; sk_t = 0; }
    if (k >= 0 && cfg_skip == 0 && !pl_paused && !pl_pend && !pl_scrub && pl_after_t <= 0 && pl_jump_t <= 0) {
        sk_do(k);
        return k == 1;
    }
    sk_t += dt;
    sk_vis = k >= 0 && cfg_skip == 1 && (sk_t < SK_TIME || pl_ui_a > 0.6f);
    if (sk_vis) sk_show_k = k;
    sk_a = clampf(sk_a + (sk_vis ? dt : -dt) * 6.0f, 0, 1);
    return 0;
}

static int sk_hit(float x, float y) {
    return sk_vis && x >= sk_px && x < sk_px + sk_pw && y >= SK_Y - 6 && y < SK_Y + SK_H + 6;
}

static void draw_skip(void) {
    if (sk_a <= 0.01f || an_on || mk_open) return;
    int a = (int)(sk_a * 255), cy = SK_Y + SK_H / 2;
    const char *lb = sk_show_k == 1 ? "Skip outro" : "Skip intro";
    int w = text_w(F_BODYB, lb) + 82, x = SCREEN_W - 40 - w;
    sk_px = x; sk_pw = w;
    rrect(x, SK_Y, w, SK_H, 23, 16, 16, 22, a * 225 / 255);
    rrect_line(x, SK_Y, w, SK_H, 23, 2, C_ACCENT, a);
    btn_icon(BI_X, x + 24, cy);
    textc(F_BODYB, x + 44, cy, AL_L, lb, C_TEXT, a);
    gc(C_TEXT, a); icon(IC_PLAY, x + w - 22, cy, 0.7f, 0, 0, 0);
}

static void draw_mark(void) {
    int h = MK_TOP + MK_N * MK_ROW + 12;
    char now[24], v[24];
    fmt_time(now, sizeof now, pl_shown_ms());
    rrect(MK_X, MK_Y, MK_W, h, 18, C_PANEL, 245);
    rrect_line(MK_X, MK_Y, MK_W, h, 18, 1, C_LINE, 255);
    text(F_SMALLB, MK_X + 20, MK_Y + 14, AL_L, "MARK THIS EPISODE", C_ACCLT, 255);
    text(F_SMALL, MK_X + 20, MK_Y + 38, AL_L, "X sets it at the current time", C_DIM, 255);
    textc(F_SUB, MK_X + MK_W - 20, MK_Y + 26, AL_R, now, C_TEXT, 255);
    for (int i = 0; i < MK_N; i++) {
        int ry = MK_Y + MK_TOP + i * MK_ROW, rh = MK_ROW - 6, cy = ry + rh / 2, sel = (i == mk_sel);
        rrect(MK_X + 12, ry, MK_W - 24, rh, 12, sel ? 52 : 36, sel ? 50 : 36, sel ? 84 : 46, 255);
        if (sel) rrect_line(MK_X + 12, ry, MK_W - 24, rh, 12, 2, C_ACCENT, 255);
        textc(F_BODYB, MK_X + 28, cy, AL_L, mk_names[i], C_TEXT, 255);
        int64_t m = i == 0 ? sk_is : i == 1 ? sk_ie : i == 2 ? sk_os : -2;
        if (m >= 0) { fmt_time(v, sizeof v, m); textc(F_SMALLB, MK_X + MK_W - 28, cy, AL_R, v, C_ACCLT, 255); }
        else if (m == -1) textc(F_SMALL, MK_X + MK_W - 28, cy, AL_R, "not set", C_DIM, 255);
    }
}

/* --- update --- */
static void update_player(float dt) {
    pl_clock += dt;
    pl_wait += dt;
    if (an_on) { update_autonext(dt); return; }

    SceAvPlayerFrameInfo fr;
    memset(&fr, 0, sizeof fr);
    if (pl_have && sceAvPlayerGetVideoData(pl_h, &fr) && fr.pData) pl_upload(&fr);
    if (pl_got_frame) {
        pl_pos = sceAvPlayerCurrentTime(pl_h);
        if (!pl_dur) pl_dur = pl_query_dur();
    }

    if (pl_got_frame && pl_resume_ms > 0) {   /* jump to the resume point (Step 3) */
        int64_t t = pl_resume_ms;
        pl_resume_ms = 0;
        if (!pl_dur || t < (int64_t)pl_dur - 5000) {
            char ts[24];
            sceAvPlayerJumpToTime(pl_h, (uint64_t)t);
            pl_jump_ms = t; pl_jump_t = 2.0f; pl_pos = (uint64_t)t;
            fmt_time(ts, sizeof ts, t);
            toast("Resumed at %s", ts);
        }
    }
    if (pl_jump_t > 0) pl_jump_t -= dt;
    if (pl_got_frame && !pl_paused) {   /* save progress every 30 s */
        pl_save_t += dt;
        if (pl_save_t >= 30.0f) { pl_save_t = 0; pl_save_prog(); prog_save(); }
    }

    if (pl_got_frame && !sceAvPlayerIsActive(pl_h)) { pl_finish(); return; }

    if (!pl_got_frame && (pl_wait > 12.0f || (pl_wait > 3.0f && !sceAvPlayerIsActive(pl_h)))) {
        toast("Can't play this video (needs MP4 H.264 + AAC)");
        player_close();
        return;
    }

    if (mk_open) update_mark(dt);
    else {
        if (sk_update(dt)) return;
        if (pressed(SCE_CTRL_CIRCLE)) { player_close(); return; }
        if (pressed(SCE_CTRL_SQUARE) && pl_got_frame) mk_open_menu();
        else if (pressed(SCE_CTRL_CROSS)) {
            if (sk_vis) { sk_do(sk_show_k); return; }
            pl_toggle_pause();
        }
        if (pressed(SCE_CTRL_TRIANGLE)) { pl_ui = !pl_ui; if (pl_ui) pl_ui_t = PL_UI_TIME; }
        if (pressed(SCE_CTRL_LTRIGGER)) { pl_prev_next(-1); return; }
        if (pressed(SCE_CTRL_RTRIGGER)) { pl_prev_next(1); return; }
        if (pl_rep_btn(0, (btn & SCE_CTRL_LEFT) != 0, dt))  pl_seek_by(-PL_SEEK);
        if (pl_rep_btn(1, (btn & SCE_CTRL_RIGHT) != 0, dt)) pl_seek_by(PL_SEEK);
        if (pl_rep_btn(2, (btn & SCE_CTRL_UP) != 0, dt))    pl_set_vol(pl_vol + 1);
        if (pl_rep_btn(3, (btn & SCE_CTRL_DOWN) != 0, dt))  pl_set_vol(pl_vol - 1);

        /* left stick = fast scrub (further = faster) */
        SceCtrlData pad;
        memset(&pad, 0, sizeof pad);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        int sx = (int)pad.lx - 128;
        if (abs(sx) > 50 && pl_got_frame && pl_dur) {
            float k = (abs(sx) - 50) / 78.0f, sp = 10.0f + 110.0f * k * k;
            if (!pl_pend) { pl_pend_base = pl_shown_ms(); pl_pend_ms = pl_pend_base; pl_pend = 1; }
            pl_pend_ms = pl_clamp_ms(pl_pend_ms + (int64_t)((sx < 0 ? -sp : sp) * dt * 1000.0f));
            pl_pend_t = 0.45f;
            pl_show_ui();
        }

        if (tap_pending) {
            if (sk_hit(tap_x, tap_y)) { tap_pending = 0; sk_do(sk_show_k); return; }
            Hit *h = tap_hit();
            if (h && h->id >= H_PBACK) { if (pl_button(h->id)) return; }
            else {
                int side = tap_x < SCREEN_W / 3 ? -1 : (tap_x > SCREEN_W * 2 / 3 ? 1 : 0);
                if (side && side == pl_tap_side && pl_clock - pl_tap_last < 0.35f) {
                    pl_seek_by(side * PL_SEEK);
                    pl_rip_t = 0.6f; pl_rip_side = side;
                } else {
                    pl_ui = !pl_ui;
                    if (pl_ui) pl_ui_t = PL_UI_TIME;
                }
                pl_tap_last = pl_clock;
                pl_tap_side = side;
            }
        }
    }

    if (pl_pend && !pl_scrub) { pl_pend_t -= dt; if (pl_pend_t <= 0) pl_commit(); }
    if (pl_after_t > 0 && !pl_paused) pl_after_t -= dt;   /* hold the jumped-to time while paused */
    if (!pl_paused && !pl_pend && pl_ui && pl_ui_t > 0) { pl_ui_t -= dt; if (pl_ui_t <= 0) pl_ui = 0; }
    int vis = pl_ui || pl_paused || pl_scrub;
    pl_ui_a = clampf(pl_ui_a + (vis ? dt : -dt) * 6.0f, 0, 1);
    if (pl_vol_t > 0) pl_vol_t -= dt;
    if (pl_rip_t > 0) pl_rip_t -= dt;

    if (!pl_paused) {   /* keep the Vita awake while watching */
        pl_tick -= dt;
        if (pl_tick <= 0) { pl_tick = 1.0f; sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT); }
    }
}

/* --- draw --- */
static void fmt_time(char *o, int n, int64_t ms) {
    if (ms < 0) ms = 0;
    int s = (int)(ms / 1000), h = s / 3600, m = (s / 60) % 60;
    s %= 60;
    if (h) snprintf(o, n, "%d:%02d:%02d", h, m, s);
    else snprintf(o, n, "%d:%02d", m, s);
}
static void pl_skip_icon(float cx, float cy, int dir, int a) {
    float s = (float)dir;
    gc(C_TEXT, a);
    tri(cx - 6 * s, cy - 8, cx + 5 * s, cy, cx - 6 * s, cy + 8);
    fillg(dir > 0 ? cx + 6 : cx - 9, cy - 8, 3, 16);
}
static void pl_spinner(int cx, int cy) {
    for (int i = 0; i < 10; i++) {
        float ang = i * 0.6283f;
        int al = 40 + (int)(215 * fmodf(i / 10.0f + pl_clock * 1.2f, 1.0f));
        disc(cx + cosf(ang) * 22, cy + sinf(ang) * 22, 4, C_ACCLT, al);
    }
}

static void draw_player(void) {
    Show *sh = &shows[pl_show];
    fill(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 255);
    if (pl_tex) {
        int dw = SCREEN_W, dh = SCREEN_W * pl_th / pl_tw;
        if (dh > SCREEN_H) { dh = SCREEN_H; dw = SCREEN_H * pl_tw / pl_th; }
        SDL_Rect d = { (SCREEN_W - dw) / 2, (SCREEN_H - dh) / 2, dw, dh };
        SDL_RenderCopy(ren, pl_tex, NULL, &d);
    }
    if (!pl_got_frame) {
        pl_spinner(SCREEN_W / 2, SCREEN_H / 2 - 10);
        textc(F_BODYB, SCREEN_W / 2, SCREEN_H / 2 + 40, AL_C, "Loading...", C_SOFT, 255);
        text(F_SUB, 40, 22, AL_L, pl_title, C_TEXT, 255);
        text(F_SMALL, 40, 52, AL_L, pl_sub, C_SOFT, 255);
        hint(20, BI_O, "Back");
        return;
    }

    if (pl_rip_t > 0) {   /* double-tap ripple */
        float k = 1 - pl_rip_t / 0.6f;
        int cx = pl_rip_side < 0 ? SCREEN_W / 6 : SCREEN_W * 5 / 6, al = (int)(255 * (1 - k));
        char r[16];
        snprintf(r, sizeof r, "%+d s", pl_rip_side * PL_SEEK);
        disc(cx, SCREEN_H / 2, 50 + 50 * k, 255, 255, 255, al * 50 / 255);
        textc(F_SUB, cx, SCREEN_H / 2, AL_C, r, C_TEXT, al);
    }

    int a = an_on ? 0 : (int)(pl_ui_a * 255);
    if (a > 3) {
        int live = pl_ui_a > 0.6f, cx = SCREEN_W / 2, cy = SCREEN_H / 2;
        grad_v(0, 0, SCREEN_W, 130, 0, 0, 0, a * 200 / 255, 0);
        grad_v(0, SCREEN_H - 170, SCREEN_W, 170, 0, 0, 0, 0, a * 225 / 255);

        /* top: back + titles */
        disc(40, 40, 22, 16, 16, 22, a * 200 / 255);
        gc(C_TEXT, a); icon(IC_LEFT, 39, 40, 0.85f, 0, 0, 0);
        if (live) hit(14, 14, 52, 52, H_PBACK, 0, 0);
        text(F_SUB, 78, 18, AL_L, pl_title, C_TEXT, a);
        text(F_SMALL, 78, 48, AL_L, pl_sub, C_SOFT, a);

        /* centre controls (hidden while the Mark menu is open) */
        if (!mk_open) {
            if (sh->neps > 1) {
                int pa = pl_ep > 0 ? a : a / 3, na = pl_ep < sh->neps - 1 ? a : a / 3;
                disc(cx - 230, cy, 24, 16, 16, 22, a * 170 / 255); pl_skip_icon(cx - 230, cy, -1, pa);
                disc(cx + 230, cy, 24, 16, 16, 22, a * 170 / 255); pl_skip_icon(cx + 230, cy, 1, na);
                if (live) {
                    hit(cx - 256, cy - 26, 52, 52, H_PPREV, 0, 0);
                    hit(cx + 204, cy - 26, 52, 52, H_PNEXT, 0, 0);
                }
            }
            char sk[16];
            disc(cx - 120, cy, 30, 16, 16, 22, a * 170 / 255);
            snprintf(sk, sizeof sk, "-%d", PL_SEEK);
            textc(F_BODYB, cx - 120, cy, AL_C, sk, C_TEXT, a);
            disc(cx + 120, cy, 30, 16, 16, 22, a * 170 / 255);
            snprintf(sk, sizeof sk, "+%d", PL_SEEK);
            textc(F_BODYB, cx + 120, cy, AL_C, sk, C_TEXT, a);
            disc(cx, cy, 40, C_ACCENT, a);
            gc(C_TEXT, a);
            if (pl_paused) icon(IC_PLAY, cx + 2, cy, 1.5f, 0, 0, 0);
            else { rrectg(cx - 11, cy - 14, 8, 28, 2); rrectg(cx + 3, cy - 14, 8, 28, 2); }
            if (live) {
                hit(cx - 152, cy - 32, 64, 64, H_PREW, 0, 0);
                hit(cx + 88, cy - 32, 64, 64, H_PFWD, 0, 0);
                hit(cx - 42, cy - 42, 84, 84, H_PPLAY, 0, 0);
            }
        }

        /* seek bar */
        int64_t shown = pl_shown_ms();
        float f = pl_dur ? clampf((float)shown / (float)pl_dur, 0, 1) : 0;
        int fw = (int)(PB_W * f);
        char t0[24], t1[24];
        fmt_time(t0, sizeof t0, shown);
        fmt_time(t1, sizeof t1, (int64_t)pl_dur);
        text(F_SMALLB, PB_X, PB_Y - 34, AL_L, t0, C_TEXT, a);
        text(F_SMALLB, PB_X + PB_W, PB_Y - 34, AL_R, pl_dur ? t1 : "--:--", C_SOFT, a);
        rrect(PB_X, PB_Y - 3, PB_W, 6, 3, 255, 255, 255, a * 60 / 255);
        rrect(PB_X, PB_Y - 3, fw, 6, 3, C_ACCENT, a);
        if (pl_dur) {   /* intro/outro marks (Step 4) */
            float k = (float)PB_W / (float)pl_dur;
            int my = PB_Y - 12;
            if (sk_ie > 0) {
                int x0 = PB_X + (int)((float)(sk_is < 0 ? 0 : sk_is) * k), x1 = PB_X + (int)((float)sk_ie * k);
                rrect(x0, my, x1 - x0 < 4 ? 4 : x1 - x0, 4, 2, C_ACCLT, a);
            } else if (sk_is >= 0) fill(PB_X + (int)((float)sk_is * k) - 1, my - 2, 3, 8, C_ACCLT, a);
            if (sk_os >= 0) {
                int x0 = PB_X + (int)((float)sk_os * k), w2 = PB_X + PB_W - x0;
                rrect(x0, my, w2 < 4 ? 4 : w2, 4, 2, C_ACCLT, a);
            }
        }
        disc(PB_X + fw, PB_Y, pl_scrub ? 11 : 8, C_TEXT, a);

        if (a > 230) {
            int x = 20;
            if (mk_open) {
                x = hint(x, BI_X, "Set");
                x = hint(x, BI_DLR, "Nudge 1 s");
                x = hint_key(x, "L / R", "10 s");
                x = hint(x, BI_O, "Close");
            } else {
                x = hint(x, BI_X, sk_vis ? (sk_show_k == 1 ? "Skip outro" : "Skip intro") : (pl_paused ? "Play" : "Pause"));
                x = hint(x, BI_O, "Back");
                x = hint(x, BI_DLR, "Seek");
                if (sh->neps > 1) x = hint_key(x, "L / R", "Episode");
                x = hint_key(x, "UP / DOWN", "Volume");
                x = hint(x, BI_TRI, "Hide");
                x = hint(x, BI_SQ, "Mark");
            }
        }
    }

    if (mk_open && !an_on) draw_mark();
    draw_skip();

    if (pl_pend) {   /* seek preview pill */
        char s[64], tt[24];
        fmt_time(tt, sizeof tt, pl_pend_ms);
        snprintf(s, sizeof s, "%+d s  \xE2\x80\xA2  %s", (int)((pl_pend_ms - pl_pend_base) / 1000), tt);
        int w = text_w(F_BODYB, s) + 40;
        rrect((SCREEN_W - w) / 2, 84, w, 44, 22, C_PANEL2, 235);
        textc(F_BODYB, SCREEN_W / 2, 106, AL_C, s, C_TEXT, 255);
    }

    if (pl_vol_t > 0) {   /* volume pill */
        int al = pl_vol_t < 0.3f ? (int)(pl_vol_t / 0.3f * 255) : 255, x = SCREEN_W - 230, y = 84;
        char v[8];
        snprintf(v, sizeof v, "%d", pl_vol * 10);
        rrect(x, y, 200, 44, 22, C_PANEL2, al * 240 / 255);
        textc(F_SMALLB, x + 20, y + 22, AL_L, "VOL", C_SOFT, al);
        rrect(x + 64, y + 19, 90, 6, 3, 255, 255, 255, al * 60 / 255);
        rrect(x + 64, y + 19, 9 * pl_vol, 6, 3, C_ACCENT, al);
        textc(F_SMALLB, x + 184, y + 22, AL_R, v, C_TEXT, al);
    }

    if (an_on) draw_autonext();
}

/* ===== Actions ===== */
static void play_episode(int si, int ep) {
    if (si < 0 || si >= nshows || ep < 0 || ep >= shows[si].neps) return;
    Show *sh = &shows[si];
    Prog *p = prog_find(sh->eps[ep].path);
    if (p && p->pos >= PROG_MIN_MS) {   /* started before */
        int64_t ms = p->pos, dur = p->dur;
        if (cfg_resume) { if (player_open(si, ep)) pl_resume_ms = ms; return; }
        char s[400];
        rs_open = 1; rs_show = si; rs_ep = ep; rs_sel = 0; rs_t = 0;
        rs_ms = ms; rs_dur = dur;
        if (sh->kind == K_ANIME || sh->neps > 1) snprintf(s, sizeof s, "%s  \xE2\x80\xA2  %s", sh->name, sh->eps[ep].label);
        else snprintf(s, sizeof s, "%s", sh->name);
        fit_text(F_BODY, s, 456, rs_line, sizeof rs_line);
        return;
    }
    player_open(si, ep);
}

/* --- "Resume watching?" box (Step 3) --- */
#define RS_W  520
#define RS_H  236
#define RS_X  ((SCREEN_W - RS_W) / 2)
#define RS_Y  ((SCREEN_H - RS_H) / 2)
#define RS_B1 250
#define RS_B2 170

static int rs_btn_at(float x, float y) {   /* 0 Resume, 1 Start over, 2 box, -1 outside */
    int bx = RS_X + 32, by = RS_Y + RS_H - 74;
    if (y >= by - 8 && y < by + 56) {
        if (x >= bx && x < bx + RS_B1) return 0;
        if (x >= bx + RS_B1 + 16 && x < bx + RS_B1 + 16 + RS_B2) return 1;
    }
    if (x >= RS_X && x < RS_X + RS_W && y >= RS_Y && y < RS_Y + RS_H) return 2;
    return -1;
}

static void resume_go(int from_start) {
    rs_open = 0;
    if (player_open(rs_show, rs_ep) && !from_start) pl_resume_ms = rs_ms;
}

static void update_resume(float dt) {
    rs_t = clampf(rs_t + dt * 6.0f, 0, 1);
    if (tap_pending) {   /* touch is checked here directly */
        int b = rs_btn_at(tap_x, tap_y);
        tap_pending = 0;
        if (b == 0 || b == 1) { rs_sel = b; resume_go(b); return; }
        if (b < 0) { rs_open = 0; return; }
    }
    if (n_left(dt)) rs_sel = 0;
    if (n_right(dt)) rs_sel = 1;
    n_up(dt); n_down(dt);
    if (pressed(SCE_CTRL_CROSS))  { resume_go(rs_sel); return; }
    if (pressed(SCE_CTRL_SQUARE)) { resume_go(1); return; }
    if (pressed(SCE_CTRL_CIRCLE)) rs_open = 0;
}

static void draw_resume(void) {
    if (!rs_open) return;
    int a = (int)(rs_t * 255);
    fill(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, a * 170 / 255);
    int w = RS_W, h = RS_H, x = RS_X, y = RS_Y + (int)(20 * (1 - rs_t));
    rrect(x, y, w, h, 20, C_PANEL, a);
    rrect_line(x, y, w, h, 20, 1, C_LINE, a);
    text(F_H2, x + 32, y + 24, AL_L, "Resume watching?", C_TEXT, a);
    text(F_BODY, x + 32, y + 68, AL_L, rs_line, C_SOFT, a);

    int bx = x + 32, bw = w - 64, by = y + 106;
    float f = rs_dur > 0 ? clampf((float)rs_ms / (float)rs_dur, 0, 1) : 0;
    int fw = (int)(bw * f);
    char t0[24], t1[24], rl[48];
    fmt_time(t0, sizeof t0, rs_ms);
    fmt_time(t1, sizeof t1, rs_dur);
    rrect(bx, by, bw, 6, 3, 255, 255, 255, a * 50 / 255);
    if (fw >= 6) rrect(bx, by, fw, 6, 3, C_ACCENT, a);
    text(F_SMALLB, bx, by + 14, AL_L, t0, C_TEXT, a);
    text(F_SMALLB, bx + bw, by + 14, AL_R, rs_dur > 0 ? t1 : "--:--", C_DIM, a);

    snprintf(rl, sizeof rl, "Resume %s", t0);
    int yy = y + h - 74;
    button(bx, yy, RS_B1, 48, 1, IC_PLAY, rl, rs_sel == 0, a);
    button(bx + RS_B1 + 16, yy, RS_B2, 48, 0, IC_REFRESH, "Start over", rs_sel == 1, a);
}

static void set_feature(int i) {
    if (nshows == 0) { wtex_free(&hero_syn); return; }
    feat = i; feat_t = 0; hero_a = 0;
    Show *sh = &shows[i];
    ensure_cover(sh);
    fit_text(F_HERO, sh->name, 600, hero_title, sizeof hero_title);
    show_meta(sh, hero_meta, sizeof hero_meta);
    genres_fmt(sh->genres, hero_genres, sizeof hero_genres);
    wtex_make(&hero_syn, F_BODY, sh->synopsis, 540, 2);
}
static void step_feature(int d) { if (nshows > 0) set_feature((feat + d + nshows) % nshows); }

static int next_ep_row(int from, int dir) {
    for (int i = from + dir; i >= 0 && i < nrows; i += dir)
        if (rows[i].ep >= 0) return i;
    return from;
}

static void close_details(void) {
    free(rows); rows = NULL;
    free(row_y); row_y = NULL;
    nrows = 0;
    wtex_free(&d_syn);
    in_details = 0;
}

static void open_details(int si) {
    Show *sh = &shows[si];
    if (in_details) close_details();
    rows = malloc(sizeof(Row) * (sh->neps * 2 + 2));
    row_y = malloc(sizeof(int) * (sh->neps * 2 + 2));
    if (!rows || !row_y) { close_details(); return; }
    in_details = 1; d_show = si; d_focus = 0; d_btn = 0;
    nrows = 0;
    int y = 0;
    for (int i = 0; i < sh->neps; i++) {
        if (sh->nseasons > 1 && (i == 0 || sh->eps[i].season != sh->eps[i - 1].season)) {
            rows[nrows].ep = -1; rows[nrows].season = sh->eps[i].season;
            row_y[nrows++] = y; y += D_ROW_HEAD;
        }
        rows[nrows].ep = i; rows[nrows].season = sh->eps[i].season;
        row_y[nrows++] = y; y += D_ROW_EP;
    }
    rows_h = y;
    d_sel = next_ep_row(-1, 1);
    d_scroll = d_target = 0;
    d_has_list = !(sh->kind == K_MOVIE && sh->neps == 1);
    ensure_cover(sh);
    fit_text(F_HERO, sh->name, SCREEN_W - 260, d_title, sizeof d_title);
    show_meta(sh, d_meta, sizeof d_meta);
    genres_fmt(sh->genres, d_genres, sizeof d_genres);
    wtex_make(&d_syn, F_BODY, sh->synopsis[0] ? sh->synopsis : "No description yet. Add an info.txt to this folder.", 690, 3);
}

static void go_page(int p) {
    if (in_details) close_details();
    page = p;
    drawer_open = 0;
}

static void do_rescan(void) {
    prog_gen++;   /* re-check watch progress for the new show list */
    if (in_details) close_details();
    scan_library();
    h_focus = h_btn = 0;
    for (int r = 0; r < 3; r++) { row_sel[r] = 0; row_scroll[r] = row_target[r] = 0; }
    g_sel[0] = g_sel[1] = 0; g_scroll[0] = g_scroll[1] = g_target[0] = g_target[1] = 0;
    h_scroll = h_target = 0;
    set_feature(0);
    toast("Library: %d anime, %d movies", nanime, nmovie);
}

static void open_drawer(void) { drawer_open = 1; drawer_sel = page; }

static void drawer_pick(int i) {
    if (i <= 2) go_page(i);
    else if (i == 3) { drawer_open = 0; do_rescan(); }
    else { drawer_open = 0; settings_open(); }
}

/* ===== Home ===== */
static int hrow[3], nhrow;   /* visible rows, top to bottom (HR_ values) */

static int hr_n(int t) { return t == HR_CW ? ncw : (t == HR_MOVIE ? nmovie : nanime); }
static int hr_slot(int t) {
    for (int i = 0; i < nhrow; i++) if (hrow[i] == t) return i;
    return -1;
}
static int home_step(int s, int d) {   /* next row with cards, -1 = none */
    for (int i = s + d; i >= 0 && i < nhrow; i += d)
        if (hr_n(hrow[i]) > 0) return i;
    return -1;
}
static void home_rows(void) {
    cw_refresh();
    nhrow = 0;
    if (ncw > 0) hrow[nhrow++] = HR_CW;
    hrow[nhrow++] = HR_ANIME;
    hrow[nhrow++] = HR_MOVIE;
}
static int row_y_of(int s) { return HERO_H + 16 + s * SEC_H; }

static void row_retarget(int t) {
    int n = hr_n(t);
    float left = (float)row_sel[t] * (CARD_W + CARD_GAP), view = SCREEN_W - 48;
    if (left < row_target[t]) row_target[t] = left;
    if (left + CARD_W > row_target[t] + view) row_target[t] = left + CARD_W - view;
    float maxs = n * (CARD_W + CARD_GAP) - CARD_GAP - view;
    if (maxs < 0) maxs = 0;
    row_target[t] = clampf(row_target[t], 0, maxs);
}

static void cw_play(int i) {   /* Continue Watching: resume straight away */
    if (i < 0 || i >= ncw) return;
    CWItem c = cw[i];
    if (player_open(c.show, c.ep) && c.pos >= PROG_MIN_MS) pl_resume_ms = c.pos;
}

static void update_home(float dt) {
    home_rows();
    int up = n_up(dt), down = n_down(dt), left = n_left(dt), right = n_right(dt), moved = 0;
    if (h_focus > 0 && hr_slot(h_focus - 1) < 0) {   /* the focused row went away */
        int s = home_step(-1, 1);
        h_focus = s >= 0 ? hrow[s] + 1 : 0;
        moved = 1;
    }
    if (h_focus == 0) {
        if (nshows > 0) {
            if (left)  { if (h_btn == 1) h_btn = 0; else step_feature(-1); }
            if (right) { if (h_btn == 0) h_btn = 1; else step_feature(1); }
        }
        if (down) { int s = home_step(-1, 1); if (s >= 0) { h_focus = hrow[s] + 1; moved = 1; } }
        if (pressed(SCE_CTRL_CROSS) && nshows > 0) {
            if (h_btn == 0) play_episode(feat, 0); else open_details(feat);
            return;
        }
    } else {
        int t = h_focus - 1, s = hr_slot(t), n = hr_n(t);
        if (row_sel[t] > n - 1) { row_sel[t] = n > 0 ? n - 1 : 0; row_retarget(t); }
        if (left && row_sel[t] > 0) { row_sel[t]--; row_retarget(t); }
        if (right && row_sel[t] < n - 1) { row_sel[t]++; row_retarget(t); }
        if (up) { int p = home_step(s, -1); h_focus = p >= 0 ? hrow[p] + 1 : 0; moved = 1; }
        if (down) { int q = home_step(s, 1); if (q >= 0) { h_focus = hrow[q] + 1; moved = 1; } }
        if (n > 0 && pressed(SCE_CTRL_CROSS)) {
            if (t == HR_CW) cw_play(row_sel[t]);
            else open_details((t == HR_MOVIE ? movie_ix : anime_ix)[row_sel[t]]);
            return;
        }
        if (t == HR_CW && n > 0 && pressed(SCE_CTRL_TRIANGLE)) cw_remove(row_sel[t]);
    }

    float maxs = row_y_of(nhrow) - SCREEN_H + 10;
    if (maxs < 0) maxs = 0;
    if (drag_dy != 0) {
        h_scroll = clampf(h_scroll + drag_dy, 0, maxs);
        h_target = h_scroll;
    } else {
        if (moved) h_target = h_focus == 0 ? 0 : row_y_of(hr_slot(h_focus - 1)) - BAR_H - 14;
        h_target = clampf(h_target, 0, maxs);
        h_scroll += (h_target - h_scroll) * lerpk(dt);
    }

    for (int s = 0; s < nhrow; s++) {
        int t = hrow[s], n = hr_n(t);
        float maxr = n * (CARD_W + CARD_GAP) - CARD_GAP - (SCREEN_W - 48);
        if (maxr < 0) maxr = 0;
        int cy = row_y_of(s) + 76 - (int)h_scroll;
        if (drag_dx != 0 && t_y0 >= cy && t_y0 < cy + CARD_H + 30) {
            row_scroll[t] = clampf(row_scroll[t] + drag_dx, 0, maxr);
            row_target[t] = row_scroll[t];
        } else row_scroll[t] += (row_target[t] - row_scroll[t]) * lerpk(dt);
    }
}

static void draw_hero(int y) {
    if (nshows == 0) {
        fill(0, y, SCREEN_W, HERO_H, 18, 18, 26, 255);
        grad_v(0, y + HERO_H - 120, SCREEN_W, 120, C_BG, 0, 255);
        dango_icon(820, y + 190, 3.0f);
        text(F_HERO, 48, y + 100, AL_L, "Welcome to aniwatch", C_TEXT, 255);
        text(F_BODY, 48, y + 166, AL_L, "Put each anime in its own folder in ux0:data/aniwatch/anime/", C_SOFT, 255);
        text(F_BODY, 48, y + 194, AL_L, "and each movie in ux0:data/aniwatch/movies/", C_SOFT, 255);
        text(F_BODY, 48, y + 222, AL_L, "then press Triangle to rescan.", C_SOFT, 255);
        return;
    }
    Show *sh = &shows[feat];
    int a = (int)(hero_a * 255);
    draw_backdrop(feat, y, HERO_H, a);
    const int tx = 48;
    text(F_HERO, tx, y + 84, AL_L, hero_title, C_TEXT, a);
    text(F_BODY, tx, y + 142, AL_L, hero_meta, C_SOFT, a);
    int gx = tx;
    if (hero_genres[0]) gx += textc(F_BODY, tx, y + 190, AL_L, hero_genres, C_DIM, a) + 16;
    if (sh->rating[0]) rating_pill(gx, y + 190, sh->rating, a);
    wtex_draw(&hero_syn, tx, y + 218, C_DIM, a);
    int by = y + 270;
    button(tx, by, 196, 48, 1, IC_PLAY, "Watch Now", h_focus == 0 && h_btn == 0, a);
    button(tx + 214, by, 156, 48, 0, IC_INFO, "Details", h_focus == 0 && h_btn == 1, a);
    hit(tx, by, 196, 48, H_HPLAY, 0, 1);
    hit(tx + 214, by, 156, 48, H_HDETAILS, 0, 1);
    if (nshows > 1) {
        circle_btn(SCREEN_W - 104, by + 24, IC_LEFT, H_HPREV, 1);
        circle_btn(SCREEN_W - 48, by + 24, IC_RIGHT, H_HNEXT, 1);
    }
}

static void draw_cw_card(int i, int x, int y, int w, int h, int sel) {
    CWItem *c = &cw[i];
    Show *sh = &shows[c->show];
    if (sh->cover_state == 0 && cover_budget > 0) { cover_budget--; ensure_cover(sh); }
    if (sh->cover) {
        SDL_Rect d = { x, y, w, h };
        tex_col(sh->cover, 255, 255, 255, 255);
        SDL_RenderCopy(ren, sh->cover, NULL, &d);
    } else {
        char fc[8]; first_char(sh->name, fc);
        rrect(x, y, w, h, 10, C_PANEL, 255);
        textc(F_H2, x + w / 2, y + h / 2, AL_C, fc, C_DIM, 255);
    }
    int tw = text_w(F_SMALLB, c->tag) + 16;
    rrect(x + 8, y + 8, tw, 26, 9, 18, 18, 24, 215);
    textc(F_SMALLB, x + 8 + tw / 2, y + 21, AL_C, c->tag, C_TEXT, 255);
    int lw = text_w(F_SMALLB, c->left) + 14;
    rrect(x + 8, y + h - 34, lw, 24, 8, 18, 18, 24, 215);
    textc(F_SMALLB, x + 8 + lw / 2, y + h - 22, AL_C, c->left, C_SOFT, 255);
    if (c->dur > 0 && c->pos > 0) {
        int bw = w - 16, fw = (int)(bw * clampf((float)c->pos / (float)c->dur, 0, 1));
        rrect(x + 8, y + h - 7, bw, 4, 2, 0, 0, 0, 190);
        if (fw >= 4) rrect(x + 8, y + h - 7, fw, 4, 2, C_ACCENT, 255);
    }
    if (sel) {
        disc(x + w / 2, y + h / 2, 24, C_ACCENT, 235);
        gc(C_TEXT, 255); icon(IC_PLAY, x + w / 2 + 2, y + h / 2, 1.0f, 108, 99, 255);
        rrect_line(x - 5, y - 5, w + 10, h + 10, 15, 3, C_ACCENT, 255);
        text(F_SMALLB, x, y + h + 10, AL_L, sh->card_name, C_ACCLT, 255);
    } else text(F_SMALLB, x, y + h + 10, AL_L, sh->card_name, C_TEXT, 255);
}

static void draw_row(int s, int y) {
    int t = hrow[s], n = hr_n(t), foc = (h_focus == t + 1), cy = y + 76;
    if (t == HR_CW) {
        sec_head(24, y, "PICK UP WHERE YOU LEFT OFF", "Continue Watching");
        for (int i = 0; i < n; i++) {
            int x = 24 + i * (CARD_W + CARD_GAP) - (int)row_scroll[t];
            if (x + CARD_W < 0 || x > SCREEN_W) continue;
            draw_cw_card(i, x, cy, CARD_W, CARD_H, foc && row_sel[t] == i);
            hit(x, cy, CARD_W, CARD_H + 30, H_ROWCARD, (t << 16) | i, 1);
        }
        return;
    }
    int *ix = t == HR_MOVIE ? movie_ix : anime_ix;
    sec_head(24, y, "YOUR LIBRARY", t == HR_MOVIE ? "Movies" : "Anime");
    if (n > 0) {
        int px = pill_btn(SCREEN_W - 24, y + 34, "See all", 1);
        hit(px, y + 16, SCREEN_W - 24 - px, 36, H_SEEALL, t == HR_MOVIE ? PG_MOVIES : PG_ANIME, 1);
    }
    if (n == 0) {
        rrect(24, cy, SCREEN_W - 48, 110, 16, C_PANEL, 255);
        textc(F_BODY, SCREEN_W / 2, cy + 55, AL_C,
              t == HR_MOVIE ? "No movies yet. Add them to ux0:data/aniwatch/movies/"
                            : "No anime yet. Add show folders to ux0:data/aniwatch/anime/", C_DIM, 255);
        return;
    }
    for (int i = 0; i < n; i++) {
        int x = 24 + i * (CARD_W + CARD_GAP) - (int)row_scroll[t];
        if (x + CARD_W < 0 || x > SCREEN_W) continue;
        draw_card(ix[i], x, cy, CARD_W, CARD_H, foc && row_sel[t] == i);
        hit(x, cy, CARD_W, CARD_H + 30, H_ROWCARD, (t << 16) | i, 1);
    }
}

static void draw_home(void) {
    home_rows();
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG, 255);
    int oy = -(int)h_scroll;
    draw_hero(oy);
    for (int s = 0; s < nhrow; s++) {
        int y = row_y_of(s) + oy;
        if (y + SEC_H < BAR_H || y > SCREEN_H) continue;
        draw_row(s, y);
    }
}

/* ===== Anime / Movies grid pages ===== */
static void update_grid(int k, float dt) {
    int *ix = k ? movie_ix : anime_ix, n = k ? nmovie : nanime, moved = 0;
    int *sel = &g_sel[k];
    if (n > 0) {
        if (n_left(dt) && *sel > 0) { (*sel)--; moved = 1; }
        if (n_right(dt) && *sel < n - 1) { (*sel)++; moved = 1; }
        if (n_up(dt) && *sel - GCOLS >= 0) { *sel -= GCOLS; moved = 1; }
        if (n_down(dt)) {
            if (*sel + GCOLS < n) *sel += GCOLS;
            else if (*sel / GCOLS < (n - 1) / GCOLS) *sel = n - 1;
            moved = 1;
        }
        if (pressed(SCE_CTRL_CROSS)) { open_details(ix[*sel]); return; }
    }
    int nr = (n + GCOLS - 1) / GCOLS;
    float maxs = GRID_Y + nr * GROW_H - (SCREEN_H - 10);
    if (maxs < 0) maxs = 0;
    if (drag_dy != 0) {
        g_scroll[k] = clampf(g_scroll[k] + drag_dy, 0, maxs);
        g_target[k] = g_scroll[k];
    } else {
        if (moved) {
            int top = GRID_Y + (*sel / GCOLS) * GROW_H;
            if (*sel < GCOLS) g_target[k] = 0;
            else if (top - g_target[k] < BAR_H + 20) g_target[k] = top - BAR_H - 20;
            if (top + GROW_H - g_target[k] > SCREEN_H - 10) g_target[k] = top + GROW_H - (SCREEN_H - 10);
            g_target[k] = clampf(g_target[k], 0, maxs);
        }
        g_scroll[k] += (g_target[k] - g_scroll[k]) * lerpk(dt);
    }
}

static void draw_grid(int k) {
    int *ix = k ? movie_ix : anime_ix, n = k ? nmovie : nanime;
    int oy = -(int)g_scroll[k];
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG, 255);
    const int x0 = (SCREEN_W - (GCOLS * GCARD_W + (GCOLS - 1) * GGAP)) / 2;
    sec_head(x0, 78 + oy, "LIBRARY", k ? "Movies" : "Anime");
    char c[32];
    snprintf(c, sizeof c, k ? "%d MOVIES" : "%d SHOWS", n);
    pill_btn(SCREEN_W - x0, 108 + oy, c, 0);
    if (n == 0) {
        textc(F_BODY, SCREEN_W / 2, 280, AL_C,
              k ? "Add movies to ux0:data/aniwatch/movies/" : "Add show folders to ux0:data/aniwatch/anime/", C_DIM, 255);
        return;
    }
    for (int i = 0; i < n; i++) {
        int x = x0 + (i % GCOLS) * (GCARD_W + GGAP);
        int y = GRID_Y + (i / GCOLS) * GROW_H + oy;
        if (y + GROW_H < BAR_H || y > SCREEN_H) continue;
        draw_card(ix[i], x, y, GCARD_W, GCARD_H, g_sel[k] == i);
        hit(x, y, GCARD_W, GCARD_H + 30, H_GRIDCARD, i, 1);
    }
}

/* ===== Online metadata (Step 3a) ===== */
#define META_HOST    "https://vitaaniwatch-meta.youshi17000.workers.dev"
#define TMP_DIR      DATA_ROOT "/tmp"
#define CA_SRC       "app0:cacert.pem"
#define CA_PATH      DATA_ROOT "/ca.pem"
#define NET_LOG      DATA_ROOT "/net_log.txt"
#define NET_MEM      (1 * 1024 * 1024)
#define NET_INSECURE 0      /* 1 = skip certificate checks (ONLY to test a CA problem) */
#define F_MAX  12
#define FC_W   150
#define FC_H   225
#define FC_GAP 22
#define FC_Y   112

static void set_feature(int i);

static int net_ready;                 /* 0 not tried, 1 ok, -1 failed */
static const char *ca_file = CA_PATH;
static char net_err[160];

static void net_log(const char *fmt, ...) {
    FILE *f = fopen(NET_LOG, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* only the roots Cloudflare certificates chain to (a full bundle runs the Vita out of locks) */
static const char *const ca_wanted[] = {
    "ISRG Root X1", "ISRG Root X2",
    "GTS Root R1", "GTS Root R2", "GTS Root R3", "GTS Root R4", "GlobalSign Root CA",
    "DigiCert Global Root CA", "DigiCert Global Root G2", "DigiCert Global Root G3",
    "Baltimore CyberTrust Root",
    "USERTrust RSA Certification Authority", "USERTrust ECC Certification Authority",
    "Comodo AAA Services root",
    "SSL.com Root Certification Authority RSA", "SSL.com Root Certification Authority ECC",
};

static void ca_trim(char *s) {
    s[strcspn(s, "\r\n")] = 0;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static int ca_build(void) {
    FILE *in = fopen(CA_SRC, "r");
    if (!in) { net_log("CA: %s missing", CA_SRC); return -1; }
    FILE *out = fopen(CA_PATH, "w");
    if (!out) { fclose(in); net_log("CA: can't write %s", CA_PATH); return -1; }
    char line[512], name[256] = "", first[96] = "";
    int keep = 0, kept = 0, seen = 0, nlines = 0;
    while (fgets(line, sizeof line, in)) {
        nlines++;
        if (keep) {
            fputs(line, out);
            if (strstr(line, "-----END CERTIFICATE-----")) { keep = 0; kept++; }
            continue;
        }
        ca_trim(line);
        if (!first[0] && line[0]) snprintf(first, sizeof first, "%s", line);
        if (strstr(line, "-----BEGIN CERTIFICATE-----")) {
            seen++;
            for (int i = 0; i < (int)(sizeof ca_wanted / sizeof *ca_wanted); i++)
                if (!strcmp(name, ca_wanted[i])) { keep = 1; break; }
            if (keep) fprintf(out, "%s\n-----BEGIN CERTIFICATE-----\n", name);
            else if (seen <= 3) net_log("CA: skip '%s'", name);
            continue;
        }
        if (!line[0] || line[0] == '=' || line[0] == '#' || strstr(line, "-----END")) continue;
        snprintf(name, sizeof name, "%s", line);   /* last text line above BEGIN = the name */
    }
    fclose(in);
    fclose(out);
    net_log("CA: %d lines, %d certificates in file, kept %d", nlines, seen, kept);
    if (!seen) net_log("CA: first line: %s", first);
    return kept;
}

static int net_init(void) {
    if (net_ready) return net_ready > 0;
    net_ready = -1;
    FILE *lf = fopen(NET_LOG, "w");
    if (lf) fclose(lf);
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    void *mem = malloc(NET_MEM);
    if (!mem) { net_log("no memory for sceNet"); return 0; }
    SceNetInitParam p;
    p.memory = mem; p.size = NET_MEM; p.flags = 0;
    net_log("sceNetInit %08X", sceNetInit(&p));
    net_log("sceNetCtlInit %08X", sceNetCtlInit());
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) { net_log("curl_global_init failed"); return 0; }
    if (ca_build() <= 0) net_log("CA: no certificates kept, HTTPS will fail");
    net_log("%s", curl_version());
    net_ready = 1;
    return 1;
}

typedef struct { char *p; size_t n, cap; FILE *f; } NetBuf;
static volatile int f_cancel, f_ok;

static size_t net_write(void *d, size_t sz, size_t nm, void *u) {
    NetBuf *b = u;
    size_t n = sz * nm;
    if (b->f) return fwrite(d, 1, n, b->f);
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 8192;
        while (c < b->n + n + 1) c *= 2;
        char *q = realloc(b->p, c);
        if (!q) return 0;
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
    b->p[b->n] = 0;
    return n;
}

static int net_abort(void *u, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)u; (void)a; (void)b; (void)c; (void)d;
    return f_cancel ? 1 : 0;
}

static long net_get(const char *url, NetBuf *b) {
    CURL *c = curl_easy_init();
    if (!c) { snprintf(net_err, sizeof net_err, "curl init failed"); return -1; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, net_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, b);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "aniwatch/" APP_VERSION " (PS Vita)");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, net_abort);
    curl_easy_setopt(c, CURLOPT_CAINFO, ca_file);
#if NET_INSECURE
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (r != CURLE_OK) {
        snprintf(net_err, sizeof net_err, "%s", curl_easy_strerror(r));
        net_log("GET %s -> curl error %d: %s", url, (int)r, net_err);
        return -1;
    }
    net_log("GET %s -> %ld", url, code);
    return code;
}

static int net_check(long code, NetBuf *b) {
    if (code < 0) return 0;                          /* net_err already set */
    if (b->p && !strncmp(b->p, "ERR", 3)) {
        char *t = strchr(b->p, '\t');
        snprintf(net_err, sizeof net_err, "%s", t ? t + 1 : "server error");
        net_err[strcspn(net_err, "\r\n")] = 0;
        return 0;
    }
    if (code != 200) { snprintf(net_err, sizeof net_err, "server error %ld", code); return 0; }
    if (!b->p) { snprintf(net_err, sizeof net_err, "empty reply"); return 0; }
    return 1;
}

static int net_file(const char *url, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(net_err, sizeof net_err, "can't write %s", path); return 0; }
    NetBuf b = { 0 };
    b.f = f;
    long code = net_get(url, &b);
    long sz = ftell(f);
    fclose(f);
    if (code != 200 || sz < 256) { sceIoRemove(path); return 0; }   /* tiny = an ERR reply */
    return 1;
}

static void url_enc(const char *s, char *o, int n) {
    static const char hx[] = "0123456789ABCDEF";
    int j = 0;
    for (; *s && j < n - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o[j++] = c;
        else { o[j++] = '%'; o[j++] = hx[c >> 4]; o[j++] = hx[c & 15]; }
    }
    o[j] = 0;
}

static void make_url(const char *path, char *out, int n) {
    if (path[0] == '/') snprintf(out, n, "%s%s", META_HOST, path);
    else if (!strncmp(path, "http", 4) && strncmp(path, META_HOST, strlen(META_HOST))) {
        char e[600]; url_enc(path, e, sizeof e);
        snprintf(out, n, "%s/img?u=%s", META_HOST, e);
    } else if (!strncmp(path, "http", 4)) snprintf(out, n, "%s", path);
    else snprintf(out, n, "%s/%s", META_HOST, path);
}

static int split_tabs(char *l, char **f, int max) {
    int n = 0;
    f[n++] = l;
    while (n < max && (l = strchr(l, '\t'))) { *l++ = 0; f[n++] = l; }
    return n;
}

static int junk_word(const char *w) {
    static const char *const junk[] = { "1080p", "720p", "480p", "2160p", "bluray", "blu-ray", "bdrip",
        "webrip", "web-dl", "web", "x264", "x265", "hevc", "complete", "batch", "dual", "multi" };
    if (!strncasecmp(w, "season", 6)) return 1;
    if ((w[0] == 's' || w[0] == 'S') && isdigit((unsigned char)w[1])) return 1;   /* S01, S2, S01E01 */
    for (int i = 0; i < (int)(sizeof junk / sizeof *junk); i++) if (!strcasecmp(w, junk[i])) return 1;
    return 0;
}

static void clean_query(const char *in, char *out, int n) {
    char t[256];
    int j = 0, depth = 0;
    for (int i = 0; in[i] && j < (int)sizeof t - 1; i++) {
        char c = in[i];
        if (c == '[' || c == '(' || c == '{') depth++;
        else if (c == ']' || c == ')' || c == '}') { if (depth) depth--; }
        else if (!depth) t[j++] = (c == '_' || c == '.') ? ' ' : c;
    }
    t[j] = 0;
    out[0] = 0;
    int len = 0;
    char *save = NULL;
    for (char *w = strtok_r(t, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
        if (junk_word(w)) break;
        if (!strcmp(w, "-")) continue;
        len += snprintf(out + len, n - len, "%s%s", len ? " " : "", w);
        if (len >= n - 1) break;
    }
    if (!out[0]) snprintf(out, n, "%s", in);
}

/* ---- fetch state ---- */
typedef struct {
    char src[8], id[40], type[24], year[12], title[160], poster[400];
    char card[160], sub[64], full[200], line[96], tag[8];
    SDL_Texture *tex;
} FRes;

enum { FJ_SEARCH = 1, FJ_APPLY };
static FRes fres[F_MAX];
static int f_nres, f_sel, f_state, f_good, f_tried[F_MAX];   /* f_state 0 results, 1 searching, 2 saving */
static int in_fetch, f_show, f_src, f_kb, f_kb_seen, f_job;
static float f_scroll, f_target, f_spin, f_kb_t;
static char f_query[128], f_qline[200], f_msg[200], f_kb_buf[160];
static const char *const f_srcs[3]    = { "kitsu", "tmdb", "all" };
static const char *const f_src_lbl[3] = { "KITSU", "TMDB", "BOTH" };
static SDL_Thread *f_thr;
static SDL_atomic_t f_done, f_listed, f_pready[F_MAX];        /* pready: 1 file ready, -1 none */
static char fj_query[128], fj_src[8], fj_id[40], fj_dir[512];
static int fj_mode, fj_kind;

static void fetch_toast(const char *m) {
    snprintf(toast_msg, sizeof toast_msg, "%s", m);
    toast_t = 2.5f;
}

/* ---- worker thread (no SDL drawing or TTF in here) ---- */
static int fetch_do_search(void) {
    char q[400], url[700];
    url_enc(fj_query, q, sizeof q);
    snprintf(url, sizeof url, "%s/search?q=%s&src=%s%s", META_HOST, q, f_srcs[fj_mode],
             fj_kind == K_MOVIE ? "&kind=movie" : "");
    NetBuf b = { 0 };
    long code = net_get(url, &b);
    int ok = net_check(code, &b);
    if (ok) {
        char *l = b.p;
        while (l && *l && f_nres < F_MAX) {
            char *nl = strchr(l, '\n');
            if (nl) *nl++ = 0;
            l[strcspn(l, "\r")] = 0;
            char *fl[8];
            int n = split_tabs(l, fl, 8);
            if (n >= 6 && !strcmp(fl[0], "R")) {
                FRes *r = &fres[f_nres++];
                snprintf(r->src, sizeof r->src, "%s", fl[1]);
                snprintf(r->id, sizeof r->id, "%s", fl[2]);
                snprintf(r->type, sizeof r->type, "%s", fl[3]);
                snprintf(r->year, sizeof r->year, "%s", fl[4]);
                snprintf(r->title, sizeof r->title, "%s", fl[5]);
                snprintf(r->poster, sizeof r->poster, "%s", n >= 7 ? fl[6] : "");
            }
            l = nl;
        }
    }
    free(b.p);
    if (!ok) return 0;
    SDL_AtomicSet(&f_listed, 1);                 /* main thread may show the list now */
    for (int i = 0; i < f_nres && !f_cancel; i++) {
        if (!fres[i].poster[0]) { SDL_AtomicSet(&f_pready[i], -1); continue; }
        char u[700], p[300];
        make_url(fres[i].poster, u, sizeof u);
        snprintf(p, sizeof p, TMP_DIR "/p%d.jpg", i);
        SDL_AtomicSet(&f_pready[i], net_file(u, p) ? 1 : -1);
    }
    return 1;
}

static void file_replace(const char *from, const char *to) {
    sceIoRemove(to);
    sceIoRename(from, to);
}

static int fetch_img(const char *path, const char *dir, const char *name) {
    char url[700], fin[600], part[620];
    make_url(path, url, sizeof url);
    snprintf(fin, sizeof fin, "%s/%s", dir, name);
    snprintf(part, sizeof part, "%s.part", fin);
    if (!net_file(url, part)) return 0;
    file_replace(part, fin);
    return 1;
}

static int fetch_do_apply(void) {
    char a[64], i2[96], url[700];
    url_enc(fj_src, a, sizeof a);
    url_enc(fj_id, i2, sizeof i2);
    snprintf(url, sizeof url, "%s/info?src=%s&id=%s", META_HOST, a, i2);
    NetBuf b = { 0 };
    long code = net_get(url, &b);
    if (!net_check(code, &b)) { free(b.p); return 0; }
    char ip[600], part[620], cover[400] = "", banner[400] = "";
    snprintf(ip, sizeof ip, "%s/info.txt", fj_dir);
    snprintf(part, sizeof part, "%s.part", ip);
    FILE *f = fopen(part, "w");
    if (!f) { free(b.p); snprintf(net_err, sizeof net_err, "can't write info.txt"); return 0; }
    int lines = 0;
    char *l = b.p;
    while (l && *l) {
        char *nl = strchr(l, '\n');
        if (nl) *nl++ = 0;
        l[strcspn(l, "\r")] = 0;
        if (!strncmp(l, "cover=", 6)) snprintf(cover, sizeof cover, "%s", l + 6);
        else if (!strncmp(l, "banner=", 7)) snprintf(banner, sizeof banner, "%s", l + 7);
        else if (strchr(l, '=')) { fprintf(f, "%s\n", l); lines++; }
        l = nl;
    }
    fclose(f);
    free(b.p);
    if (!lines) { sceIoRemove(part); snprintf(net_err, sizeof net_err, "no info for this title"); return 0; }
    if (cover[0]) fetch_img(cover, fj_dir, "cover.jpg");
    if (banner[0]) fetch_img(banner, fj_dir, "banner.jpg");
    file_replace(part, ip);
    return 1;
}

static int fetch_thread(void *u) {
    (void)u;
    f_ok = (f_job == FJ_SEARCH) ? fetch_do_search() : fetch_do_apply();
    SDL_AtomicSet(&f_done, 1);
    return 0;
}

/* ---- main thread ---- */
static void fetch_stop(void) {
    if (f_thr) { f_cancel = 1; SDL_WaitThread(f_thr, NULL); f_thr = NULL; }
    f_cancel = 0;
}

static void fetch_start(int job) {
    fetch_stop();
    f_job = job; f_ok = 0; net_err[0] = 0;
    SDL_AtomicSet(&f_done, 0);
    SDL_AtomicSet(&f_listed, 0);
    f_thr = SDL_CreateThreadWithStackSize(fetch_thread, "aw_fetch", 256 * 1024, NULL);
    if (!f_thr) { snprintf(net_err, sizeof net_err, "could not start a thread"); SDL_AtomicSet(&f_done, 1); }
}

static void fetch_free(void) {
    for (int i = 0; i < F_MAX; i++) {
        if (fres[i].tex) SDL_DestroyTexture(fres[i].tex);
        fres[i].tex = NULL;
        f_tried[i] = 0;
        SDL_AtomicSet(&f_pready[i], 0);
    }
    f_nres = 0;
}

static void fetch_search(void) {
    fetch_stop();
    fetch_free();
    f_sel = 0; f_scroll = f_target = 0; f_state = 1; f_msg[0] = 0;
    char t[200];
    snprintf(t, sizeof t, "Results for \"%s\"", f_query);
    fit_text(F_BODY, t, 470, f_qline, sizeof f_qline);
    snprintf(fj_query, sizeof fj_query, "%s", f_query);
    fj_mode = f_src;
    fj_kind = shows[f_show].kind;
    fetch_start(FJ_SEARCH);
}

static void fetch_fit(void) {
    for (int i = 0; i < f_nres; i++) {
        FRes *r = &fres[i];
        char ty[24];
        int k;
        for (k = 0; r->type[k] && k < 23; k++) ty[k] = toupper((unsigned char)r->type[k]);
        ty[k] = 0;
        snprintf(r->tag, sizeof r->tag, "%s", strcmp(r->src, "tmdb") ? "KITSU" : "TMDB");
        fit_text(F_SMALLB, r->title, FC_W, r->card, sizeof r->card);
        fit_text(F_SUB, r->title, SCREEN_W - 80, r->full, sizeof r->full);
        if (r->year[0] && ty[0]) snprintf(r->sub, sizeof r->sub, "%s  \xE2\x80\xA2  %s", r->year, ty);
        else snprintf(r->sub, sizeof r->sub, "%s", r->year[0] ? r->year : ty);
        snprintf(r->line, sizeof r->line, "%s%s%s", r->sub, r->sub[0] ? "  \xE2\x80\xA2  " : "", r->tag);
    }
}

static void fetch_close(void) {
    fetch_stop();
    f_job = 0;
    fetch_free();
    if (f_kb) { SDL_StopTextInput(); f_kb = 0; }
    in_fetch = 0;
    f_state = 0;
}

static void fetch_open(int si) {
    Show *sh = &shows[si];
    if (sh->kind == K_MOVIE && !strcmp(sh->dir, MOVIE_ROOT)) {
        fetch_toast("Put this movie in its own folder to fetch info");
        return;
    }
    if (!net_init()) { fetch_toast("Network failed to start"); return; }
    int st = 0;
    sceNetCtlInetGetState(&st);
    if (st != SCE_NETCTL_STATE_CONNECTED) { fetch_toast("Not connected to Wi-Fi"); return; }
    sceIoMkdir(TMP_DIR, 0777);
    in_fetch = 1;
    f_show = si;
    f_src = sh->kind == K_MOVIE ? 2 : 0;
    f_kb = 0;
    clean_query(sh->name, f_query, sizeof f_query);
    fetch_search();
}

static void fetch_reload_show(int si) {
    Show *sh = &shows[si];
    sh->synopsis[0] = sh->genres[0] = sh->year[0] = sh->rating[0] = sh->type[0] = sh->status[0] = 0;
    find_img(sh->cover_path, sizeof sh->cover_path, sh->dir, cover_names, 6);
    find_img(sh->banner_path, sizeof sh->banner_path, sh->dir, banner_names, 5);
    char ip[600];
    snprintf(ip, sizeof ip, "%s/info.txt", sh->dir);
    load_info(sh, ip);
    if (sh->cover) { SDL_DestroyTexture(sh->cover); sh->cover = NULL; }
    finish_show(sh);          /* resets cover_state and card_name */
    bcache_clear();           /* drop the old banner */
}

static void fetch_finish(void) {
    int si = f_show;
    fetch_close();
    fetch_reload_show(si);
    open_details(si);
    d_btn = 2;
    if (feat == si) set_feature(si);
    fetch_toast("Info saved");
}

static void fetch_pick(void) {
    if (f_state != 0 || f_sel >= f_nres) return;
    snprintf(fj_src, sizeof fj_src, "%s", fres[f_sel].src);
    snprintf(fj_id, sizeof fj_id, "%s", fres[f_sel].id);
    snprintf(fj_dir, sizeof fj_dir, "%s", shows[f_show].dir);
    f_state = 2;
    fetch_start(FJ_APPLY);
}

static void fetch_kb_open(void) {
    f_kb = 1; f_kb_seen = 0; f_kb_t = 0; f_kb_buf[0] = 0;
    SDL_StartTextInput();
}

static void fetch_text_event(const char *t) {
    if (!in_fetch || !f_kb) return;
    size_t n = strlen(f_kb_buf);
    snprintf(f_kb_buf + n, sizeof f_kb_buf - n, "%s", t);
}

static void update_fetch(float dt) {
    f_spin += dt;
    if (f_state == 1 && SDL_AtomicGet(&f_listed)) {
        fetch_fit();
        f_state = 0; f_good = 1;
        snprintf(f_msg, sizeof f_msg, "Press Square to type a different name");
    }
    if (f_job && SDL_AtomicGet(&f_done)) {
        if (f_thr) { SDL_WaitThread(f_thr, NULL); f_thr = NULL; }
        int job = f_job;
        f_job = 0;
        if (job == FJ_APPLY) {
            if (f_ok) { fetch_finish(); return; }
            f_state = 0;
            char m[200];
            snprintf(m, sizeof m, "Couldn't save: %s", net_err);
            fetch_toast(m);
        } else if (f_state == 1) {
            f_state = 0; f_good = 0;
            snprintf(f_msg, sizeof f_msg, "%s", net_err[0] ? net_err : "Unknown error");
        }
    }
    if (f_state != 1) {                          /* one poster texture per frame */
        for (int i = 0; i < f_nres; i++) {
            if (f_tried[i]) continue;
            int st = SDL_AtomicGet(&f_pready[i]);
            if (!st) continue;
            f_tried[i] = 1;
            if (st == 1) {
                char p[300];
                snprintf(p, sizeof p, TMP_DIR "/p%d.jpg", i);
                fres[i].tex = load_image_fit(p, POSTER_W, POSTER_H, 16);
            }
            break;
        }
    }
    if (f_kb) {
        int shown = SDL_IsScreenKeyboardShown(win);
        f_kb_t += dt;
        if (shown) f_kb_seen = 1;
        else if (f_kb_seen || f_kb_t > 1.5f) {
            f_kb = 0;
            SDL_StopTextInput();
            char *s = f_kb_buf;
            while (*s == ' ') s++;
            int n = (int)strlen(s);
            while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
            if (s[0]) { snprintf(f_query, sizeof f_query, "%s", s); fetch_search(); }
            else if (!f_kb_seen) fetch_toast("Keyboard not available");
        }
        return;
    }
    if (f_state == 2) return;
    if (pressed(SCE_CTRL_CIRCLE)) { fetch_close(); return; }
    if (pressed(SCE_CTRL_SQUARE)) { fetch_kb_open(); return; }
    if (pressed(SCE_CTRL_LTRIGGER)) { f_src = (f_src + 2) % 3; fetch_search(); return; }
    if (pressed(SCE_CTRL_RTRIGGER)) { f_src = (f_src + 1) % 3; fetch_search(); return; }
    int left = n_left(dt), right = n_right(dt), moved = 0;
    if (f_state == 0 && f_nres > 0) {
        if (left && f_sel > 0) { f_sel--; moved = 1; }
        if (right && f_sel < f_nres - 1) { f_sel++; moved = 1; }
        if (pressed(SCE_CTRL_CROSS)) { fetch_pick(); return; }
    }
    float maxs = f_nres * (FC_W + FC_GAP) - FC_GAP + 80 - SCREEN_W;
    if (maxs < 0) maxs = 0;
    if (drag_dx != 0) {
        f_scroll = clampf(f_scroll + drag_dx, 0, maxs);
        f_target = f_scroll;
    } else {
        if (moved) {
            float x = 40 + f_sel * (FC_W + FC_GAP);
            if (x - f_target < 40) f_target = x - 40;
            if (x + FC_W - f_target > SCREEN_W - 40) f_target = x + FC_W - (SCREEN_W - 40);
            f_target = clampf(f_target, 0, maxs);
        }
        f_scroll += (f_target - f_scroll) * lerpk(dt);
    }
}

static void fetch_spinner(int cx, int cy) {
    for (int i = 0; i < 8; i++) {
        float a = i * 0.785398f;
        int al = 50 + (int)(205 * fmodf(f_spin * 1.4f + i / 8.0f, 1.0f));
        disc(cx + (int)(cosf(a) * 18), cy + (int)(sinf(a) * 18), 4, C_ACCENT, al);
    }
}

static void fetch_button(int x, int y, int w, int h, int sel) {
    if (sel) rrect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, C_ACCENT, 70);
    if (sel) rrect(x, y, w, h, h / 2, 52, 50, 84, 255);
    else     rrect(x, y, w, h, h / 2, C_PANEL2, 255);
    if (sel) rrect_line(x, y, w, h, h / 2, 2, C_ACCENT, 255);
    textc(F_BODYB, x + w / 2, y + h / 2, AL_C, "Fetch Info", C_TEXT, 255);
}

static void draw_fetch(void) {
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG, 255);
    draw_backdrop(f_show, 0, 410, 60);
    text(F_H2, 40, 20, AL_L, "Fetch Info", C_TEXT, 255);
    text(F_BODY, 40, 62, AL_L, f_qline, C_DIM, 255);

    int sx = SCREEN_W - 40 - 3 * 92 + 6;
    for (int i = 0; i < 3; i++) {
        int x = sx + i * 92;
        if (f_src == i) rrect(x, 30, 86, 34, 17, C_ACCENT, 255);
        else            rrect(x, 30, 86, 34, 17, C_PANEL, 255);
        textc(F_SMALLB, x + 43, 47, AL_C, f_src_lbl[i], C_TEXT, f_src == i ? 255 : 170);
        hit(x, 30, 86, 34, H_FSRC, i, 1);
    }
    rrect(sx - 152, 30, 140, 34, 17, C_PANEL2, 255);
    textc(F_SMALLB, sx - 82, 47, AL_C, "Type a name", C_SOFT, 255);
    hit(sx - 152, 30, 140, 34, H_FKB, 0, 1);

    if (f_state == 1) {
        fetch_spinner(SCREEN_W / 2, 240);
        textc(F_BODY, SCREEN_W / 2, 290, AL_C, "Searching...", C_SOFT, 255);
    } else if (f_nres == 0) {
        textc(F_SUB, SCREEN_W / 2, 230, AL_C, f_good ? "No matches found" : "Couldn't search", C_TEXT, 255);
        textc(F_BODY, SCREEN_W / 2, 268, AL_C, f_msg, C_DIM, 255);
    } else {
        for (int i = 0; i < f_nres; i++) {
            FRes *r = &fres[i];
            int x = 40 + i * (FC_W + FC_GAP) - (int)f_scroll;
            if (x + FC_W < 0 || x > SCREEN_W) continue;
            int sel = (i == f_sel), y = FC_Y - (sel ? 6 : 0);
            rrect(x + 4, y + 8, FC_W, FC_H, 12, 0, 0, 0, 120);
            if (r->tex) {
                SDL_Rect d = { x, y, FC_W, FC_H };
                tex_col(r->tex, 255, 255, 255, 255);
                SDL_RenderCopy(ren, r->tex, NULL, &d);
            } else {
                char fc[8];
                first_char(r->title, fc);
                rrect(x, y, FC_W, FC_H, 12, C_PANEL, 255);
                textc(F_HERO, x + FC_W / 2, y + FC_H / 2, AL_C, fc, C_DIM, 255);
            }
            if (sel) rrect_line(x - 3, y - 3, FC_W + 6, FC_H + 6, 15, 3, C_ACCENT, 255);
            int tw = r->tag[0] == 'T' ? 54 : 60;
            rrect(x + 8, y + 8, tw, 22, 11, 0, 0, 0, 180);
            textc(F_SMALLB, x + 8 + tw / 2, y + 19, AL_C, r->tag, C_ACCLT, 255);
            if (sel) text(F_SMALLB, x, FC_Y + FC_H + 10, AL_L, r->card, C_TEXT, 255);
            else     text(F_SMALLB, x, FC_Y + FC_H + 10, AL_L, r->card, C_SOFT, 255);
            text(F_SMALL, x, FC_Y + FC_H + 32, AL_L, r->sub, C_DIM, 255);
            hit(x, y, FC_W, FC_H, H_FRES, i, 1);
        }
        text(F_SUB, 40, 414, AL_L, fres[f_sel].full, C_TEXT, 255);
        text(F_BODY, 40, 448, AL_L, fres[f_sel].line, C_ACCLT, 255);
    }
    text(F_SMALL, SCREEN_W - 40, 452, AL_R, "This product uses the TMDB API but is not endorsed or certified by TMDB.", C_DIM, 200);

    if (f_state == 2) {
        fill(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 170);
        fetch_spinner(SCREEN_W / 2, 250);
        textc(F_BODYB, SCREEN_W / 2, 300, AL_C, "Saving info...", C_TEXT, 255);
    }
}


/* ===== Details page ===== */
static void details_retarget(void) {
    float maxs = D_LIST_Y + rows_h - (SCREEN_H - 40);
    if (maxs < 0) maxs = 0;
    if (d_focus == 0 || !d_has_list) { d_target = 0; return; }
    int top = D_LIST_Y + row_y[d_sel], h = D_ROW_EP;
    if (d_sel > 0 && rows[d_sel - 1].ep < 0) { top -= D_ROW_HEAD; h += D_ROW_HEAD; }
    if (top - d_target < BAR_H + 16) d_target = top - BAR_H - 16;
    if (top + h - d_target > SCREEN_H - 40) d_target = top + h - (SCREEN_H - 40);
    d_target = clampf(d_target, 0, maxs);
}

static void update_details(float dt) {
    Show *sh = &shows[d_show];
    int up = n_up(dt), down = n_down(dt), left = n_left(dt), right = n_right(dt), moved = 0;
    if (pressed(SCE_CTRL_CIRCLE)) { close_details(); return; }
    if (pressed(SCE_CTRL_SQUARE)) { fetch_open(d_show); return; }
    if (d_focus == 0) {
        if (left && d_btn > 0) d_btn--;
        if (right && d_btn < 2) d_btn++;
        if (down && d_has_list) { d_focus = 1; moved = 1; }
        if (pressed(SCE_CTRL_CROSS)) {
            if (d_btn == 0) play_episode(d_show, 0);
            else if (d_btn == 1) { close_details(); return; }
            else { fetch_open(d_show); return; }
        }
    } else {
        if (up) { int n = next_ep_row(d_sel, -1); if (n == d_sel) d_focus = 0; else d_sel = n; moved = 1; }
        if (down) { d_sel = next_ep_row(d_sel, 1); moved = 1; }
        if (sh->nseasons > 1 && left) {
            int cs = rows[d_sel].season, i = d_sel;
            while (i > 0 && rows[i].season == cs) i--;
            int ps = rows[i].season;
            while (i > 0 && rows[i - 1].season == ps) i--;
            d_sel = rows[i].ep >= 0 ? i : next_ep_row(i, 1);
            moved = 1;
        }
        if (sh->nseasons > 1 && right) {
            int cs = rows[d_sel].season, i = d_sel;
            while (i < nrows - 1 && rows[i].season == cs) i++;
            if (rows[i].season != cs) d_sel = rows[i].ep >= 0 ? i : next_ep_row(i, 1);
            moved = 1;
        }
        if (pressed(SCE_CTRL_CROSS)) play_episode(d_show, rows[d_sel].ep);
    }
    float maxs = d_has_list ? D_LIST_Y + rows_h - (SCREEN_H - 40) : 0;
    if (maxs < 0) maxs = 0;
    if (drag_dy != 0) {
        d_scroll = clampf(d_scroll + drag_dy, 0, maxs);
        d_target = d_scroll;
    } else {
        if (moved) details_retarget();
        d_scroll += (d_target - d_scroll) * lerpk(dt);
    }
}

static void draw_details(void) {
    Show *sh = &shows[d_show];
    int oy = -(int)d_scroll;
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG, 255);
    draw_backdrop(d_show, oy, 410, 255);

    int px = 40, py = 84 + oy, pw = 160, ph = 240;
    rrect(px + 4, py + 8, pw, ph, 12, 0, 0, 0, 120);
    if (sh->cover) {
        SDL_Rect d = { px, py, pw, ph };
        tex_col(sh->cover, 255, 255, 255, 255);
        SDL_RenderCopy(ren, sh->cover, NULL, &d);
    } else {
        char fc[8]; first_char(sh->name, fc);
        rrect(px, py, pw, ph, 12, C_PANEL, 255);
        textc(F_HERO, px + pw / 2, py + ph / 2, AL_C, fc, C_DIM, 255);
    }

    int ix = 228;
    text(F_HERO, ix, 80 + oy, AL_L, d_title, C_TEXT, 255);
    text(F_BODY, ix, 136 + oy, AL_L, d_meta, C_SOFT, 255);
    int gx = ix;
    if (d_genres[0]) gx += textc(F_BODY, ix, 182 + oy, AL_L, d_genres, C_DIM, 255) + 16;
    if (sh->rating[0]) rating_pill(gx, 182 + oy, sh->rating, 255);
    wtex_draw(&d_syn, ix, 210 + oy, C_DIM, 255);
    int by = 296 + oy;
    button(ix, by, 196, 48, 1, IC_PLAY, "Watch Now", d_focus == 0 && d_btn == 0, 255);
    button(ix + 214, by, 130, 48, 0, IC_LEFT, "Back", d_focus == 0 && d_btn == 1, 255);
    hit(ix, by, 196, 48, H_DPLAY, 0, 1);
    hit(ix + 214, by, 130, 48, H_DBACK, 0, 1);
    {
    }
    if (!d_has_list) return;
    sec_head(40, 380 + oy, sh->kind == K_MOVIE ? "PARTS" : "EPISODES", sh->kind == K_MOVIE ? "Parts" : "Episodes");
    char c[48];
    if (sh->nseasons > 1) snprintf(c, sizeof c, "%d SEASONS  \xE2\x80\xA2  %d EPISODES", sh->nseasons, sh->neps);
    else snprintf(c, sizeof c, "%d EPISODES", sh->neps);
    pill_btn(SCREEN_W - 40, 414 + oy, c, 0);

    for (int i = 0; i < nrows; i++) {
        int y = D_LIST_Y + row_y[i] + oy;
        int h = rows[i].ep >= 0 ? D_ROW_EP : D_ROW_HEAD;
        if (y + h < BAR_H || y > SCREEN_H) continue;
        if (rows[i].ep < 0) {
            char hd[32];
            if (rows[i].season == SEASON_SPECIAL) snprintf(hd, sizeof hd, "SPECIALS");
            else snprintf(hd, sizeof hd, "SEASON %d", rows[i].season);
            rrect(40, y + 17, 16, 4, 2, C_ACCENT, 255);
            textc(F_SMALLB, 66, y + 19, AL_L, hd, C_ACCLT, 255);
            continue;
        }
        Episode *e = &sh->eps[rows[i].ep];
        int sel = (d_focus == 1 && i == d_sel);
        rrect(40, y, SCREEN_W - 80, D_ROW_EP - 8, 14, sel ? 42 : 28, sel ? 42 : 28, sel ? 56 : 36, 255);
        if (sel) rrect_line(40, y, SCREEN_W - 80, D_ROW_EP - 8, 14, 2, C_ACCENT, 255);
        rrect(52, y + 9, 56, 40, 10, 16, 16, 22, 255);
        char num[16];
        if (e->number > 0) snprintf(num, sizeof num, "%d", e->number); else snprintf(num, sizeof num, "-");
        if (sel) textc(F_SUB, 80, y + 29, AL_C, num, C_ACCLT, 255);
        else     textc(F_SUB, 80, y + 29, AL_C, num, C_SOFT, 255);
        text(F_BODYB, 124, y + 7, AL_L, e->label, C_TEXT, 255);
        text(F_SMALL, 124, y + 32, AL_L, e->sub, C_DIM, 255);
        {   /* watch progress (Step 3) */
            Prog *p = prog_find(e->path);
            if (p && p->pos > 0 && p->dur > 0) {
                int fw = (int)(52 * clampf((float)p->pos / (float)p->dur, 0, 1));
                rrect(54, y + 44, 52, 3, 1, 255, 255, 255, 40);
                if (fw >= 2) rrect(54, y + 44, fw, 3, 1, C_ACCENT, 255);
            }
            if (p && p->done && !sel) {
                disc(SCREEN_W - 74, y + 29, 14, C_TINT, 255);
                gc(C_ACCLT, 255); check_mark(SCREEN_W - 74, y + 29, 0.8f);
            }
        }
        if (sel) {
            disc(SCREEN_W - 74, y + 29, 17, C_ACCENT, 255);
            gc(C_TEXT, 255); icon(IC_PLAY, SCREEN_W - 72, y + 29, 0.65f, 108, 99, 255);
        }
        hit(40, y, SCREEN_W - 80, D_ROW_EP - 8, H_EP, i, 1);
    }
}

/* ===== Top bar, hints, drawer, toast ===== */
static const char *tab_names[3] = { "Home", "Anime", "Movies" };

static void draw_topbar(void) {
    fill(0, 0, SCREEN_W, BAR_H, C_BAR, 240);
    fill(0, BAR_H - 1, SCREEN_W, 1, C_LINE, 255);
    disc(38, 30, 22, C_PANEL, 255);
    gc(C_TEXT, 255); icon(IC_MENU, 38, 30, 0.9f, C_PANEL);
    hit(14, 6, 48, 48, H_MENU, 0, 0);

    rrect(70, 8, 178, 44, 22, C_PANEL, 255);
    dango_icon(100, 30, 0.75f);
    int w = textc(F_LOGO, 124, 29, AL_L, "ani", C_TEXT, 255);
    textc(F_LOGO, 124 + w, 29, AL_L, "watch", C_ACCLT, 255);

    const int nx = (SCREEN_W - 308) / 2;
    rrect(nx, 8, 308, 44, 22, C_PANEL, 255);
    rrect(nx + 4 + (int)tab_x, 12, 100, 36, 18, C_PANEL2, 255);
    for (int i = 0; i < 3; i++) {
        int cx = nx + 4 + i * 100 + 50;
        if (i == page) textc(F_BODYB, cx, 30, AL_C, tab_names[i], C_TEXT, 255);
        else           textc(F_BODYB, cx, 30, AL_C, tab_names[i], C_DIM, 255);
        hit(nx + 4 + i * 100, 8, 100, 44, H_TAB, i, 0);
    }

    disc(SCREEN_W - 94, 30, 22, C_PANEL, 255);
    gc(C_TEXT, 255); icon(IC_REFRESH, SCREEN_W - 94, 30, 0.85f, C_PANEL);
    hit(SCREEN_W - 118, 6, 48, 48, H_REFRESH, 0, 0);
    disc(SCREEN_W - 40, 30, 22, C_PANEL, 255);
    gc(C_TEXT, 255); icon(IC_GEAR, SCREEN_W - 40, 30, 0.85f, C_PANEL);
    hit(SCREEN_W - 64, 6, 48, 48, H_SETTINGS, 0, 0);
}

static void draw_hints(void) {
    grad_v(0, SCREEN_H - 46, SCREEN_W, 46, C_BG, 0, 235);
    int x = 20;
    if (rs_open) {
        x = hint(x, BI_X, rs_sel ? "Start over" : "Resume");
        x = hint(x, BI_SQ, "Start over");
        x = hint(x, BI_O, "Cancel");
        x = hint(x, BI_DLR, "Choose");
    } else if (in_fetch) {
        if (f_kb || f_state == 2) return;
        if (f_state == 0 && f_nres > 0) x = hint(x, BI_X, "Pick");
        x = hint(x, BI_O, "Back");
        x = hint_key(x, "SQUARE", "Type a name");
        x = hint_key(x, "L / R", "Source");
    } else if (in_settings) {
        x = hint(x, BI_X, st_sel == 4 ? (st_confirm ? "Confirm" : "Clear") : "Change");
        if (st_sel < 4) x = hint(x, BI_DLR, "Change");
        x = hint(x, BI_O, "Back");
    } else if (drawer_open) {
        x = hint(x, BI_X, "Select");
        x = hint(x, BI_O, "Close");
    } else if (in_details) {
        x = hint(x, BI_X, "Play");
        x = hint(x, BI_O, "Back");
        x = hint_key(x, "SQUARE", "Fetch info");
        if (shows[d_show].nseasons > 1 && d_focus == 1) x = hint(x, BI_DLR, "Season");
    } else {
        int cwf = (page == PG_HOME && h_focus == HR_CW + 1);
        x = hint(x, BI_X, cwf ? "Resume" : "Select");
        x = hint_key(x, "L / R", "Tabs");
        x = hint(x, BI_TRI, cwf ? "Remove" : "Rescan");
        x = hint_key(x, "START", "Menu");
    }
}

static const char *drawer_names[5] = { "Home", "Anime", "Movies", "Rescan library", "Settings" };
static const int   drawer_icons[5] = { IC_HOME, IC_TV, IC_FILM, IC_REFRESH, IC_GEAR };

static void draw_drawer(void) {
    if (drawer_t <= 0.01f) return;
    float e = 1 - (1 - drawer_t) * (1 - drawer_t) * (1 - drawer_t);
    fill(0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, (int)(160 * drawer_t));
    int x = (int)(-DRAWER_W * (1 - e));
    if (drawer_open) {
        hit(0, 0, SCREEN_W, SCREEN_H, H_DRAWER_OUT, 0, 0);
        hit(x, 0, DRAWER_W, SCREEN_H, H_NONE, 0, 0);
    }
    fill(x, 0, DRAWER_W, SCREEN_H, 15, 15, 19, 255);
    fill(x + DRAWER_W - 1, 0, 1, SCREEN_H, C_LINE, 255);
    dango_icon(x + 46, 46, 1.1f);
    int w = textc(F_H2, x + 80, 46, AL_L, "ani", C_TEXT, 255);
    textc(F_H2, x + 80 + w, 46, AL_L, "watch", C_ACCLT, 255);
    fill(x, 94, DRAWER_W, 1, C_LINE, 255);
    for (int i = 0; i < 5; i++) {
        int y = 116 + i * 56, sel = (i == drawer_sel);
        if (sel) rrect(x + 16, y, DRAWER_W - 32, 46, 16, C_TINT, 255);
        if (sel) gc(C_ACCLT, 255); else gc(C_SOFT, 255);
        if (sel) icon(drawer_icons[i], x + 48, y + 23, 0.9f, C_TINT);
        else     icon(drawer_icons[i], x + 48, y + 23, 0.9f, 15, 15, 19);
        if (sel) textc(F_SUB, x + 80, y + 23, AL_L, drawer_names[i], C_ACCLT, 255);
        else     textc(F_SUB, x + 80, y + 23, AL_L, drawer_names[i], C_SOFT, 255);
        if (drawer_open) hit(x + 16, y, DRAWER_W - 32, 46, H_DRAWER_ITEM, i, 0);
    }
    disc(x + 30, SCREEN_H - 30, 5, 60, 200, 120, 255);
    textc(F_BODY, x + 44, SCREEN_H - 30, AL_L, APP_VERSION, C_SOFT, 255);
}

static void update_drawer(float dt) {
    if (n_up(dt) && drawer_sel > 0) drawer_sel--;
    if (n_down(dt) && drawer_sel < 4) drawer_sel++;
    n_left(dt); n_right(dt);
    if (pressed(SCE_CTRL_CROSS)) drawer_pick(drawer_sel);
    else if (pressed(SCE_CTRL_CIRCLE) || pressed(SCE_CTRL_START)) drawer_open = 0;
}

static void draw_toast(void) {
    if (toast_t <= 0) return;
    int a = toast_t < 0.3f ? (int)(toast_t / 0.3f * 255) : 255;
    int w = text_w(F_BODYB, toast_msg) + 56, x = (SCREEN_W - w) / 2, y = SCREEN_H - 96;
    rrect(x, y, w, 44, 22, C_PANEL2, a * 245 / 255);
    rrect_line(x, y, w, 44, 22, 1, C_LINE, a);
    disc(x + 24, y + 22, 5, C_ACCENT, a);
    textc(F_BODYB, x + 38, y + 22, AL_L, toast_msg, C_TEXT, a);
}

/* ===== Settings screen (Step 3 + 4) ===== */
#define ST_N     5
#define ST_CLEAR 4
#define ST_Y     92
#define ST_ROW   68
static const char *const st_names[ST_N] = { "Auto next", "Countdown", "Resume", "Skip intro/outro", "Clear watch history" };
static const char *const st_desc[ST_N] = {
    "Play the next episode when one ends",
    "How long the Up next card waits",
    "What happens when you open a started episode",
    "Auto skips marked parts, Button shows a skip button (Square marks)",
    "Removes progress, checkmarks and Continue Watching (keeps skip marks)" };

static void settings_open(void) { in_settings = 1; st_sel = 0; st_confirm = 0; drawer_open = 0; }
static void settings_close(void) { cfg_save(); in_settings = 0; st_confirm = 0; }

static void st_change(int i, int d) {
    if (i == 0) cfg_auto = !cfg_auto;
    else if (i == 1) {
        cfg_count += 5 * d;
        if (cfg_count > 15) cfg_count = 5;
        if (cfg_count < 5) cfg_count = 15;
    } else if (i == 2) cfg_resume = !cfg_resume;
    else if (i == 3) cfg_skip = (cfg_skip + (d > 0 ? 1 : 2)) % 3;
}

static void st_activate(int i) {
    if (i == ST_CLEAR) {
        if (!st_confirm) { st_confirm = 1; return; }
        st_confirm = 0;
        prog_clear();
        toast("Watch history cleared");
        return;
    }
    st_change(i, 1);
}

static void update_settings(float dt) {
    if (pressed(SCE_CTRL_CIRCLE)) { settings_close(); return; }
    int up = n_up(dt), down = n_down(dt), left = n_left(dt), right = n_right(dt);
    if (up && st_sel > 0) { st_sel--; st_confirm = 0; }
    if (down && st_sel < ST_N - 1) { st_sel++; st_confirm = 0; }
    if (st_sel < ST_CLEAR) {
        if (left) st_change(st_sel, -1);
        if (right) st_change(st_sel, 1);
    }
    if (pressed(SCE_CTRL_CROSS)) st_activate(st_sel);
}

static const char *st_value(int i, char *b, int n) {
    if (i == 0) return cfg_auto ? "On" : "Off";
    if (i == 1) { snprintf(b, n, "%d s", cfg_count); return b; }
    if (i == 2) return cfg_resume ? "Always resume" : "Ask";
    if (i == 3) return cfg_skip == 0 ? "Auto" : (cfg_skip == 1 ? "Button" : "Off");
    return st_confirm ? "Press again to clear" : "Clear";
}

static void draw_settings(void) {
    char vb[32];
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG, 255);
    disc(40, 40, 22, C_PANEL, 255);
    gc(C_TEXT, 255); icon(IC_LEFT, 39, 40, 0.85f, 0, 0, 0);
    hit(14, 14, 52, 52, H_ST_BACK, 0, 0);
    text(F_H2, 78, 22, AL_L, "Settings", C_TEXT, 255);
    for (int i = 0; i < ST_N; i++) {
        int y = ST_Y + i * ST_ROW, sel = (i == st_sel), rh = ST_ROW - 8;
        rrect(40, y, SCREEN_W - 80, rh, 14, sel ? 42 : 28, sel ? 42 : 28, sel ? 56 : 36, 255);
        if (sel) rrect_line(40, y, SCREEN_W - 80, rh, 14, 2, C_ACCENT, 255);
        text(F_BODYB, 64, y + 7, AL_L, st_names[i], C_TEXT, 255);
        text(F_SMALL, 64, y + 33, AL_L, st_desc[i], C_DIM, 255);
        const char *v = st_value(i, vb, sizeof vb);
        int pw = 220, px = SCREEN_W - 64 - pw, cy = y + rh / 2;
        if (i == ST_CLEAR && st_confirm) {
            rrect(px, cy - 17, pw, 34, 17, 190, 60, 72, 255);
            textc(F_SMALLB, px + pw / 2, cy, AL_C, v, C_TEXT, 255);
        } else if (sel) {
            rrect(px, cy - 17, pw, 34, 17, C_TINT, 255);
            textc(F_SMALLB, px + pw / 2, cy, AL_C, v, C_ACCLT, 255);
        } else {
            rrect(px, cy - 17, pw, 34, 17, C_PANEL2, 255);
            textc(F_SMALLB, px + pw / 2, cy, AL_C, v, C_SOFT, 255);
        }
        if (i < ST_CLEAR && sel) {
            gc(C_ACCLT, 255);
            icon(IC_LEFT, px + 18, cy, 0.6f, 0, 0, 0);
            icon(IC_RIGHT, px + pw - 18, cy, 0.6f, 0, 0, 0);
        }
        hit(40, y, SCREEN_W - 80, rh, H_ST_ROW, i, 0);
    }
    text(F_SMALL, 40, ST_Y + ST_N * ST_ROW + 8, AL_L, "Settings are saved when you leave this screen.", C_DIM, 255);
}

/* ===== Taps ===== */
static void handle_tap(void) {
    Hit *h = tap_hit();
    if (!h) return;
    int r, i;
    switch (h->id) {
    case H_MENU:     open_drawer(); break;
    case H_TAB:      go_page(h->arg); break;
    case H_REFRESH:  do_rescan(); break;
    case H_SETTINGS: settings_open(); break;
    case H_HPLAY:    h_focus = 0; h_btn = 0; play_episode(feat, 0); break;
    case H_HDETAILS: h_focus = 0; h_btn = 1; open_details(feat); break;
    case H_HPREV:    step_feature(-1); break;
    case H_HNEXT:    step_feature(1); break;
    case H_ROWCARD:
        r = h->arg >> 16; i = h->arg & 0xFFFF;
        if (r < 0 || r > HR_MOVIE) break;
        h_focus = r + 1; row_sel[r] = i;
        if (r == HR_CW) cw_play(i);
        else open_details((r == HR_MOVIE ? movie_ix : anime_ix)[i]);
        break;
    case H_SEEALL:   go_page(h->arg); break;
    case H_GRIDCARD:
        g_sel[page - 1] = h->arg;
        open_details((page == PG_MOVIES ? movie_ix : anime_ix)[h->arg]);
        break;
    case H_DPLAY:    if (in_details) play_episode(d_show, 0); break;
    case H_DBACK:    if (in_details) close_details(); break;
    case H_EP:
        if (in_details && h->arg < nrows && rows[h->arg].ep >= 0) {
            d_focus = 1; d_sel = h->arg;
            play_episode(d_show, rows[h->arg].ep);
        }
        break;
    case H_DRAWER_ITEM: drawer_sel = h->arg; drawer_pick(h->arg); break;
    case H_DRAWER_OUT:  drawer_open = 0; break;
    case H_DFETCH: if (in_details && !in_fetch) { d_focus = 0; d_btn = 2; fetch_open(d_show); } break;
    case H_FRES:   if (in_fetch && f_state == 0) { if (f_sel == h->arg) fetch_pick(); else f_sel = h->arg; } break;
    case H_FSRC:   if (in_fetch && f_state != 2) { f_src = h->arg; fetch_search(); } break;
    case H_FKB:    if (in_fetch && f_state != 2) fetch_kb_open(); break;
    case H_ST_BACK: if (in_settings) settings_close(); break;
    case H_ST_ROW:
        if (in_settings && h->arg >= 0 && h->arg < ST_N) {
            if (st_sel != h->arg) st_confirm = 0;
            st_sel = h->arg;
            st_activate(h->arg);
        }
        break;
    }
}

/* ===== Main ===== */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sceIoMkdir(DATA_ROOT, 0777);
    sceIoMkdir(ANIME_ROOT, 0777);
    sceIoMkdir(MOVIE_ROOT, 0777);
    pl_mod_res = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { sceKernelExitProcess(0); return 0; }
    win = SDL_CreateWindow("aniwatch", 0, 0, SCREEN_W, SCREEN_H, 0);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    TTF_Init();
    const char *bold = file_exists(FONT_BOLD) ? FONT_BOLD : FONT_REG;
    for (int i = 0; i < F_COUNT; i++) {
        fonts[i] = TTF_OpenFont(font_bold[i] ? bold : FONT_REG, font_px[i]);
        if (!fonts[i]) { sceKernelExitProcess(0); return 0; }   /* font missing from the vpk */
    }
    circ_tex = make_ring_tex(CIRC, CIRC / 2.0f, 0);
    dots_tex = make_dots();

    fill(0, 0, SCREEN_W, SCREEN_H, C_BG, 255);
    dango_icon(SCREEN_W / 2, 230, 2.0f);
    textc(F_SUB, SCREEN_W / 2, 300, AL_C, "Scanning library...", C_SOFT, 255);
    SDL_RenderPresent(ren);
    scan_library();
    prog_load();
    cfg_load();
    skip_load();
    set_feature(0);

    Uint32 last = SDL_GetTicks();
    for (;;) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        frame_no++;

        drag_dx = drag_dy = 0; tap_pending = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_TEXTINPUT) { fetch_text_event(ev.text.text); continue; }
            if (in_player) pl_touch_event(&ev);
            else handle_touch_event(&ev);
        }
        read_input();

        if (in_player) update_player(dt);
        else {
            if (rs_open) update_resume(dt);
            else if (in_fetch) update_fetch(dt);
            else if (in_settings) update_settings(dt);
            else if (drawer_open) update_drawer(dt);
            else if (pressed(SCE_CTRL_START)) open_drawer();
            else if (in_details) update_details(dt);
            else if (pressed(SCE_CTRL_LTRIGGER)) go_page((page + 2) % 3);
            else if (pressed(SCE_CTRL_RTRIGGER)) go_page((page + 1) % 3);
            else if (pressed(SCE_CTRL_TRIANGLE) && !(page == PG_HOME && h_focus == HR_CW + 1)) do_rescan();
            else if (page == PG_HOME) update_home(dt);
            else update_grid(page - 1, dt);
            handle_tap();
        }

        if (toast_t > 0) toast_t -= dt;
        drawer_t = clampf(drawer_t + (drawer_open ? dt : -dt) * 5.0f, 0, 1);
        hero_a = clampf(hero_a + dt * 3.0f, 0, 1);
        tab_x += (page * 100 - tab_x) * lerpk(dt);
        if (!in_player && !in_details && !in_settings && !rs_open && page == PG_HOME && !drawer_open && nshows > 1) {
            feat_t += dt;
            if (feat_t > 9.0f) step_feature(1);
        }

        cover_budget = 2;
        nhits = 0;
        if (in_player) {
            draw_player();
            draw_toast();
        } else {
            if (in_fetch) draw_fetch();
            else if (in_settings) draw_settings();
            else if (in_details) draw_details();
            else if (page == PG_HOME) draw_home();
            else draw_grid(page - 1);
            if (!in_fetch && !in_settings) draw_topbar();
            draw_hints();
            draw_drawer();
            draw_resume();
            draw_toast();
        }
        SDL_RenderPresent(ren);
        memcpy(hits_prev, hits, nhits * sizeof(Hit));
        nhits_prev = nhits;
    }
    return 0;
}

/* ===== END OF FILE ===== */
