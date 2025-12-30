#include "../syscall.h"
#include "../string.h"

int main(int argc, char ** argv) {
    _print(argv[1]);
    _exit();
}