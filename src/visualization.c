#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "visualization.h"

/* ================================================================== *
 *  UNIFIED "DUMB" RENDERER
 *  --------------------------------------------------------------
 *  This file knows NOTHING about Dijkstra internals, fork, pipes or
 *  semaphores.  It only:
 *    - lays out node positions,
 *    - interpolates traveler dots between nodes (visUpdate), and
 *    - draws whatever state the VisContext currently holds (visDraw).
 *
 *  Each milestone's main is the "brain": it decides animState / status
 *  / nodeOccupied, and this renderer merely reflects them on screen.
 * ================================================================== */

static void assignCirclePositions(NodePos *pos, int N) {
    float cx = WIN_W * 0.46f;
    float cy = WIN_H * 0.50f;
    float r  = (WIN_H < WIN_W ? WIN_H : WIN_W) * 0.36f;
    for (int i = 0; i < N; i++) {
        float angle = (2.0f * 3.14159265f * i) / N - 3.14159265f / 2.0f;
        pos[i].x = cx + r * cosf(angle);
        pos[i].y = cy + r * sinf(angle);
    }
}

static bool isPathEdge(const DijkstraResult *r, int u, int v) {
    if (!r->found) return false;
    for (int i = 0; i < r->pathLength - 1; i++)
        if (r->path[i] == u && r->path[i + 1] == v) return true;
    return false;
}

static void drawArrow(float x1, float y1, float x2, float y2, Color col) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) return;
    float nx = dx / len, ny = dy / len;
    float sx = x1 + nx * (NODE_RADIUS + 4);
    float sy = y1 + ny * (NODE_RADIUS + 4);
    float ex = x2 - nx * (NODE_RADIUS + 4);
    float ey = y2 - ny * (NODE_RADIUS + 4);
    DrawLineEx((Vector2){sx, sy}, (Vector2){ex, ey}, 2.0f, col);
    float headLen = 12.0f, headAng = 0.45f;
    float ax1 = ex - headLen * cosf(atan2f(ny, nx) - headAng);
    float ay1 = ey - headLen * sinf(atan2f(ny, nx) - headAng);
    float ax2 = ex - headLen * cosf(atan2f(ny, nx) + headAng);
    float ay2 = ey - headLen * sinf(atan2f(ny, nx) + headAng);
    DrawLineEx((Vector2){ex, ey}, (Vector2){ax1, ay1}, 2.0f, col);
    DrawLineEx((Vector2){ex, ey}, (Vector2){ax2, ay2}, 2.0f, col);
}

static bool drawButton(const char *label, int x, int y, int w, int h, bool active) {
    Rectangle rect = {(float)x, (float)y, (float)w, (float)h};
    bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
    bool click = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    Color bg = active ? COL_BTN_ACTIVE : (hover ? COL_BTN_HOVER : COL_BTN_IDLE);
    DrawRectangleRounded(rect, 0.3f, 8, bg);
    DrawRectangleRoundedLines(rect, 0.3f, 8, active ? COL_PATH_EDGE : COL_NODE_BORDER);
    int fw = MeasureText(label, 18);
    DrawText(label, x + (w - fw) / 2, y + (h - 18) / 2, 18, COL_TEXT);
    return click;
}

/* colors for up to 10 travelers */
static const Color palette[] = {
    COL_ENTITY, GREEN, ORANGE, MAGENTA, {0, 255, 255, 255},
    YELLOW, PINK, LIME, SKYBLUE, VIOLET
};

/* True if the on-screen Play/Stop button was clicked this frame.
   Uses the FIXED button rect so it always matches what visDraw renders. */
static bool buttonClicked(void) {
    Rectangle r = {(float)BTN_X, (float)BTN_Y, (float)BTN_W, (float)BTN_H};
    return CheckCollisionPointRec(GetMousePosition(), r) &&
           IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

/* Reset every traveler back to its source so the run can be replayed
   (visual reset only; OS process re-launch is the main's job). */
static void resetTravelers(VisContext *ctx) {
    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        t->animState  = ANIM_IDLE;
        t->pathIdx    = 0;
        t->edgeTimer  = 0;
        t->pauseTimer = 0;
        t->status     = TS_NORMAL;
        t->waitNode   = -1;
        t->fromNode   = t->src;
        t->entityX    = ctx->positions[t->src].x;
        t->entityY    = ctx->positions[t->src].y;
    }
    if (ctx->nodeOccupied)
        memset(ctx->nodeOccupied, 0, sizeof(bool) * ctx->graph->numVertices);
}

