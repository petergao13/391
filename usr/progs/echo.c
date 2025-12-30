#include "../syscall.h"
#include "../string.h"

void main(int argc, char* argv[]) {
    for(int i = 1; i < argc; i++){
        dprintf(1, "%s", argv[i]);
        if(i < argc-1){
            dprintf(1, " ");
        }
    }
    dprintf(1, "\n");
    _exit();
}