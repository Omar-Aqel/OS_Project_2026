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
    TravelerReq *travelers = NULL;
    int numTravelers = 0;

    Graph *g = readGraphFromFile(filename, &travelers, &numTravelers);
    if (g == NULL) return EXIT_FAILURE;

    if (numTravelers < 1) {
        fprintf(stderr, "Error: No travelers defined in graph file\n");
        freeGraph(g);
        free(travelers);
        return EXIT_FAILURE;
    }

    int querySrc = travelers[0].src;
    int queryDst = travelers[0].dst;
    
    DijkstraResult result = runDijkstra(g, querySrc, queryDst);
    printResult(&result, querySrc, queryDst);

#ifdef ENABLE_GUI
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Dijkstra Visualizer - Milestone 3");
    SetTargetFPS(TARGET_FPS);

    VisContext *ctx = visCreate(g, travelers, &result, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Error: failed to create visualisation context\n");
        CloseWindow(); 
        freeResult(&result); 
        freeGraph(g); 
        free(travelers);
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
    free(travelers);
    return EXIT_SUCCESS;
}