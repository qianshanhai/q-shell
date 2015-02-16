/*
 * Copyright (C) 2014 Qian Shanhai (qianshanhai@gmail.com)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int init_rand()
{
	unsigned int now = (unsigned int)time(NULL);

	srand(now ^ (unsigned int)getpid());

	return 0;
}

#define RC ((rand() % (127 - 29)) + 29)

struct set_key {
	unsigned int rand;
	char buf[64];
};

int _cmp_key(const void *a, const void *b)
{
	struct set_key *pa = (struct set_key *)a;
	struct set_key *pb = (struct set_key *)b;

	return pa->rand > pb->rand ? 1 : -1;
}

int main(int argc, char *argv[])
{
	int i, j, len, total;
	char *p, buf[128], tmp[128];
	FILE *fp;
	struct set_key key[68];

	init_rand();

	unlink("passwd.c");

	p = getpass("server key: ");
	strcpy(buf, p);
	memset(p, 0, strlen(p));

	if (strlen(buf) < 6) {
		printf("strlen of key must great than 6 !\n");
		return 1;
	} 

	p = getpass("server key again: ");
	strcpy(tmp, p);
	memset(p, 0, strlen(p));

	if (strcmp(buf, tmp) != 0) {
		printf("key not match!\n");
		return 1;
	}

	if ((fp = fopen("passwd.c", "w+")) == NULL) {
		printf("write passwd.c error\n");
		return 1;
	}

	len = strlen(buf);

	fprintf(fp, "#include <string.h>\n");
	for (i = 0; i < len; i++) {
		total = rand() % 7 + 1;
		for (j = 0; j < total; j++) {
			fprintf(fp, "\tunsigned char K%u = %d; \n", rand(), RC);
		}
		fprintf(fp, "\tchar T%d = %d; \n", i, buf[i]);

		total = rand() % 5 + 3;
		for (j = 0; j < total; j++) {
			fprintf(fp, "\tunsigned char K%u = %d; \n", rand(), RC);
		}
		fprintf(fp, "\tint I%d = %d; \n", i, i);
	}
	
	for (i = 0; i < len; i++) {
		fprintf(fp, "\tchar *F%d(int i){return i == 0 ? &T%d : &T%d; } \n",
				i, i, i == len - 1 ? 0 : i + 1);
	}

	fprintf(fp, "char fall()\n{\n\treturn ");
	for (i = 0; i < len; i++) {
		fprintf(fp, " (*F%d(%d)) %s", i, i, i == len - 1 ? ";\n" : "+");
	}
	fprintf(fp, "}\n");

	fprintf(fp, "int __set_key(char *__p)\n{\n"
				"\tchar *x = __p; \n");

	for (i = 0; i < len; i++) {
		key[i].rand = rand() % 5;
		sprintf(key[i].buf, "\tx[I%d] = *F%d(0); \n", i, i);
	}

	qsort(key, len, sizeof(struct set_key), _cmp_key);

	for (i = 0; i < len; i++) {
		fprintf(fp, "%s", key[i].buf);
	}

	fprintf(fp, "\treturn fall(); \n}\n");
	fclose(fp);

	memset(buf, 0, len);
	memset(tmp, 0, len);

	return 0;
}
