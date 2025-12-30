#include "../syscall.h"
#include "../string.h"

int main(int argc, char ** argv) {
    for(int i = 1; i < 16; i++){
        int child_fd = _open(-1, "c/trek");
        if(child_fd >= 0){
            int child_tid = _fork();
            if(child_tid == 0){
                _exec(child_fd, 0, NULL);
            }
            _close(child_fd);
        }
    }
    while(_wait(0) > 0);
}