#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include "graph.h"
#include "dijkstra.h"

#ifdef ENABLE_GUI
#include "raylib.h"
#endif

/* ── IPC message types ────────────────────────────────── */
typedef enum {
    MSG_WAITING,   /* child wants to enter a node (may block on sem) */
    MSG_ARRIVED,   /* child acquired semaphore — now inside node      */
    MSG_DEPARTED,  /* child released semaphore — now traveling        */
    MSG_FINISHED   /* child completed its entire journey              */
} MsgType;

typedef struct {
    MsgType type;
    pid_t   pid;
    int     node;      /* relevant node                */
    int     nextNode;  /* -1 = destination / not used  */
} IPCMsg;

/* ── Timing ──────────────────────────────────────────── */
#define TRAVEL_USEC_PER_UNIT  300000u   /* 300 ms per weight unit */
#define NODE_STAY_USEC       1000000u   /* 1 s in node (critical section) */
#define EDGE_STEP_SEC          0.30f    /* GUI: seconds per weight unit */

/* ── GUI layout ──────────────────────────────────────── */
#define WIN_W       1100
#define WIN_H        700
#define TARGET_FPS    60
#define NODE_RADIUS   22

/* ── Traveler visual states ──────────────────────────── */
typedef enum {
    TS_IDLE,
    TS_WAITING,   /* blocked outside node — different colour */
    TS_IN_NODE,   /* inside node (critical section) */
    TS_MOVING,    /* traveling between nodes */
    TS_DONE
} TravelerStatus;

typedef struct {
    pid_t          pid;
    int            src, dst;
    int            fromNode;       /* last node that was entered */
    int            toNode;         /* next node we are heading to */
    float          animTimer;
    float          animDuration;
    float          x, y;
    TravelerStatus status;
    bool           waited;
#ifdef ENABLE_GUI
    Color          color;
#endif
} M6Traveler;

typedef struct { float x, y; } NodePos;

/* ── Helpers ─────────────────────────────────────────── */
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

/* ── GUI drawing ─────────────────────────────────────── */
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

static Color dimColor(Color c) {
    return (Color){(unsigned char)(c.r/4), (unsigned char)(c.g/4),
                   (unsigned char)(c.b/4), 220};
}

static void drawArrow(float x1, float y1, float x2, float y2, Color col) {
    float dx = x2-x1, dy = y2-y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) return;
    float nx = dx/len, ny = dy/len;
    float sx = x1+nx*(NODE_RADIUS+4), sy = y1+ny*(NODE_RADIUS+4);
    float ex = x2-nx*(NODE_RADIUS+4), ey = y2-ny*(NODE_RADIUS+4);
    DrawLineEx((Vector2){sx,sy},(Vector2){ex,ey},2.0f,col);
    float ang=atan2f(ny,nx), h=12.0f, ha=0.45f;
    DrawLineEx((Vector2){ex,ey},(Vector2){ex-h*cosf(ang-ha),ey-h*sinf(ang-ha)},2.0f,col);
    DrawLineEx((Vector2){ex,ey},(Vector2){ex-h*cosf(ang+ha),ey-h*sinf(ang+ha)},2.0f,col);
}

