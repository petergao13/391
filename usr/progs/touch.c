#include "../syscall.h"
#include "../string.h"

void main(int argc, char* argv[]) {
    if(argc < 2){
        dprintf(2, "Invalid number of arguments\n");
        _exit();
    }
    for(int i = 1; i < argc; i++){
        int retVal = _fscreate(argv[i]);
        if(retVal < 0){
            dprintf(2, "Could not create file: %s\n", argv[i]);
            continue;
        }
    }
    _exit();
}