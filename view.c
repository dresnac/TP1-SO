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

typedef struct { //no lo copie
    sem_t sem_view_ready;
    sem_t sem_view_done;
    sem_t sem_master_mutex;
    sem_t sem_game_mutex;
    sem_t sem_reader_mutex;
    unsigned int readers;
} sync_t;

game_state_t* state;
sync_t* sync; 

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

    return 0;
}