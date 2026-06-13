/* ================================================================== *
 *  Milestone 6 — Node-access synchronization
 * ================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include "graph.h"
#include "dijkstra.h"

#ifdef ENABLE_GUI
#include "raylib.h"
#include "visualization.h"
#endif

/* ---- pipe message protocol (milestone 6) ------------------------- */
typedef enum { MSG_WAITING, MSG_ARRIVED, MSG_DEPARTED, MSG_FINISHED } MsgType;

typedef struct {
    int     traveler;
    MsgType type;
    int     node;
    int     next;
    int     from;     /* node it is coming from (for the waiting marker) */
} PipeMsg;

#define EDGE_MS  300    /* ms per weight unit (slow demo); matches renderer */
#define DWELL_MS 1000    /* node dwell stays exactly 1 s   */

/* Safe millisecond sleep (usleep is undefined for >= 1s; nanosleep isn't). */
static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void childRun(int id, const DijkstraResult *res, const Graph *g,
                     int writeFd, sem_t *nodeSems) {
    if (!res->found || res->pathLength < 1) {
        PipeMsg m = { id, MSG_FINISHED, -1, -1, -1 };
        write(writeFd, &m, sizeof(m));
        _exit(0);
    }

    int prev = res->path[0];
    for (int i = 0; i < res->pathLength; i++) {
        int node = res->path[i];
        int next = (i + 1 < res->pathLength) ? res->path[i + 1] : -1;

        /* ---- announce we WANT this node, then block on its lock ---- */
        PipeMsg w = { id, MSG_WAITING, node, next, prev };
        write(writeFd, &w, sizeof(w));
        printf("[traveler %d | pid %d] waiting for node %d\n", id, getpid(), node);
        fflush(stdout);

        sem_wait(&nodeSems[node]);          /* === ENTER CRITICAL SECTION === */

        PipeMsg arr = { id, MSG_ARRIVED, node, next, prev };
        write(writeFd, &arr, sizeof(arr));
        printf("[traveler %d | pid %d] entered node %d | next: %d\n",
               id, getpid(), node, next);
        fflush(stdout);

        sleep_ms(DWELL_MS);                 /* dwell exactly 1 s inside */

        if (next != -1) {
            /* release THIS node before requesting the next -> no hold&wait */
            PipeMsg dep = { id, MSG_DEPARTED, node, next, prev };
            write(writeFd, &dep, sizeof(dep));
            sem_post(&nodeSems[node]);       /* === LEAVE CRITICAL SECTION === */

            int wgt = 1;
            Edge *e = g->adjList[node].head;
            while (e && e->dest != next) e = e->next;
            if (e) wgt = e->weight;
            sleep_ms((long)wgt * EDGE_MS);   /* travel = weight * EDGE_MS */
            prev = node;
        } else {
            /* destination reached: release the lock so others may use it */
            sem_post(&nodeSems[node]);
            PipeMsg fin = { id, MSG_FINISHED, node, -1, prev };
            write(writeFd, &fin, sizeof(fin));
            printf("[traveler %d | pid %d] finished at node %d\n",
                   id, getpid(), node);
            fflush(stdout);
        }
    }
    _exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <graph_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    TravelerReq *travelers = NULL;
    int numTravelers = 0;
    Graph *g = readGraphFromFile(argv[1], &travelers, &numTravelers);
    if (!g) return EXIT_FAILURE;
    if (numTravelers < 1) {
        fprintf(stderr, "Error: no travelers defined\n");
        freeGraph(g); free(travelers); return EXIT_FAILURE;
    }

    DijkstraResult *results = malloc(sizeof(DijkstraResult) * numTravelers);
    if (!results) { freeGraph(g); free(travelers); return EXIT_FAILURE; }
    for (int i = 0; i < numTravelers; i++)
        results[i] = runDijkstra(g, travelers[i].src, travelers[i].dst);

    /* ---- shared semaphore array: one per node, init value 1 ---- */
    int N = g->numVertices;
    sem_t *nodeSems = mmap(NULL, sizeof(sem_t) * N,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (nodeSems == MAP_FAILED) { perror("mmap"); return EXIT_FAILURE; }
    for (int i = 0; i < N; i++)
        sem_init(&nodeSems[i], 1 /* shared between processes */, 1);

    int (*pipes)[2] = malloc(sizeof(int[2]) * numTravelers);
    pid_t *pids     = malloc(sizeof(pid_t) * numTravelers);
    if (!pipes || !pids) { fprintf(stderr,"alloc failed\n"); return EXIT_FAILURE; }

    for (int i = 0; i < numTravelers; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(EXIT_FAILURE); }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        else if (pid == 0) {
            for (int k = 0; k <= i; k++) close(pipes[k][0]);
            childRun(i, &results[i], g, pipes[i][1], nodeSems);
            _exit(0);
        } else {
            pids[i] = pid;
            close(pipes[i][1]);
            int fl = fcntl(pipes[i][0], F_GETFL, 0);
            fcntl(pipes[i][0], F_SETFL, fl | O_NONBLOCK);
        }
    }

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra - Milestone 6 (Synchronization)");
    SetTargetFPS(TARGET_FPS);

    VisContext *ctx = visCreate(g, travelers, results, numTravelers);
    if (!ctx) { CloseWindow(); return EXIT_FAILURE; }
    ctx->playing = true;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        for (int i = 0; i < numTravelers; i++) {
            PipeMsg m;
            while (read(pipes[i][0], &m, sizeof(m)) == (ssize_t)sizeof(m)) {
                TravelerState *t = &ctx->travelers[m.traveler];
                switch (m.type) {
                    case MSG_WAITING:
                        t->status   = TS_WAITING;
                        t->waitNode = m.node;
                        t->fromNode = m.from;
                        break;
                    case MSG_ARRIVED:
                        t->status   = TS_IN_NODE;
                        t->waitNode = -1;
                        t->fromNode = m.node;
                        ctx->nodeOccupied[m.node] = true;
                        for (int k = 0; k < t->result->pathLength; k++)
                            if (t->result->path[k] == m.node) { t->pathIdx = k; break; }
                        t->animState  = ANIM_PAUSE_AT_NODE;
                        t->pauseTimer = 0.0f;
                        t->entityX = ctx->positions[m.node].x;
                        t->entityY = ctx->positions[m.node].y;
                        break;
                    case MSG_DEPARTED:
                        ctx->nodeOccupied[m.node] = false;
                        t->status    = TS_MOVING;
                        t->animState = ANIM_MOVE_ON_EDGE;
                        t->edgeTimer = 0.0f;
                        { Edge *e = g->adjList[m.node].head;
                          while (e && e->dest != m.next) e = e->next;
                          t->totalEdgeSteps = e ? e->weight : 1; }
                        break;
                    case MSG_FINISHED:
                        ctx->nodeOccupied[m.node] = false;
                        t->status    = TS_NORMAL;
                        t->animState = ANIM_DONE;
                        break;
                }
            }
        }

        /* the engine does the position math (separation of concerns) */
        visInterpolate(ctx, dt);

        BeginDrawing();
            visDraw(ctx);
        EndDrawing();
    }

    visFree(ctx);
    CloseWindow();
#endif

    for (int i = 0; i < numTravelers; i++) {
        kill(pids[i], SIGTERM);
        waitpid(pids[i], NULL, 0);
        close(pipes[i][0]);
        freeResult(&results[i]);
    }
    for (int i = 0; i < N; i++) sem_destroy(&nodeSems[i]);
    munmap(nodeSems, sizeof(sem_t) * N);
    free(pipes); free(pids); free(results);
    free(travelers); freeGraph(g);
    return EXIT_SUCCESS;
}