static void drawScene6(const Graph *g, const NodePos *pos,
                       const M6Traveler *tv, int nT,
                       const bool *nodeOccupied) {
    ClearBackground((Color){15,17,26,255});
    DrawRectangle(WIN_W-240, 0, 240, WIN_H, (Color){20,22,34,255});

    /* edges */
    for (int u = 0; u < g->numVertices; u++)
        for (Edge *e = g->adjList[u].head; e; e = e->next) {
            int v = e->dest;
            drawArrow(pos[u].x, pos[u].y, pos[v].x, pos[v].y, (Color){100,110,130,255});
            float mx=(pos[u].x+pos[v].x)/2.0f, my=(pos[u].y+pos[v].y)/2.0f;
            char buf[16]; snprintf(buf,sizeof buf,"%d",e->weight);
            int tw=MeasureText(buf,13);
            DrawText(buf,(int)(mx-tw/2),(int)(my-8),13,(Color){170,180,200,255});
        }

    /* nodes — occupied nodes get a bright red double ring */
    for (int i = 0; i < g->numVertices; i++) {
        Color border = nodeOccupied[i]
                       ? (Color){255,60,60,255}
                       : (Color){100,110,160,255};
        DrawCircle((int)pos[i].x,(int)pos[i].y,NODE_RADIUS,(Color){40,44,62,255});
        DrawCircleLines((int)pos[i].x,(int)pos[i].y,NODE_RADIUS,border);
        if (nodeOccupied[i])
            DrawCircleLines((int)pos[i].x,(int)pos[i].y,NODE_RADIUS+4,border);
        char lbl[8]; snprintf(lbl,sizeof lbl,"%d",i);
        int lw=MeasureText(lbl,16);
        DrawText(lbl,(int)(pos[i].x-lw/2),(int)(pos[i].y-8),16,(Color){220,225,240,255});
    }

    /* travelers */
    for (int i = 0; i < nT; i++) {
        if (tv[i].status==TS_IDLE || tv[i].status==TS_DONE) continue;
        bool waiting = (tv[i].status == TS_WAITING);
        Color col    = waiting ? dimColor(tv[i].color) : tv[i].color;
        int   r      = waiting ? 10 : 14;
        DrawCircle((int)tv[i].x,(int)tv[i].y,r,col);
        DrawCircleLines((int)tv[i].x,(int)tv[i].y,r,WHITE);
        if (waiting)
            DrawCircleLines((int)tv[i].x,(int)tv[i].y,r+5,col); /* extra ring for "waiting" */
        char lbl[8]; snprintf(lbl,sizeof lbl,"T%d",i);
        int lw=MeasureText(lbl,10);
        DrawText(lbl,(int)tv[i].x-lw/2,(int)tv[i].y-5,10,WHITE);
    }

    /* side panel */
    int px=WIN_W-228, py=24;
    DrawText("DIJKSTRA",px,py,22,(Color){80,200,120,255});          py+=26;
    DrawText("SYNC - MILESTONE 6",px,py,14,(Color){170,180,200,255}); py+=24;
    DrawLine(px,py,WIN_W-12,py,(Color){100,110,160,255});            py+=16;

    for (int i=0;i<nT;i++) {
        const char *st = tv[i].status==TS_DONE    ? "Done"    :
                         tv[i].status==TS_WAITING  ? "Waiting" :
                         tv[i].status==TS_IN_NODE  ? "In Node" :
                         tv[i].status==TS_MOVING   ? "Moving"  : "Idle";
        char buf[64];
        snprintf(buf,sizeof buf,"T%d: %d->%d [%s]",i,tv[i].src,tv[i].dst,st);
        DrawText(buf,px,py,13,tv[i].color); py+=22;
    }

    py+=8;
    DrawLine(px,py,WIN_W-12,py,(Color){100,110,160,255}); py+=14;
    DrawText("IPC: anonymous pipes",    px,py,12,(Color){170,180,200,255}); py+=16;
    DrawText("Sync: POSIX semaphores",  px,py,12,(Color){170,180,200,255}); py+=16;
    DrawText("(mmap shared memory)",    px,py,12,(Color){170,180,200,255}); py+=20;
    DrawText("Red ring = node occupied",px,py,12,(Color){255,60,60,255});   py+=16;
    DrawText("Dim+ring = waiting",      px,py,12,(Color){160,160,160,255});

    char fpsBuf[32]; snprintf(fpsBuf,sizeof fpsBuf,"FPS: %d",GetFPS());
    DrawText(fpsBuf,WIN_W-228,WIN_H-24,13,(Color){170,180,200,255});
}
#endif /* ENABLE_GUI */

