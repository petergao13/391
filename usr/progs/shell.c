#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../heap.h"

#define BUFSIZE 1024
#define MAXARGS 8
#define MAXCMDS 8

static int is_space(char c) {
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static void trim_eol(char *s) {
	size_t n = strlen(s);
	while (n > 0) {
		char c = s[n - 1];
		if (c != '\n' && c != '\r') {
			break;
		}
		s[n - 1] = '\0';
		n--;
	}
}

static char *dup_range(const char *start, size_t n) {
	char *out = malloc(n + 1);
	if (!out) return NULL;
	memcpy(out, start, n);
	out[n] = '\0';
	return out;
}

struct command {
	int argc;
	char *argv[MAXARGS + 1]; // argv[argc] == NULL
	char *infile;
	char *outfile;
};

static void free_command(struct command *cmd) {
	for (int i = 0; i < cmd->argc; i++) {
		free(cmd->argv[i]);
		cmd->argv[i] = NULL;
	}
	cmd->argc = 0;
	if (cmd->infile) {
		free(cmd->infile);
		cmd->infile = NULL;
	}
	if (cmd->outfile) {
		free(cmd->outfile);
		cmd->outfile = NULL;
	}
}

static char *dup_resolve_prog(const char *word) {
	if (strchr(word, '/') != NULL) {
		return strdup(word);
	}
	// Default user commands live in c/.
	char temp[strlen(word) + 3];
	snprintf(temp, sizeof(temp), "c/%s", word);
	return strdup(temp);
}

static int parse_line(char *buf, struct command *cmds, int *cmd_count_out) {
	// Parse into up to MAXCMDS pipeline stages.
	for (int i = 0; i < MAXCMDS; i++) {
		cmds[i].argc = 0;
		for (int j = 0; j < MAXARGS + 1; j++) cmds[i].argv[j] = NULL;
		cmds[i].infile = NULL;
		cmds[i].outfile = NULL;
	}

	int cmd_count = 1;
	char *p = buf;

	while (*p != '\0') {
		while (is_space(*p)) p++;
		if (*p == '\0') break;

		if (*p == PIPE) {
			// Finalize current stage.
			if (cmds[cmd_count - 1].argc == 0) {
				printf("Invalid command before '|'\n");
				return -1;
			}
			if (cmd_count >= MAXCMDS) {
				printf("Too many pipeline commands\n");
				return -1;
			}
			cmd_count++;
			p++; // consume '|'
			continue;
		}

		if (*p == FIN || *p == FOUT) {
			char op = *p;
			p++; // consume '<' or '>'
			while (is_space(*p)) p++;
			if (*p == '\0') {
				printf("Missing file after redirection\n");
				return -1;
			}
			const char *start = p;
			while (*p != '\0' && !is_space(*p) && *p != FIN && *p != FOUT && *p != PIPE) p++;
			size_t n = p - start;
			if (n == 0) {
				printf("Missing file after redirection\n");
				return -1;
			}
			char *file = dup_range(start, n);
			if (!file) {
				printf("Out of memory\n");
				return -1;
			}
			if (op == FIN) cmds[cmd_count - 1].infile = file;
			else cmds[cmd_count - 1].outfile = file;
			continue;
		}

		// Parse a word => argv token.
		const char *start = p;
		while (*p != '\0' && !is_space(*p) && *p != FIN && *p != FOUT && *p != PIPE) p++;
		size_t n = p - start;
		if (n == 0) continue;
		if (cmds[cmd_count - 1].argc >= MAXARGS) {
			printf("Invalid number of arguments\n");
			return -1;
		}
		char *word = dup_range(start, n);
		if (!word) {
			printf("Out of memory\n");
			return -1;
		}

		if (cmds[cmd_count - 1].argc == 0) {
			// Resolve program path from the first argv token.
			char *resolved = dup_resolve_prog(word);
			free(word);
			if (!resolved) return -1;
			cmds[cmd_count - 1].argv[0] = resolved;
		} else {
			cmds[cmd_count - 1].argv[cmds[cmd_count - 1].argc] = word;
		}
		cmds[cmd_count - 1].argc++;
	}

	if (cmds[cmd_count - 1].argc == 0) {
		printf("Invalid empty command\n");
		return -1;
	}

	// NULL terminate argv arrays.
	for (int i = 0; i < cmd_count; i++) {
		cmds[i].argv[cmds[i].argc] = NULL;
	}
	*cmd_count_out = cmd_count;
	return 0;
}

static void exec_pipeline(struct command *cmds, int cmd_count) {
	int pipe_w[MAXCMDS];
	int pipe_r[MAXCMDS];
	for (int i = 0; i < MAXCMDS; i++) {
		pipe_w[i] = -1;
		pipe_r[i] = -1;
	}

	// Create pipes for N commands.
	for (int i = 0; i < cmd_count - 1; i++) {
		(void)_pipe(&pipe_w[i], &pipe_r[i]);
	}

	for (int i = 0; i < cmd_count; i++) {
		int child_tid = _fork();
		if (child_tid == 0) {
			// Redirect STDIN for this stage.
			if (cmds[i].infile != NULL) {
				int fd = _open(-1, cmds[i].infile);
				if (fd < 0) {
					dprintf(2, "Cannot open input file: %s\n", cmds[i].infile);
					_exit();
				}
				_close(STDIN);
				_uiodup(fd, STDIN);
				_close(fd);
			} else if (i > 0) {
				_close(STDIN);
				_uiodup(pipe_r[i - 1], STDIN);
			}

			// Redirect STDOUT for this stage.
			if (cmds[i].outfile != NULL) {
				int fd = _open(-1, cmds[i].outfile);
				if (fd < 0) {
					if (_fscreate(cmds[i].outfile) < 0) {
						dprintf(2, "Could not create file: %s\n", cmds[i].outfile);
						_exit();
					}
					fd = _open(-1, cmds[i].outfile);
				}
				if (fd < 0) {
					dprintf(2, "Could not open file: %s\n", cmds[i].outfile);
					_exit();
				}
				_close(STDOUT);
				_uiodup(fd, STDOUT);
				_close(fd);
			} else if (i < cmd_count - 1) {
				_close(STDOUT);
				_uiodup(pipe_w[i], STDOUT);
			}

			// Close all pipe fds (children should not keep extra ends open).
			for (int j = 0; j < cmd_count - 1; j++) {
				if (pipe_r[j] >= 0) _close(pipe_r[j]);
				if (pipe_w[j] >= 0) _close(pipe_w[j]);
			}

			int child_fd = _open(-1, cmds[i].argv[0]);
			if (child_fd < 0) {
				dprintf(2, "Cannot open specified file\n");
				_exit();
			}
			_exec(child_fd, cmds[i].argc, cmds[i].argv);
			_exit();
		}
	}

	// Parent: close all pipes so readers see EOF.
	for (int i = 0; i < cmd_count - 1; i++) {
		if (pipe_r[i] >= 0) _close(pipe_r[i]);
		if (pipe_w[i] >= 0) _close(pipe_w[i]);
	}

	while (_wait(0) > 0) {
		// Drain all children.
	}
}

int main()
{
    char buf[BUFSIZE];

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

		trim_eol(buf);

		if (0 == strcmp(buf, "exit")) {
			_exit();
		}

		if(buf[0] == '\0'){
			continue;
		}

		struct command cmds[MAXCMDS];
		int cmd_count = 0;
		if (parse_line(buf, cmds, &cmd_count) < 0) {
			// parse_line prints an error message; just continue.
			for (int i = 0; i < MAXCMDS; i++) free_command(&cmds[i]);
			continue;
		}

		exec_pipeline(cmds, cmd_count);

		for (int i = 0; i < cmd_count; i++) free_command(&cmds[i]);
	}
}