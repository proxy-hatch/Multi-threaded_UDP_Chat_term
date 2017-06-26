CC=gcc
CFLAGS=-w -std=c99
PROG=s-talk
OBJS= main.o LIST.o
# pthread vs lpthread: https://stackoverflow.com/a/23251828
PTHREADFLAGS=-pthread

s-talk: $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(PTHREADFLAGS) $(OBJS)

LIST.o: LIST.c LIST.h
	$(CC) $(CFLAGS) -c LIST.c

main.o: main.c
	$(CC) $(CFLAGS) -c main.c


clean:
	rm *.o