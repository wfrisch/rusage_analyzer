#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <unistd.h>


void print_usage(const char* prog_name) {
	fprintf(stderr, "usage: %s [1|0]\n", prog_name);
	fprintf(stderr, "create a temporary file and, if the argument is 1, writes 4 KiB\n");
	fprintf(stderr, "detectable with `ru_oublock`\n");
}


int main(int argc, char** argv) {
	const int len = 4096;
	char buf[len];
	bool do_write = false;
	char out_name[] = "demo_io.XXXXXX";
	int out_fd;

	if (argc != 2) {
		print_usage(argv[0]);
		return 1;
	}
	do_write = (strcmp(argv[1], "1") == 0);

	memset(buf, ' ', len);
	
	if ((out_fd = mkstemp(out_name)) == -1) {
		perror("mkstemp");
		return 1;
	}

	if (do_write) {
		if (write(out_fd, buf, len) == -1) {
			perror("write");
			return 1;
		}
	}

	close(out_fd);
	unlink(out_name);

	return 0;
}
