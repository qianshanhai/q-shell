/*
 * Copyright (C) 2014 Qian Shanhai (qianshanhai@gmail.com)
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#define ONE_READ_TOTAL 256000

static int m_dev_random_flag = 0;
static int m_fd = -1;
static unsigned int *m_buf = NULL;
static int m_total = ONE_READ_TOTAL;
static int m_pos = ONE_READ_TOTAL - 1;

void my_srand(unsigned int seed)
{
	srand(getpid());

	if ((m_fd = open("/dev/urandom", O_RDONLY)) == -1) {
		if ((m_fd = open("/dev/random", O_RDONLY)) == -1) {
			m_dev_random_flag = 0;
			return;
		}
	}

	m_buf = (unsigned int *)calloc(ONE_READ_TOTAL + 1, sizeof(unsigned int));

	m_dev_random_flag = 1;
}

unsigned int my_rand()
{
	unsigned int next;
	size_t size = ONE_READ_TOTAL * sizeof(unsigned int);

	if (m_dev_random_flag == 0)
		return (unsigned int)rand();

	if (m_pos >= m_total - 1) {
		if (read(m_fd, m_buf, size) != size)
			return (unsigned int)rand();
		m_pos = 0;
	}

	next = m_buf[m_pos++];

	return next;
}

char *my_randomize(char *buf, int len)
{
	unsigned int x, *p;
	int i, total = len / sizeof(int);
	int left = len % sizeof(int);

	for (i = 0; i < total; i++) {
		p = (unsigned int *)(buf + i * sizeof(int));
		*p = my_rand();
	}
	if (left > 0) {
		x = my_rand();
		memcpy(buf + i * sizeof(int), &x, left);
	}

	return buf;
}
