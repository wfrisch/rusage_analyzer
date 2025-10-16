#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s SIZE\n", argv[0]);
		return 1;
	}
	char* foo = malloc(atoi(argv[1]));
	if (!foo) {
		fprintf(stderr, "malloc failed\n");
		return 1;
	}
	return 0;
}
