#include "../syscall.h"
#include "../string.h"

void main(int argc, char* argv[]) {
    int fd = -1;

    if(strcmp(argv[1], "c") == 0){
        fd = _open(-1, "c/");
    }
    else if(strcmp(argv[1], "dev") == 0){
        fd = _open(-1, "dev/");
    }
    else if(argc == 1 || argv[1] == NULL){
        fd = _open(-1, "//");
    }
    else{
        dprintf(2, "Cannot open specified listing\n");
        _exit();
    }
    if(fd < 0){
        dprintf(2, "Cannot open specified listing\n");
        _exit();
    }

    char buf[512];
    int bytesRead;
    while ((bytesRead = _read(fd, buf, 512)) > 0) {
        dprintf(1, "%s\n", buf);
    }

    if (_close(fd) < 0) {
        dprintf(2, "Could not close specified listing\n");
        _exit();
    }

    _exit();
}