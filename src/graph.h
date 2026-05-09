#ifndef GRAPH_H
#define GRAPH_H

typedef struct Edge {
    int dest;
    int weight;
    struct Edge *next;
} Edge;

typedef struct {
    Edge *head;
} AdjNode;

typedef struct {
    int numVertices;
    AdjNode *adjList;
} Graph;

Graph* createGraph(int numVertices);
void addEdge(Graph *g, int src, int dest, int weight);
void freeGraph(Graph *g);
Graph* readGraphFromFile(const char *filename, int *querySrc, int *queryDst);

#endif