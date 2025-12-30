#include "../syscall.h"
#include "../string.h"

void main(int argc, char* argv[]) {
    int fd = 0;
    if(argc > 1){
        fd = _open(-1, argv[1]);
    }
    if(fd < 0){
        dprintf(2, "Cannot open specified file: %s\n", argv[1]);
        _exit();
    }

    char buf[512];
    int bytesRead;

    unsigned long long byteCount = 0; 
    unsigned long long wordCount = 0;
    unsigned long long lineCount = 0;

    int wordFlag = 0;
    
    while ((bytesRead = _read(fd, buf, 512)) > 0) {
        buf[bytesRead] = '\0';
        for (int i = 0; i < bytesRead+1; i++) {
            if (buf[i] != '\0') {
                byteCount++;
            }

            if (buf[i] == '\n') {
                lineCount++;
            }

            if (buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\0' && wordFlag == 0) {
                wordFlag = 1;
            }
            
            if ((buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') && wordFlag == 1) {
                wordCount++;
                wordFlag = 0;
            }
        }
    }

    dprintf(1, "%llu\t%llu\t%llu\n", lineCount, wordCount, byteCount);

    if(fd > 1 && _close(fd) < 0){
        dprintf(2, "Could not close specified file\n");
        _exit();
    }
    
    _exit();
}