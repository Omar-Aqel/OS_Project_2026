#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include "graph.h"
#include "dijkstra.h"

#ifdef ENABLE_GUI
#include "raylib.h"
#include "visualization.h"
#endif

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

    DijkstraResult *results = malloc(sizeof(DijkstraResult) * numTravelers);
    if (!results && numTravelers > 0) { fprintf(stderr, "Error: memory allocation failed\n"); freeGraph(g); free(travelers); return EXIT_FAILURE; }

    for (int i = 0; i < numTravelers; i++) {
        results[i] = runDijkstra(g, travelers[i].src, travelers[i].dst);
    }

    pid_t *pids = malloc(sizeof(pid_t) * numTravelers);
    if (!pids && numTravelers > 0) { fprintf(stderr, "Error: memory allocation failed\n"); for (int i=0;i<numTravelers;i++) freeResult(&results[i]); free(results); freeGraph(g); free(travelers); return EXIT_FAILURE; }

    for (int i = 0; i < numTravelers; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork failed"); exit(EXIT_FAILURE); }
        else if (pid == 0) {
            printf("[%d] started\n", getpid());
            while (1) pause();
            exit(EXIT_SUCCESS);
        } else {
            pids[i] = pid;
        }
    }

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra - Milestone 4 (Multiple Travelers)");
    SetTargetFPS(TARGET_FPS);

    VisContext *ctx = visCreate(g, travelers, results, numTravelers);
    if (ctx == NULL) {
        fprintf(stderr, "Error: failed to create visualization context\n");
        CloseWindow();
        return EXIT_FAILURE;
    }

    bool *childTerminated = calloc(numTravelers, sizeof(bool));

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        visUpdate(ctx, dt);

        for (int i = 0; i < numTravelers; i++) {
            if (ctx->travelers[i].animState == ANIM_DONE && !childTerminated[i]) {
                kill(pids[i], SIGTERM);
                childTerminated[i] = true;
            }
        }

        BeginDrawing();
            visDraw(ctx);
        EndDrawing();
    }

    visFree(ctx);
    CloseWindow();
    free(childTerminated);
#endif

    for (int i = 0; i < numTravelers; i++) {
        kill(pids[i], SIGTERM);
        waitpid(pids[i], NULL, 0);
        freeResult(&results[i]);
    }

    free(pids);
    free(results);
    free(travelers);
    freeGraph(g);

    return EXIT_SUCCESS;
}
