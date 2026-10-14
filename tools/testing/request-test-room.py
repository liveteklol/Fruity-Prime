#!/usr/bin/env python3
"""Request an isolated test room from a loopback directory using the real wire protocol."""
import argparse
import json
import pathlib
import re
import socket
import struct
import time
import uuid


def protocol():
    source = pathlib.Path(__file__).resolve().parents[2] / "src/MphRead.Native/Mods/Network/Protocol/Packets/PacketTypes.hpp"
    return int(re.search(r"ProtocolVersion = (\d+)", source.read_text()).group(1))


def text(value, size):
    encoded = value.encode("ascii")
    if len(encoded) > size:
        raise ValueError("test room text exceeds the wire limit")
    return encoded.ljust(size, b"\0")


def request(port, room, name, mode=3, lobby=False):
    packet = bytes([20]) + struct.pack("<BBBHH", protocol(), 8, mode, 30, 999)
    packet += text(room, 40) + text(name, 32) + bytes([0, int(lobby), 1, 0, 0])
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(35)
        sock.sendto(packet, ("127.0.0.1", port))
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            reply, sender = sock.recvfrom(1232)
            if sender != ("127.0.0.1", port) or len(reply) != 116 or reply[0] != 21:
                continue
            result = {"started": bool(reply[1]), "port": struct.unpack_from("<H", reply, 2)[0],
                      "reason": reply[4:100].split(b"\0")[0].decode("ascii"),
                      "owner_token": uuid.UUID(bytes_le=reply[100:116]).hex}
            return result
        raise TimeoutError("directory did not return a HostReply")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--room", default="TEST ARENA")
    parser.add_argument("--name", default="HomingMatrix")
    parser.add_argument("--mode", choices=("Battle", "PrimeHunter"), default="Battle")
    parser.add_argument("--lobby", action="store_true")
    args = parser.parse_args()
    result = request(args.port, args.room, args.name, 14 if args.mode == "PrimeHunter" else 3, args.lobby)
    print(json.dumps(result))
    raise SystemExit(0 if result["started"] else 1)
