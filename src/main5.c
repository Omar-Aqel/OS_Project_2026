#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include "graph.h"
#include "dijkstra.h"

#ifdef ENABLE_GUI
#include "raylib.h"
#endif

/* ── IPC message (child → parent) ────────────────────── */
typedef enum { MSG_ARRIVED, MSG_FINISHED } MsgType;

typedef struct {
    MsgType type;
    pid_t   pid;
    int     currentNode;
    int     nextNode;   /* -1 = DESTINATION */
} IPCMsg;

/* ── Timing ───────────────────────────────────────────── */
#define TRAVEL_USEC_PER_UNIT  300000u   /* 300 ms per weight unit  */
#define NODE_PAUSE_USEC      1000000u   /* 1000 ms pause per node  */
#define EDGE_STEP_SEC          0.30f    /* GUI animation: s/weight */

/* ── GUI layout ───────────────────────────────────────── */
#define WIN_W       1100
#define WIN_H        700
#define TARGET_FPS    60
#define NODE_RADIUS   22

/* ── Per-traveler animation state (parent side) ───────── */
typedef struct {
    pid_t  pid;
    int    src, dst;
    int    fromNode, toNode;        /* current animation segment */
    float  animTimer, animDuration;
    float  x, y;                   /* screen position           */
    bool   started, done, waited;
#ifdef ENABLE_GUI
    Color  color;
#endif
} M5Traveler;

typedef struct { float x, y; } NodePos;

/* ── Helpers ──────────────────────────────────────────── */
static void computePositions(NodePos *pos, int N) {
    float cx = WIN_W * 0.46f;
    float cy = WIN_H * 0.50f;
    float r  = (WIN_H < WIN_W ? WIN_H : WIN_W) * 0.36f;
    for (int i = 0; i < N; i++) {
        float a = (2.0f * 3.14159265f * i) / N - 3.14159265f / 2.0f;
        pos[i].x = cx + r * cosf(a);
        pos[i].y = cy + r * sinf(a);
    }
}

static int edgeWeight(const Graph *g, int u, int v) {
    for (Edge *e = g->adjList[u].head; e; e = e->next)
        if (e->dest == v) return e->weight;
    return 1;
}

/* ── GUI drawing ──────────────────────────────────────── */
#ifdef ENABLE_GUI
static const Color PALETTE[] = {
    {255, 210,  60, 255},
    { 50, 205,  50, 255},
    {255, 140,   0, 255},
    {255,   0, 255, 255},
    {  0, 255, 255, 255},
    {255, 255,   0, 255},
    {255, 182, 193, 255},
    {  0, 250, 154, 255},
    {135, 206, 235, 255},
    {238, 130, 238, 255},
};

static void drawArrow(float x1, float y1, float x2, float y2, Color col) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) return;
    float nx = dx/len, ny = dy/len;
    float sx = x1 + nx*(NODE_RADIUS+4), sy = y1 + ny*(NODE_RADIUS+4);
    float ex = x2 - nx*(NODE_RADIUS+4), ey = y2 - ny*(NODE_RADIUS+4);
    DrawLineEx((Vector2){sx, sy}, (Vector2){ex, ey}, 2.0f, col);
    float ang = atan2f(ny, nx), h = 12.0f, ha = 0.45f;
    DrawLineEx((Vector2){ex, ey},
               (Vector2){ex - h*cosf(ang-ha), ey - h*sinf(ang-ha)}, 2.0f, col);
    DrawLineEx((Vector2){ex, ey},
               (Vector2){ex - h*cosf(ang+ha), ey - h*sinf(ang+ha)}, 2.0f, col);
}

