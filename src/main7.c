/* ================================================================== *
 * Milestone 7 — Scheduling Algorithms (FCFS & SJF)
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

typedef enum { MSG_WAITING, MSG_ARRIVED, MSG_DEPARTED, MSG_FINISHED } MsgType;

typedef struct {
    int     traveler;
    MsgType type;
    int     node;
    int     next;
    int     from;
} PipeMsg;


#define EDGE_MS  600    
#define DWELL_MS 1000

typedef enum { ALGO_FCFS, ALGO_SJF } SchedAlgo;

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ *
 * Child Process: NO Semaphores. Uses Wake-up Pipes from Parent.
 * ------------------------------------------------------------------ */
static void childRun(int id, const DijkstraResult *res, const Graph *g,
                     int writeFd, int wakeFd) {
    if (!res->found || res->pathLength < 1) {
        PipeMsg m = { id, MSG_FINISHED, -1, -1, -1 };
        write(writeFd, &m, sizeof(m));
        _exit(0);
    }

    int prev = res->path[0];
    for (int i = 0; i < res->pathLength; i++) {
        int node = res->path[i];
        int next = (i + 1 < res->pathLength) ? res->path[i + 1] : -1;

        // 1. Tell parent we want to enter
        PipeMsg w = { id, MSG_WAITING, node, next, prev };
        write(writeFd, &w, sizeof(w));
        printf("[traveler %d | pid %d] waiting for node %d\n", id, getpid(), node);
        fflush(stdout);

        // 2. Freeze here until parent sends a wake-up signal over the pipe
        char goSignal;
        read(wakeFd, &goSignal, 1); 

        // 3. We are inside!
        PipeMsg arr = { id, MSG_ARRIVED, node, next, prev };
        write(writeFd, &arr, sizeof(arr));
        printf("[traveler %d | pid %d] entered node %d | next: %d\n", id, getpid(), node, next);
        fflush(stdout);

        sleep_ms(DWELL_MS); // Dwell 1 second

        if (next != -1) {
            // 4. Leave node, tell parent so they can wake the next waiting car
            PipeMsg dep = { id, MSG_DEPARTED, node, next, prev };
            write(writeFd, &dep, sizeof(dep));
            
            int wgt = 1;
            Edge *e = g->adjList[node].head;
            while (e && e->dest != next) e = e->next;
            if (e) wgt = e->weight;
            sleep_ms((long)wgt * EDGE_MS); 
            prev = node;
        } else {
            PipeMsg fin = { id, MSG_FINISHED, node, -1, prev };
            write(writeFd, &fin, sizeof(fin));
            printf("[traveler %d | pid %d] finished at node %d\n", id, getpid(), node);
            fflush(stdout);
        }
    }
    _exit(0);
}

/* ------------------------------------------------------------------ *
 * Scheduling Logic (Parent)
 * ------------------------------------------------------------------ */
