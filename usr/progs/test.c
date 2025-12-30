#include "../syscall.h"
#include "../string.h"

int main(int argc, char ** argv) {
    int child_fd;
    char uart[12];
    for(int i = 2; i <= 7; i++){
        snprintf(uart, 12, "dev/uart%d", i);
        uart[11] = '\0';

        int child_tid = _fork();
        if(child_tid == 0){
            child_fd = _open(-1, "c/trek");
            _open(2, uart);
            
            _close(0);
            _close(1);
            _uiodup(2, 0);
            _uiodup(2, 1);
            _exec(child_fd, 0, NULL);
        }
    }

    child_fd = _open(-1, "c/trek");
    _exec(child_fd, 0, NULL);
    _close(child_fd);
    _exit();
}