/* update_counters_in_shared_memory.c
 * Build: gcc -o shared_counters_sysv shared_counters_sysv.c
 * Run:   ./shared_counters_sysv <num_workers> <num_increments_per_worker>
 * Example: ./shared_counters_sysv 4 100000
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#define MAX_WORKERS 128

typedef struct {
    long global_counter;     /* a shared global counter updated by workers */
    long counters[MAX_WORKERS]; /* per-worker counters (master can aggregate) */
} shared_t;

/* System V semaphore helpers */
static int sem_op(int semid, int semnum, int op) {
    struct sembuf sb;
    sb.sem_num = semnum;
    sb.sem_op = op;
    sb.sem_flg = 0;
    return semop(semid, &sb, 1);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_workers> <num_increments_per_worker>\n", argv[0]);
        return 1;
    }

    int num_workers = atoi(argv[1]);
    long num_iters = atol(argv[2]);

    if (num_workers <= 0 || num_workers > MAX_WORKERS) {
        fprintf(stderr, "num_workers must be 1..%d\n", MAX_WORKERS);
        return 1;
    }
    if (num_iters < 0) {
        fprintf(stderr, "num_increments_per_worker must be >= 0\n");
        return 1;
    }

    /* create shared memory */
    int shmid = shmget(IPC_PRIVATE, sizeof(shared_t), IPC_CREAT | 0600);
    if (shmid == -1) {
        fprintf(stderr, "shmget failed: %s\n", strerror(errno));
        return 1;
    }

    shared_t *s = (shared_t *)shmat(shmid, NULL, 0);
    if (s == (void *)-1) {
        fprintf(stderr, "shmat failed: %s\n", strerror(errno));
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    s->global_counter = 0;
    for (int i = 0; i < MAX_WORKERS; ++i) s->counters[i] = 0;

    /* create a semaphore set with 1 semaphore */
    int semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if (semid == -1) {
        fprintf(stderr, "semget failed: %s\n", strerror(errno));
        shmdt(s);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    /* initialize semaphore to 1 */
    if (semctl(semid, 0, SETVAL, 1) == -1) {
        fprintf(stderr, "semctl SETVAL failed: %s\n", strerror(errno));
        shmdt(s);
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        return 1;
    }

    pid_t pids[MAX_WORKERS];

    for (int i = 0; i < num_workers; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at worker %d: %s\n", i, strerror(errno));
            for (int k = 0; k < i; ++k) kill(pids[k], SIGTERM);
            shmdt(s);
            shmctl(shmid, IPC_RMID, NULL);
            semctl(semid, 0, IPC_RMID);
            return 1;
        }
        if (pid == 0) {
            for (long it = 0; it < num_iters; ++it) {
                if (sem_op(semid, 0, -1) == -1) {
                    fprintf(stderr, "sem_op P failed in child %d: %s\n", i, strerror(errno));
                    _exit(1);
                }
                s->global_counter += 1;
                s->counters[i] += 1;
                if (sem_op(semid, 0, 1) == -1) {
                    fprintf(stderr, "sem_op V failed in child %d: %s\n", i, strerror(errno));
                    _exit(1);
                }
            }
            _exit(0);
        }
        pids[i] = pid;
    }

    for (int i = 0; i < num_workers; ++i) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            fprintf(stderr, "waitpid failed for pid %d: %s\n", pids[i], strerror(errno));
        }
    }

    printf("Master: global_counter = %ld (expected %ld)\n", s->global_counter, (long)num_workers * num_iters);
    long sum = 0;
    for (int i = 0; i < num_workers; ++i) {
        printf(" worker %2d counter = %12ld\n", i, s->counters[i]);
        sum += s->counters[i];
    }
    printf("Sum of per-worker counters = %ld\n", sum);

    shmdt(s);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    return 0;
}

