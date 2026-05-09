#include <stdio.h>
#include <stdlib.h>
#include "graph.h"

Graph *createGraph(int numVertices) {
    if (numVertices <= 0) {
        fprintf(stderr, "Error: number of vertices must be positive\n");
        return NULL;
    }
    Graph *g = (Graph *)malloc(sizeof(Graph));
    if (g == NULL) {
        fprintf(stderr, "Error: memory allocation failed for Graph\n");
        return NULL;
    }
    g->numVertices = numVertices;
    g->adjList = (AdjNode *)malloc(sizeof(AdjNode) * numVertices);
    if (g->adjList == NULL) {
        fprintf(stderr, "Error: memory allocation failed for adjacency list\n");
        free(g);
        return NULL;
    }
    for (int i = 0; i < numVertices; i++)
        g->adjList[i].head = NULL;
    return g;
}

void addEdge(Graph *g, int src, int dest, int weight) {
    Edge *newEdge = (Edge *)malloc(sizeof(Edge));
    if (newEdge == NULL) {
        fprintf(stderr, "Error: memory allocation failed for edge\n");
        return;
    }
    newEdge->dest        = dest;
    newEdge->weight      = weight;
    newEdge->next        = g->adjList[src].head;
    g->adjList[src].head = newEdge;
}

void freeGraph(Graph *g) {
    if (g == NULL) return;
    for (int i = 0; i < g->numVertices; i++) {
        Edge *current = g->adjList[i].head;
        while (current != NULL) {
            Edge *temp = current;
            current    = current->next;
            free(temp);
        }
    }
    free(g->adjList);
    free(g);
}

Graph *readGraphFromFile(const char *filename, int *querySrc, int *queryDst) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return NULL;
    }
    int N, M;
    if (fscanf(file, "%d %d", &N, &M) != 2) {
        fprintf(stderr, "Error: invalid file format on line 1\n");
        fclose(file);
        return NULL;
    }
    if (N < 0 || M < 0) {
        fprintf(stderr, "Error: negative numbers are not allowed\n");
        fclose(file);
        return NULL;
    }
    Graph *g = createGraph(N);
    if (g == NULL) { fclose(file); return NULL; }
    for (int i = 0; i < M; i++) {
        int src, dest, weight;
        if (fscanf(file, "%d %d %d", &src, &dest, &weight) != 3) {
            fprintf(stderr, "Error: invalid edge format at edge %d\n", i+1);
            freeGraph(g); fclose(file); return NULL;
        }
        if (src < 0 || dest < 0 || weight < 0) {
            fprintf(stderr, "Error: negative numbers are not allowed\n");
            freeGraph(g); fclose(file); return NULL;
        }
        if (src >= N || dest >= N) {
            fprintf(stderr, "Error: vertex index out of range at edge %d\n", i+1);
            freeGraph(g); fclose(file); return NULL;
        }
        addEdge(g, src, dest, weight);
    }
    if (fscanf(file, "%d %d", querySrc, queryDst) != 2) {
        fprintf(stderr, "Error: missing or invalid query on last line\n");
        freeGraph(g); fclose(file); return NULL;
    }
    if (*querySrc < 0 || *queryDst < 0 || *querySrc >= N || *queryDst >= N) {
        fprintf(stderr, "Error: query vertex index out of range\n");
        freeGraph(g); fclose(file); return NULL;
    }
    fclose(file);
    return g;
}
