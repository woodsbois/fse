CC = gcc
CFLAGS = -Wall -Wextra -std=c99

all: fse

fse: tester.o search.o
	$(CC) $(CFLAGS) -o fse tester.o search.o -lpthread

tester.o: tester.c
	$(CC) $(CFLAGS) -c tester.c -o tester.o

search.o: search.c
	$(CC) $(CFLAGS) -c search.c -o search.o

clean:
	rm -f fse *.o
	rm -f results_no_lock.txt
