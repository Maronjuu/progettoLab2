CC=gcc
CFLAGS=-std=c11 -Wall -g -O3 -pthread
LDLIBS=-lm

EXECS = pagerank

all: $(EXECS)

# regola per creare l'eseguibile
pagerank: pagerank.o xerrori.o
	$(CC) $(CFLAGS) -o pagerank pagerank.o xerrori.o $(LDLIBS)

# primo file oggetto
pagerank.o: pagerank.c xerrori.h
	$(CC) $(CFLAGS) -c pagerank.c

# secondo file oggetto
xerrori.o: xerrori.c xerrori.h
	$(CC) $(CFLAGS) -c xerrori.c

clean:
	rm -f *.o $(EXECS)

