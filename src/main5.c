/* ================================================================== *
 *  Milestone 5 — IPC with anonymous pipes
* ================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include "graph.h"
#include "dijkstra.h"

#ifdef ENABLE_GUI
#include "raylib.h"
#include "visualization.h"
#endif

/* ---- pipe message protocol (milestone 5) ------------------------- */
typedef enum { MSG_ARRIVED, MSG_DEPARTED, MSG_FINISHED, MSG_NO_PATH } MsgType;

typedef struct {
    int     traveler;   /* which traveler                 */
    MsgType type;
    int     node;       /* node arrived at / departed from */
    int     next;       /* next node (or -1)               */
} PipeMsg;

#define EDGE_MS  900     /* ms per weight unit, matches renderer  */
#define DWELL_MS 1000    /* ms dwell inside a node                */

/* Sleep for an arbitrary number of milliseconds.  usleep() is unsafe
   here because POSIX leaves it undefined for values >= 1,000,000 us
   (i.e. >= 1 s), which a heavy edge (weight*300ms) easily exceeds.
   nanosleep() has no such limit. */
static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ *
 *  Child process body: walk the path, narrate over the pipe.
 * ------------------------------------------------------------------ */
static void childRun(int id, const DijkstraResult *res,
                     const Graph *g, int writeFd) {
    if (!res->found || res->pathLength < 1) {
        PipeMsg m = { id, MSG_NO_PATH, -1, -1 }; // edit add : MSG_NO_PATH
        write(writeFd, &m, sizeof(m));
        _exit(0);
    }

    for (int i = 0; i < res->pathLength; i++) {
        int node = res->path[i];
        int next = (i + 1 < res->pathLength) ? res->path[i + 1] : -1;

        PipeMsg arr = { id, MSG_ARRIVED, node, next };
        write(writeFd, &arr, sizeof(arr));
        printf("[traveler %d | pid %d] arrived at node %d | next node: %d\n",
               id, getpid(), node, next);
        fflush(stdout);

        sleep_ms(DWELL_MS);               /* dwell 1 s inside the node */

        if (next != -1) {
            PipeMsg dep = { id, MSG_DEPARTED, node, next };
            write(writeFd, &dep, sizeof(dep));

            /* travel time = edge weight * 300ms */
            int w = 1;
            Edge *e = g->adjList[node].head;
            while (e && e->dest != next) e = e->next;
            if (e) w = e->weight;
            sleep_ms((long)w * EDGE_MS);   /* travel time = weight * EDGE_MS */
        }
    }

    PipeMsg fin = { id, MSG_FINISHED, res->path[res->pathLength - 1], -1 };
    write(writeFd, &fin, sizeof(fin));
    printf("[traveler %d | pid %d] finished\n", id, getpid());
    fflush(stdout);
    _exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <graph_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    TravelerReq *travelers = NULL;
    int numTravelers = 0;

    Graph *g = readGraphFromFile(filename, &travelers, &numTravelers);
    if (g == NULL) return EXIT_FAILURE;
    if (numTravelers < 1) {
        fprintf(stderr, "Error: no travelers defined\n");
        freeGraph(g); free(travelers); return EXIT_FAILURE;
    }

    DijkstraResult *results = malloc(sizeof(DijkstraResult) * numTravelers);
    if (!results) { freeGraph(g); free(travelers); return EXIT_FAILURE; }
    for (int i = 0; i < numTravelers; i++)
        results[i] = runDijkstra(g, travelers[i].src, travelers[i].dst);

    /* one pipe per traveler */
    int (*pipes)[2] = malloc(sizeof(int[2]) * numTravelers);
    pid_t *pids     = malloc(sizeof(pid_t) * numTravelers);
    if (!pipes || !pids) { fprintf(stderr,"alloc failed\n"); return EXIT_FAILURE; }

    for (int i = 0; i < numTravelers; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(EXIT_FAILURE); }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        else if (pid == 0) {
            /* child: close all other fds, keep its own write end */
            for (int k = 0; k <= i; k++) close(pipes[k][0]);
            childRun(i, &results[i], g, pipes[i][1]);
            _exit(0);
        } else {
            pids[i] = pid;
            close(pipes[i][1]);                 /* parent reads only */
            int fl = fcntl(pipes[i][0], F_GETFL, 0);
            fcntl(pipes[i][0], F_SETFL, fl | O_NONBLOCK);
        }
    }

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra - Milestone 5 (IPC Pipes)");
    SetTargetFPS(TARGET_FPS);

    VisContext *ctx = visCreate(g, travelers, results, numTravelers);
    if (!ctx) { CloseWindow(); return EXIT_FAILURE; }
    ctx->playing = true;          /* animation driven by message timing */

    bool *finished = calloc(numTravelers, sizeof(bool));

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        /* ---- drain pipes, translate IPC -> VisContext state ---- */
        for (int i = 0; i < numTravelers; i++) {
            PipeMsg m;
            ssize_t n;
            while ((n = read(pipes[i][0], &m, sizeof(m))) == (ssize_t)sizeof(m)) {
                TravelerState *t = &ctx->travelers[m.traveler];
                switch (m.type) {
                    case MSG_ARRIVED:
                        t->status   = TS_IN_NODE;
                        t->fromNode = m.node;
                        /* snap dot onto the node it just reached */
                        for (int k = 0; k < t->result->pathLength; k++)
                            if (t->result->path[k] == m.node) { t->pathIdx = k; break; }
                        t->animState = ANIM_PAUSE_AT_NODE;
                        t->pauseTimer = 0.0f;
                        t->entityX = ctx->positions[m.node].x;
                        t->entityY = ctx->positions[m.node].y;
                        break;
                    case MSG_DEPARTED:
                        t->status    = TS_MOVING;
                        t->animState = ANIM_MOVE_ON_EDGE;
                        t->edgeTimer = 0.0f;
                        { Edge *e = g->adjList[m.node].head;
                          while (e && e->dest != m.next) e = e->next;
                          t->totalEdgeSteps = e ? e->weight : 1; }
                        break;
                        case MSG_NO_PATH:
                        printf("[Parent Alert] Traveler %d has NO PATH to destination!\n", m.traveler);
                        t->status    = TS_NORMAL;
                        t->animState = ANIM_DONE;
                        finished[m.traveler] = true;
                        break;
                    case MSG_FINISHED:
                        t->status    = TS_NORMAL;
                        t->animState = ANIM_DONE;
                        finished[m.traveler] = true;
                        break;
                }
            }
        }

        /* the engine does the position math (separation of concerns):
           the brain above decided WHEN each traveler moves (via IPC);
           visInterpolate just advances the dot along its current edge. */
        visInterpolate(ctx, dt);

        BeginDrawing();
            visDraw(ctx);
        EndDrawing();
    }

    visFree(ctx);
    CloseWindow();
    free(finished);
#endif

    for (int i = 0; i < numTravelers; i++) {
        kill(pids[i], SIGTERM);
        waitpid(pids[i], NULL, 0);
        close(pipes[i][0]);
        freeResult(&results[i]);
    }
    free(pipes); free(pids); free(results);
    free(travelers); freeGraph(g);
    return EXIT_SUCCESS;
}