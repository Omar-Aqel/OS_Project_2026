# OS Project 2026 — Graph Traversal Simulation

A multi-milestone C project that simulates travelers navigating a weighted directed graph using Dijkstra's shortest-path algorithm. Each milestone adds a new OS concept — processes, IPC, synchronization, and scheduling — visualized in real time with a **raylib** GUI.

---

## Table of Contents

- [Project Overview](#project-overview)
- [Prerequisites](#prerequisites)
- [Project Structure](#project-structure)
- [Input File Format](#input-file-format)
- [Milestones](#milestones)
  - [Milestone 1 — Dijkstra (Terminal)](#milestone-1--dijkstra-terminal)
  - [Milestone 2 / 3 — Static & Animated GUI](#milestone-2--3--static--animated-gui)
  - [Milestone 4 — Multiple Travelers (Processes)](#milestone-4--multiple-travelers-processes)
  - [Milestone 5 — IPC via Anonymous Pipes](#milestone-5--ipc-via-anonymous-pipes)
  - [Milestone 6 — Node-Access Synchronization](#milestone-6--node-access-synchronization)
  - [Milestone 7 — Scheduling Algorithms (FCFS vs SJF)](#milestone-7--scheduling-algorithms-fcfs-vs-sjf)
- [Building & Running](#building--running)
- [Cleaning Up](#cleaning-up)

---

## Project Overview

The simulation models a set of **travelers**, each defined by a source and destination node. Every traveler independently computes the shortest path through a weighted directed graph and then traverses it. Across milestones, travelers evolve from a single sequential computation to fully concurrent OS processes that communicate, synchronize, and contend for shared resources under different scheduling policies.

| Milestone | Key OS Concept |
|-----------|----------------|
| 1 | Algorithm (Dijkstra) |
| 2 / 3 | GUI visualization + animation |
| 4 | Processes (`fork`, `SIGTERM`, `waitpid`) |
| 5 | IPC — anonymous pipes |
| 6 | Synchronization — POSIX semaphores in shared memory |
| 7 | Scheduling — FCFS vs SJF user-space scheduler |

---

## Prerequisites

| Dependency | Notes |
|------------|-------|
| GCC | Any modern version with C11 support |
| GNU Make | Standard build tool |
| **raylib** | Required for milestones 2–7 (GUI). Install via your package manager or build from source. |
| POSIX-compliant Linux | `fork`, `pipe`, `mmap`, `sem_init` used throughout |

Install raylib on Ubuntu/Debian:
```bash
sudo apt install libraylib-dev
```

---

## Project Structure

```
OS_Project_2026/
├── Makefile
├── graph.txt          # Default demo graph (5 nodes, 3 travelers)
├── graph6.txt         # Demo for Milestone 6 (forced node contention)
├── graph7.txt         # Demo for Milestone 7 (scheduling comparison)
└── src/
    ├── graph.h / graph.c          # Graph data structure & file parser
    ├── dijkstra.h / dijkstra.c    # Dijkstra's algorithm
    ├── visualization.h / visualization.c  # raylib GUI (shared by M2–M7)
    ├── main.c                     # Milestone 1–3 entry point
    ├── main4.c                    # Milestone 4
    ├── main5.c                    # Milestone 5
    ├── main6.c                    # Milestone 6
    └── main7.c                    # Milestone 7
```

---

## Input File Format

```
<num_vertices> <num_edges>
<src> <dst> <weight>
...                       ← one edge per line
# travelers
<num_travelers>
<src> <dst>
...                       ← one traveler per line
```

### Example — `graph.txt`

```
5 6
0 1 10
0 2 3
1 3 2
2 1 4
2 3 8
3 4 5
#travelers
3
0 4
1 4
2 3
```

Lines beginning with `#` are treated as section separators/comments.

---

## Milestones

### Milestone 1 — Dijkstra (Terminal)

Reads the graph file, runs Dijkstra's algorithm for the **first** traveler defined in the file, and prints the shortest path and total weight to the terminal. No GUI.

```bash
make milestone1
./dijkstra <graph_file>
```

**Example output:**
```
Shortest path from 0 to 4: 0 -> 2 -> 1 -> 3 -> 4  (total weight: 20)
```

---

### Milestone 2 / 3 — Static & Animated GUI

Displays the graph in a raylib window. Milestone 3 extends the same binary with smooth animation of a single traveler moving along the computed shortest path.

```bash
make milestone2   # or: make milestone3  (identical binary)
./sim <graph_file>
```

- Press **Space** to start or reset the animation.
- The shortest path edges are highlighted; the traveler moves node-to-node in real time.

---

### Milestone 4 — Multiple Travelers (Processes)

Introduces **multi-processing**. The parent process computes Dijkstra for every traveler, then `fork`s one child per traveler. Children sleep briefly and print a started message. All travelers are animated simultaneously in the GUI, each drawn in a distinct color. When a traveler's journey ends the parent sends `SIGTERM` to its child, then calls `waitpid` for all children before exiting.

```bash
make milestone4
./sim <graph_file>
```

**OS concepts:** `fork(2)`, `SIGTERM`, `waitpid(2)`, process lifecycle.

---

### Milestone 5 — IPC via Anonymous Pipes

Each child **independently** computes its own Dijkstra path (no data shared by the parent). While traversing, it sends `IPCMsg` structs to the parent through a dedicated anonymous pipe after arriving at each node, then sleeps for `weight × 300 ms + 1000 ms` before moving on. The parent reads these messages in a non-blocking GUI loop, logs each event, and smoothly animates the traveler.

```bash
make milestone5
./sim <graph_file>
```

**Why anonymous pipes?** One-directional, per-child channel perfectly matches the "child reports to parent" pattern. Messages are a small fixed-size struct (well within `PIPE_BUF`), so writes are atomic — no risk of interleaved data. Simpler setup than shared memory; cleanup is just `close(2)`.

**Log format (parent stdout only):**
```
[PID=<pid>] arrived at node <X> | next node: <Y>
[PID=<pid>] arrived at node <X> | DESTINATION
[PID=<pid>] finished
```

**OS concepts:** `pipe(2)`, `read`/`write`, non-blocking I/O (`O_NONBLOCK`), `nanosleep`.

---

### Milestone 6 — Node-Access Synchronization

Adds a **mutual-exclusion constraint**: at most one traveler may occupy any given node at a time.

- Before entering a node, a child sends `MSG_WAITING` and blocks on that node's semaphore.
- Once acquired, it sends `MSG_ARRIVED`, holds the node for **1 second** (critical section), then releases the semaphore and sends `MSG_DEPARTED`.
- Travel time between nodes (outside the critical section) is `weight × 300 ms`.

```bash
make milestone6
./sim <graph_file>
```

**Synchronization mechanism — POSIX unnamed semaphores in shared memory:**
```c
sem_t *node_sems = mmap(NULL, sizeof(sem_t) * numNodes,
                        PROT_READ|PROT_WRITE,
                        MAP_SHARED|MAP_ANONYMOUS, -1, 0);
sem_init(&node_sems[i], 1 /*pshared*/, 1 /*binary semaphore*/);
```
One binary semaphore per node, initialized to 1. `sem_wait` on entry, `sem_post` on exit. Because the memory is `MAP_SHARED|MAP_ANONYMOUS`, all forked children share the same semaphore array. POSIX `sem_wait` on Linux is FIFO-fair, so starvation is impossible.

**GUI indicators:**

| Visual | Meaning |
|--------|---------|
| Bright red double ring around node | Node is currently occupied |
| Dim color + outer ring on traveler | Traveler waiting outside node |
| Normal color at node center | Traveler inside node (critical section) |

**Log format:**
```
[PID=<pid>] waiting for node <X>
[PID=<pid>] arrived at node <X> | next node: <Y>
[PID=<pid>] arrived at node <X> | DESTINATION
[PID=<pid>] finished
```

**Demo — `graph6.txt`:** Three travelers are all routed through node 2, forcing visible queuing. Run with:
```bash
./sim graph6.txt
```

**OS concepts:** `mmap(2)`, `sem_init`, `sem_wait`, `sem_post`, critical sections.

---

### Milestone 7 — Scheduling Algorithms (FCFS vs SJF)

Replaces the OS-level semaphore locking with a **user-space scheduler** managed entirely by the parent process. When multiple travelers request access to a node, the parent places them in a wait queue and grants access based on the selected algorithm.

| Algorithm | Behavior |
|-----------|----------|
| **FCFS** (First Come First Serve) | Traveler who sent `MSG_WAITING` first is admitted first — standard FIFO. |
| **SJF** (Shortest Job First) | Parent grants access to the traveler with the **lowest total path weight**, allowing shorter journeys to bypass longer ones. |

**Impact on Waiting Times:**

| Algorithm | Waiting Time | Trade-off |
|-----------|-------------|-----------|
| **FCFS** | Fair — no starvation, but a traveler with a very long path can block the node and inflate average wait times (Convoy Effect). | Fairness over efficiency |
| **SJF** | Lower average waiting time — shorter journeys finish quickly and free the node sooner, but travelers with long total paths risk starvation if shorter jobs keep arriving. | Efficiency over fairness |

Children no longer use semaphores. Instead, after sending `MSG_WAITING` they block on a **wake-up pipe** (one per child, parent → child direction). The parent signals the chosen traveler by writing a byte to its wake-up pipe.

```bash
make milestone7
./sim-schd fcfs <graph_file>
./sim-schd sjf  <graph_file>
```

**Demo — `graph7.txt`:** Three travelers all converge on node 3 with different total path weights (T0 = 25, T1 = 15, T2 = 7). Under FCFS they enter in arrival order; under SJF, T2 enters first, then T1, then T0.

**OS concepts:** User-space scheduling, bidirectional pipe pairs, non-blocking reads, scheduling policy trade-offs.

---

## Building & Running

```bash
# Build a specific milestone
make milestone1
make milestone2    # or milestone3
make milestone4
make milestone5
make milestone6
make milestone7

# Run (milestones 1)
./dijkstra graph.txt

# Run (milestones 2–4)
./sim graph.txt

# Run (milestones 5)
./sim graph5.txt

# Run (milestones 6)
./sim graph6.txt

# Run (milestone 7)
./sim-schd fcfs graph7.txt
./sim-schd sjf  graph7.txt
```

---

## Cleaning Up

```bash
make clean
```

Removes the `dijkstra`, `sim`, and `sim-schd` executables.
