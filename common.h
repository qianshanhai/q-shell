/*
 * Copyright (C) 2014 Qian Shanhai (qianshanhai@gmail.com)
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <termios.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <paths.h>
#include <fnmatch.h>
#include <sys/mman.h>
#include <pwd.h>
#include <grp.h>
#include <utime.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <arpa/inet.h>

#include "my_rand.h"

#define CMD_TO1 2000000
#define CMD_TO2 4000000

#define ROOT_SHELL 201
#define ROOT_CMD 202
#define ROOT_GET 203
#define ROOT_PUT 204
#define ROOT_UPDATE 205
#define CHANGE_PASSWD 206

#include "config.h"

#define ONCE_SIZE 128
#define MUL 256
#define HEAD_LEN 16
#define PUT_LEN 896
#define GET_LEN 896

#define Ctrl(c)         ((c) & 037)
#define PUT_PATH_CHAR  '\x1'

struct winsize win;
int remote_mode = 0;

extern int grantpt (int __fd);
extern int unlockpt (int __fd);
extern char **environ;

int sock_fd = -1, sock_in = -1, sock_out = -1;

typedef void (*sighandler_t)(int);

sighandler_t my_signal(int sig, sighandler_t func)
{
	struct sigaction act, oact;

	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	if (sig == SIGALRM) {
#ifdef SA_INTERRUPT
		act.sa_flags |= SA_INTERRUPT;
#endif
	} else {
#ifdef SA_RESTART
		act.sa_flags |= SA_RESTART;
#endif
	}
	if (sigaction(sig, &act, &oact) == -1)
		return SIG_ERR;

	return oact.sa_handler;
}

int dashf(const char *fname)
{
	struct stat st;

	return (stat(fname, &st) == 0 && S_ISREG(st.st_mode));
}

int filesize(const char *fname)
{
	struct stat st;

	if (stat(fname, &st) == 0)
		return (int)st.st_size;

	return 0;
}

int dashx(const char *fname)
{
	struct stat st;

	return (stat(fname, &st) == 0 && (st.st_mode & 0755));
}

int dashd(const char *fname)
{
	struct stat st;

	return (stat(fname, &st) == 0 && S_ISDIR(st.st_mode));
}

void *mapfile(const char *file, int *size)
{  
	int fd;
	struct stat st;
	void *p;

	if ((fd = open(file, O_RDONLY)) == -1)
		return NULL;

	if (fstat(fd, &st) == -1 || st.st_size <= 0) {
		close(fd);
		return NULL;
	}

	p = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (p == (void *)-1)
		return NULL;

	*size = (int)st.st_size;

	return p;
}

int f_cp(const char *src, const char *dst, int mode)
{
	char *p;
	int fd, size, ret = -1;

	if ((p = mapfile(src, &size)) == NULL)
		return -1;

	if ((fd = open(dst, O_WRONLY | O_CREAT | mode, 0750)) != -1) {
		if (write(fd, p, size) == size)
			ret = 0;
		close(fd);
	}
	munmap(p, size);

	return ret;
}

char *ltrim(char *p)
{
	for (; *p == ' ' || *p == '\t'; p++);
	return p;
}

char *rtrim(char *p)
{
	int i, len = strlen(p);

	for (i = len - 1; i >= 0; i--) {
		if (p[i] != ' ' && p[i] != '\t')
			break;
		p[i] = 0;
	}
	return p;
}

char *trim(char *p)
{
	rtrim(p);

	return ltrim(p);
}

char *chomp(char *p)
{
	int i, len = strlen(p);

	for (i = len - 1; i >= 0; i--) {
		if (p[i] != '\r' && p[i] != '\n')
			break;
		p[i] = 0;
	}

	return p;
}

char **split(char c, const char *s, int *len)
{
	static char buf[BUFSIZ];
	static char *sp[128] = {buf};
	char *m = buf, *t = (char *)s;
	int y = 0, x = sizeof(sp) / sizeof(char*);

	for(; (*m = *t) && (t - s) < sizeof(buf); t++, m++) {
		if(*t == c) {
			*m = 0;
			sp[(++y) % x] = m + 1;
		}
	}
	*m = 0;
	*len = ++y;

	return sp;
}

int set_fl(int fd, int flag)
{
	int val;

	if ((val = fcntl(fd, F_GETFL, 0)) == -1)
		return -1;

	val |= flag;

	if (fcntl(fd, F_SETFL, val) == -1)
		return -1;

	return 0;
}

int winsize_pack(const struct winsize *w, char *buf)
{
	int t;

	t = sprintf(buf, "%04d %04d %04d %04d ", (int)w->ws_row,
			(int)w->ws_col, (int)w->ws_xpixel, (int)w->ws_ypixel);
	return t;
}

int winsize_unpack(struct winsize *w, char *buf)
{
	buf[4] = buf[9] = buf[14] = buf[19] = 0;

	w->ws_row = (unsigned short int)atoi(buf);
	w->ws_col = (unsigned short int)atoi(buf + 5);
	w->ws_xpixel = (unsigned short int)atoi(buf + 10);
	w->ws_ypixel = (unsigned short int)atoi(buf + 15);
}

int _read_data(int fd, char *buf, int len, int time)
{
	fd_set rfds;
	struct timeval tv;
	int n, remain = len;

	int sec = time / 1000000;
	int usec = time % 1000000;

	while (1) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		tv.tv_sec = sec;
		tv.tv_usec = usec;

		n = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (n == -1)
			return -1;
		else if (n == 0)
			return 0;

		if (FD_ISSET(fd, &rfds)) {
			if ((n = read(fd, buf, len)) <= 0) {
				return -1;
			}
			return n;
		}
	}

	return 0;
}

int read_data(int fd, char *buf, int len)
{
	return _read_data(fd, buf, len, 5000000);
}

int read_data_quick(int fd, char *buf, int len)
{
	return _read_data(fd, buf, len, 2000000);
}

int read_line(int fd, char *buf, int len)
{
	fd_set rfds;
	struct timeval tv;
	char ch;
	int n, copy = 0;

	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		tv.tv_sec = 2;
		tv.tv_usec = 0;
		if ((n = select(fd + 1, &rfds, NULL, NULL, &tv)) <= 0)
			break;

		if (FD_ISSET(fd, &rfds)) {
			if ((n = read(fd, &ch, 1)) == 1) {
				buf[copy++] = ch;
				if (ch == '\n')
					break;
			}
		}
	}

	return copy;
}

#include "blowfish.h"

static KOC_CTX *ctx = NULL;
static char *passwd = NULL;

int init_key(const char *key, int len)
{
	ctx = (KOC_CTX *)malloc(sizeof(*ctx));

	koc_init(ctx, key, len);

	return 0;
}

int my_encrypt(const unsigned char *in, unsigned char *out, int len)
{
	int i;

	memcpy(out, in, len);

	for (i = 1; i < len; i++)
		out[i] ^= out[i - 1];

	return my_crypt(koc_encrypt, htonl, ctx, (char *)out, (char *)out, len);
}

int my_decrypt(const unsigned char *in, unsigned char *out, int len)
{
	int i;

	if (my_crypt(koc_decrypt, ntohl, ctx, (char *)in, (char *)out, len) == -1)
		return -1;

	for (i = len - 1; i > 0; i--)
		out[i] ^= out[i - 1];

	return len;
}

#if defined(__hpux) || defined(__linux__)
char *ptsname(int);
#endif

int master;
struct termios origtty;
struct winsize win;

void _daemon(int flag)
{
	int n, i;

	chdir("/tmp");

	if (flag) {
		n = getdtablesize();
		for (i = 0; i <= n; i++)
			close(i);
	}

	if (fork())
		exit(0);

	if (setsid() == -1)
		exit(0);

	if (fork())
		exit(0);

	for (n = 1; n <= NSIG; n++)
		my_signal(n, SIG_IGN);
}

void sig_chld(int signo)
{
	pid_t   pid;
	int     stat;

	while ((pid = waitpid(-1, &stat, WNOHANG)) > 0);

	return;
}

void set_default_signal()
{
	my_signal(SIGHUP, SIG_DFL);
	my_signal(SIGTERM, SIG_DFL);
	my_signal(SIGINT, SIG_DFL);
	my_signal(SIGQUIT, SIG_DFL);
	my_signal(SIGTSTP, SIG_DFL);
}

struct data {
	unsigned char buf[ONCE_SIZE * MUL];
	unsigned char tmp[ONCE_SIZE * MUL];
	int len;
};

struct data *in_data = NULL;

void init_in_data()
{
	if (in_data == NULL)
		in_data = (struct data *)malloc(sizeof(struct data));
	memset(in_data, 0, sizeof(*in_data));
}

int file_append(const char *file, const void *data, int size)
{
	int fd, ret = 0;

	if ((fd = open(file, O_RDWR | O_APPEND | O_CREAT, 0644)) == -1) {
		return -1;
	}

	if (write(fd, data, size) != size) {
		ret = -1;
	}
	close(fd);

	return ret;
}

int debug(const char *fmt, ...)
{
	int n;
	va_list ap;
	char buf[4096];
	
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	file_append("my_server.log", buf, n);

	return n;
}

int get_len(unsigned char *msg, int *len, int *pad)
{
	unsigned char head[HEAD_LEN+1];

	if (my_decrypt(msg, head, HEAD_LEN) == -1)
		return -1;

	*len = (int)head[5] * 255 * 255 + (int)head[7] * 255 + (int)head[11];
	*pad = (int)head[13];

	return *len;
}

int set_len(unsigned char *msg, int len)
{
	unsigned char head[HEAD_LEN+1];

	my_randomize(head, HEAD_LEN);

	head[5] = len / 255 / 255;
	head[7] = (len - head[5] * 255 * 255) / 255;
	head[11] = len - head[5] * 255 * 255 - head[7] * 255;
	head[13] = (len % 8 == 0 ? 0 : (8 - len % 8));

	my_encrypt(head, msg, HEAD_LEN);

	return (int)head[13];
}

int unpack(unsigned char *msg, const char *buf, int len)
{
	int dlen, pad;

	if (len > 0)
		memcpy(in_data->buf + in_data->len, buf, len);

	in_data->len += len;

	if (in_data->len < (int)HEAD_LEN)
		return 0;

	if (get_len(in_data->buf, &dlen, &pad) == -1)
		return -1;

	if (dlen > in_data->len - (int)HEAD_LEN)
		return 0;

	if (my_decrypt(in_data->buf + HEAD_LEN, msg, dlen + pad) == -1)
		return -1;

	in_data->len -= ((int)HEAD_LEN + dlen + pad);

	if (in_data->len > 0) {
		memmove(in_data->buf, in_data->buf + HEAD_LEN + dlen + pad, in_data->len);
	}

	return dlen;
}

int pack(unsigned char *msg, const char *buf, int len)
{
	int pad = set_len(msg, len);

	my_encrypt((unsigned char *)buf, msg + HEAD_LEN, len + pad);

	return len + pad + HEAD_LEN;
}

#define max(x, y) ((x) > (y) ? (x) : (y))
#define max3(x, y, z) (max((x), (y)) > (z) ? max((x), (y)) : (z))

unsigned char *__msg;
char *__buf;

void loop(pid_t pid)
{
	int t, n;
	unsigned int count = 0;
	unsigned char *msg = __msg;
	char *buf = __buf;
	fd_set rmask;
	struct timeval tv;
	time_t last, now;

	init_in_data();
	time(&last);

	for (;;) {
		FD_ZERO(&rmask);
		FD_SET(sock_fd, &rmask);
		FD_SET(sock_in, &rmask);

		tv.tv_sec = 0;
		tv.tv_usec = pid > 0 ? 200000 : 100000;

		if ((t = unpack(msg, buf, 0)) > 0) {
			if (write(sock_out, msg, t) != t)
				break;
		}

		if (pid == 0) {
			time(&now);
			if (now - last > 150) {
				buf[0] = Ctrl('Q');
				if ((t = pack(msg, buf, 1)) > 0)
					write(sock_fd, msg, t);
				time(&last);
			}
		}

		n = select(max(sock_fd, sock_in) + 1, &rmask, NULL, NULL, &tv);

		if (n < 0) {
			if (errno != EINTR)
				break;
			else
				continue;
		} else if (n == 0)
			continue;

		if (pid == 0)
			time(&last);

		if (FD_ISSET(sock_fd, &rmask)) {
			if ((n = read(sock_fd, buf, ONCE_SIZE + HEAD_LEN)) <= 0)
				break;
			if ((t = unpack(msg, buf, n)) > 0) {
				if (pid > 0) {
					if (msg[0] == 255 && msg[1] == 255 && t == 22) {
						winsize_unpack(&win, (char *)msg + 2);
						if (ioctl(STDIN_FILENO, TIOCSWINSZ, &win) == 0)
							kill(pid, SIGWINCH);
					} else {
						if (write(sock_out, msg, t) != t)
							break;
					}
				} else {
					if (write(sock_out, msg, t) != t)
						break;
				}
			}
		}

		if (FD_ISSET(sock_in, &rmask)) {
			if ((n = read(sock_in, buf, ONCE_SIZE)) <= 0)
				break;
			if ((t = pack(msg, buf, n)) > 0) {
				if (pid > 0) {
					if (count++ % 10 == 0)
						usleep(100);
				}
				if (write(sock_fd, msg, t) != t)
					break;
			}
		}
	}
}

char *get_version(char *v)
{
	char *p, tmp[] = _VERSION;

	if ((p = strchr(tmp, '.')))
		*p = 0;

	strcpy(v, tmp);

	return v;
}

int my_crypt(void (*func)(const KOC_CTX *ctx, unsigned int *l, unsigned int *r),
		uint32_t (*hn)(uint32_t r),
		KOC_CTX *ctx, const char *in, char *out, int len)
{
	int i;
	unsigned int left, right;

	if (len % 8 != 0)
		return -1;

	for (i = 0; i < len / 8; i++) {
		left = hn(*((unsigned int *)(in + i * 8)));
		right = hn(*((unsigned int *)(in + i * 8 + 4)));
		func(ctx, &left, &right);
		*((unsigned int *)(out + i * 8)) = hn(left);
		*((unsigned int *)(out + i * 8 + 4)) = hn(right);
	}

	return len;
}

#define V_LEN 128

int client_verify_version(int fd)
{
	int len;
	char tmp[32];
	unsigned char buf[V_LEN], msg[V_LEN];

	my_randomize(buf, V_LEN);

	buf[50] = 'o';
	buf[80] = 'k';

	strcpy((char *)buf + 25, get_version(tmp));

	if (my_encrypt(buf, msg, V_LEN) == -1)
		return -1;

	if (send(fd, msg, V_LEN, 0) != V_LEN)
		return -1;

	if (read_data(fd, (char *)buf, V_LEN) != V_LEN)
		return -1;

	if (my_decrypt(buf, msg, V_LEN) == -1)
		return -1;

	if (msg[50] == 'O' && msg[80] == 'K') {
		printf("version not match: local = %s, remote = %s.\n",
				get_version(tmp), (char *)msg + 25);
		return -1;
	}

	if (msg[50] != 'o' || msg[80] != 'k')
		return -1;

	return 0;
}

int server_verify_version(int fd)
{
	int len, flag = 0;
	char tmp[32], tmp2[32];
	unsigned char buf[V_LEN], msg[V_LEN];

	if (read_data(fd, (char *)buf, V_LEN) != V_LEN)
		return -1;

	if (my_decrypt(buf, msg, V_LEN) == -1)
		return -1;

	if (msg[50] != 'o' || msg[80] != 'k')
		return -1;

	strcpy(tmp2, (char *)msg+ 25);

	my_randomize(buf, V_LEN);

	get_version(tmp);

	if (strcmp(tmp, tmp2) == 0) {
		buf[50] = 'o';
		buf[80] = 'k';
	} else {
		buf[50] = 'O';
		buf[80] = 'K';
		flag = -1;
	}

	strcpy((char *)buf + 25, tmp);

	if (my_encrypt(buf, msg, V_LEN) == -1)
		return -1;

	if (send(fd, msg, V_LEN, 0) != V_LEN)
		return -1;

	return 0;
}

typedef unsigned char verify_buf_t[1024];

int verify_server(int fd)
{
	verify_buf_t r0, r2, tmp, buf;

	/* 1. send blowfish(r0) */

	my_randomize((char *)r0, V_LEN);
	my_encrypt(r0, tmp, V_LEN);
	write(fd, tmp, V_LEN);

	/* 2. receive blowfish(r1 + r0), check r0 */

	if (read_data(fd, buf, V_LEN * 2) != V_LEN * 2)
		return -1;

	my_decrypt(buf, tmp, V_LEN * 2);

	if (memcmp(tmp + V_LEN, r0, V_LEN) != 0)
		return -1;

	/* 3. send blowfish(r2 + r1) */

	my_randomize((char *)r2, V_LEN);
	memcpy(r2 + V_LEN, tmp, V_LEN);

	my_encrypt(r2, buf, V_LEN * 2);
	write(fd, buf, V_LEN * 2);

	return 0;
}

