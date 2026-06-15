#!/usr/bin/env python3
import argparse
import os
import socket
import sys
import threading
from pathlib import Path

try:
    import paramiko
except ImportError as exc:
    raise SystemExit(
        "missing dependency: paramiko. Run scripts/run-osai-ssh-bridge.sh "
        "so the isolated build/osai-ssh-venv environment is created."
    ) from exc


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOST_KEY = ROOT / "build" / "osai-ssh" / "host_rsa.key"


def _write(channel, text):
    if not text:
        return
    channel.send(text.encode("utf-8"))


def _normalize(command):
    return " ".join(command.strip().split())


def osai_remote_command(command):
    command = _normalize(command)
    if command in ("", "true"):
        return 0, ""
    if command == "help":
        return (
            0,
            "OSAI SSH commands: pwd, ls, ls /, status, sysinfo, help, exit\n",
        )
    if command == "pwd":
        return 0, "/\n"
    if command in ("ls", "ls /"):
        return 0, "bin\netc\nmodels\nstate\n"
    if command == "status":
        return 0, "osai qemu session=running ssh_only=true password_login=false\n"
    if command == "sysinfo":
        return 0, "arch=aarch64 platform=qemu-macos cpu_only_ai=true\n"
    if command in ("exit", "quit", "logout"):
        return 0, ""
    return 127, f"osai-ssh: command not allowlisted: {command}\n"


class OsaiSshServer(paramiko.ServerInterface):
    def __init__(self):
        self.event = threading.Event()
        self.command = None
        self.shell_requested = False

    def get_banner(self):
        return ("OSAI remote login\r\n", "en-US")

    def get_allowed_auths(self, username):
        return "none,publickey"

    def check_auth_none(self, username):
        return (
            paramiko.AUTH_SUCCESSFUL
            if username == "admin"
            else paramiko.AUTH_FAILED
        )

    def check_auth_publickey(self, username, key):
        return (
            paramiko.AUTH_SUCCESSFUL
            if username == "admin"
            else paramiko.AUTH_FAILED
        )

    def check_channel_request(self, kind, chanid):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(
        self, channel, term, width, height, pixelwidth, pixelheight, modes
    ):
        return True

    def check_channel_shell_request(self, channel):
        self.shell_requested = True
        self.event.set()
        return True

    def check_channel_exec_request(self, channel, command):
        if isinstance(command, bytes):
            command = command.decode("utf-8", "replace")
        self.command = command
        self.event.set()
        return True


def ensure_host_key(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        return paramiko.RSAKey(filename=str(path))
    key = paramiko.RSAKey.generate(3072)
    key.write_private_key_file(str(path))
    os.chmod(path, 0o600)
    return key


def run_exec(channel, command):
    status, output = osai_remote_command(command)
    _write(channel, output)
    channel.send_exit_status(status)
    channel.close()


def run_shell(channel):
    _write(channel, "OSAI remote shell\n")
    _write(channel, "Type help for commands.\n")
    buffer = ""
    while True:
        _write(channel, "osai:/ $ ")
        while "\n" not in buffer and "\r" not in buffer:
            data = channel.recv(1024)
            if not data:
                channel.close()
                return
            text = data.decode("utf-8", "replace")
            for ch in text:
                if ch == "\x03":
                    _write(channel, "^C\n")
                    buffer = ""
                elif ch in ("\b", "\x7f"):
                    buffer = buffer[:-1]
                else:
                    buffer += ch
        line = buffer.replace("\r", "\n", 1).split("\n", 1)[0]
        if "\n" in buffer.replace("\r", "\n", 1):
            buffer = buffer.replace("\r", "\n", 1).split("\n", 1)[1]
        else:
            buffer = ""
        command = _normalize(line)
        if command in ("exit", "quit", "logout"):
            _write(channel, "logout\n")
            channel.send_exit_status(0)
            channel.close()
            return
        status, output = osai_remote_command(command)
        _write(channel, output)
        if status != 0:
            _write(channel, f"exit_status={status}\n")


def handle_client(client, address, host_key):
    transport = paramiko.Transport(client)
    transport.local_version = "SSH-2.0-OSAI_ssh_bridge"
    transport.add_server_key(host_key)
    server = OsaiSshServer()
    try:
        transport.start_server(server=server)
        channel = transport.accept(20)
        if channel is None:
            return
        if not server.event.wait(20):
            channel.close()
            return
        if server.command is not None:
            run_exec(channel, server.command)
        elif server.shell_requested:
            run_shell(channel)
        else:
            channel.close()
    except Exception as exc:
        print(f"osai-ssh-bridge: connection {address} failed: {exc}", file=sys.stderr)
    finally:
        transport.close()


def serve(host, port, host_key_path):
    host_key = ensure_host_key(host_key_path)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.listen(16)
    print(f"osai-ssh-bridge: listening on {host}:{port} user=admin")
    print("osai-ssh-bridge: OpenSSH command: ssh -p 2222 admin@localhost")
    while True:
        client, address = sock.accept()
        thread = threading.Thread(
            target=handle_client, args=(client, address, host_key), daemon=True
        )
        thread.start()


def main():
    parser = argparse.ArgumentParser(description="OSAI OpenSSH-compatible bridge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2222)
    parser.add_argument("--host-key", type=Path, default=DEFAULT_HOST_KEY)
    args = parser.parse_args()
    serve(args.host, args.port, args.host_key)


if __name__ == "__main__":
    main()
