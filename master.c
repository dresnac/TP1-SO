// This is a personal academic project. Dear PVS-Studio, please check it.


// master.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

void dormir_microsegundos(int micros) {
    struct timespec req;
    req.tv_sec = micros / 1000000;
    req.tv_nsec = (micros % 1000000) * 1000;
    nanosleep(&req, NULL);
}

#define MAX_PLAYERS 9
#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"
#define PIPE_READ 0
#define PIPE_WRITE 1
#define PARAMS_ERROR 2
#define X 1
#define Y 0

// === Estructuras de datos ===
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

// === Variables globales ===
game_state_t* state;
sync_t* sync;
int shm_fd_state, shm_fd_sync;
int (*pipes)[2];
pid_t* children;
int delay = 200;
int timeout = 10;
int view_flag = 0;

int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// === Utilidades ===
void cleanup() {
    munmap(state, sizeof(game_state_t) + sizeof(int) * state->width * state->height);
    munmap(sync, sizeof(sync_t));
    shm_unlink(SHM_STATE);
    shm_unlink(SHM_SYNC);
    free(pipes);
    free(children);
}

void check_params(int width, int height, int num_of_players){
    int error = 0;
    if(width < 10){
        fprintf(stderr, "El ancho del tablero debe ser un entero mayor o igual a 10.\n");
        error = 1;
    }
    if(height < 10){
        fprintf(stderr, "El alto del tablero debe ser un entero mayor o igual a 10.\n");
        error = 1;
    }
    if(timeout <= 0){
        fprintf(stderr, "El tiempo de timeout debe ser mayor a 0 segundos\n");
        error = 1;
    }
    if((delay/1000) < 0 || (delay/1000) > timeout){
        fprintf(stderr, "El tiempo de delay debe ser positivo y menor que el tiempo de timeout\n");
        error = 1;
    }
    if(num_of_players > 9){
        fprintf(stderr, "La cantidad de jugadores máxima es 9.\n");
        error = 1;
    }


    if(error){
        exit(PARAMS_ERROR);
    }

}

void init_shared_memory(int width, int height) {
    shm_fd_state = shm_open(SHM_STATE, O_CREAT | O_RDWR, 0600);
    ftruncate(shm_fd_state, sizeof(game_state_t) + sizeof(int) * width * height);
    state = mmap(NULL, sizeof(game_state_t) + sizeof(int) * width * height, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_state, 0);
    
    shm_fd_sync = shm_open(SHM_SYNC, O_CREAT | O_RDWR, 0600);
    ftruncate(shm_fd_sync, sizeof(sync_t));
    sync = mmap(NULL, sizeof(sync_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_sync, 0);

    // Inicializar semaforos
    sem_init(&sync->sem_view_ready, 1, 0);
    sem_init(&sync->sem_view_done, 1, 0);
    sem_init(&sync->sem_master_mutex, 1, 1);
    sem_init(&sync->sem_game_mutex, 1, 1);
    sem_init(&sync->sem_reader_mutex, 1, 1);
    sync->readers = 0;
}

void distribute_players(int width, int height, int num_players) {

    int positions[MAX_PLAYERS][2] = {
        {0, 0}, {0, width - 1}, {height - 1, 0}, {height - 1, width - 1}, // Esquinas
        {height / 2, width / 2}, // Centro
        {height / 4, width / 4}, {height / 4, width - width / 4}, // Mitad superior
        {height - height / 4, width / 4}, {height - height / 4, width - width / 4} // Mitad inferior
    };

    for (int i = 0; i < num_players; i++) {
        // state->players[i].x = (i * 2) % width;
        // state->players[i].y = (i * 2) / width;

        state->players[i].x = positions[i][X];
        state->players[i].y = positions[i][Y];
        state->board[state->players[i].y * width + state->players[i].x] = -i;
        state->players[i].score = 0;
        state->players[i].invalid_moves = 0;
        state->players[i].valid_moves = 0;
    }
}

void init_board(int width, int height, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < width * height; i++) {
        state->board[i] = (rand() % 9) + 1;
    }
}

bool is_valid_move(int x, int y) {
    if (x < 0 || x >= state->width || y < 0 || y >= state->height) return false;
    int cell = state->board[y * state->width + x];
    return cell > 0;
}

void notify_view() {
    if(view_flag){
        sem_post(&sync->sem_view_ready);
        sem_wait(&sync->sem_view_done);
    }
}

