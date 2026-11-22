# OS Project=
Hamna Hassan
BSAI24021

This is a software network switch written in C. It runs as a multithreaded
server on Linux. Each host is a separate program that connects to one port of
the switch and sends frames to other hosts. The switch learns which host is on
which port and sends each frame only where it needs to go.

## Folders

tier1 - the core switch (items 1 to 5)
tier2 - tier 1 plus all advanced items (items 6 to 13)

Each folder has its own README with all the build and run steps.

## What I completed

Tier 1: all items, 1 to 5
- partial reads and writes, SIGPIPE ignored, host can leave in the middle of a frame
- one queue and one sender thread per port, full queue means drop and count
- broadcast frames are shared with a reference count
- MAC table with 16 bucket locks, aging, MAC moved, size limit with LRU, port security
- counting semaphore for free ports, sem_timedwait, "switch full" reply

Tier 2: all items, 6 to 13
- 3 priority queues per port, weighted round robin 5:3:1 with aging
- token bucket rate limit, timer thread, pthread_cond_timedwait
- VLANs and a trunk port
- port mirroring
- live reconfiguration: shutdown, up, vlan, clear mac
- swctl admin tool over a UNIX socket, counters in shared memory
- one signal thread with sigwait: SIGINT, SIGHUP, SIGUSR1
- logger thread with a message queue

## Build
I built and tested everything on Linux, in Docker on my Mac.

    cd tier2
    make
    make clean

The same works in tier1 and tier2.

## Run
    ./switch <tcp_port> <num_ports> [-c config_file]
    ./host <switch_ip> <tcp_port> <mac>
    ./swctl <tcp_port> <command>

Example:
    ./switch 9000 8 -c switch.conf
    ./host 127.0.0.1 9000 02:00:00:00:00:0a

In the host, type a destination and a message:
    bcast hello
    02:00:00:00:00:0b hi

## Config file
One setting per line:

    aging 30          MAC entries older than 30 seconds are removed
    max_macs 1024     most MAC entries in the table
    max_per_port 16   most MACs on one port, more and the port is shut down
    rate 0            frames per second per port, 0 means no limit
    port_wait 5       seconds a new host waits for a free port
    vlan 1 10         port 1 is on VLAN 10
    trunk 8           port 8 is the trunk port

Send SIGHUP to the switch to reload the file without restarting.

## Testing

Every tier builds with gcc -std=c11 -Wall -Wextra -Werror -g -O0.
I tested the switch with valgrind (0 errors, no leaks) and with
ThreadSanitizer (no warnings).