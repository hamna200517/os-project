CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -O0 -pthread
OBJS = switch.o port.o frame.o queue.o mac_table.o
TARGETS = switch host

all: $(TARGETS)

switch: $(OBJS)
	$(CC) $(CFLAGS) -o switch $(OBJS)

host: host.o frame.o
	$(CC) $(CFLAGS) -o host host.o frame.o

switch.o: switch.c switch.h frame.h queue.h mac_table.h
	$(CC) $(CFLAGS) -c switch.c

port.o: port.c switch.h frame.h queue.h mac_table.h
	$(CC) $(CFLAGS) -c port.c

frame.o: frame.c frame.h
	$(CC) $(CFLAGS) -c frame.c

queue.o: queue.c queue.h frame.h
	$(CC) $(CFLAGS) -c queue.c

mac_table.o: mac_table.c mac_table.h frame.h
	$(CC) $(CFLAGS) -c mac_table.c

host.o: host.c frame.h
	$(CC) $(CFLAGS) -c host.c

tsan:
	$(CC) $(CFLAGS) -fsanitize=thread -o switch_tsan switch.c port.c frame.c queue.c mac_table.c

clean:
	rm -f $(TARGETS) switch_tsan *.o

.PHONY: all clean tsan