"""Loopback-only SSH server for the interactive capture integration tests."""

import socket
import threading

import paramiko


class Server(paramiko.ServerInterface):
    def __init__(self):
        self.shell = threading.Event()

    def check_auth_password(self, username, password):
        if username == "logger" and password == "fixture-password":
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return "password"

    def check_channel_request(self, kind, chanid):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height,
                                  pixelwidth, pixelheight, modes):
        return True

    def check_channel_shell_request(self, channel):
        self.shell.set()
        return True


def main():
    key = paramiko.RSAKey.generate(2048)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(30)
        print(f"READY {listener.getsockname()[1]}", flush=True)
        connection, _ = listener.accept()
        with connection:
            transport = paramiko.Transport(connection)
            transport.add_server_key(key)
            server = Server()
            try:
                transport.start_server(server=server)
                channel = transport.accept(15)
                if channel is None or not server.shell.wait(5):
                    return
                channel.sendall(b"\x1b[32mdevice# \x1b[0m")
                command = bytearray()
                while True:
                    data = channel.recv(4096)
                    if not data:
                        break
                    for byte in data:
                        if byte == 3:
                            command.clear()
                            channel.sendall(b"^C\r\ndevice# ")
                        elif byte in (8, 127):
                            if command:
                                command.pop()
                                channel.sendall(b"\b \b")
                        elif byte == 13:
                            channel.sendall(b"\r\n")
                            if command == b"exit":
                                channel.sendall(b"bye\r\n")
                                channel.send_exit_status(0)
                                channel.close()
                                return
                            channel.sendall(b"result: " + command + b"\r\ndevice# ")
                            command.clear()
                        else:
                            command.append(byte)
                            channel.sendall(bytes([byte]))
            except (EOFError, OSError, paramiko.SSHException):
                pass
            finally:
                transport.close()


if __name__ == "__main__":
    main()
