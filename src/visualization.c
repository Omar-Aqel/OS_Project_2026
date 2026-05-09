#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "visualization.h"

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

static bool onPath(const DijkstraResult *r, int v) {
    if (!r->found) return false;
    for (int i = 0; i < r->pathLength; i++)
        if (r->path[i] == v) return true;
    return false;
}

static bool isPathEdge(const DijkstraResult *r, int u, int v) {
    if (!r->found) return false;
    for (int i = 0; i < r->pathLength - 1; i++)
        if (r->path[i] == u && r->path[i + 1] == v) return true;
    return false;
}

static void drawArrow(float x1, float y1, float x2, float y2, Color col) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) return;
    float nx = dx / len;
    float ny = dy / len;
    float sx = x1 + nx * (NODE_RADIUS + 4);
    float sy = y1 + ny * (NODE_RADIUS + 4);
    float ex = x2 - nx * (NODE_RADIUS + 4);
    float ey = y2 - ny * (NODE_RADIUS + 4);
    DrawLineEx((Vector2){sx, sy}, (Vector2){ex, ey}, 2.0f, col);
    float headLen = 12.0f;
    float headAng = 0.45f;
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
    DrawRectangleRoundedLines(rect, 0.3f, 8,
        active ? COL_PATH_EDGE : COL_NODE_BORDER);
    int fw = MeasureText(label, 18);
    DrawText(label, x + (w - fw) / 2, y + (h - 18) / 2, 18, COL_TEXT);
    return click;
}

VisContext *visCreate(const Graph *g, const DijkstraResult *r, int src, int dst) {
    VisContext *ctx = (VisContext *)calloc(1, sizeof(VisContext));
    if (!ctx) return NULL;
    ctx->graph   = g;
    ctx->result  = r;
    ctx->src     = src;
    ctx->dst     = dst;
    ctx->positions = (NodePos *)malloc(sizeof(NodePos) * g->numVertices);
    if (!ctx->positions) { free(ctx); return NULL; }
    assignCirclePositions(ctx->positions, g->numVertices);
    ctx->entityX   = ctx->positions[src].x;
    ctx->entityY   = ctx->positions[src].y;
    ctx->animState = ANIM_IDLE;
    ctx->pathIdx   = 0;
    ctx->playing   = false;
    return ctx;
}

void visFree(VisContext *ctx) {
    if (!ctx) return;
    free(ctx->positions);
    free(ctx);
}

void visUpdate(VisContext *ctx, float dt) {
    if (IsKeyPressed(KEY_SPACE)) {
        if (ctx->animState == ANIM_DONE) {
            ctx->animState  = ANIM_IDLE;
            ctx->pathIdx    = 0;
            ctx->edgeTimer  = 0;
            ctx->pauseTimer = 0;
            ctx->entityX    = ctx->positions[ctx->src].x;
            ctx->entityY    = ctx->positions[ctx->src].y;
        }
        ctx->playing = !ctx->playing;
    }
    if (!ctx->playing) return;
    if (!ctx->result->found || ctx->result->pathLength <= 1) {
        ctx->animState = ANIM_DONE;
        ctx->playing   = false;
        return;
    }
    if (ctx->animState == ANIM_IDLE) {
        ctx->animState     = ANIM_MOVE_ON_EDGE;
        ctx->edgeTimer     = 0.0f;
        int u = ctx->result->path[0];
        int v = ctx->result->path[1];
        Edge *e = ctx->graph->adjList[u].head;
        while (e && e->dest != v) e = e->next;
        ctx->totalEdgeSteps = e ? e->weight : 1;
    }
    if (ctx->animState == ANIM_MOVE_ON_EDGE) {
        ctx->edgeTimer += dt;
        int   fromIdx = ctx->pathIdx;
        int   toIdx   = ctx->pathIdx + 1;
        float fromX   = ctx->positions[ctx->result->path[fromIdx]].x;
        float fromY   = ctx->positions[ctx->result->path[fromIdx]].y;
        float toX     = ctx->positions[ctx->result->path[toIdx]].x;
        float toY     = ctx->positions[ctx->result->path[toIdx]].y;
        float totalDur = ctx->totalEdgeSteps * EDGE_STEP_DURATION;
        float t = ctx->edgeTimer / totalDur;
        if (t > 1.0f) t = 1.0f;
        ctx->entityX = fromX + t * (toX - fromX);
        ctx->entityY = fromY + t * (toY - fromY);
        if (ctx->edgeTimer >= totalDur) {
            ctx->entityX = toX;
            ctx->entityY = toY;
            ctx->pathIdx++;
            if (ctx->pathIdx >= ctx->result->pathLength - 1) {
                ctx->animState = ANIM_DONE;
                ctx->playing   = false;
            } else {
                ctx->animState  = ANIM_PAUSE_AT_NODE;
                ctx->pauseTimer = 0.0f;
            }
        }
    } else if (ctx->animState == ANIM_PAUSE_AT_NODE) {
        ctx->pauseTimer += dt;
        if (ctx->pauseTimer >= NODE_PAUSE_DURATION) {
            ctx->animState = ANIM_MOVE_ON_EDGE;
            ctx->edgeTimer = 0.0f;
            int u = ctx->result->path[ctx->pathIdx];
            int v = ctx->result->path[ctx->pathIdx + 1];
            Edge *e = ctx->graph->adjList[u].head;
            while (e && e->dest != v) e = e->next;
            ctx->totalEdgeSteps = e ? e->weight : 1;
        }
    }
}