int verify_client(int fd)
{
	verify_buf_t r1, tmp, buf;

	/* 1. read server r0, send blowfish(r1 + r0) */

	if (read_data(fd, buf, V_LEN) != V_LEN)
		return -1;

	my_decrypt(buf, tmp, V_LEN);

	my_randomize(r1, V_LEN);
	memcpy(r1 + V_LEN, tmp, V_LEN);

	my_encrypt(r1, tmp, V_LEN * 2);
	write(fd, tmp, V_LEN * 2);

	/* 2. read server r1, and check r1 */

	if (read_data(fd, buf, V_LEN * 2) != V_LEN  * 2)
		return -1;
	my_decrypt(buf, tmp, V_LEN * 2);

	if (memcmp(r1, tmp + V_LEN, V_LEN) != 0)
		return -1;

	return 0;
}

int get_file(const char *file, int sock)
{
	int n, t;
	FILE *fp;
	char *buf = __buf;
	unsigned char *msg = __msg;

	if ((fp = fopen(file, "w+")) == NULL)
		return -1;

	while ((n = read_data(sock, buf, PUT_LEN + HEAD_LEN)) > 0) {
		if ((t = unpack(msg, buf, n)) > 0) {
			fwrite(msg, t, 1, fp);
		}
	}
	if ((t = unpack(msg, buf, 0)) > 0) {
		fwrite(msg, t, 1, fp);
	}
	fclose(fp);

	return 0;
}

int put_file(const char *file, int sock)
{
	int n, t, size, remain;
	char *p, *buf = __buf, tmp[12];
	unsigned char *msg = __msg;

	if ((p = mapfile(file, &size)) == NULL)
		return -1;

	remain = size;

	while (1) {
		n = remain > GET_LEN ? GET_LEN : remain;
		if (n <= 0)
			break;
		if ((t = pack(msg, p + size - remain, n)) > 0) {
			if (write(sock, msg, t) != t) {
				munmap(p, size);
				return -1;
			}
		}
		remain -= n;
	}
	munmap(p, size);

	return 0;
}

void my_setrlimit(rlim_t cur, rlim_t max)
{
	struct rlimit r;

	r.rlim_cur = cur;
	r.rlim_max = max;
	setrlimit(RLIMIT_CPU, &r);
}
