#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>


// gcc-12 -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror=implicit-function-declaration -pthread svec.c -o svec
// ./svec test.in test.out
int main(int argc, char* argv[]){
    size_t BYTES_TO_READ = 1;

    if (argc != 3) {
        printf("Usage: %s <input file> <output file>\n", argv[0]);
    }

    int fd_in = open(argv[1], O_RDONLY);

    if (fd_in == -1) {
        perror("Failed to open input file");
        exit(errno);
    }

    int fd_out = open(argv[2], O_WRONLY | O_CREAT | O_APPEND | O_TRUNC , S_IRUSR | S_IWUSR);

    if (fd_out == -1) {
        perror("Failed to open output file");
        exit(errno);
    }

    char buffer[1];
    int read_result;
    while((read_result = read(fd_in,buffer, BYTES_TO_READ)) > 0){
        printf("%c", buffer[0]);
        int write_result = write(fd_out, buffer,read_result);
        if(write_result < 0){
            perror("Failed to write to output file");
            exit(errno);
        }
    }

    if(read_result == -1){
        perror("Failed reading from input file");
        exit(errno);
    }

    close(fd_in);
    close(fd_out);

    return 0;

}