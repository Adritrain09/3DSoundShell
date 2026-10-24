#ifndef NAVIDROME_H
#define NAVIDROME_H
/* ============================================================
   navidrome.h - mode Navidrome (ND1..ND6 / M2)
   ============================================================ */
#include <stdbool.h>
#include "main.h"

/* cycle de vie */
void nd_enter(void);                          /* demarre la connexion (thread) */
void navidrome_draw_top(void);
void navidrome_draw_bottom(void);
void navidrome_input(AppState *state);

/* etat (pour UI et banc) */
int         nd_get_step(void);
const char *nd_get_err(void);
const char *nd_get_srvinfo(void);
/* ND6 : getters navigation (pour banc hote) */
int         nd_get_nav_depth(void);
int         nd_get_nav_sel(void);
int         nd_get_count(void);
const char *nd_get_msg(void);
int         nd_get_nav_kind(void);
int         nd_get_pane(void);
int         nd_get_lcat(void);
int         nd_get_wifi_live(void);
int         nd_get_wifi_lost(void);
int         nd_get_lsel(void);
const char *nd_get_item_title(int i);
int         nd_get_persist(void);
int         nd_get_playing(void);
void        nd_clear_playing(void);   /* une piste locale prend la main */
void        nd_temp_clear(void);      /* vide Temp (fermeture de l app) */
int         nd_get_confirm(void);
int         nd_get_diag_done(void);
int         nd_get_fail_at(void);
const char *nd_get_diag1(void);
const char *nd_get_diag2(void);
int         nd_cover_state(void);
const char *nd_cover_id(void);
void        nd_set_cover_enabled(int e);
void        nd_force_reconnect(void);

/* element de liste (albums / artistes / morceaux) */
typedef struct {
    char id[40];       /* les ids Navidrome sont des UUID (36 car.) */
    char title[100];
    char sub[68];
    char sfx[8];
    char cover[40];    /* id "coverArt" Subsonic si distinct de id (morceaux) */
    bool starred;       /* marque favori (champ "starred" present cote serveur) */
    int  num;          /* duration (s) ou albumCount, -1 si absent */
} NdItem;

/* helpers purs (testables sur banc hote) */
void nd_md5_hex(const char *in, char out[33]);
void nd_make_salt(char out[17]);
int  nd_build_ping_url(const char *base, const char *user, const char *pass,
                       char *out, int outsz);
int  nd_api_url(const char *base, const char *view,
                const char *user, const char *pass, char *out, int outsz);
bool nd_http_variant(const char *url, char *out, int outsz);
bool nd_json_str(const char *js, const char *key, char *out, int outsz);
bool nd_json_int(const char *js, const char *key, int *outv);
int  nd_collect(const char *js, const char *key,
                const char *f_title, const char *f_sub,
                NdItem *items, int max);
void nd_sanitize(const char *title, const char *sfx, char *out, int outsz);
bool nd_dl_file(const char *url, const char *path, char *err, int errsz);

#endif
