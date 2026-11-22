// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Speaker mode (AVRCP 1.6 CT): the phone's track as its AVRCP target reports it. Pure: the attribute texts of
 * ESP_AVRC_CT_METADATA_RSP_EVT (not NUL-terminated, any bytes) go in through pc_btmeta_put; what the UI shows comes out. */
enum {                                   /* the attribute ids of a metadata response (the bits of esp_avrc_md_attr_mask_t) */
    PC_BTMETA_TITLE = 0x01, PC_BTMETA_ARTIST = 0x02, PC_BTMETA_ALBUM = 0x04, PC_BTMETA_TRACK = 0x08,
    PC_BTMETA_TOTAL = 0x10, PC_BTMETA_GENRE = 0x20, PC_BTMETA_TIME = 0x40, PC_BTMETA_ART = 0x80,
};
#define PC_BTMETA_TEXT 160               /* bytes per text with the NUL: about 50 CJK characters */
#define PC_BTMETA_HANDLE 7               /* a BIP image handle: exactly seven ASCII digits */
typedef struct {
    char title[PC_BTMETA_TEXT], artist[PC_BTMETA_TEXT], album[PC_BTMETA_TEXT];   /* valid UTF-8, "" = not sent */
    uint16_t track, total;               /* the track's place on the album and the album's track count; 0 = not sent */
    uint32_t play_ms;                    /* the track's length (AVRCP "playing time", ms); 0 = not sent */
    char art[PC_BTMETA_HANDLE + 1];      /* the cover-art handle, "" = none (the peer has no cover art for this track) */
} pc_btmeta_t;

/* src[0..len) (stops at a NUL) into dst[cap] as valid UTF-8: broken sequences are dropped, control characters become spaces, spaces at
 * both ends go, a cut never splits a character; dst always NUL-terminated (cap >= 1). Returns the length written. */
size_t pc_btmeta_copy(char *dst, size_t cap, const uint8_t *src, int len);
/* one attribute of a response; true when the stored value changed (unknown ids and the genre are ignored: false) */
bool pc_btmeta_put(pc_btmeta_t *m, uint8_t attr_id, const uint8_t *text, int len);
bool pc_btmeta_known(const pc_btmeta_t *m);   /* a title was sent: the page shows the track instead of the phone's name */
/* the now page's title band, as a local track's ("title  -  artist"; the title alone without an artist), cut whole into out[cap] */
void pc_btmeta_line(const pc_btmeta_t *m, char *out, size_t cap);

/* ---- the speaker mode's AVRCP controller as a pure state machine (fix round 1: I1, I2) ----
 * The IDF glue (bt_meta_avrc.c) and the simulator's fake phone (fake_bt_svc.c) only turn callbacks into events and carry out the actions;
 * every decision is here. One task (the LVGL task) feeds it, in the order the events happened.
 * Metadata: an answer is the attributes of one burst (no attribute for PC_BMA_SETTLE_MS ends it, and so does an attribute id it already had: the next
 * answer has begun); with more than one ask out, the older
 * answers are dropped (an answer to an ask sent before a TRACK_CHANGED describes the old track). TRACK_CHANGED shows PC_BTMETA_WAIT as the
 * title at once (the old track's fields go) until the new answer.
 * Cover art: one GET at a time. A newer handle while a GET runs marks it stale: its data are dropped when they come, then the new handle is
 * asked. The timeout (PC_BMA_ART_WAIT_MS) ends the current GET only: its data are dropped from then on, and the cover-art link is cycled
 * (no new GET on a link that may still carry the old transfer). The cover-art link is (re)connected at most once per track.
 * An early end on our side (no_mem, lost pieces) is reported at once, but the GET stays in flight (IDF has no abort): its data are dropped until
 * the phone's own final / failure, the link going, or the timeout (which cycles the link). A connect that never answers ends after the same
 * wait. When the answer to the newest ask never comes and the title still shows the wait mark, the attributes are asked once more per track. */
