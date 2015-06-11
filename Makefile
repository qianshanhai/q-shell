#
HOST_CC = gcc

ifeq ($(android), yes)
ANDK=$(NDK_HOME)
CC = arm-linux-androideabi-gcc
API =13
INC = -I$(ANDK)/platforms/android-$(API)/arch-arm/usr/include
LIBS = -L$(ANDK)/platforms/android-$(API)/arch-arm/usr/lib -lc -lm
else
CC = gcc
endif

VERSION_DEF = -D_VERSION="\"$(shell cat VERSION | perl -pne chomp)\""

CFLAGS = $(INC) $(DEBUG) $(VERSION_DEF) -O2 -s
LIBS += -lm

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
	@$(HOST_CC) -o setkey $< 
	@ ./setkey && $(CC) $(CFLAGS) -c passwd.c -o $@ 
	@ cat /bin/ls > passwd.c && rm -f passwd.c

clean:
	rm -f *.o $(module) client.o server.o $(objs) setkey

