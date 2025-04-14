// This is a personal academic project. Dear PVS-Studio, please check it.


#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>
#include <time.h>

void dormir_microsegundos(int micros) {
    struct timespec req;
    req.tv_sec = micros / 1000000;
    req.tv_nsec = (micros % 1000000) * 1000;
    nanosleep(&req, NULL);
}

#define MOVEMENTS 8
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
int my_index = -1;
int width, height;

int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

void begin_read(){
    sem_wait(&sync->sem_reader_mutex);
    sync->readers++;
    if (sync->readers == 1)
        sem_wait(&sync->sem_game_mutex);
    sem_post(&sync->sem_reader_mutex);
}

void end_read() {
    sem_wait(&sync->sem_reader_mutex);
    sync->readers--;
    if (sync->readers == 0)
        sem_post(&sync->sem_game_mutex);
    sem_post(&sync->sem_reader_mutex);
}

bool can_move_to(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    int cell = state->board[y * width + x];
    return cell > 0; 
}

void player_loop(){
    while (!state->finished) {
        begin_read();
        player_t* me = &state->players[my_index];

        if(me->blocked){
            end_read();
            sem_wait(&sync->sem_game_mutex);
            sem_post(&sync->sem_game_mutex);
            break;
        }

        bool found_move = false;

        for (int dir = 0; dir < MOVEMENTS; dir++){
            int x = me->x + dx[dir];
            int y = me->y + dy[dir];
            if (can_move_to(x,y)) {
                unsigned char move = dir;
                write(STDOUT_FILENO, &move, 1);
                end_read();
                dormir_microsegundos(100000);
                found_move = true;
                break;
            }
        }

        end_read();

        if (!found_move) {
            sem_wait(&sync->sem_game_mutex);
            me->blocked = true;
            sem_post(&sync->sem_game_mutex);
            break;
        }

      
        dormir_microsegundos(200000); //si no hay mov invalido esperar
    }
}

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

    player_loop();

    return 0;
}