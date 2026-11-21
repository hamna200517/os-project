Virtual Switch - Tier 1

This is a network switch made in C for Linux. Hosts connect to the
switch with TCP and each host gets one port. The switch learns where
every host is and sends frames only where they need to go. Broadcast
frames and frames to unknown hosts go to all ports.

What tier 1 does:
- reads and writes full frames even if they come in parts
- every port has its own queue and sender thread so a slow host
  does not slow down the others
- broadcast frames are not copied, they have a refcount and the last
  port to use it frees it
- mac table with one lock per bucket, aging, mac moved, a size limit
  and port security
- port limit with a semaphore, if no port is free for 5 seconds the
  host gets "switch full"


How to build

make          builds switch and host
make tsan     builds switch_tsan for thread sanitizer
make clean    deletes everything


How to run

First start the switch:
./switch 9000 4

9000 is the tcp port and 4 is the number of ports.

Then start hosts, each one in its own terminal:
./host 127.0.0.1 9000 02:00:00:00:00:0a
./host 127.0.0.1 9000 02:00:00:00:00:0b

If you put -n at the end the host never reads. I used this to test
a frozen host.


How to send

In a host terminal write the destination mac and then the message:
bcast hello
02:00:00:00:00:0b hi

bcast sends to everyone. -c100 sends the message 100 times and -p0
sends it with high priority, for example:
bcast -c100 test


How to stop

Press ctrl-c in the switch terminal. It closes everything and frees
all the memory.


Settings

There is no config file in tier 1. The settings are defines in
switch.h (aging_time, max_macs, max_per_port, port_wait), queue.h
(queuesize) and mac_table.h (mac_locks). Change them and run make
again.


Files

frame.c / frame.h       frame header, read_full, write_full, refcount
queue.c / queue.h       the queue of each port
mac_table.c / mac_table.h   the mac table
switch.h                settings and the port struct
port.c                  receiver and sender threads, forwarding
switch.c                main, accept thread, aging thread
host.c                  test host


Checking

./switch_tsan 9000 4
valgrind --leak-check=full ./switch 9000 4

Both should show no errors after ctrl-c.