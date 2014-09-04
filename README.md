q-shell -- Quick Shell for Unix administrator
=======

### introduction

q-shell is quick shell for remote login into Unix system,
it use blowfish crypt algorithm to protect transport data from client to server,
you can get two program: 'qsh' for client, and 'qshd' for server,
those program can rename by any name with you prefer.

### compile

Just enter 'make' and it will automation to compile, but, you must input the 
server key.

### usage

1. server:

just run qshd

		 $ ./qshd

but, you would like to run after change it to other name, such as:

		 $ mv qshd smbd
		 $ export PATH=.:$PATH
		 $ smbd

2. client:

set some environment variable, then run qsh:

		$ export _IP=127.0.0.1
		$ export _PORT=2800
		$ ./qsh shell

now you already login into server $\_P .

### more function

q-shell include more function to manage system:

1. put/get files:

		$ ./qsh get /path/to/server/file .
		$ ./qsh put /path/to/local/file  /path/to/server/file

2. run a command on server:

		$ ./qsh exec 'ls -l /bin'

3. update server program:

		$ ./qsh update /path/to/local/qshd

This function will update remote qshd, and run again.

4. automation to run command on many server:

		$ for i in {10..20} ; do \
		      export _IP=192.168.0.$i
		      export _PORT=2800
		      export _P=key   # set key
		      ./qsh exec 'ls -l /bin'
		  done

Note: qsh use $\_P to fetch server key, so you should erase all history data after to use $\_P.

### about VERSION file

Client and server must have the same main version serial, otherwise, the client cannot
to connect the server. the main version serial like, both is 1.x, or 2.x, or 3.x, etc.

For example, if the client version is 1.1, and server is 1.5, so client can connect to
the server, but if server version is 2.1, in that way the client cannot connect to server.


