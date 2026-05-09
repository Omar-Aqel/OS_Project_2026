#ifndef VISUALIZATION_H
#define VISUALIZATION_H

#include "graph.h"
#include "dijkstra.h"
#include "raylib.h"

#define WIN_W        1100
#define WIN_H         700
#define TARGET_FPS     60
#define NODE_RADIUS    22

#define EDGE_STEP_DURATION  0.30f
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

typedef enum {
    ANIM_IDLE,
    ANIM_MOVE_ON_EDGE,
    ANIM_PAUSE_AT_NODE,
    ANIM_DONE
} AnimState;

typedef struct {
    float x, y;
} NodePos;

typedef struct {
    const Graph         *graph;
    const DijkstraResult *result;
    NodePos             *positions;
    int                  src, dst;
    AnimState   animState;
    int         pathIdx;
    float       edgeT;
    float       edgeTimer;
    float       pauseTimer;
    int         totalEdgeSteps;
    float       entityX, entityY;
    bool        playing;
} VisContext;

VisContext *visCreate(const Graph *g, const DijkstraResult *r, int src, int dst);
void        visUpdate(VisContext *ctx, float dt);
void        visDraw(const VisContext *ctx);
void        visFree(VisContext *ctx);

#endif