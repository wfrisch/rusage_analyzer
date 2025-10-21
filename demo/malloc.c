#include <stdio.h>
#include <stdlib.h>
#include <memory.h>


void print_usage(const char* prog_name) {
	fprintf(stderr, "usage: %s SIZE\n", prog_name);
}


int main(int argc, char** argv) {
	int len;
	char* buf;

	if (argc != 2) {
		print_usage(argv[0]);
		return 1;
	}

	len = atoi(argv[1]);
	if (len < 0) {
		print_usage(argv[0]);
		return 1;
	}

	buf = malloc(len);
	if (!buf) {
		perror("malloc");
		return 1;
	}

	memset(buf, 0, len);
	printf("%s", buf);  // memset would be optimized out otherwise

	return 0;
}
