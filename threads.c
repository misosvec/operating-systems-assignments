#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>

#define BUF_SIZE 64 * 1024

char bufs[2][BUF_SIZE];
int buf_read[2] = {-1, -1}; // amount of bytes read, -1 = available to read
pthread_mutex_t muts[2] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
pthread_cond_t conds[2] = {PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};

void *read_data();

void *write_data();

int main() {
    // kompiloval som pomocou:
    // gcc -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror=implicit-function-declaration -pthread src/svec.c -o svec && ./svec < test.in > out.txt
    // ked som si studoval ako to urobit tak som sa docital o ping pong bufferingu
    // mam dva buffre, ked sa jeden naplni, zacne sa s neho zapisovat a zacne sa taktiez citat do druheho buffera
    // ked skonci zapisovanie buffer je opat dostupny na citanie a takto stale dokola
    pthread_t t1, t2;

    pthread_create(&t1, NULL, read_data, NULL);
    pthread_create(&t2, NULL, write_data, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    pthread_mutex_destroy(&muts[0]);
    pthread_mutex_destroy(&muts[1]);

    pthread_cond_destroy(&conds[0]);
    pthread_cond_destroy(&conds[1]);

    return 0;
}


void *read_data() {
    while (1) {
        // first buffer
        pthread_mutex_lock(&muts[0]);
        while (buf_read[0] > 0) {
            pthread_cond_wait(&conds[0], &muts[0]);
        }
        buf_read[0] = (int) read(STDIN_FILENO, bufs[0], BUF_SIZE);;
        pthread_cond_signal(&conds[0]);
        if (buf_read[0] == 0) {
            pthread_mutex_unlock(&muts[0]);
            break;
        }
        pthread_mutex_unlock(&muts[0]);

        // second buffer
        pthread_mutex_lock(&muts[1]);
        while (buf_read[1] > 0) {
            pthread_cond_wait(&conds[1], &muts[1]);
        }

        buf_read[1] = (int) read(STDIN_FILENO, bufs[1], BUF_SIZE);
        pthread_cond_signal(&conds[1]);
        if (buf_read[1] == 0) {
            pthread_mutex_unlock(&muts[1]);
            break;
        }
        pthread_mutex_unlock(&muts[1]);
    }
    return 0;
}

void *write_data() {
    while (1) {
        // first buffer
        pthread_mutex_lock(&muts[0]);
        while (buf_read[0] < 0) {
            pthread_cond_wait(&conds[0], &muts[0]);
        }
        if (buf_read[0] == 0) {
            pthread_mutex_unlock(&muts[0]);
            break;
        }
        write(STDOUT_FILENO, bufs[0], (size_t) buf_read[0]);
        buf_read[0] = -1;
        pthread_cond_signal(&conds[0]);
        pthread_mutex_unlock(&muts[0]);

        // second buffer
        pthread_mutex_lock(&muts[1]);
        while (buf_read[1] < 0) {
            pthread_cond_wait(&conds[1], &muts[1]);
        }
        if (buf_read[1] == 0) {
            pthread_mutex_unlock(&muts[1]);
            break;
        }
        write(STDOUT_FILENO, bufs[1], (size_t) buf_read[1]);
        buf_read[1] = -1;
        pthread_cond_signal(&conds[1]);
        pthread_mutex_unlock(&muts[1]);
    }
    return 0;
}