VisContext *visCreate(const Graph *g, TravelerReq *reqs,
                      DijkstraResult *results, int numTravelers) {
    VisContext *ctx = (VisContext *)calloc(1, sizeof(VisContext));
    if (!ctx) return NULL;

    ctx->graph        = g;
    ctx->numTravelers = numTravelers;

    ctx->positions = (NodePos *)malloc(sizeof(NodePos) * g->numVertices);
    if (!ctx->positions) { free(ctx); return NULL; }
    assignCirclePositions(ctx->positions, g->numVertices);

    ctx->nodeOccupied = (bool *)calloc(g->numVertices, sizeof(bool));
    if (!ctx->nodeOccupied) { free(ctx->positions); free(ctx); return NULL; }

    ctx->travelers = (TravelerState *)malloc(sizeof(TravelerState) * numTravelers);
    if (!ctx->travelers) { free(ctx->nodeOccupied); free(ctx->positions); free(ctx); return NULL; }

    for (int i = 0; i < numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        t->result         = &results[i];
        t->src            = reqs[i].src;
        t->dst            = reqs[i].dst;
        t->entityX        = ctx->positions[reqs[i].src].x;
        t->entityY        = ctx->positions[reqs[i].src].y;
        t->animState      = ANIM_IDLE;
        t->pathIdx        = 0;
        t->color          = palette[i % 10];
        t->edgeTimer      = 0.0f;
        t->pauseTimer     = 0.0f;
        t->totalEdgeSteps = 1;
        t->status         = TS_NORMAL;
        t->waitNode       = -1;
        t->fromNode       = reqs[i].src;
    }

    ctx->playing = false;
    return ctx;
}

void visFree(VisContext *ctx) {
    if (!ctx) return;
    free(ctx->positions);
    free(ctx->travelers);
    free(ctx->nodeOccupied);
    free(ctx);
}

/* ------------------------------------------------------------------ *
 *  visUpdate: VISUAL interpolation only.
 *  Used by milestones 4 & 5, which let the renderer self-animate
 *  travelers along their (already computed) paths.  Milestone 6 drives
 *  positions itself and sets ctx->playing=true but its travelers move
 *  via the same edge-interpolation; the brain only flips status /
 *  nodeOccupied.  No IPC, no semaphores, no Dijkstra here.
 * ------------------------------------------------------------------ */