static void drawScene(const Graph *g, const NodePos *pos,
                      const M5Traveler *tv, int nT) {
    ClearBackground((Color){ 15,  17,  26, 255});
    DrawRectangle(WIN_W-240, 0, 240, WIN_H, (Color){20, 22, 34, 255});

    /* edges */
    for (int u = 0; u < g->numVertices; u++)
        for (Edge *e = g->adjList[u].head; e; e = e->next) {
            int v = e->dest;
            drawArrow(pos[u].x, pos[u].y, pos[v].x, pos[v].y,
                      (Color){100,110,130,255});
            float mx = (pos[u].x+pos[v].x)/2.0f;
            float my = (pos[u].y+pos[v].y)/2.0f;
            char buf[16]; snprintf(buf, sizeof buf, "%d", e->weight);
            int tw = MeasureText(buf, 13);
            DrawText(buf, (int)(mx - tw/2), (int)(my-8), 13,
                     (Color){170,180,200,255});
        }

    /* nodes */
    for (int i = 0; i < g->numVertices; i++) {
        DrawCircle((int)pos[i].x, (int)pos[i].y, NODE_RADIUS,
                   (Color){40,44,62,255});
        DrawCircleLines((int)pos[i].x, (int)pos[i].y, NODE_RADIUS,
                        (Color){100,110,160,255});
        char lbl[8]; snprintf(lbl, sizeof lbl, "%d", i);
        int lw = MeasureText(lbl, 16);
        DrawText(lbl, (int)(pos[i].x - lw/2), (int)(pos[i].y-8), 16,
                 (Color){220,225,240,255});
    }

    /* travelers */
    for (int i = 0; i < nT; i++) {
        if (!tv[i].started) continue;
        DrawCircle((int)tv[i].x, (int)tv[i].y, 14, tv[i].color);
        DrawCircleLines((int)tv[i].x, (int)tv[i].y, 14, WHITE);
        char lbl[8]; snprintf(lbl, sizeof lbl, "T%d", i);
        int lw = MeasureText(lbl, 10);
        DrawText(lbl, (int)tv[i].x - lw/2, (int)tv[i].y - 5, 10, WHITE);
    }

    /* side panel */
    int px = WIN_W-228, py = 24;
    DrawText("DIJKSTRA", px, py, 22, (Color){80,200,120,255});      py += 26;
    DrawText("IPC - MILESTONE 5", px, py, 14, (Color){170,180,200,255}); py += 24;
    DrawLine(px, py, WIN_W-12, py, (Color){100,110,160,255});        py += 16;

    for (int i = 0; i < nT; i++) {
        const char *st = tv[i].done    ? "Done"   :
                         tv[i].started ? "Moving" : "Waiting";
        char buf[64];
        snprintf(buf, sizeof buf, "T%d: %d->%d [%s]",
                 i, tv[i].src, tv[i].dst, st);
        DrawText(buf, px, py, 13, tv[i].color);
        py += 22;
    }

    py += 8;
    DrawLine(px, py, WIN_W-12, py, (Color){100,110,160,255}); py += 14;
    DrawText("IPC: anonymous pipes", px, py, 12, (Color){170,180,200,255}); py += 16;
    DrawText("Each child computes", px, py, 12, (Color){170,180,200,255}); py += 14;
    DrawText("Dijkstra independently", px, py, 12, (Color){170,180,200,255});

    char fpsBuf[32]; snprintf(fpsBuf, sizeof fpsBuf, "FPS: %d", GetFPS());
    DrawText(fpsBuf, WIN_W-228, WIN_H-24, 13, (Color){170,180,200,255});
}
#endif /* ENABLE_GUI */

/* ── Process one IPC message (used by both GUI and terminal) ── */
static void handleMsg(const IPCMsg *msg, M5Traveler *t,
                      const NodePos *pos, const Graph *g,
                      int *doneCount, pid_t *pids, int idx) {
    if (msg->type == MSG_ARRIVED) {
        int cur = msg->currentNode, nxt = msg->nextNode;
        if (nxt >= 0)
            printf("[PID=%d] arrived at node %d | next node: %d\n",
                   (int)msg->pid, cur, nxt);
        else
            printf("[PID=%d] arrived at node %d | DESTINATION\n",
                   (int)msg->pid, cur);
        fflush(stdout);

        t->started      = true;
        t->fromNode     = cur;
        t->toNode       = nxt;
        t->animTimer    = 0.0f;
        t->x            = pos[cur].x;
        t->y            = pos[cur].y;
        t->animDuration = (nxt >= 0)
                          ? (float)edgeWeight(g, cur, nxt) * EDGE_STEP_SEC
                          : 0.0f;
    } else { /* MSG_FINISHED */
        printf("[PID=%d] finished\n", (int)msg->pid);
        fflush(stdout);
        t->done   = true;
        t->toNode = -1;
        (*doneCount)++;
        kill(pids[idx], SIGTERM);
        waitpid(pids[idx], NULL, 0);
        t->waited = true;
    }
}

