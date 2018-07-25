#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
const char pattern[] = "ATCGATGCTGAGGCCACATT";
size_t len = sizeof(pattern) - 1;
short prefix[sizeof(pattern) - 1];

static void match(const char* __restrict buf, size_t n, const char* __restrict pattern, const short* __restrict prefix)
{
	size_t matched = 0;
	size_t j, line_start = 0;

	uint64_t value[len - 8];

	const char* start = (const char*)memchr(buf, pattern[0], n);
	if(NULL == start) return;

	for(j = start - buf; j < n - 8; j ++)
	{
		/* This is the key part to the optimization: it compres the first
		 * 8 bytes from current location and determine if the ffirst 8 bytes
		 * is matching. If yes, then we are able to start with matched length 8.
		 * If it's not, current match should be 0 */
		if(matched < 1 && *(uint64_t*)(buf + j - matched) == *(uint64_t*)pattern)
			matched = 8, j += 8;

		char ch = buf[j];

		/* In this part we should do this only when matched is larger than 0,
		 * since the first 8 bytes has been matched by the BM matcher */
		if(matched >= 8 && matched < len)
		{
			for(;matched > 0 && ch != pattern[matched];
			     matched = prefix[matched - 1]);
			if(matched != 0 || (ch == pattern[0]))
				matched ++;
		}

		if(ch == '\n')
		{
			if(matched == len)
				fwrite(buf + line_start, 1, j - line_start + 1, stdout);
			matched = 0;
			line_start = j + 1;
		}
	}
}

int main(int argc, char** argv)
{
	size_t i;
	prefix[0] = 0;
	for(i = 1; i < len; i ++)
	{
		for(prefix[i] = prefix[i - 1] + 1;
		    prefix[i] > 1 &&
		    pattern[prefix[i] - 1] != pattern[i];
		    prefix[i] = prefix[prefix[i] - 2] + 1);
		if(prefix[i] == 1 && pattern[0] != pattern[i])
			prefix[i] = 0;
	}

#if 1
	int fd = open(argv[1], O_RDONLY);
	struct stat st;
	fstat(fd, &st);
	off_t sz = (st.st_size + 4095)  & ~(off_t)4095;
	const char* mapped = (const char*)mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);

	match(mapped, st.st_size, pattern, prefix);
#else
	char buf[4096];
	FILE* fp = fopen(argv[1], "r");
	struct stat st;
	while(NULL != fgets(buf, sizeof(buf), fp))
		match(buf, strlen(buf), pattern, prefix);
#endif
	return 0;
}
