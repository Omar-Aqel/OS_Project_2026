#ifndef VISUALIZATION_H
#define VISUALIZATION_H

#include "graph.h"
#include "dijkstra.h"
#include "raylib.h"

#define WIN_W        1100
#define WIN_H         700
#define TARGET_FPS     60
#define NODE_RADIUS    22

#define EDGE_STEP_DURATION  0.60f
#define NODE_PAUSE_DURATION 1.00f

#define COL_BG          (Color){ 15,  17,  26, 255}
#define COL_EDGE        (Color){100, 110, 130, 255}
#define COL_PATH_EDGE   (Color){ 80, 200, 120, 255}
#define COL_NODE        (Color){ 40,  44,  62, 255}
#define COL_NODE_BORDER (Color){100, 110, 160, 255}
#define COL_PATH_NODE   (Color){ 80, 200, 120, 255}
#define COL_SRC         (Color){ 60, 160, 255, 255}
#define COL_DST         (Color){255, 100,  80, 255}
#define COL_ENTITY      (Color){255, 210,  60, 255}
#define COL_TEXT        (Color){220, 225, 240, 255}
#define COL_WEIGHT      (Color){170, 180, 200, 255}
#define COL_BTN_IDLE    (Color){ 50,  54,  74, 255}
#define COL_BTN_HOVER   (Color){ 70,  74, 100, 255}
#define COL_BTN_ACTIVE  (Color){ 80, 200, 120, 255}
#define COL_OCCUPIED    (Color){235,  70,  60, 255}   /* red ring: node busy   */
#define COL_WAITING     (Color){235, 170,  60, 255}   /* amber: traveler waits */

/* Fixed Play/Stop button geometry (bottom of the side panel), so that
   click-detection and drawing always agree regardless of how many
   travelers are listed above it. */
#define BTN_X  (WIN_W - 228)
#define BTN_Y  (WIN_H - 70)
#define BTN_W  200
#define BTN_H  40

/* ------------------------------------------------------------------ *
 *  Animation phase of a traveler.  This is purely a *visual* phase
 *  used by the dumb renderer to know where to draw the dot.
 *  How the phase changes over time is decided ENTIRELY by the main
 *  of each milestone (the "brain"); visUpdate only interpolates.
 * ------------------------------------------------------------------ */
typedef enum {
    ANIM_IDLE,          /* not started yet, sitting at source            */
    ANIM_MOVE_ON_EDGE,  /* moving along an edge                          */
    ANIM_PAUSE_AT_NODE, /* paused inside a node (1 s dwell)              */
    ANIM_DONE           /* reached destination                          */
} AnimState;

/* ------------------------------------------------------------------ *
 *  Synchronization-related status of a traveler (milestone 6 only).
 *  Milestones 4 & 5 leave this at TS_NORMAL and never set nodeOccupied,
 *  so no rings/waiting markers ever appear there.
 * ------------------------------------------------------------------ */
typedef enum {
    TS_NORMAL,   /* default; used by milestones 4 & 5                    */
    TS_MOVING,   /* travelling on an edge (m6)                           */
    TS_WAITING,  /* blocked outside a node, waiting for the lock (m6)    */
    TS_IN_NODE   /* holds the node lock, dwelling inside it (m6)         */
} TravelerStatus;

typedef struct {
    float x, y;
} NodePos;

typedef struct {
    const DijkstraResult *result;
    int                  src, dst;

    /* visual animation phase (driven by main via visUpdate or directly) */
    AnimState            animState;
    int                  pathIdx;        /* current edge index in path    */
    float                edgeTimer;
    float                pauseTimer;
    int                  totalEdgeSteps; /* = edge weight                 */
    float                entityX, entityY;
    Color                color;

    /* synchronization status (milestone 6); harmless elsewhere          */
    TravelerStatus       status;
    int                  waitNode;       /* node it is waiting outside of */
    int                  fromNode;       /* node it came from (for placing
                                            the waiting marker)            */
} TravelerState;

typedef struct {
    const Graph      *graph;
    NodePos          *positions;
    TravelerState    *travelers;
    int              numTravelers;
    bool             playing;

    /* per-node occupancy flag (milestone 6).  Owned & written by main;
       read-only for the renderer.  NULL/all-false elsewhere.            */
    bool             *nodeOccupied;
} VisContext;

/* ---- unified renderer API (identical for every milestone) ---------- */
VisContext *visCreate(const Graph *g, TravelerReq *reqs,
                      DijkstraResult *results, int numTravelers);
void        visUpdate(VisContext *ctx, float dt);   /* visual interpolation only */
void        visDraw(const VisContext *ctx);         /* dumb renderer            */
void        visFree(VisContext *ctx);

/* Visual interpolation of travelers whose status is TS_MOVING, advancing
   their (x,y) along the current edge by dt.  Used by milestones 5 & 6,
   where the *main* (brain) decides WHEN a traveler moves (via IPC) but the
   actual position math lives here in the engine.  Reads/writes only the
   visual fields; contains no OS logic.  Also handles the SPACE key for
   play/pause so the button behaves identically across all milestones. */
void        visInterpolate(VisContext *ctx, float dt);

/* convenience alias requested in the design ("visInit") */
#define visInit(g, reqs, results, n) visCreate((g), (reqs), (results), (n))

#endif