# OS Project 2026 — Graph Traversal Simulation

## Build & Run

### Milestone 1 — Dijkstra (terminal only)
```bash
make milestone1
./dijkstra <graph_file>
```
Reads a weighted directed graph and a single traveler (src dst) from a file,
runs Dijkstra's shortest-path algorithm, and prints the result to the terminal.

---

### Milestone 2 / 3 — Static & Animated GUI
```bash
make milestone2   # or milestone3 (same binary)
./sim <graph_file>
```
Displays the graph with raylib. Milestone 3 adds smooth animation of the
traveler moving along the shortest path. Press **Space** to start/reset.

---

### Milestone 4 — Multiple Travelers (parent + child processes)
```bash
make milestone4
./sim <graph_file>
```
The parent reads the file, computes Dijkstra for every traveler, forks one
child per traveler (children sleep and print `[PID] started`), then animates
all travelers simultaneously in the GUI. Each traveler is drawn in a distinct
color. The parent sends `SIGTERM` to each child when its journey ends, then
waits for all children before exiting.

---

### Milestone 5 — IPC via Anonymous Pipes
```bash
make milestone5
./sim <graph_file>
```
Each child **independently** computes its own Dijkstra path (the parent does
not share route data). As the child traverses its path it sends an `IPCMsg`
struct to the parent through an anonymous pipe after arriving at each node,
then sleeps for `weight × 300 ms + 1000 ms` before moving on. The parent
reads these messages (non-blocking) in its GUI loop, prints the log, and
smoothly animates the traveler from node to node.

#### IPC choice: anonymous pipes (`pipe(2)`)
- **Why pipes:** one-directional, per-child channel perfectly matches the
  "child reports to parent" communication pattern. No shared state is needed;
  each message is a small fixed-size struct, well within `PIPE_BUF`, so
  writes are atomic and there is no risk of interleaved data. Pipes are
  simpler to set up than shared memory and need no cleanup beyond `close(2)`.

#### Log format (all output from parent only)
```
[PID=<pid>] arrived at node <X> | next node: <Y>
[PID=<pid>] arrived at node <X> | DESTINATION
[PID=<pid>] finished
```

---

### Milestone 6 — Node-Access Synchronization
```bash
make milestone6
./sim <graph_file>
```
Adds a **mutual-exclusion constraint**: at most one traveler may occupy any
given node at a time. Each child still computes its own Dijkstra path. Before
entering a node the child sends `MSG_WAITING` to the parent, then blocks on
the node's semaphore. Once it acquires the semaphore it sends `MSG_ARRIVED`,
stays in the node for exactly **1 second** (the critical section), then
releases the semaphore and sends `MSG_DEPARTED`. Travel between nodes (outside
the critical section) takes `weight × 300 ms`.

#### IPC mechanism: anonymous pipes (same as Milestone 5)
One pipe per child, child → parent direction only.

#### Synchronization mechanism: POSIX unnamed semaphores in shared memory
```c
sem_t *node_sems = mmap(NULL, sizeof(sem_t) * numNodes,
                        PROT_READ|PROT_WRITE,
                        MAP_SHARED|MAP_ANONYMOUS, -1, 0);
sem_init(&node_sems[i], 1 /*pshared*/, 1 /*binary*/);
```
One binary semaphore per node, initialised to 1. `sem_wait` on entry,
`sem_post` on exit. Because the semaphores live in `MAP_SHARED|MAP_ANONYMOUS`
memory they are visible to all forked children. POSIX `sem_wait` is fair
(FIFO for blocked callers on Linux), so starvation is impossible.

#### GUI indicators
| Visual | Meaning |
|--------|---------|
| Bright red double ring around node | Node is occupied |
| Dim colour + extra outer ring on traveler | Traveler waiting outside node |
| Normal colour at node centre | Traveler inside node |

#### Log format
```
[PID=<pid>] waiting for node <X>
[PID=<pid>] arrived at node <X> | next node: <Y>
[PID=<pid>] arrived at node <X> | DESTINATION
[PID=<pid>] finished
```

#### Demo file (`graph6.txt`) — 3 travelers all forced through node 2
```
5 4
0 2 1
1 2 1
3 2 1
2 4 1
# travelers
3
0 4
1 4
3 4
```
Run with `./sim graph6.txt` to see all three travelers queue up at node 2
and enter one-by-one.

---

## Input file format

```
# graph definition
<num_vertices> <num_edges>
<src> <dst> <weight>
...
# travelers
<num_travelers>
<src> <dst>
...
```

### Example (`graph.txt`)
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
