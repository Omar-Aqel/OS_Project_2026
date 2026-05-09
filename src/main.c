#include <stdio.h>
#include <stdlib.h>
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
    int querySrc, queryDst;
    Graph *g = readGraphFromFile(filename, &querySrc, &queryDst);
    if (g == NULL) return EXIT_FAILURE;
    DijkstraResult result = runDijkstra(g, querySrc, queryDst);
    printResult(&result, querySrc, queryDst);

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra Visualizer");
    SetTargetFPS(TARGET_FPS);
    VisContext *ctx = visCreate(g, &result, querySrc, queryDst);
    if (ctx == NULL) {
        fprintf(stderr, "Error: failed to create visualisation context\n");
        CloseWindow(); freeResult(&result); freeGraph(g);
        return EXIT_FAILURE;
    }
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        visUpdate(ctx, dt);
        BeginDrawing();
            visDraw(ctx);
        EndDrawing();
    }
    visFree(ctx);
    CloseWindow();
#endif

    freeResult(&result);
    freeGraph(g);
    return EXIT_SUCCESS;
}
