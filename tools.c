#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void * xmalloc (size_t what)
{
	void* v = malloc(what);
	if (!v) {
		fprintf(stderr, "out of memory!\n");
		exit(13);
	}
	memset(v, 0, what);
	return v;
}

void * xrealloc (void *ptr, size_t what)
{
	ptr = realloc(ptr, what);
	if (!ptr) {
		fprintf(stderr, "out of memory!\n");
		exit(13);
	}
	return ptr;
}

char * strncpyz (char * dest, char const * src, size_t n)
{
	dest[n] = 0;
	return strncpy(dest, src, n);
}
