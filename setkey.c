/*
 * Copyright (C) 2014 Qian Shanhai (qianshanhai@gmail.com)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	int i, len;
	char *p, buf[128], tmp[128];
	FILE *fp;

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

	fprintf(fp, "void __set_key(char *__p)\n{\n");

	for (i = 0; i < len; i++) {
		fprintf(fp, "__p[%d] = '%c'; \n", i, buf[i]);
	}
	fprintf(fp, "}\n");

	fclose(fp);

	memset(buf, 0, len);
	memset(tmp, 0, len);

	return 0;
}
