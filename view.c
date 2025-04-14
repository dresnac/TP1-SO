// This is a personal academic project. Dear PVS-Studio, please check it.


#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>

// #include <unistd.h>     // para usleep()
#include <sys/time.h>   // para gettimeofday()



#define MAX_PLAYERS 9
#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"

typedef struct {
    char name[16];
    unsigned int score;
    unsigned int invalid_moves;
    unsigned int valid_moves;
    unsigned short x, y;
    pid_t pid;
    bool blocked;
} player_t;

typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned int num_players;
    player_t players[MAX_PLAYERS];
    bool finished;
    int board[];
} game_state_t;

typedef struct { 
    sem_t sem_view_ready;
    sem_t sem_view_done;
    sem_t sem_master_mutex;
    sem_t sem_game_mutex;
    sem_t sem_reader_mutex;
    unsigned int readers;
} sync_t;

game_state_t* state;
sync_t* sync; 
void print_board() {
    printf("\033[H\033[J"); // Clear screen
    printf("Tablero (%dx%d):\n", state->width, state->height);

    for (int y = 0; y < state->height; y++) {
        for (int x = 0; x < state->width; x++) {
            int cell = state->board[y * state->width + x];
            if (cell > 0)
                printf(" %d ", cell);
            else
                printf(" P%d", -cell);
        }
        printf("\n");
    }

    printf("\nJugadores:\n");
    for (int i = 0; i < state->num_players; i++) {
        player_t* p = &state->players[i];
        printf("P%d (%s): score=%u, valid=%u, invalid=%u, pos=(%d,%d)%s\n",
               i, p->name, p->score, p->valid_moves, p->invalid_moves,
               p->x, p->y, p->blocked ? " [BLOCKED]" : "");
    }
}

void start_view_loop() {
    while (!state->finished) {
        sem_wait(&sync->sem_view_ready); // Esperar a que el master indique imprimir

        sem_wait(&sync->sem_reader_mutex);
        sync->readers++;
        if (sync->readers == 1)
            sem_wait(&sync->sem_game_mutex);
        sem_post(&sync->sem_reader_mutex);

        print_board();

        sem_wait(&sync->sem_reader_mutex);
        sync->readers--;
        if (sync->readers == 0)
            sem_post(&sync->sem_game_mutex);
        sem_post(&sync->sem_reader_mutex);

        sem_post(&sync->sem_view_done); // Avisar al master que termine
        usleep(100); // Pequeña espera para evitar flicker
    }
    print_board();
    printf("\nJuego terminado.\n");
}

int main(int argc, char* argv[]){
    if(argc < 3) {
        fprintf(stderr, "Uso: %s <ancho> <alto>\n", argv[0]);
        return 1;
    }
    
    int width = atoi(argv[1]);
    int height = atoi(argv[2]);

    int fd_state = shm_open(SHM_STATE, O_RDWR, 0);
    state = mmap(NULL, sizeof(game_state_t) + sizeof(int) * width * height, PROT_READ | PROT_WRITE, MAP_SHARED, fd_state, 0);

    int fd_sync = shm_open(SHM_SYNC, O_RDWR, 0);
    sync = mmap(NULL, sizeof(sync_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_sync, 0);

    start_view_loop();

    return 0;
}