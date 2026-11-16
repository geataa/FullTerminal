"""Real Windows OpenSSH / ConPTY integration, with an isolated loopback SSH server.

Optional test-only dependency: python -m pip install --target build/ssh-test-deps paramiko
No user keys, SSH config, known_hosts, network hosts or persistent server are used.
"""
import pathlib
import socket
import subprocess
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "build" / "ssh-test-deps"))
try:
    import paramiko
except ImportError:
    print("SKIP: optional SSH integration dependency paramiko is not installed")
    sys.exit(77)


class Login(paramiko.ServerInterface):
    def __init__(self):
        self.shell = threading.Event()
        self.size = (80, 24)

    def get_banner(self):
        return "Authentication banner\r\nPlease continue\r\n", "en"

    def get_allowed_auths(self, username):
        return "password"

    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if (username, password) == ("tester", "fixture-password") else paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height, pixelwidth, pixelheight, modes):
        assert term == b"xterm-256color", term
        self.size = width, height
        return True

    def check_channel_window_change_request(self, channel, width, height, pixelwidth, pixelheight):
        self.size = width, height
        return True

    def check_channel_shell_request(self, channel):
        self.shell.set()
        return True


def run(client_exe):
    key = paramiko.RSAKey.generate(2048)
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    listener.settimeout(.2)
    port = listener.getsockname()[1]
    stop = threading.Event()
    errors = []
    completed = []

    def serve():
        while not stop.is_set():
            try:
                peer, _ = listener.accept()
            except socket.timeout:
                continue
            transport = paramiko.Transport(peer)
            try:
                transport.add_server_key(key)
                login = Login()
                transport.start_server(server=login)
                channel = transport.accept(10)
                assert channel is not None and login.shell.wait(10), "SSH shell was not requested"
                channel.settimeout(10)
                # A banner is intentionally separated from the prompt. The C++ test
                # releases it only AFTER it has checked that loading is still open.
                channel.sendall(b"\x1b]0;/usr/bin/bash\x07Welcome to Ubuntu fixture\r\n"
                                b"System load: 0.44\r\nENVIRONMENT_PENDING\r\n")
                while True:
                    data = channel.recv(128)
                    if not data:
                        break
                    for char in data:
                        if char == ord("r"):
                            channel.sendall(b"\x1b[32mGREEN\x1b[0m\r\ntester@fixture:~$ ")
                        elif char == ord("s"):
                            channel.sendall(f"\r\nSIZE:{login.size[0]}x{login.size[1]}\r\ntester@fixture:~$ ".encode())
                        elif char == ord("q"):
                            channel.send_exit_status(0)
                            channel.close()
                            completed.append(True)
                            break
                    if channel.closed:
                        break
            except Exception as exc:
                errors.append(str(exc))
            finally:
                transport.close()

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        result = subprocess.run([client_exe, "--ssh-loopback", str(port)], timeout=35)
    finally:
        stop.set()
        thread.join(timeout=12)
        listener.close()
    assert not thread.is_alive(), "SSH fixture thread did not stop"
    assert result.returncode == 0, f"C++ OpenSSH fixture exited {result.returncode}"
    assert not errors, errors
    assert len(completed) == 3, f"Expected manual login, retry and automatic login; got {len(completed)}"
    print("PASS: real Windows OpenSSH authentication, delayed MOTD, ANSI colors, remote PTY resize, retry")


if __name__ == "__main__":
    run(sys.argv[1])
