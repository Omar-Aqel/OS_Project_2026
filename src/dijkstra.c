#include <stdio.h>
#include <stdlib.h>
#include "dijkstra.h"

static int minUnvisited(const int *dist, const int *visited, int N) {
    int minDist = INF, minIndex = -1;
    for (int i = 0; i < N; i++)
        if (!visited[i] && dist[i] < minDist) {
            minDist = dist[i]; minIndex = i;
        }
    return minIndex;
}

static int *reconstructPath(const int *prev, int src, int dst, int N, int *pathLength) {
    int count = 0, cur = dst;
    while (cur != -1) { count++; cur = prev[cur]; if (count > N) break; }
    int *path = (int *)malloc(sizeof(int) * count);
    if (path == NULL) { fprintf(stderr, "Error: memory allocation failed for path\n"); return NULL; }
    cur = dst;
    for (int i = count - 1; i >= 0; i--) { path[i] = cur; cur = prev[cur]; }
    *pathLength = count;
    return path;
}

DijkstraResult runDijkstra(const Graph *g, int src, int dst) {
    DijkstraResult result = {NULL, 0, 0, 0};
    int N = g->numVertices;
    if (src == dst) {
        result.path = (int *)malloc(sizeof(int));
        if (!result.path) { fprintf(stderr, "Error: memory allocation failed\n"); return result; }
        result.path[0] = src; result.pathLength = 1; result.found = 1;
        return result;
    }
    int *dist    = (int *)malloc(sizeof(int) * N);
    int *prev    = (int *)malloc(sizeof(int) * N);
    int *visited = (int *)malloc(sizeof(int) * N);
    if (!dist || !prev || !visited) {
        fprintf(stderr, "Error: memory allocation failed in Dijkstra\n");
        free(dist); free(prev); free(visited); return result;
    }
    for (int i = 0; i < N; i++) { dist[i] = INF; prev[i] = -1; visited[i] = 0; }
    dist[src] = 0;
    for (int iteration = 0; iteration < N; iteration++) {
        int u = minUnvisited(dist, visited, N);
        if (u == -1 || u == dst) break;
        visited[u] = 1;
        Edge *edge = g->adjList[u].head;
        while (edge != NULL) {
            int v = edge->dest, w = edge->weight;
            if (!visited[v] && dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w; prev[v] = u;
            }
            edge = edge->next;
        }
    }
    if (dist[dst] == INF) { free(dist); free(prev); free(visited); return result; }
    result.path = reconstructPath(prev, src, dst, N, &result.pathLength);
    if (result.path) { result.totalWeight = dist[dst]; result.found = 1; }
    free(dist); free(prev); free(visited);
    return result;
}

void printResult(const DijkstraResult *result, int src, int dst) {
    if (src == dst) { printf("%d\n0\n", src); return; }
    if (!result->found) { printf("No path found\n"); return; }
    for (int i = 0; i < result->pathLength; i++) {
        if (i > 0) printf(" -> ");
        printf("%d", result->path[i]);
    }
    printf("\n%d\n", result->totalWeight);
}

void freeResult(DijkstraResult *result) {
    if (result == NULL) return;
    free(result->path);
    result->path = NULL;
    result->pathLength = 0;
}
