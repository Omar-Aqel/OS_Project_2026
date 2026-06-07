CC      = gcc
CFLAGS  = -Wall -Wextra -g -Isrc
SRC_DIR = src

# Source files shared by all milestones
COMMON_SRC = $(SRC_DIR)/graph.c $(SRC_DIR)/dijkstra.c

# Raylib link flags (Linux)
RAYLIB_FLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

# ─────────────────────────────────────────────
#  milestone1 — terminal only (no GUI)
# ─────────────────────────────────────────────
milestone1: $(COMMON_SRC) $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) $(COMMON_SRC) $(SRC_DIR)/main.c \
		-o dijkstra

# ─────────────────────────────────────────────
#  milestone2 — static graph visualisation
#  milestone3 — adds animation (same binary)
#  Both produce the executable: sim
# ─────────────────────────────────────────────
milestone2 milestone3: $(COMMON_SRC) $(SRC_DIR)/main.c $(SRC_DIR)/visualization.c
	$(CC) $(CFLAGS) \
		-DENABLE_GUI \
		$(COMMON_SRC) \
		$(SRC_DIR)/main.c \
		$(SRC_DIR)/visualization.c \
		-o sim \
		$(RAYLIB_FLAGS)


# ─────────────────────────────────────────────
#  milestone4 — Multiple Travelers
# ─────────────────────────────────────────────
milestone4: $(COMMON_SRC) $(SRC_DIR)/main4.c $(SRC_DIR)/visualization.c
	$(CC) $(CFLAGS) -DENABLE_GUI $(COMMON_SRC) $(SRC_DIR)/main4.c $(SRC_DIR)/visualization.c -o sim $(RAYLIB_FLAGS)


# ─────────────────────────────────────────────
#  milestone5 — IPC with anonymous pipes
# ─────────────────────────────────────────────
milestone5: $(COMMON_SRC) $(SRC_DIR)/main5.c
	$(CC) $(CFLAGS) -DENABLE_GUI $(COMMON_SRC) $(SRC_DIR)/main5.c -o sim $(RAYLIB_FLAGS)


# ─────────────────────────────────────────────
#  Utility
# ─────────────────────────────────────────────
clean:
	rm -f dijkstra sim
