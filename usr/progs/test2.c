#include "../syscall.h"
#include "../string.h"

int main(int argc, char ** argv) {
    if (argc < 2 || argv[1] == NULL) {
        dprintf(2, "Invalid number of arguments\n");
        _exit();
    }
    _print(argv[1]);
    _exit();
}