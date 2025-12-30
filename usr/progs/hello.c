#include "string.h"
#include "../syscall.h"

void main(int i, char ** string) {
    while (1) {
        _print(*string);
    }
}