/* ── Process one IPC message ─────────────────────────── */
static void handleMsg6(const IPCMsg *msg, M6Traveler *t,
                       const NodePos *pos, const Graph *g,
                       bool *nodeOccupied,
                       int *doneCount, pid_t *pids, int idx) {
    switch (msg->type) {

    case MSG_WAITING:
        printf("[PID=%d] waiting for node %d\n", (int)msg->pid, msg->node);
        fflush(stdout);
        /* Place traveler near the target node (75% of the way from last known pos) */
        if (t->status != TS_MOVING) {
            if (t->fromNode >= 0) {
                t->x = pos[t->fromNode].x
                       + 0.75f*(pos[msg->node].x - pos[t->fromNode].x);
                t->y = pos[t->fromNode].y
                       + 0.75f*(pos[msg->node].y - pos[t->fromNode].y);
            } else {
                /* first node — place at node position */
                t->x = pos[msg->node].x;
                t->y = pos[msg->node].y;
            }
        }
        /* If already MOVING toward the node, keep current animated position */
        t->status = TS_WAITING;
        t->toNode = msg->node;
        break;

    case MSG_ARRIVED:
        if (msg->nextNode >= 0)
            printf("[PID=%d] arrived at node %d | next node: %d\n",
                   (int)msg->pid, msg->node, msg->nextNode);
        else
            printf("[PID=%d] arrived at node %d | DESTINATION\n",
                   (int)msg->pid, msg->node);
        fflush(stdout);
        t->status   = TS_IN_NODE;
        t->fromNode = msg->node;
        t->toNode   = msg->nextNode;
        t->x        = pos[msg->node].x;
        t->y        = pos[msg->node].y;
        nodeOccupied[msg->node] = true;
        break;

    case MSG_DEPARTED:
        nodeOccupied[msg->node] = false;
        if (msg->nextNode >= 0) {
            t->status       = TS_MOVING;
            t->animTimer    = 0.0f;
            t->animDuration = (float)edgeWeight(g, msg->node, msg->nextNode)
                              * EDGE_STEP_SEC;
            t->toNode       = msg->nextNode;
        }
        /* if nextNode == -1 (destination), stay at IN_NODE until MSG_FINISHED */
        break;

    case MSG_FINISHED:
        printf("[PID=%d] finished\n", (int)msg->pid);
        fflush(stdout);
        if (t->fromNode >= 0) nodeOccupied[t->fromNode] = false;
        t->status = TS_DONE;
        (*doneCount)++;
        kill(pids[idx], SIGTERM);
        waitpid(pids[idx], NULL, 0);
        t->waited = true;
        break;
    }
}

