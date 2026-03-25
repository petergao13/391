#include "../syscall.h"
#include "../string.h"

void main(int argc, char* argv[]) {
    const char *path = NULL;
    if (argc < 2 || argv[1] == NULL) {
        path = "//";
    } else if (strcmp(argv[1], "c") == 0) {
        path = "c/";
    } else if (strcmp(argv[1], "dev") == 0) {
        path = "dev/";
    } else {
        dprintf(2, "Cannot open specified listing\n");
        _exit();
    }

    int fd = _open(-1, path);
    if (fd < 0) {
        dprintf(2, "Cannot open specified listing\n");
        _exit();
    }

    char buf[512];
    long bytesRead;
    while ((bytesRead = _read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytesRead] = '\0';
        dprintf(1, "%s\n", buf);
    }

    if (_close(fd) < 0) {
        dprintf(2, "Could not close specified listing\n");
        _exit();
    }

    _exit();
}