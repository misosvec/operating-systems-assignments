#include <stdio.h>
#include <pthread.h>
#include <fcntl.h> // open sys call
#include <sys/errno.h> // errno
#include <stdlib.h> // exit()
#include <sys/uio.h> // read
#include <unistd.h> // read, write
#define BUF_SIZE 1024

char* buf[BUF_SIZE];
int read_buf = -1;
pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buf_cond = PTHREAD_COND_INITIALIZER;

void* read_data(void *arg);
void* write_data(void *arg);

int main(int argc, char* argv[]) {

    if (argc != 2) {
        printf("Usage: %s <input file>\n", argv[0]);
        exit(1);
    }

    pthread_t t1, t2;

    pthread_create(&t1, NULL, read_data, argv[1]);
    pthread_create(&t2, NULL, write_data, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    pthread_mutex_destroy(&buf_mutex);
    pthread_cond_destroy(&buf_cond);
}

void* read_data(void* arg){
    char* filename = (char*) arg;
    int fd_in = open(filename, O_RDONLY);
    if (fd_in == -1) {
        perror("Failed to open input file");
        exit(errno);
    }
    while(1) {
        pthread_mutex_lock(&buf_mutex);
        while (read_buf > 0) {
            pthread_cond_wait(&buf_cond, &buf_mutex);
        }
        int bytes_read = read(fd_in, buf, BUF_SIZE);
        if (bytes_read == 0) {
            exit(0);
        }
        if (bytes_read < 0) {
            perror("Failed to read from standard input");
            exit(errno);
        } else {
            read_buf = bytes_read;
            pthread_cond_signal(&buf_cond);
        }
        pthread_mutex_unlock(&buf_mutex);
    }
}

void* write_data(void* arg){
    while (1) {
        pthread_mutex_lock(&buf_mutex);
        while (read_buf < 0) {
            pthread_cond_wait(&buf_cond, &buf_mutex);
        }
        int bytes_written = write(STDOUT_FILENO, buf, read_buf);
        if (bytes_written < 0) {
            perror("Failed to write to standard output");
            exit(errno);
        }
        if (bytes_written == read_buf) {
            read_buf = -1;
            pthread_cond_signal(&buf_cond);
        }

        pthread_mutex_unlock(&buf_mutex);
    }
}