/* ── main ────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <graph_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    TravelerReq *travelers = NULL;
    int numTravelers = 0;
    Graph *g = readGraphFromFile(argv[1], &travelers, &numTravelers);
    if (!g) return EXIT_FAILURE;

    int numNodes = g->numVertices;

    /* ── Shared-memory semaphores (one per node, binary, initial=1) ── */
    sem_t *node_sems = mmap(NULL, sizeof(sem_t) * numNodes,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (node_sems == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }
    for (int i = 0; i < numNodes; i++)
        sem_init(&node_sems[i], 1 /* pshared */, 1 /* initial */);

    /* ── Pipes (one per traveler, child writes, parent reads) ── */
    int (*pipes)[2] = malloc(sizeof(int[2]) * numTravelers);
    if (!pipes) { freeGraph(g); free(travelers); return EXIT_FAILURE; }
    for (int i = 0; i < numTravelers; i++)
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(EXIT_FAILURE); }

    pid_t *pids = malloc(sizeof(pid_t) * numTravelers);
    if (!pids) { freeGraph(g); free(travelers); free(pipes); return EXIT_FAILURE; }

    /* ── Fork children ── */
    for (int i = 0; i < numTravelers; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }

        if (pid == 0) {
            /* ── CHILD PROCESS ── */
            for (int j = 0; j < numTravelers; j++) {
                close(pipes[j][0]);
                if (j != i) close(pipes[j][1]);
            }
            int wfd  = pipes[i][1];
            int src  = travelers[i].src;
            int dst  = travelers[i].dst;

            /* Child independently computes its own Dijkstra path */
            DijkstraResult res = runDijkstra(g, src, dst);
            IPCMsg msg = { .pid = getpid() };

            if (!res.found || res.pathLength < 1) {
                msg.type = MSG_FINISHED; msg.node = src; msg.nextNode = -1;
                write(wfd, &msg, sizeof msg);
            } else {
                for (int k = 0; k < res.pathLength; k++) {
                    int node = res.path[k];
                    int next = (k+1 < res.pathLength) ? res.path[k+1] : -1;

                    /* 1. Notify parent: about to try entering node */
                    msg.type = MSG_WAITING; msg.node = node; msg.nextNode = next;
                    write(wfd, &msg, sizeof msg);

                    /* 2. Acquire semaphore — CRITICAL SECTION ENTRY
                       Blocks here if another traveler is already in `node` */
                    sem_wait(&node_sems[node]);

                    /* 3. Notify parent: entered node */
                    msg.type = MSG_ARRIVED; msg.node = node; msg.nextNode = next;
                    write(wfd, &msg, sizeof msg);

                    /* 4. Stay in node for exactly 1 second (critical section) */
                    usleep(NODE_STAY_USEC);

                    /* 5. Release semaphore — CRITICAL SECTION EXIT */
                    sem_post(&node_sems[node]);

                    /* 6. Notify parent: left node */
                    msg.type = MSG_DEPARTED; msg.node = node; msg.nextNode = next;
                    write(wfd, &msg, sizeof msg);

                    /* 7. Travel to next node (outside critical section) */
                    if (next >= 0) {
                        int w = edgeWeight(g, node, next);
                        usleep((unsigned)w * TRAVEL_USEC_PER_UNIT);
                    }
                }
                msg.type = MSG_FINISHED;
                msg.node = res.path[res.pathLength - 1];
                msg.nextNode = -1;
                write(wfd, &msg, sizeof msg);
                freeResult(&res);
            }

            close(wfd);
            exit(EXIT_SUCCESS);
        } else {
            pids[i] = pid;
        }
    }

    /* ── PARENT PROCESS ── */
    for (int i = 0; i < numTravelers; i++) {
        close(pipes[i][1]);                          /* close write ends */
        fcntl(pipes[i][0], F_SETFL, O_NONBLOCK);    /* non-blocking reads */
    }

    NodePos *pos          = malloc(sizeof(NodePos) * numNodes);
    bool    *nodeOccupied = calloc(numNodes, sizeof(bool));
    computePositions(pos, numNodes);

    M6Traveler *state = calloc(numTravelers, sizeof(M6Traveler));
    for (int i = 0; i < numTravelers; i++) {
        state[i].pid      = pids[i];
        state[i].src      = travelers[i].src;
        state[i].dst      = travelers[i].dst;
        state[i].fromNode = -1;            /* not yet entered any node */
        state[i].toNode   = -1;
        state[i].x        = pos[travelers[i].src].x;
        state[i].y        = pos[travelers[i].src].y;
        state[i].status   = TS_IDLE;
#ifdef ENABLE_GUI
        state[i].color    = PALETTE[i % 10];
#endif
    }

    int doneCount = 0;

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra - Milestone 6 (Node Synchronization)");
    SetTargetFPS(TARGET_FPS);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        /* Drain all pending IPC messages from each pipe */
        for (int i = 0; i < numTravelers; i++) {
            if (state[i].status == TS_DONE) continue;
            IPCMsg msg;
            while (read(pipes[i][0], &msg, sizeof msg) == (ssize_t)sizeof msg) {
                handleMsg6(&msg, &state[i], pos, g,
                           nodeOccupied, &doneCount, pids, i);
                if (state[i].status == TS_DONE) break;
            }
        }

        /* Smooth movement animation */
        for (int i = 0; i < numTravelers; i++) {
            M6Traveler *t = &state[i];
            if (t->status != TS_MOVING || t->toNode < 0) continue;
            t->animTimer += dt;
            float pct = (t->animDuration > 0.0f)
                        ? t->animTimer / t->animDuration : 1.0f;
            if (pct > 1.0f) pct = 1.0f;
            t->x = pos[t->fromNode].x
                   + pct*(pos[t->toNode].x - pos[t->fromNode].x);
            t->y = pos[t->fromNode].y
                   + pct*(pos[t->toNode].y - pos[t->fromNode].y);
        }

        BeginDrawing();
            drawScene6(g, pos, state, numTravelers, nodeOccupied);
        EndDrawing();

        if (doneCount >= numTravelers) break;
    }

    CloseWindow();
#else
    /* Terminal-only: poll pipes */
    while (doneCount < numTravelers) {
        for (int i = 0; i < numTravelers; i++) {
            if (state[i].status == TS_DONE) continue;
            IPCMsg msg;
            while (read(pipes[i][0], &msg, sizeof msg) == (ssize_t)sizeof msg) {
                handleMsg6(&msg, &state[i], pos, g,
                           nodeOccupied, &doneCount, pids, i);
                if (state[i].status == TS_DONE) break;
            }
        }
        usleep(5000);
    }
#endif

    /* ── Cleanup ── */
    for (int i = 0; i < numNodes; i++) sem_destroy(&node_sems[i]);
    munmap(node_sems, sizeof(sem_t) * numNodes);

    for (int i = 0; i < numTravelers; i++) {
        close(pipes[i][0]);
        if (!state[i].waited) {
            kill(pids[i], SIGTERM);
            waitpid(pids[i], NULL, 0);
        }
    }
    free(nodeOccupied);
    free(pos);
    free(state);
    free(pids);
    free(pipes);
    free(travelers);
    freeGraph(g);
    return EXIT_SUCCESS;
}
