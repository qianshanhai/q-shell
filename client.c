/*
 * Copyright (C) 2014 Qian Shanhai (qianshanhai@gmail.com)
 */

#include "common.h"

static int m_raw_mode = 0;

static char m_cmd_string[512];
static char m_local_path[512];
static char m_remote_path[512];

void sig_win(int sig)
{
	int n, t;
	struct winsize w;
	char *buf = __buf;
	unsigned char *msg = __msg;

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &w) == 0) {
		memcpy(&win, &w, sizeof(w));
		buf[0] = buf[1] = 255;
		t = winsize_pack(&w, buf + 2);
		if ((n = pack(msg, buf, 2 + t)) > 0)
			write(sock_fd, msg, n);
	}
}

void enter_raw_mode()
{
	struct termios tio;

	if (tcgetattr(STDIN_FILENO, &tio) == -1)
		return;

	ioctl(STDIN_FILENO, TIOCGWINSZ, &win);

	memcpy(&origtty, &tio, sizeof(tio));

	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
	tio.c_cc[VLNEXT] = Ctrl('V');
#ifdef VDSUSP
	tio.c_cc[VDSUSP] = 0;
#endif

	tio.c_lflag &= ~(ECHO | ECHOE | ECHOK | ICANON);

	tio.c_lflag |= ISIG;
	tio.c_cflag |= CS8;
	tio.c_oflag |= OPOST;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) == 0)
		m_raw_mode = 1;
}

void leave_raw_mode()
{
	if (!m_raw_mode)
		return;

	ioctl(STDIN_FILENO, TIOCSWINSZ, &win);

	if (tcsetattr(STDIN_FILENO, TCSADRAIN, &origtty) == 0)
		m_raw_mode = 0;
}

void sig_send(int sig)
{
	int t;
	unsigned char msg[64];
	char ch = 0;

	switch (sig) {
		case SIGINT:
			ch = Ctrl('C');
			break;
		case SIGQUIT:
			ch = Ctrl('\\');
			break;
		case SIGTSTP:
			ch = Ctrl('Z');
			break;
		default:
			ch = (char)sig;
	}

	if ((t = pack(msg, &ch, 1)) > 0)
		write(sock_fd, msg, t);
}

void client_cmd()
{
	int n, t;
	char *buf = __buf, tmp[12];
	unsigned char *msg = __msg;

	init_in_data();

	if ((n = pack(msg, m_cmd_string, GET_LEN)) <= 0)
		return;

	write(sock_fd, msg, n);

	while ((n = _read_data(sock_in, buf, GET_LEN, 200000)) > 0) {
		if ((t = pack(msg, buf, n)) > 0) {
			write(sock_fd, msg, t);
#ifdef SEND_DEBUG
			n = read_data_quick(sock_fd, tmp, TRANS_LEN);
#endif
		}	
	}

	while ((n = _read_data(sock_fd, buf, GET_LEN + HEAD_LEN, CMD_TO2)) > 0) {
		if ((t = unpack(msg, buf, n)) > 0) {
			write(sock_out, msg, t);
#ifdef SEND_DEBUG
			write(sock_fd, TRANS_STRING, TRANS_LEN);
#endif
		}
	}

	if ((t = unpack(msg, NULL, 0)) > 0) {
		write(sock_out, msg, t);
#ifdef SEND_DEBUG
		write(sock_fd, TRANS_STRING, TRANS_LEN);
#endif
	}
}

void client_update()
{
	int n;
	char tmp[256];
	unsigned char *msg = __msg;

	n = filesize(m_local_path);
	
	sprintf(tmp, "%d", n);
	if ((n = pack(msg, tmp, strlen(tmp))) > 0) {
		write(sock_fd, msg, n);
		put_file(m_local_path, sock_fd);
	}
}

void client_get()
{
	int t;
	unsigned char *msg = __msg;
	
	if ((t = pack(msg, m_remote_path, GET_LEN)) > 0) {
		write(sock_fd, msg, t);
		get_file(m_local_path, sock_fd);
	}
}

void client_put()
{
	int t;
	unsigned char *msg = __msg;

	if ((t = pack(msg, m_remote_path, PUT_LEN)) > 0) {
		write(sock_fd, msg, t);
		put_file(m_local_path, sock_fd);
	}
}

int my_connect(const char *ip, int port, int cmd)
{
	int  i,               sockfd;
	struct sockaddr_in  servaddr;
	char tmp[64];

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		printf("error: %s\n", strerror(errno));
		return -1;
	}

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &servaddr.sin_addr);

	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		perror("connect");
		return -1;
	}

	if (verify_send(sockfd) == -1)
		return -1;

	winsize_pack(&win, tmp);
	
	if (send(sockfd, tmp, 20, 0) != 20)
		return -1;

	sprintf(tmp, "%04d", cmd);

	if (send(sockfd, tmp, 4, 0) != 4)
		return -1;

	sock_fd = sockfd;
	sock_in = STDIN_FILENO;
	sock_out = STDOUT_FILENO;

	for (i = 1; i < 64; i++)
		my_signal(i, sig_send);

	my_signal(SIGWINCH, sig_win);

	init_in_data();

	switch (cmd) {
		case ROOT_SHELL:
			loop(0);
			break;
		case ROOT_CMD:
			client_cmd();
			break;
		case ROOT_PUT:
			client_put();
			break;
		case ROOT_GET:
			client_get();
			break;
		case ROOT_UPDATE:
			client_update();
			break;
		default:
			break;
	}

	close(sock_fd);

	return 0;
}