void game_loop(int num_players) {
    fd_set readfds;
    struct timeval last_move_time;
    gettimeofday(&last_move_time, NULL);


    while (!state->finished) {
        FD_ZERO(&readfds);
        int maxfd = -1;

        for (int i = 0; i < num_players; i++) {
            if (!state->players[i].blocked) {
                FD_SET(pipes[i][PIPE_READ], &readfds);
                if (pipes[i][PIPE_READ] > maxfd) maxfd = pipes[i][PIPE_READ];
            }
        }

        struct timeval tv = {timeout, 0};
        int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);

        if (ready == 0) {
            state->finished = true;
            notify_view();
            break;  
        }

        for (int i = 0; i < num_players; i++) {
            if (FD_ISSET(pipes[i][PIPE_READ], &readfds)) {
                unsigned char dir;
                int r = read(pipes[i][PIPE_READ], &dir, 1);

                if (r <= 0) {
                    state->players[i].blocked = true;
                    continue;
                }

                int nx = state->players[i].x + dx[dir];
                int ny = state->players[i].y + dy[dir];

                if (dir > 7 || !is_valid_move(nx, ny)) {
                    state->players[i].invalid_moves++;
                    continue;
                }

                int reward = state->board[ny * state->width + nx];
                state->players[i].score += reward;
                state->players[i].valid_moves++;

                state->board[ny * state->width + nx] = -i;
                state->players[i].x = nx;
                state->players[i].y = ny;

                notify_view();
                dormir_microsegundos(delay * 1000);

                gettimeofday(&last_move_time, NULL);
            }
        }

        // Check for game end (no players can move)
        bool all_blocked = true;
        for (int i = 0; i < num_players; i++) {
            if (!state->players[i].blocked) {
                all_blocked = false;
                break;
            }
        }
        if (all_blocked) {
            state->finished = true;
            notify_view();
            break;
        }
    }
    //notify_view();
}

int main(int argc, char* argv[]) {
    int width = 10, height = 10, seed = time(NULL);
    char* view_bin = NULL;
    view_flag = 0;
    char* player_bins[MAX_PLAYERS];
    int num_players = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w")) {
            if (i + 1 < argc) width = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h")) {
            if (i + 1 < argc) height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-d")) {
            if (i + 1 < argc) delay = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-t")) {
            if (i + 1 < argc) timeout = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-s")) {
            if (i + 1 < argc) seed = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-v")) {
            if (i + 1 < argc){
                view_bin = argv[++i];
                view_flag = 1;
            } 
        } else if (!strcmp(argv[i], "-p")) {
            while (i + 1 < argc && num_players < MAX_PLAYERS && argv[i + 1][0] != '-') {
                player_bins[num_players++] = argv[++i];
            }
        }
    }

    check_params(width, height, num_players);//normalizar: algunos params son globales y otros locales, deberían ser todos lo mismo

    printf("width: %d\n", width);
    printf("height: %d\n", height);
    printf("delay: %d\n", delay);
    printf("timeout: %d\n", timeout);
    printf("seed: %d\n", seed);
    printf("view: %s\n", view_flag? view_bin:"-");
    printf("num_players: %d\n", num_players);
    for(int i=0; i < num_players; i++){
        printf("\t%s\n", player_bins[i]);
    }
    sleep(2);
    

    init_shared_memory(width, height);
    state->width = width;
    state->height = height;
    state->num_players = num_players;
    state->finished = false;

    init_board(width, height, seed);
    distribute_players(width, height, num_players);

    pipes = malloc(sizeof(int[2]) * num_players);
    if (pipes == NULL) {
        perror("malloc failed for pipes");
        exit(EXIT_FAILURE);
    }
    children = malloc(sizeof(pid_t) * (num_players + view_flag));
    if (children == NULL) {
        perror("malloc failed for children");
        exit(EXIT_FAILURE);
    }

    if (view_flag) {
        pid_t pid = fork();
        if (pid == 0) {
            char w_str[8], h_str[8];
            sprintf(w_str, "%d", width);
            sprintf(h_str, "%d", height);
            execl(view_bin, view_bin, w_str, h_str, NULL);  //checkear si va view_bin o view_flag
            perror("execl view"); exit(1);
        }
        children[num_players] = pid;
    }

    for (int i = 0; i < num_players; i++) {
        pipe(pipes[i]);
        pid_t pid = fork();
        if (pid == 0) {
            dup2(pipes[i][PIPE_WRITE], STDOUT_FILENO);
            close(pipes[i][PIPE_READ]);
            char w_str[8], h_str[8];
            sprintf(w_str, "%d", width);
            sprintf(h_str, "%d", height);
            execl(player_bins[i], player_bins[i], w_str, h_str, NULL);
            perror("execl player"); exit(1);
        }
        
        close(pipes[i][PIPE_WRITE]);
        state->players[i].pid = pid;
        children[i] = pid;
    }

    notify_view();
    game_loop(num_players);

    for (int i = 0; i < num_players + view_flag; i++) {
        int status;
        waitpid(children[i], &status, 0);
    }

    for (int i = 0; i < num_players; i++) {
        printf("Jugador %d (%s): Puntaje = %u\n", i, state->players[i].name, state->players[i].score);
    }

    cleanup();
    return 0;
}
