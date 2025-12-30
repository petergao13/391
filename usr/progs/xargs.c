#include "../syscall.h"
#include "../string.h"

/*A program that reads items from STDIN, separated by spaces or newlines, and executes the
command specified in its arguments with those items as additional arguments.*/
void main(int argc, char* argv[]) {
    if(argc < 2){
        dprintf(2, "Invalid number of arguments\n");
        _exit();
    }
	char * temp_argv[9];
    // shifts argvs down by 1 to get rid of the c/xargs
    for(int i = 1; i < argc; i++){
        if(i == 1 && strchr(argv[i], '/') == NULL){
            char temp[strlen(argv[i])+3];
            snprintf(temp, sizeof(temp), "c/%s", argv[i]);
            temp_argv[i-1] = strdup(temp);
        }
        else{
            temp_argv[i-1] = strdup(argv[i]);
        }
    }
    argc--;

    // parses through STDIN and addend args until we have MAXARGS (8)
    char buf[1024];
    int read = _read(0, buf, 1024);
    if(read < 0){
        dprintf(2, "STDIN is invalid\n");
        _exit();
    }
    buf[read] = '\0';
    char * head = buf;
    int found = 0;
    for(int i = 0; i < read+1; i++){
        if(buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\0' && found == 0){
            head = &buf[i];
            found = 1;
        }
        if((buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') && found == 1){
            found = 0;
            buf[i] = '\0';
            if(argc < 8){
                temp_argv[argc] = strdup(head);
                argc++;
            }
            else{
                dprintf(2, "Invalid number of arguments\n");
                _exit();
            }
        }
    }
    temp_argv[argc] = NULL;

    // executues prog
    int child_fd = _open(-1, temp_argv[0]);
    if(child_fd < 0){
        dprintf(2, "Cannot open specified file\n");
    }
    else{
        int child_tid = _fork();
        if(child_tid == 0){
            _exec(child_fd, argc, temp_argv);
        }
        while(_wait(0) > 0);
    }
    _close(child_fd);
    _exit();
}