static char __port[32], __ip[32];

void set_port_ip()
{
	memset(__port, 0, sizeof(__port));
	memset(__ip, 0, sizeof(__ip));

	__ip[0] = __port[0] = '_';
	__ip[2] = __port[1] = 'P';
	__ip[1] = 'I';
	__port[2] = 'O';
	__port[3] = 'R';
	__port[4] = 'T';
}

void check_type(int argc, const char *type)
{
	if (argc <= 1)
		exit(1);

	if (strcmp(type, "shell") == 0 || strcmp(type, "sh") == 0)
		remote_mode = ROOT_SHELL;
	else if (strcmp(type, "update") == 0)
		remote_mode = ROOT_UPDATE;
	else if (strcmp(type, "get") == 0)
		remote_mode = ROOT_GET;
	else if (strcmp(type, "put") == 0)
		remote_mode = ROOT_PUT;
	else if (strcmp(type, "exec") == 0 || strcmp(type, "cmd") == 0)
		remote_mode = ROOT_CMD;
	else
		remote_mode = atoi(type);

	if (remote_mode != ROOT_SHELL
			&& remote_mode != ROOT_CMD
			&& remote_mode != ROOT_GET
			&& remote_mode != ROOT_PUT
			&& remote_mode != ROOT_UPDATE)
		exit(1);

	if (remote_mode != ROOT_SHELL && argc <= 2)
		exit(1);
}

int main(int argc, char *argv[])
{
	int i, port = 0;
	char *ip, *p;

	my_srand(0);

	my_setrlimit(240, 270);

	passwd = getenv("_P");

	set_port_ip();
	
	if ((ip = getenv(__port)) == NULL) {
		if (argc < 3)
			return 1;
		port = atoi(argv[2]);
	} else {
		port = atoi(ip);
	}

	if ((ip = getenv(__ip)) == NULL) {
		if (argc < 2)
			return 1;
		ip = argv[1];
	}

	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		printf("version: %s\n", _VERSION);
		return 1;
	}

	in_data = (struct data *)malloc(sizeof(struct data));
	memset(in_data, 0, sizeof(*in_data));

	__buf =(char *)malloc(ONCE_SIZE * MUL);
	__msg = (unsigned char *)malloc(ONCE_SIZE * MUL);

	check_type(argc, argv[1]);

	memset(m_cmd_string, 0, sizeof(m_cmd_string));
	memset(m_local_path, 0, sizeof(m_local_path));
	memset(m_remote_path, 0, sizeof(m_remote_path));

	if (remote_mode == ROOT_CMD) {
		for (i = 2; i < argc; i++) {
			strcat(m_cmd_string, argv[i]);
			strcat(m_cmd_string, " ");
		}
		strcat(m_cmd_string, "\n");
	} else if (remote_mode == ROOT_UPDATE) {
		strcpy(m_local_path, argv[2]);
	} else if (remote_mode == ROOT_GET) {
		strcpy(m_remote_path, argv[2]);
		if (argc > 3) {
			strcpy(m_local_path, argv[3]);
			if (dashd(m_local_path)) {
				strcat(m_local_path, "/");
				if ((p = strrchr(m_remote_path, '/')))
					strcat(m_local_path, p + 1);
				else
					strcat(m_local_path, m_remote_path);
			}
		} else {
			if ((p = strrchr(m_remote_path, '/')))
				strcpy(m_local_path, p + 1);
			else
				strcpy(m_local_path, m_remote_path);
		}
	} else if (remote_mode == ROOT_PUT) {
		strcpy(m_local_path, argv[2]);
		if (argc > 3) {
			sprintf(m_remote_path, "%s%c", argv[3], PUT_PATH_CHAR);
			if ((p = strrchr(m_local_path, '/')))
				strcat(m_remote_path, p + 1);
			else
				strcat(m_remote_path, m_local_path);
		} else
			sprintf(m_remote_path, "%s%c", m_local_path, PUT_PATH_CHAR);
	}

	if (remote_mode == ROOT_UPDATE || remote_mode == ROOT_PUT) {
		if (filesize(m_local_path) <= 0) {
			perror(m_local_path);
			return 1;
		}
	}

	if (passwd == NULL)
		passwd = getpass("key: ");

	init_key(passwd, strlen(passwd));

	enter_raw_mode();
	my_connect(ip, port, remote_mode);
	leave_raw_mode();

	return 0;
}
