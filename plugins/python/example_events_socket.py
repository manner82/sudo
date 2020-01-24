import sudo

import socket
import os


class SocketEventsIOPlugin(sudo.Plugin):
    """
    An example sudo plugin demonstrating the sudo event API capabilities
    (waiting for a file descriptor).

    You can install it as an IO plugin by adding the following to sudo.conf:
        Plugin python_io python_plugin.so \
            ModulePath=<path>/example_events_socket.py \
            ClassName=SocketEventsIOPlugin

    After that, run a program with sudo which runs for a long time:
        sudo bash

    And you can send a message to that sudo run by using a unix socket:
        echo "Hello!" | socat - UNIX-CONNECT:/tmp/sudo_example.s
    """

    socket_path = "/tmp/sudo_example.s"

    def __init__(self, *argv, **kwargs):
        super().__init__(*argv, **kwargs)
        self._ensure_socket_removed()

        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
        self.socket.bind(self.socket_path)
        self.socket.listen()

        event = sudo.Event(read_fd=self.socket.fileno(), persist=True)
        event.on_readable = self._on_socket_readable
        event.register()

    def __del__(self):
        self.socket.close()
        self._ensure_socket_removed()

    def _on_socket_readable(self):
        conn, _ = self.socket.accept()
        try:
            msg = conn.recv(4096).decode("UTF-8").strip()
            sudo.log_info("** Message received: {} **".format(msg))

        finally:
            conn.close()

    def _ensure_socket_removed(self):
        try:
            os.remove(self.socket_path)
        except OSError:
            pass
