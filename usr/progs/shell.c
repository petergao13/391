#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../heap.h"

#define BUFSIZE 1024
#define MAXARGS 8

struct prog{
	int argc;
	char * argv[MAXARGS + 1];
	struct prog * next;
};

/**
* @brief parses a set of files and returns the operation after that set of files: <, >, |, '\0',
* @param fd file descriptor number
* @param path string path to file
* @return new buf head
*/
char * parse(char* buf, int * argc, char** argv, char * op) {
	*argc = 0;
	char * head;
	char * tail = buf;
	for(;;){
		head = tail;
		while(*head == ' '){
			head++;
		}
		tail = head;
		while(*tail != ' ' && *tail != '\0' && *tail != FOUT && *tail != FIN && *tail != PIPE){
			tail++;
		}
		*op = *tail;
		*tail = '\0';
		tail++;
		if(*argc < 8){
			argv[*argc] = head;
			*argc += 1;
		}
		else{
			*argc = -1;
			return NULL;
		}
		if(*op == ' '){
			while(*tail == ' '){
				tail++;
			}
			if(*tail == '\0' || *tail == FOUT || *tail == FIN || *tail == PIPE){
				*op = *tail;
				tail++;
			}
		}
		if(*op == '\0' || *op == FOUT || *op == FIN || *op == PIPE){
			return tail;
		}
	}
	return tail;
}

int main()
{
    char buf[BUFSIZE];
	int argc;
	char* argv[MAXARGS + 1];

  	_open(CONSOLEOUT, "dev/uart1");		// console device
	_close(STDIN);              		// close any existing stdin
	_uiodup(CONSOLEOUT, STDIN);      	// stdin from console
	_close(STDOUT);              		// close any existing stdout
	_uiodup(CONSOLEOUT, STDOUT);     	// stdout to console

	printf("Starting 391 Shell\n");

	for (;;)
	{
		printf("LUMON OS> ");
		getsn(buf, BUFSIZE - 1);

		if (0 == strcmp(buf, "exit")) {
			_exit();
		}

		if(buf[0] == '\0'){
			continue;
		}

		// FIXME
		// Call your parse function and exec the user input
		struct prog * prog_list_head = NULL;
		struct prog * prog_list_tail = NULL;
		struct prog * prog_in = NULL;
		struct prog * prog_out = NULL;
		struct prog * prog = NULL;
		char next_op = -1;
		char prev_op = -1;
		char * buffer = buf;
		int retVal;

		// create prog structs for each program and add to appropriate list
		while(next_op != '\0'){
			buffer = parse(buffer, &argc, argv, &next_op);
			if(argc == -1){
				printf("Invalid number of arguments\n");
				break;
			}

			// make and initialize prog
			prog = calloc(1, sizeof(struct prog));
			prog->argc = argc;
			prog->next = NULL;
			// copy argv
			for(int i = 0; i < argc; i++){
				if(i == 0 && strchr(argv[0], '/') == NULL && prev_op != FIN && prev_op != FOUT){
					char temp[strlen(argv[i])+3];
					snprintf(temp, sizeof(temp), "c/%s", argv[i]);
					prog->argv[i] = strdup(temp);
				}
				else{
					prog->argv[i] = strdup(argv[i]);
				}
			}
			prog->argv[argc] = NULL;

			// add program to prog_list
			if(prev_op != FIN && prev_op != FOUT){
				if(prog_list_tail == NULL){
					prog_list_head = prog;
				}
				else{
					prog_list_tail->next = prog;
				}
				prog_list_tail = prog;
			}
			// add program to prog_in
			else if(prev_op == FIN){
				prog_in = prog;
			}
			// add program to prog_out
			else if(prev_op == FOUT){
				prog_out = prog;
			}
			prev_op = next_op;
		}

		if(argc == -1){
			continue;
		}

		int wfd = -1;
		int rfd = -1;

		prog = prog_list_head;
		while(prog != NULL){
			_close(STDIN);
			_uiodup(CONSOLEOUT, STDIN);
			_close(STDOUT);
			_uiodup(CONSOLEOUT, STDOUT);
			if(prog == prog_list_head && prog == prog_list_tail){
				// in
				if(prog_in != NULL){
					_close(STDIN);
					_open(STDIN, prog_in->argv[0]);
				}
				// out
				if(prog_out != NULL){
					_close(STDOUT);
					retVal = _open(STDOUT, prog_out->argv[0]);
					if(retVal < 0){
						retVal = _fscreate(prog_out->argv[0]);
						if(retVal < 0){
							dprintf(2, "Could not create file: %s\n", prog_out->argv[0]);
							break;
						}
						_open(STDOUT, prog_out->argv[0]);
					}
				}
			}
			else if(prog == prog_list_head){
				// in
				if(prog_in != NULL){
					_close(STDIN);
					_open(STDIN, prog_in->argv[0]);
				}
				// out
				_pipe(&wfd, &rfd);
				_close(STDOUT);
				_uiodup(wfd, STDOUT);
				_close(wfd);
				wfd = -1;
			}
			else if(prog == prog_list_tail){
				// in
				_close(STDIN);
				_uiodup(rfd, STDIN);
				_close(rfd);
				rfd = -1;
				// out
				if(prog_out != NULL){
					_close(STDOUT);
					retVal = _open(STDOUT, prog_out->argv[0]);
					if(retVal < 0){
						retVal = _fscreate(prog_out->argv[0]);
						if(retVal < 0){
							dprintf(2, "Could not create file: %s\n", prog_out->argv[0]);
							break;
						}
						_open(STDOUT, prog_out->argv[0]);
					}
				}
			}
			else{
				// in
				_close(STDIN);
				_uiodup(rfd, STDIN);
				_close(rfd);
				rfd = -1;
				// out
				_pipe(&wfd, &rfd);
				_close(STDOUT);
				_uiodup(wfd, STDOUT);
				_close(wfd);
				wfd = -1;
			}
			int child_fd = _open(-1, prog->argv[0]);
			if(child_fd < 0){
				dprintf(2, "Cannot open specified file\n");
			}
			else{
				int child_tid = _fork();
				if(child_tid == 0){
					_exec(child_fd, prog->argc, prog->argv);
				}
				_close(child_fd);
			}
			prog = prog->next;
		}
		while(_wait(0) > 0);
	}
}