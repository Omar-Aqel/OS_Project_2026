#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "graph.h"


#define INF 1000000000


typedef struct {
    int *path;
    int  pathLength;
    int  totalWeight;
    int  found;
} DijkstraResult;

DijkstraResult runDijkstra(const Graph *g, int src, int dst);
void           printResult(const DijkstraResult *result, int src, int dst);
void           freeResult(DijkstraResult *result);

#endif