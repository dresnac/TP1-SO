#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

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


game_state_t* state;
int my_index = -1;
int width, height;

int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

int main(int argc, char* argv[]){
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ancho> <alto>\n", argv[0]);
        return 1;
    }
    width = atoi(argv[1]);
    height = atoi(argv[2]);

    srand(getpid()); //usamos pid como seed asi varia

    int fd_state = shm_open(SHM_STATE, O_RDWR, 0);
    state = mmap(NULL, sizeof(game_state_t)+sizeof(int)*width*height, PROT_READ | PROT_WRITE, MAP_SHARED, fd_state, 0);

    int fd_sync = shm_open(SHM_SYNC, O_RDWR, 0);
    sync = mmap(NULL, sizeof(sync_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_sync, 0);

    pid_t pid = getpid();
    for (int i = 0; i < state->num_players; i++) {
        if (state->players[i].pid == pid) {
            my_index = i;
            snprintf(state->players[i].name, 16, "J%d", i);
            break;
        }
    }

    if (my_index == -1){
        fprintf(stderr, "No se ha encontrado el jugador");
        return 1;        
    }

    return 0;
}