CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -O0 -pthread
OBJS = switch.o port.o admin.o frame.o queue.o mac_table.o logger.o stats.o config.o
TARGETS = switch host swctl

all: $(TARGETS)

switch: $(OBJS)
	$(CC) $(CFLAGS) -o switch $(OBJS) -lrt

host: host.o frame.o
	$(CC) $(CFLAGS) -o host host.o frame.o

swctl: swctl.o stats.o frame.o
	$(CC) $(CFLAGS) -o swctl swctl.o stats.o frame.o -lrt

switch.o: switch.c switch.h frame.h queue.h mac_table.h config.h logger.h stats.h
	$(CC) $(CFLAGS) -c switch.c

port.o: port.c switch.h frame.h queue.h mac_table.h config.h logger.h stats.h
	$(CC) $(CFLAGS) -c port.c

admin.o: admin.c switch.h logger.h stats.h
	$(CC) $(CFLAGS) -c admin.c

frame.o: frame.c frame.h
	$(CC) $(CFLAGS) -c frame.c

queue.o: queue.c queue.h frame.h
	$(CC) $(CFLAGS) -c queue.c

mac_table.o: mac_table.c mac_table.h frame.h logger.h
	$(CC) $(CFLAGS) -c mac_table.c

logger.o: logger.c logger.h
	$(CC) $(CFLAGS) -c logger.c

stats.o: stats.c stats.h mac_table.h
	$(CC) $(CFLAGS) -c stats.c

config.o: config.c config.h mac_table.h
	$(CC) $(CFLAGS) -c config.c

host.o: host.c frame.h
	$(CC) $(CFLAGS) -c host.c

swctl.o: swctl.c stats.h frame.h
	$(CC) $(CFLAGS) -c swctl.c

tsan:
	$(CC) $(CFLAGS) -fsanitize=thread -o switch_tsan switch.c port.c admin.c frame.c queue.c mac_table.c logger.c stats.c config.c -lrt

clean:
	rm -f $(TARGETS) switch_tsan *.o

.PHONY: all clean tsan