void visUpdate(VisContext *ctx, float dt) {
    if (IsKeyPressed(KEY_SPACE) || buttonClicked()) {
        bool allDone = true;
        for (int i = 0; i < ctx->numTravelers; i++)
            if (ctx->travelers[i].animState != ANIM_DONE) allDone = false;

        if (allDone) {
            resetTravelers(ctx);
        }
        ctx->playing = !ctx->playing;
    }

    if (!ctx->playing) return;

    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];

        if (!t->result->found || t->result->pathLength <= 1) {
            t->animState = ANIM_DONE;
            continue;
        }

        if (t->animState == ANIM_IDLE) {
            t->animState = ANIM_MOVE_ON_EDGE;
            t->edgeTimer = 0.0f;
            int u = t->result->path[0];
            int v = t->result->path[1];
            Edge *e = ctx->graph->adjList[u].head;
            while (e && e->dest != v) e = e->next;
            t->totalEdgeSteps = e ? e->weight : 1;
        }

        if (t->animState == ANIM_MOVE_ON_EDGE) {
            t->edgeTimer += dt;
            int fromIdx = t->pathIdx;
            int toIdx   = t->pathIdx + 1;
            float fromX = ctx->positions[t->result->path[fromIdx]].x;
            float fromY = ctx->positions[t->result->path[fromIdx]].y;
            float toX   = ctx->positions[t->result->path[toIdx]].x;
            float toY   = ctx->positions[t->result->path[toIdx]].y;

            float totalDur = t->totalEdgeSteps * EDGE_STEP_DURATION;
            float percent  = t->edgeTimer / totalDur;
            if (percent > 1.0f) percent = 1.0f;

            t->entityX = fromX + percent * (toX - fromX);
            t->entityY = fromY + percent * (toY - fromY);

            if (t->edgeTimer >= totalDur) {
                t->entityX = toX;
                t->entityY = toY;
                t->pathIdx++;
                if (t->pathIdx >= t->result->pathLength - 1) {
                    t->animState = ANIM_DONE;
                } else {
                    t->animState  = ANIM_PAUSE_AT_NODE;
                    t->pauseTimer = 0.0f;
                }
            }
        } else if (t->animState == ANIM_PAUSE_AT_NODE) {
            t->pauseTimer += dt;
            if (t->pauseTimer >= NODE_PAUSE_DURATION) {
                t->animState = ANIM_MOVE_ON_EDGE;
                t->edgeTimer = 0.0f;
                int u = t->result->path[t->pathIdx];
                int v = t->result->path[t->pathIdx + 1];
                Edge *e = ctx->graph->adjList[u].head;
                while (e && e->dest != v) e = e->next;
                t->totalEdgeSteps = e ? e->weight : 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 *  visInterpolate: VISUAL position math for milestones 5 & 6.
 *  The brain (main) sets a traveler's status to TS_MOVING and resets
 *  edgeTimer when it receives MSG_DEPARTED; this function advances the
 *  dot smoothly along the edge.  It contains NO OS logic — no IPC, no
 *  semaphores, no Dijkstra.  It also reads the SPACE key so the
 *  play/pause control behaves the same way it does in milestone 4.
 * ------------------------------------------------------------------ */
void visInterpolate(VisContext *ctx, float dt) {
    if (IsKeyPressed(KEY_SPACE) || buttonClicked())
        ctx->playing = !ctx->playing;

    if (!ctx->playing) return;

    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        if (t->status != TS_MOVING || !t->result->found) continue;

        int fromIdx = t->pathIdx;
        int toIdx   = t->pathIdx + 1;
        if (toIdx >= t->result->pathLength) continue;

        t->edgeTimer += dt;
        float fx = ctx->positions[t->result->path[fromIdx]].x;
        float fy = ctx->positions[t->result->path[fromIdx]].y;
        float tx = ctx->positions[t->result->path[toIdx]].x;
        float ty = ctx->positions[t->result->path[toIdx]].y;
        float dur = t->totalEdgeSteps * EDGE_STEP_DURATION;
        float pc  = dur > 0 ? t->edgeTimer / dur : 1.0f;
        if (pc > 1.0f) pc = 1.0f;
        t->entityX = fx + pc * (tx - fx);
        t->entityY = fy + pc * (ty - fy);
    }
}

/* ------------------------------------------------------------------ *
 *  visDraw: pure renderer.  Reads VisContext, draws it, computes
 *  nothing about how the state was produced.
 * ------------------------------------------------------------------ */
void visDraw(const VisContext *ctx) {
    const Graph *g = ctx->graph;
    int N = g->numVertices;
    const NodePos *p = ctx->positions;

    ClearBackground(COL_BG);
    DrawRectangle(WIN_W - 240, 0, 240, WIN_H, (Color){20, 22, 34, 255});

    /* edges + weights */
    for (int u = 0; u < N; u++) {
        Edge *e = g->adjList[u].head;
        while (e) {
            int v = e->dest;
            bool isPath = false;
            Color edgeCol = COL_EDGE;
            for (int i = 0; i < ctx->numTravelers; i++) {
                if (isPathEdge(ctx->travelers[i].result, u, v)) {
                    isPath  = true;
                    edgeCol = ctx->travelers[i].color;
                    break;
                }
            }
            drawArrow(p[u].x, p[u].y, p[v].x, p[v].y, edgeCol);
            float mx = (p[u].x + p[v].x) / 2.0f;
            float my = (p[u].y + p[v].y) / 2.0f;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", e->weight);
            int tw = MeasureText(buf, 13);
            DrawText(buf, (int)(mx - tw / 2), (int)(my - 8), 13,
                     isPath ? edgeCol : COL_WEIGHT);
            e = e->next;
        }
    }

    /* nodes (+ red ring if occupied) */
    for (int i = 0; i < N; i++) {
        DrawCircle((int)p[i].x, (int)p[i].y, NODE_RADIUS, COL_NODE);
        DrawCircleLines((int)p[i].x, (int)p[i].y, NODE_RADIUS, COL_NODE_BORDER);

        if (ctx->nodeOccupied && ctx->nodeOccupied[i]) {
            DrawCircleLines((int)p[i].x, (int)p[i].y, NODE_RADIUS + 5, COL_OCCUPIED);
            DrawCircleLines((int)p[i].x, (int)p[i].y, NODE_RADIUS + 8, COL_OCCUPIED);
        }
        char label[16];
        snprintf(label, sizeof(label), "%d", i);
        int lw = MeasureText(label, 16);
        DrawText(label, (int)(p[i].x - lw / 2), (int)(p[i].y - 8), 16, COL_TEXT);
    }

    /* travelers */
    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        if (!t->result->found) continue;
        if (t->animState == ANIM_IDLE && t->status == TS_NORMAL) continue;

        float dx = t->entityX, dy = t->entityY;
        bool finished = (t->animState == ANIM_DONE);

        /* a waiting traveler is parked just OUTSIDE its target node,
           on the side facing the node it came from */
        if (t->status == TS_WAITING && t->waitNode >= 0) {
            float nx = p[t->waitNode].x;
            float ny = p[t->waitNode].y;
            float fx = p[t->fromNode].x;
            float fy = p[t->fromNode].y;
            float vx = fx - nx, vy = fy - ny;
            float len = sqrtf(vx*vx + vy*vy);
            if (len < 1.0f) { vx = 0; vy = -1; len = 1; }
            float off = NODE_RADIUS + 18;
            dx = nx + (vx / len) * off;
            dy = ny + (vy / len) * off;
        }

        /* Finished travelers fan out in a small ring around their
           destination node, so several arrivals at the same node don't
           stack on top of each other and hide it. */
        if (finished) {
            float ang = (2.0f * 3.14159265f * i) / ctx->numTravelers;
            float fanR = NODE_RADIUS + 20;
            dx = p[t->dst].x + fanR * cosf(ang);
            dy = p[t->dst].y + fanR * sinf(ang);
        }

        Color body = t->color;
        if (t->status == TS_WAITING) body = COL_WAITING;
        /* mute + make translucent once done: clearly "completed" without
           covering the node's number or ring */
        if (finished) {
            body.r = (unsigned char)(body.r * 0.55f);
            body.g = (unsigned char)(body.g * 0.55f);
            body.b = (unsigned char)(body.b * 0.55f);
            body.a = 110;                    /* semi-transparent */
        }

        float radius = finished ? 9.0f : 14.0f;
        Color ringCol = finished ? (Color){180, 190, 210, 110}
                      : (t->status == TS_WAITING ? COL_OCCUPIED : WHITE);

        DrawCircle((int)dx, (int)dy, radius, body);
        DrawCircleLines((int)dx, (int)dy, radius, ringCol);
        if (t->status == TS_WAITING)
            DrawCircleLines((int)dx, (int)dy, 18, COL_WAITING);

        char tLabel[16];
        snprintf(tLabel, sizeof(tLabel), "T%d", i);
        int tw = MeasureText(tLabel, 10);
        Color labelCol = finished ? (Color){200, 205, 220, 130} : WHITE;
        DrawText(tLabel, (int)dx - tw/2, (int)dy - 5, 10, labelCol);
    }

    /* side panel */
    int px = WIN_W - 228;
    int py = 24;
    DrawText("DIJKSTRA", px, py, 22, COL_PATH_NODE);
    DrawText("UNIFIED RENDERER", px, py + 26, 14, COL_WEIGHT);
    DrawLine(px, py + 48, WIN_W - 12, py + 48, COL_NODE_BORDER);
    py += 60;

    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        char buf[80];
        if (t->result->found) {
            const char *st;
            switch (t->status) {
                case TS_WAITING: st = "Waiting"; break;
                case TS_IN_NODE: st = "In-Node"; break;
                case TS_MOVING:  st = "Moving";  break;
                default: st = (t->animState == ANIM_DONE) ? "Done" : "Moving";
            }
            snprintf(buf, sizeof(buf), "T%d: %d->%d (Cost:%d) [%s]",
                     i, t->src, t->dst, t->result->totalWeight, st);
        } else {
            snprintf(buf, sizeof(buf), "T%d: %d->%d (No Path)", i, t->src, t->dst);
        }
        DrawText(buf, px, py, 13, t->status == TS_WAITING ? COL_WAITING : t->color);
        py += 24;
    }

    py += 10;
    DrawLine(px, py, WIN_W - 12, py, COL_NODE_BORDER); py += 14;
    DrawText("Timing:", px, py, 13, COL_WEIGHT); py += 17;
    char timeBuf[40];
    snprintf(timeBuf, sizeof(timeBuf), "Edge step : %d ms/unit",
             (int)(EDGE_STEP_DURATION * 1000));
    DrawText(timeBuf, px, py, 12, COL_TEXT); py += 16;
    DrawText("Node dwell: 1000 ms", px, py, 12, COL_TEXT); py += 24;
    DrawText("[SPACE] Play / Reset", px, py, 12, COL_WEIGHT); py += 28;

    /* draw the Play/Stop button at its FIXED location so that the click
       hit-test in buttonClicked() always lines up with what is shown */
    drawButton(ctx->playing ? "  STOP" : "  PLAY",
               BTN_X, BTN_Y, BTN_W, BTN_H, ctx->playing);

    char fpsBuf[32];
    snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", GetFPS());
    DrawText(fpsBuf, WIN_W - 228, WIN_H - 24, 13, COL_WEIGHT);
}