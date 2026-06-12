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
    (void)r; (void)v;  // unused in Milestone 4
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

// colors for 10 travelers
static const Color palette[] = {
    COL_ENTITY, GREEN, ORANGE, MAGENTA, {0, 255, 255, 255}, YELLOW, PINK, LIME, SKYBLUE, VIOLET
};

VisContext *visCreate(const Graph *g, TravelerReq *reqs, DijkstraResult *results, int numTravelers) {
    VisContext *ctx = (VisContext *)calloc(1, sizeof(VisContext));
    if (!ctx) return NULL;
    
    ctx->graph = g;
    ctx->numTravelers = numTravelers;
    
    ctx->positions = (NodePos *)malloc(sizeof(NodePos) * g->numVertices);
    if (!ctx->positions) { free(ctx); return NULL; }
    assignCirclePositions(ctx->positions, g->numVertices);
    
    ctx->travelers = (TravelerState *)malloc(sizeof(TravelerState) * numTravelers);
    if (!ctx->travelers) { free(ctx->positions); free(ctx); return NULL; }
    
    for (int i = 0; i < numTravelers; i++) {
        ctx->travelers[i].result = &results[i];
        ctx->travelers[i].src = reqs[i].src;
        ctx->travelers[i].dst = reqs[i].dst;
        ctx->travelers[i].entityX = ctx->positions[reqs[i].src].x;
        ctx->travelers[i].entityY = ctx->positions[reqs[i].src].y;
        ctx->travelers[i].animState = ANIM_IDLE;
        ctx->travelers[i].pathIdx = 0;
        ctx->travelers[i].color = palette[i % 10];
        ctx->travelers[i].edgeTimer = 0.0f;
        ctx->travelers[i].pauseTimer = 0.0f;
        ctx->travelers[i].totalEdgeSteps = 1;
    }
    
    ctx->playing = false;
    return ctx;
}

void visFree(VisContext *ctx) {
    if (!ctx) return;
    free(ctx->positions);
    free(ctx->travelers);
    free(ctx);
}

void visUpdate(VisContext *ctx, float dt) {
    if (IsKeyPressed(KEY_SPACE)) {
        bool allDone = true;
        for (int i = 0; i < ctx->numTravelers; i++) {
            if (ctx->travelers[i].animState != ANIM_DONE) allDone = false;
        }
        
        if (allDone) {
            for (int i = 0; i < ctx->numTravelers; i++) {
                ctx->travelers[i].animState = ANIM_IDLE;
                ctx->travelers[i].pathIdx = 0;
                ctx->travelers[i].edgeTimer = 0;
                ctx->travelers[i].pauseTimer = 0;
                ctx->travelers[i].entityX = ctx->positions[ctx->travelers[i].src].x;
                ctx->travelers[i].entityY = ctx->positions[ctx->travelers[i].src].y;
            }
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
            int toIdx = t->pathIdx + 1;
            float fromX = ctx->positions[t->result->path[fromIdx]].x;
            float fromY = ctx->positions[t->result->path[fromIdx]].y;
            float toX = ctx->positions[t->result->path[toIdx]].x;
            float toY = ctx->positions[t->result->path[toIdx]].y;
            
            float totalDur = t->totalEdgeSteps * EDGE_STEP_DURATION;
            float percent = t->edgeTimer / totalDur;
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
                    t->animState = ANIM_PAUSE_AT_NODE;
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
            bool isPath = false;
            Color edgeCol = COL_EDGE;

            for (int i = 0; i < ctx->numTravelers; i++) {
                if (isPathEdge(ctx->travelers[i].result, u, v)) {
                    isPath = true;
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
            DrawText(buf, (int)(mx - tw / 2), (int)(my - 8), 13, isPath ? edgeCol : COL_WEIGHT);
            e = e->next;
        }
    }

    for (int i = 0; i < N; i++) {
        DrawCircle((int)p[i].x, (int)p[i].y, NODE_RADIUS, COL_NODE);
        DrawCircleLines((int)p[i].x, (int)p[i].y, NODE_RADIUS, COL_NODE_BORDER);
        char label[16];
        snprintf(label, sizeof(label), "%d", i);
        int lw = MeasureText(label, 16);
        DrawText(label, (int)(p[i].x - lw / 2), (int)(p[i].y - 8), 16, COL_TEXT);
    }

    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        if (t->result->found && t->animState != ANIM_IDLE) {
            DrawCircle((int)t->entityX, (int)t->entityY, 14, t->color);
            DrawCircleLines((int)t->entityX, (int)t->entityY, 14, WHITE);

            char tLabel[16];
            snprintf(tLabel, sizeof(tLabel), "T%d", i);
            int tw = MeasureText(tLabel, 10);
            DrawText(tLabel, (int)t->entityX - tw/2, (int)t->entityY - 5, 10, WHITE);
        }
    }

    int px = WIN_W - 228;
    int py = 24;
    DrawText("DIJKSTRA", px, py, 22, COL_PATH_NODE);
    DrawText("MULTIPLE TRAVELERS", px, py + 26, 14, COL_WEIGHT);
    DrawLine(px, py + 48, WIN_W - 12, py + 48, COL_NODE_BORDER);
    py += 60;

    for (int i = 0; i < ctx->numTravelers; i++) {
        TravelerState *t = &ctx->travelers[i];
        char buf[64];

        if (t->result->found) {
            const char *stateStr = (t->animState == ANIM_DONE) ? "Done" : "Moving";
            snprintf(buf, sizeof(buf), "T%d: %d->%d (Cost: %d) [%s]",
                     i, t->src, t->dst, t->result->totalWeight, stateStr);
        } else {
            snprintf(buf, sizeof(buf), "T%d: %d->%d (No Path)", i, t->src, t->dst);
        }

        DrawText(buf, px, py, 13, t->color);
        py += 24;
    }

    py += 10;
    DrawLine(px, py, WIN_W - 12, py, COL_NODE_BORDER); py += 14;
    DrawText("Timing:", px, py, 13, COL_WEIGHT); py += 17;
    DrawText("Edge step : 300 ms/unit", px, py, 12, COL_TEXT); py += 16;
    DrawText("Node pause: 1000 ms", px, py, 12, COL_TEXT); py += 24;
    DrawText("[SPACE] Play / Reset", px, py, 12, COL_WEIGHT); py += 28;

    drawButton(ctx->playing ? "  STOP" : "  PLAY", px, py, 200, 40, ctx->playing);

    char fpsBuf[32];
    snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", GetFPS());
    DrawText(fpsBuf, WIN_W - 228, WIN_H - 24, 13, COL_WEIGHT);
}
