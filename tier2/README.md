Virtual Switch - Tier 2

This is the same switch as tier 1 with more features added. It runs
on Linux. Hosts connect with TCP, each host gets one port, and the
switch sends frames only where they need to go.

What tier 2 adds:
- every port has 3 queues (high, normal, low). The sender picks the
  next frame with weighted round robin 5:3:1, and a low frame that
  waited too long goes next so it never starves
- rate limit with a token bucket. a timer thread adds tokens and a
  sender with no tokens waits with pthread_cond_timedwait
- vlans. hosts only reach hosts in the same vlan. one port can be a
  trunk port that carries all vlans
- port mirroring. all traffic of one port can be copied to a sniffer
  port without slowing the first port down
- live changes while traffic is running: shut a port down, bring it
  up, change its vlan, clear the mac table
- swctl, a separate admin program that talks to the switch over a
  unix socket. the counters are in shared memory with a process
  shared mutex
- signals. one thread handles all signals with sigwait:
  ctrl-c stops the switch cleanly, SIGHUP reloads the config file,
  SIGUSR1 writes the mac table to mac_dump.txt
- a logger thread writes all log messages to switch.log with the time


How to build

make          builds switch, host and swctl
make tsan     builds switch_tsan for thread sanitizer
make clean    deletes everything


How to run

Start the switch with the config file:
./switch 9000 8 -c switch.conf

9000 is the tcp port and 8 is the number of ports. -c is optional,
without it all ports are in vlan 1 and there is no trunk.

Start hosts, each one in its own terminal:
./host 127.0.0.1 9000 02:00:00:00:00:0a
./host 127.0.0.1 9000 02:00:00:00:00:0b

-n at the end makes a host that never reads (to test a frozen host).
-v 20 sets the vlan in the host's frames. this only matters for a
host on the trunk port, on a normal port the switch uses the port's
vlan.


How to send

In a host terminal write the destination mac and then the message:
bcast hello
02:00:00:00:00:0b hi

-p0 sends with high priority, -p2 with low, -c100 sends 100 times:
bcast -p0 urgent
bcast -c100 test


Admin commands

./swctl show mac
./swctl show ports
./swctl shutdown 2
./swctl up 2
./swctl vlan 3 20
./swctl mirror 1 4       (copy port 1 to port 4, mirror 1 0 stops it)
./swctl clear mac
./swctl stats
./swctl stats --watch    (live counters every second, ctrl-c to stop)

swctl uses tcp port 9000 by default, for another switch use -p:
./swctl -p 9100 show ports


Signals

kill -HUP $(pidof switch)     reloads switch.conf
kill -USR1 $(pidof switch)    writes the mac table to mac_dump.txt
ctrl-c in the switch terminal stops it, closes everything and frees
all memory


Config file

One setting per line, # starts a comment. Missing settings keep the
default.

aging 30          seconds before an old mac is removed
max_macs 1024     most macs in the table
max_per_port 16   most macs on one port (port security)
rate 0            frames per second per port, 0 means no limit
port_wait 5       seconds a new host waits for a free port
trunk 8           the trunk port, 0 means no trunk
vlan 1 10         port 1 is in vlan 10 (one line for each port)

If the file has a bad line the switch does not start. If it is bad
on SIGHUP the switch keeps the old settings.


Files the switch makes

switch.log                    the log
mac_dump.txt                  made by SIGUSR1
/tmp/vswitch_9000.sock        the socket swctl uses
/dev/shm/vswitch_9000         the shared memory counters

The socket and shared memory are removed when the switch stops.


Source files

frame.c / frame.h       frame header, read_full, write_full, refcount
queue.c / queue.h       3 priority queues, round robin, token bucket
mac_table.c / mac_table.h   the mac table
switch.h                the port struct
port.c                  receiver and sender threads, vlans, mirroring,
                        shutdown and up
switch.c                main, accept, aging, timer and signal threads
admin.c                 answers swctl
stats.c / stats.h       counters in shared memory
logger.c / logger.h     the logger thread
config.c / config.h     reads the config file
host.c                  test host
swctl.c                 admin tool
switch.conf             example config


Checking

./switch_tsan 9000 8 -c switch.conf
valgrind --leak-check=full ./switch 9000 8 -c switch.conf

Both should show no errors after ctrl-c.