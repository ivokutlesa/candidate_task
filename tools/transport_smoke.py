#!/usr/bin/env python3
"""Checks only the supplied server transport. No additional Python packages."""
import os
import socket
import subprocess
import sys
import time


def choose_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def main():
    if len(sys.argv) != 2:
        print('Usage: python3 tools/transport_smoke.py PATH_TO_VIRTUAL_CONTROLLER')
        return 2
    executable = os.path.abspath(sys.argv[1])
    port = choose_port()
    proc = subprocess.Popen([executable, str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for attempt in range(40):
            try:
                connection = socket.create_connection(('127.0.0.1', port), timeout=0.5)
                break
            except OSError:
                if proc.poll() is not None:
                    raise RuntimeError('Server exited before binding')
                time.sleep(0.05)
        else:
            raise RuntimeError('Could not connect to test server')
        with connection:
            connection.settimeout(3)
            with connection.makefile('rb') as stream:
                connection.sendall(b'PING\nTEMP,70.0\n')
                seen = set()
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline and len(seen) != 3:
                    msg = stream.readline().decode('ascii', errors='replace').strip()
                    if msg == 'PONG': seen.add('pong')
                    if msg == 'ACK,TEMP': seen.add('ack')
                    if msg.startswith('STATUS,') and len(msg.split(',')) == 6: seen.add('status')
                assert seen == {'pong', 'ack', 'status'}, f'missing protocol responses: {seen}'
        print('PASS transport smoke: TCP, command handling, telemetry')
        print('NOTE: controller logic and C# unit tests are intentionally failing in starter.')
        return 0
    finally:
        proc.terminate()
        try: proc.wait(timeout=2)
        except subprocess.TimeoutExpired: proc.kill()


if __name__ == '__main__':
    sys.exit(main())
