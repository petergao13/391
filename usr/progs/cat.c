#include "../syscall.h"
#include "../string.h"

void main(int argc, char* argv[]) {
    if(argc < 2){
        dprintf(2, "Invalid number of arguments\n");
        _exit();
    }
    int fd = _open(-1, argv[1]);
    if(fd < 0){
        dprintf(2, "Cannot open specified file: %s\n", argv[1]);
        _exit();
    }
    char buf[512];
    int bytesRead;
    while((bytesRead = _read(fd, buf, 512)) > 0){
        for(int i = 0; i < bytesRead; i++){
            if(buf[i] != '\r'){
                dprintf(1, "%c", buf[i]);
            }
        }
    }
    if(_close(fd) < 0){
        dprintf(2, "Could not close specified file\n");
        _exit();
    }
    _exit();
}