void visDraw(const VisContext *ctx) {
    const Graph *g  = ctx->graph;
    int          N  = g->numVertices;
    const NodePos *p = ctx->positions;
    ClearBackground(COL_BG);
    DrawRectangle(WIN_W - 240, 0, 240, WIN_H, (Color){20, 22, 34, 255});
    for (int u = 0; u < N; u++) {
        Edge *e = g->adjList[u].head;
        while (e) {
            int v = e->dest;
            bool path = isPathEdge(ctx->result, u, v);
            Color col = path ? COL_PATH_EDGE : COL_EDGE;
            drawArrow(p[u].x, p[u].y, p[v].x, p[v].y, col);
            float mx = (p[u].x + p[v].x) / 2.0f;
            float my = (p[u].y + p[v].y) / 2.0f;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", e->weight);
            int tw = MeasureText(buf, 13);
            DrawText(buf, (int)(mx - tw / 2), (int)(my - 8), 13,
                     path ? COL_PATH_EDGE : COL_WEIGHT);
            e = e->next;
        }
    }
    for (int i = 0; i < N; i++) {
        Color fill, border;
        if      (i == ctx->src) { fill = COL_SRC; border = COL_SRC; }
        else if (i == ctx->dst) { fill = COL_DST; border = COL_DST; }
        else if (onPath(ctx->result, i)) { fill = COL_NODE; border = COL_PATH_NODE; }
        else { fill = COL_NODE; border = COL_NODE_BORDER; }
        DrawCircle((int)p[i].x, (int)p[i].y, NODE_RADIUS, fill);
        DrawCircleLines((int)p[i].x, (int)p[i].y, NODE_RADIUS, border);
        char label[8];
        snprintf(label, sizeof(label), "%d", i);
        int lw = MeasureText(label, 16);
        DrawText(label, (int)(p[i].x - lw / 2), (int)(p[i].y - 8), 16, COL_TEXT);
    }
    if (ctx->result->found && ctx->animState != ANIM_IDLE) {
        DrawCircle((int)ctx->entityX, (int)ctx->entityY, 14, COL_ENTITY);
        DrawCircleLines((int)ctx->entityX, (int)ctx->entityY, 14, WHITE);
    }
    int px = WIN_W - 228;
    int py = 24;
    DrawText("DIJKSTRA", px, py, 22, COL_PATH_NODE);
    DrawText("VISUALIZER", px, py + 26, 14, COL_WEIGHT);
    DrawLine(px, py + 48, WIN_W - 12, py + 48, COL_NODE_BORDER);
    py += 60;
    char buf[64];
    snprintf(buf, sizeof(buf), "Source :  %d", ctx->src);
    DrawText(buf, px, py, 15, COL_SRC); py += 22;
    snprintf(buf, sizeof(buf), "Dest   :  %d", ctx->dst);
    DrawText(buf, px, py, 15, COL_DST); py += 22;
    if (ctx->result->found) {
        snprintf(buf, sizeof(buf), "Cost   :  %d", ctx->result->totalWeight);
        DrawText(buf, px, py, 15, COL_TEXT); py += 28;
        DrawText("Path:", px, py, 14, COL_WEIGHT); py += 18;
        for (int i = 0; i < ctx->result->pathLength; i++) {
            snprintf(buf, sizeof(buf), i < ctx->result->pathLength - 1
                     ? "  %d  ->" : "  %d", ctx->result->path[i]);
            DrawText(buf, px, py, 14, COL_PATH_NODE); py += 18;
        }
    } else {
        DrawText("No path found", px, py, 14, COL_DST); py += 22;
    }
    py += 10;
    DrawLine(px, py, WIN_W - 12, py, COL_NODE_BORDER); py += 14;
    DrawText("Timing:", px, py, 13, COL_WEIGHT); py += 17;
    DrawText("Edge step : 300 ms/unit", px, py, 12, COL_TEXT); py += 16;
    DrawText("Node pause: 1000 ms", px, py, 12, COL_TEXT); py += 24;
    const char *stateStr =
        ctx->animState == ANIM_IDLE          ? "Idle" :
        ctx->animState == ANIM_MOVE_ON_EDGE  ? "Moving..." :
        ctx->animState == ANIM_PAUSE_AT_NODE ? "Pausing..." : "Done";
    snprintf(buf, sizeof(buf), "State: %s", stateStr);
    DrawText(buf, px, py, 13, COL_WEIGHT); py += 28;
    DrawText("[SPACE] Play / Pause", px, py, 12, COL_WEIGHT); py += 28;
    drawButton(ctx->playing ? "  STOP" : "  PLAY", px, py, 200, 40, ctx->playing);
    snprintf(buf, sizeof(buf), "FPS: %d", GetFPS());
    DrawText(buf, WIN_W - 228, WIN_H - 24, 13, COL_WEIGHT);
}