// Helper to pick the next traveler from a node's wait queue
int pickNextTraveler(int queue[], int qSize, SchedAlgo algo, DijkstraResult *results) {
    if (qSize == 0) return -1;
    
    if (algo == ALGO_FCFS) {
        return 0; // First Come First Serve: always pick the first in queue
    } 
    else { // ALGO_SJF
        // Shortest Job First: Pick traveler with smallest totalWeight
        int bestIdx = 0;
        int minWeight = results[queue[0]].totalWeight;
        for (int i = 1; i < qSize; i++) {
            int w = results[queue[i]].totalWeight;
            if (w < minWeight) {
                minWeight = w;
                bestIdx = i;
            }
        }
        return bestIdx;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <fcfs|sjf> <graph_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    SchedAlgo algo = ALGO_FCFS;
    if (strcmp(argv[1], "sjf") == 0) algo = ALGO_SJF;
    else if (strcmp(argv[1], "fcfs") != 0) {
        fprintf(stderr, "Invalid algorithm. Use 'fcfs' or 'sjf'.\n");
        return EXIT_FAILURE;
    }

    TravelerReq *travelers = NULL;
    int numTravelers = 0;
    Graph *g = readGraphFromFile(argv[2], &travelers, &numTravelers);
    if (!g) return EXIT_FAILURE;

    DijkstraResult *results = malloc(sizeof(DijkstraResult) * numTravelers);
    for (int i = 0; i < numTravelers; i++)
        results[i] = runDijkstra(g, travelers[i].src, travelers[i].dst);

    int N = g->numVertices;

    // Wait Queues logic for Parent Scheduler
    int **waitQueues = malloc(sizeof(int*) * N);
    int *qSize = calloc(N, sizeof(int));
    bool *nodeOccupied = calloc(N, sizeof(bool));
    for(int i=0; i<N; i++) waitQueues[i] = malloc(sizeof(int) * numTravelers);

    // Pipes: pipes[child_id] (Child -> Parent), wakePipes[child_id] (Parent -> Child)
    int (*pipes)[2] = malloc(sizeof(int[2]) * numTravelers);
    int (*wakePipes)[2] = malloc(sizeof(int[2]) * numTravelers);
    pid_t *pids = malloc(sizeof(pid_t) * numTravelers);

    for (int i = 0; i < numTravelers; i++) {
        pipe(pipes[i]);
        pipe(wakePipes[i]);
        
        pid_t pid = fork();
        if (pid == 0) {
            for (int k = 0; k <= i; k++) { close(pipes[k][0]); close(wakePipes[k][1]); }
            childRun(i, &results[i], g, pipes[i][1], wakePipes[i][0]);
            _exit(0);
        } else {
            pids[i] = pid;
            close(pipes[i][1]); close(wakePipes[i][0]);
            int fl = fcntl(pipes[i][0], F_GETFL, 0);
            fcntl(pipes[i][0], F_SETFL, fl | O_NONBLOCK);
        }
    }

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    char title[100];
    sprintf(title, "Milestone 7 - Algorithm: %s", (algo==ALGO_FCFS) ? "FCFS" : "SJF");
    InitWindow(WIN_W, WIN_H, title);
    SetTargetFPS(TARGET_FPS);

    VisContext *ctx = visCreate(g, travelers, results, numTravelers);
    ctx->playing = true;

  while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // ==========================================
        // Phase 1: Read all incoming messages first (Batching)
        // ==========================================
        for (int i = 0; i < numTravelers; i++) {
            PipeMsg m;
            while (read(pipes[i][0], &m, sizeof(m)) == (ssize_t)sizeof(m)) {
                TravelerState *t = &ctx->travelers[m.traveler];
                int node = m.node;

                switch (m.type) {
                    case MSG_WAITING:
                        t->status = TS_WAITING; t->waitNode = node; t->fromNode = m.from;
                        // Place the traveler in the queue only; do not grant the node immediately!
                        waitQueues[node][qSize[node]++] = m.traveler;
                        break;

                    case MSG_ARRIVED:
                        t->status = TS_IN_NODE; t->waitNode = -1; t->fromNode = node;
                        ctx->nodeOccupied[node] = true;
                        nodeOccupied[node] = true; // Confirm node occupancy
                        for (int k = 0; k < t->result->pathLength; k++)
                            if (t->result->path[k] == node) { t->pathIdx = k; break; }
                        t->animState = ANIM_PAUSE_AT_NODE; t->pauseTimer = 0.0f;
                        t->entityX = ctx->positions[node].x; t->entityY = ctx->positions[node].y;
                        break;

                    case MSG_DEPARTED:
                    case MSG_FINISHED:
                        if (m.type == MSG_DEPARTED) {
                            t->status = TS_MOVING; t->animState = ANIM_MOVE_ON_EDGE; t->edgeTimer = 0.0f;
                            Edge *e = g->adjList[node].head;
                            while (e && e->dest != m.next) e = e->next;
                            t->totalEdgeSteps = e ? e->weight : 1;
                        } else {
                            t->status = TS_NORMAL; t->animState = ANIM_DONE;
                        }
                        // Clear node occupancy only
                        nodeOccupied[node] = false;
                        ctx->nodeOccupied[node] = false;
                        break;
                }
            }
        }

        // ==========================================
        // Phase 2: Schedule and wake up the winner (Scheduling)
        // ==========================================
        for (int node = 0; node < N; node++) {
            // If the node is free and there are travelers in its queue
            if (!nodeOccupied[node] && qSize[node] > 0) {
                nodeOccupied[node] = true; // Reserve it immediately for the winner
                
                // Apply the algorithm (SJF or FCFS) to pick the best from the queue
                int bestIdx = pickNextTraveler(waitQueues[node], qSize[node], algo, results);
                int luckyTraveler = waitQueues[node][bestIdx];

                printf("\n--- Scheduler Decision for Node %d ---\n", node);
                
                // 1. מי ממתין? (whos waiting؟)
                printf("Waiting travelers in queue: ");
                for(int k = 0; k < qSize[node]; k++) {
                    int t_id = waitQueues[node][k];
                    if (algo == ALGO_SJF) {
                        printf("[ID: %d, Weight: %d] ", t_id, results[t_id].totalWeight);
                    } else {
                        printf("[ID: %d] ", t_id);
                    }
                }
                printf("\n");

                // 2. מי נבחר ולמה? (who did we chose and why ?)
                if (algo == ALGO_FCFS) {
                    printf("Chosen: Traveler %d\n", luckyTraveler);
                    printf("Reason: FCFS (First-Come, First-Served) - Arrived first in the queue.\n");
                } else {
                    printf("Chosen: Traveler %d\n", luckyTraveler);
                    printf("Reason: SJF (Shortest Job First) - Has the minimum total path weight (%d).\n", results[luckyTraveler].totalWeight);
                }
                printf("--------------------------------------\n");
                fflush(stdout);

                // Remove the winner from the queue 
                for (int k = bestIdx; k < qSize[node]-1; k++) {
                    waitQueues[node][k] = waitQueues[node][k+1];
                }
                qSize[node]--;

                // Send the wake-up signal to the winner
                write(wakePipes[luckyTraveler][1], "G", 1);
            }
        }

        visInterpolate(ctx, dt);
        BeginDrawing(); visDraw(ctx); EndDrawing();
    }
    visFree(ctx); CloseWindow();
#endif

    // Cleanup
    for (int i = 0; i < numTravelers; i++) {
        kill(pids[i], SIGTERM); waitpid(pids[i], NULL, 0);
        close(pipes[i][0]); close(wakePipes[i][1]); freeResult(&results[i]);
    }
    for(int i=0; i<N; i++) free(waitQueues[i]);
    free(waitQueues); free(qSize); free(nodeOccupied);
    free(pipes); free(wakePipes); free(pids); free(results); free(travelers); freeGraph(g);
    return EXIT_SUCCESS;
}