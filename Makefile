CC = gcc
CFLAGS = -Wall
MAIN = httpd
OBJS = httpd.o readLine.o
all : $(MAIN)

$(MAIN) : $(OBJS)
	$(CC) $(CFLAGS) -o $(MAIN) $(OBJS)

httpd.o : httpd.c readLine.h defs.h
	$(CC) $(CFLAGS) -c httpd.c
	
readLine.o : readLine.c readLine.h defs.h
	$(CC) $(CFLAGS) -c readLine.c

clean :
	rm *.o $(MAIN) core*
