#
CC = gcc

OS = $(shell uname -s | perl -pne chomp)
VERSION_DEF = -D_VERSION="\"$(shell cat VERSION | perl -pne chomp)\""

CFLAGS = $(DEBUG) $(VERSION_DEF) -O2 -s
LIBS = -lm

objs = blowfish.o my_rand.o

client = qsh 
server = qshd
module = $(client) $(server)

%.o:%.c
	@$(CC) $(CFLAGS) -o $@ -c $<
	@echo "   CC    $@"

all: $(module)

$(client): client.o $(objs)
	@$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "   LD    $@"

$(server): server.o $(objs) passwd.o
	@$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "   LD    $@"
	@rm -f passwd.o 

passwd.o: setkey.c
	@$(CC) $(CFLAGS) -o setkey $< $(LIBS)
	@ ./setkey && $(CC) $(CFLAGS) -c passwd.c -o $@ 
	@ cat /bin/ls > passwd.c && rm -f passwd.c

clean:
	rm -f *.o $(module) client.o server.o $(objs) setkey

