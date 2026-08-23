#!/usr/bin/env python3
"""A UDP relay that holds every datagram for a while before passing it on.

The Pi is the only place the latency bugs show, and it is also the one
machine in this setup that cannot be redeployed from here. This puts the
same delay on a loopback server, so a fix can be measured against latency
without a round trip to a Raspberry Pi -- and with the delay set to a known
number rather than whatever the internet is doing this minute.

  ./udp-lag.py <listen-port> <server-host> <server-port> <one-way-ms> [jitter-ms] [loss-%]

Each client that talks to the listen port gets its own upstream socket, so
the server sees several distinct peers exactly as it would over the wire.
"""
import heapq, random, select, socket, sys, threading, time

listen_port = int(sys.argv[1])
server = (sys.argv[2], int(sys.argv[3]))
delay = float(sys.argv[4]) / 1000.0
jitter = float(sys.argv[5]) / 1000.0 if len(sys.argv) > 5 else 0.0
loss = float(sys.argv[6]) / 100.0 if len(sys.argv) > 6 else 0.0

down = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
down.bind(("127.0.0.1", listen_port))
up = {}          # client address -> socket facing the server
owner = {}       # socket -> client address
queue = []       # (due, seq, socket, payload, destination)
seq = 0
lock = threading.Lock()
stats = [0, 0]   # forwarded, dropped

def send_later(sock, payload, dest):
    global seq
    if loss > 0 and random.random() < loss:
        stats[1] += 1
        return
    wait = delay + (random.uniform(-jitter, jitter) if jitter else 0.0)
    with lock:
        seq += 1
        heapq.heappush(queue, (time.monotonic() + max(wait, 0.0), seq, sock, payload, dest))

def pump():
    while True:
        with lock:
            now = time.monotonic()
            ready = []
            while queue and queue[0][0] <= now:
                ready.append(heapq.heappop(queue))
        for _, _, sock, payload, dest in ready:
            try:
                sock.sendto(payload, dest)
                stats[0] += 1
            except OSError:
                pass
        time.sleep(0.001)

threading.Thread(target=pump, daemon=True).start()
print(f"[lag] :{listen_port} -> {server[0]}:{server[1]}, {delay*1000:.0f} ms each way"
      f" (+/- {jitter*1000:.0f} ms jitter, {loss*100:.1f}% loss)", flush=True)

while True:
    socks = [down] + list(up.values())
    for ready in select.select(socks, [], [], 0.5)[0]:
        try:
            payload, addr = ready.recvfrom(65535)
        except OSError:
            continue
        if ready is down:
            if addr not in up:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.bind(("127.0.0.1", 0))
                up[addr] = s
                owner[s] = addr
                print(f"[lag] client {addr} -> local port {s.getsockname()[1]}", flush=True)
            send_later(up[addr], payload, server)
        else:
            send_later(down, payload, owner[ready])
