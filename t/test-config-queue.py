"""Exercise two distinct completion batches in the headless IPC test."""
import errno
import json
import os
from pathlib import Path
import socket
import struct
import sys
import time
import xml.etree.ElementTree as ET

sock, path = sys.argv[1], Path(sys.argv[2])
magic = 0x5357494D


def submit(command):
    conn = socket.socket(socket.AF_UNIX)
    conn.settimeout(5)
    conn.connect(sock)
    data = command.encode()
    conn.sendall(struct.pack("<II", magic, len(data)) + data)
    return conn


def reply(conn):
    with conn, conn.makefile("rb") as stream:
        tag, size = struct.unpack("<II", stream.read(8))
        assert tag == magic
        return stream.read(size).decode()


def open_writer():
    deadline = time.monotonic() + 5
    while True:
        try:
            return os.fdopen(os.open(path, os.O_NONBLOCK | os.O_WRONLY), "wb")
        except OSError as exc:
            if exc.errno != errno.ENXIO or time.monotonic() >= deadline:
                raise
            time.sleep(0.01)


saved = path.with_suffix(".before-queue-test")
path.rename(saved)
try:
    os.mkfifo(path)
    first = submit("CONFIG RULES LIST")
    with open_writer() as gate:
        # Worker is in xmlReadFile; main loop still services requests.
        for _ in range(29):
            assert reply(submit("CONFIG RELOAD")).startswith("OK ")
        status = submit("CONFIG STATUS")
        assert reply(submit("PING")).startswith("OK ")
        last = submit("CONFIG RULES LIST")
        assert reply(submit("PING")).startswith("OK ")
        for command in ("CONFIG RELOAD", "CONFIG SET tiling.columns 8"):
            assert reply(submit(command)).startswith("ERR 5 configuration queue is full")
        path.unlink()
        os.mkfifo(path)
        gate.write(b"<labwc_config/>")

    with open_writer() as gate:
        assert reply(first).startswith("OK ")
        assert json.loads(reply(status).removeprefix("OK "))["dirty"]
        # All barriers completed, but the last (read-only) job is still
        # pending. Only a real reconfigure can load this disk-only value.
        tree = ET.parse(saved)
        tree.find("./theme/cornerRadius").text = "7"
        replacement = path.with_suffix(".replacement")
        tree.write(replacement)
        replacement.replace(path)
        assert json.loads(reply(submit("CONFIG GET theme.corner-radius"))[3:])["value"] != 7
        gate.write(b"<labwc_config/>")
    assert reply(last).startswith("OK ")
    assert json.loads(reply(submit("CONFIG GET theme.corner-radius"))[3:])["value"] == 7
finally:
    saved.replace(path)