#define PC_BTMETA_WAIT "\xE2\x80\x94"         /* the title while the new track's answer is out */
#define PC_BMA_SETTLE_MS 150
#define PC_BMA_ASK_FORGET_MS 3000               /* an ask without any answer by then is forgotten */
#define PC_BMA_ART_WAIT_MS 15000
#define PC_BMA_ART_MAX (256u * 1024u)           /* a linked thumbnail is ~10-30 KB */
#define PC_BMA_META_MASK (PC_BTMETA_TITLE | PC_BTMETA_ARTIST | PC_BTMETA_ALBUM | PC_BTMETA_TRACK | PC_BTMETA_TOTAL | PC_BTMETA_TIME)
typedef enum {
    PC_BMA_LINK,                         /* on: the AVRCP link is up (a new phone: everything of the old one goes); off: down, or the mode ends */
    PC_BMA_FEAT,                         /* on: the phone's target has cover art */
    PC_BMA_CAPS,                         /* on: TRACK_CHANGED can be registered */
    PC_BMA_TRACK,                        /* TRACK_CHANGED */
    PC_BMA_ATTR,                         /* id, data[len]: one attribute of an answer */
    PC_BMA_CA,                           /* on: the cover-art link is up; off: down (status = the reason) */
    PC_BMA_DATA,                         /* status (0 ok), data[len], final: a piece of the thumbnail */
    PC_BMA_LOST,                         /* events were lost (the glue's queue was full) */
    PC_BMA_TICK,                         /* time passes (the driver: every 50 ms) */
} pc_bma_kind_t;
typedef struct { uint8_t kind; bool on; uint8_t id; bool final; int status; const uint8_t *data; int len; uint32_t now_ms; } pc_bma_ev_t;
enum {                                   /* pc_bma_out_t.act */
    PC_BMA_DO_CAPS = 1u << 0,            /* GetCapabilities */
    PC_BMA_DO_REG = 1u << 1,             /* register TRACK_CHANGED */
    PC_BMA_DO_ASK = 1u << 2,             /* GetElementAttributes(ask_mask) */
    PC_BMA_DO_CA_CONNECT = 1u << 3,
    PC_BMA_DO_CA_DROP = 1u << 4,         /* disconnect the cover-art link */
    PC_BMA_DO_GET = 1u << 5,             /* GetLinkedThumbnail(get) */
    PC_BMA_DO_META = 1u << 6,            /* publish *meta (the page's track) */
    PC_BMA_DO_ART = 1u << 7,             /* publish art[art_len] for art_hdl (the receiver owns it) */
    PC_BMA_DO_ART_CLEAR = 1u << 8,       /* the published cover is no longer valid */
    PC_BMA_DO_FAIL = 1u << 9,            /* a GET ended without a picture: fail names why */
};
typedef struct {
    uint32_t act;
    uint8_t ask_mask;
    char get[PC_BTMETA_HANDLE + 1];
    const pc_btmeta_t *meta;
    uint8_t *art; uint32_t art_len, art_ms; char art_hdl[PC_BTMETA_HANDLE + 1];
    const char *fail;                    /* status | too_big | no_mem | empty | timeout | disconnect | lost */
} pc_bma_out_t;
typedef struct {
    void *(*grow)(void *p, size_t n);    /* realloc (PSRAM on the board) */
    void (*drop)(void *p);
    bool link, tg_art, track_ok;
    pc_btmeta_t stage, shown;
    bool burst; uint8_t burst_ids; uint32_t settle_at;   /* an answer in progress: the attribute ids it had */
    uint8_t asks_out; uint32_t ask_at;
    uint32_t track_n, ca_track;          /* TRACK_CHANGED count; the track of the last connect attempt */
    uint8_t ca;                          /* PC_BMA_CA_* */
    uint32_t ca_at;                      /* when the connect was asked (a connect that never answers ends after PC_BMA_ART_WAIT_MS) */
    uint32_t reask_track;                /* the track (n + 1) whose lost answer was asked again (once per track) */
    bool getting, stale, abandoned;      /* abandoned: we ended it early (no_mem, lost) but the phone still streams it: kept in flight, data dropped */
    uint32_t gen, get_at;
    char asked[PC_BTMETA_HANDLE + 1], have[PC_BTMETA_HANDLE + 1];
    uint8_t *rx; uint32_t rx_len, rx_cap; bool rx_big;
    const char *art_state;               /* none | no_ca | connecting | asking | ok | fail */
    const char *last_fail;
} pc_bma_t;
enum { PC_BMA_CA_DOWN, PC_BMA_CA_CONNECTING, PC_BMA_CA_UP, PC_BMA_CA_DROPPING };
void pc_bma_init(pc_bma_t *s, void *(*grow)(void *, size_t), void (*drop)(void *));
void pc_bma_event(pc_bma_t *s, const pc_bma_ev_t *ev, pc_bma_out_t *out);   /* out is cleared first */
void pc_bma_free(pc_bma_t *s);           /* the receive buffer (a reset without events) */
bool pc_bma_busy(const pc_bma_t *s);     /* a metadata answer is awaited or in progress, or a cover download runs: the clock (TICK) is needed */

#ifdef __cplusplus
}
#endif