/* ── main ─────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <graph_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    TravelerReq *travelers = NULL;
    int numTravelers = 0;
    Graph *g = readGraphFromFile(argv[1], &travelers, &numTravelers);
    if (!g) return EXIT_FAILURE;

    /* Create one pipe per traveler */
    int (*pipes)[2] = malloc(sizeof(int[2]) * numTravelers);
    if (!pipes) { freeGraph(g); free(travelers); return EXIT_FAILURE; }
    for (int i = 0; i < numTravelers; i++)
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(EXIT_FAILURE); }

    pid_t *pids = malloc(sizeof(pid_t) * numTravelers);
    if (!pids) { freeGraph(g); free(travelers); free(pipes); return EXIT_FAILURE; }

    /* Fork children */
    for (int i = 0; i < numTravelers; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }

        if (pid == 0) {
            /* ── Child ── */
            /* Close all read ends and sibling write ends */
            for (int j = 0; j < numTravelers; j++) {
                close(pipes[j][0]);
                if (j != i) close(pipes[j][1]);
            }
            int wfd = pipes[i][1];
            int src = travelers[i].src;
            int dst = travelers[i].dst;

            /* Child independently computes its own Dijkstra path */
            DijkstraResult res = runDijkstra(g, src, dst);
            IPCMsg msg = { .pid = getpid() };

            if (!res.found || res.pathLength < 1) {
                msg.type        = MSG_FINISHED;
                msg.currentNode = src;
                msg.nextNode    = -1;
                write(wfd, &msg, sizeof msg);
            } else {
                for (int k = 0; k < res.pathLength; k++) {
                    msg.type        = MSG_ARRIVED;
                    msg.currentNode = res.path[k];
                    msg.nextNode    = (k+1 < res.pathLength)
                                     ? res.path[k+1] : -1;
                    write(wfd, &msg, sizeof msg);

                    if (k < res.pathLength - 1) {
                        int w = edgeWeight(g, res.path[k], res.path[k+1]);
                        usleep((unsigned)w * TRAVEL_USEC_PER_UNIT
                               + NODE_PAUSE_USEC);
                    }
                }
                msg.type        = MSG_FINISHED;
                msg.currentNode = res.path[res.pathLength-1];
                msg.nextNode    = -1;
                write(wfd, &msg, sizeof msg);
                freeResult(&res);
            }

            close(wfd);
            exit(EXIT_SUCCESS);
        } else {
            pids[i] = pid;
        }
    }

    /* ── Parent ── */
    /* Close write ends; set read ends non-blocking */
    for (int i = 0; i < numTravelers; i++) {
        close(pipes[i][1]);
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);
    }

    NodePos *pos = malloc(sizeof(NodePos) * g->numVertices);
    computePositions(pos, g->numVertices);

    M5Traveler *state = calloc(numTravelers, sizeof(M5Traveler));
    for (int i = 0; i < numTravelers; i++) {
        state[i].pid      = pids[i];
        state[i].src      = travelers[i].src;
        state[i].dst      = travelers[i].dst;
        state[i].fromNode = travelers[i].src;
        state[i].toNode   = -1;
        state[i].x        = pos[travelers[i].src].x;
        state[i].y        = pos[travelers[i].src].y;
#ifdef ENABLE_GUI
        state[i].color    = PALETTE[i % 10];
#endif
    }

    int doneCount = 0;

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra - Milestone 5 (IPC Pipes)");
    SetTargetFPS(TARGET_FPS);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        /* Drain all available messages from each pipe */
        for (int i = 0; i < numTravelers; i++) {
            if (state[i].done) continue;
            IPCMsg msg;
            while (read(pipes[i][0], &msg, sizeof msg) == (ssize_t)sizeof msg) {
                handleMsg(&msg, &state[i], pos, g, &doneCount, pids, i);
                if (state[i].done) break;
            }
        }

        /* Smooth animation toward next node */
        for (int i = 0; i < numTravelers; i++) {
            M5Traveler *t = &state[i];
            if (!t->started || t->done || t->toNode < 0) continue;
            t->animTimer += dt;
            float pct = (t->animDuration > 0.0f)
                        ? t->animTimer / t->animDuration : 1.0f;
            if (pct > 1.0f) pct = 1.0f;
            t->x = pos[t->fromNode].x
                   + pct * (pos[t->toNode].x - pos[t->fromNode].x);
            t->y = pos[t->fromNode].y
                   + pct * (pos[t->toNode].y - pos[t->fromNode].y);
        }

        BeginDrawing();
            drawScene(g, pos, state, numTravelers);
        EndDrawing();

        if (doneCount >= numTravelers) break;
    }

    CloseWindow();
#else
    /* Terminal-only: poll pipes with short sleep */
    while (doneCount < numTravelers) {
        for (int i = 0; i < numTravelers; i++) {
            if (state[i].done) continue;
            IPCMsg msg;
            while (read(pipes[i][0], &msg, sizeof msg) == (ssize_t)sizeof msg) {
                handleMsg(&msg, &state[i], pos, g, &doneCount, pids, i);
                if (state[i].done) break;
            }
        }
        usleep(5000);
    }
#endif

    /* Cleanup */
    for (int i = 0; i < numTravelers; i++) {
        close(pipes[i][0]);
        if (!state[i].waited) {
            kill(pids[i], SIGTERM);
            waitpid(pids[i], NULL, 0);
        }
    }
    free(pos);
    free(state);
    free(pids);
    free(pipes);
    free(travelers);
    freeGraph(g);
    return EXIT_SUCCESS;
}
