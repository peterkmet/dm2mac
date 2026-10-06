"""dm2-play: sends one request to the control socket of a running build/dm2 --control and prints the reply.

    uv run dm2-play <cmd> [json-fields]      for example: uv run dm2-play key '{"key": "up"}'

The reply is printed as compact JSON on one line. The exit code is 1 when the reply has "ok": false. The socket path
comes from --socket, else from DM2_CONTROL, else ~/.dm2/control.sock (docs/llm-control.md).
"""

import argparse
import json
import os
import socket
import sys

DEFAULT_SOCKET = "~/.dm2/control.sock"
TIMEOUT = 150.0  # a wait of 6000 ticks takes 100 seconds


def socket_path(path: str | None = None) -> str:
    return os.path.expanduser(path or os.environ.get("DM2_CONTROL", DEFAULT_SOCKET))


def request(payload: dict, path: str | None = None, timeout: float = TIMEOUT) -> dict:
    """Sends one request line to the game and returns the reply object."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(socket_path(path))
        s.sendall(json.dumps(payload).encode() + b"\n")
        data = b""
        while not data.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    return json.loads(data)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="dm2-play", description="Sends one request to the DM2 control socket.")
    parser.add_argument("--socket", help="socket path (default: $DM2_CONTROL or ~/.dm2/control.sock)")
    parser.add_argument("cmd", help="state, key, click, move, command, wait, screenshot, quit")
    parser.add_argument("fields", nargs="?", help="the other fields of the request as a JSON object")
    options = parser.parse_args(argv)
    payload = {"cmd": options.cmd, **(json.loads(options.fields) if options.fields else {})}
    if "path" in payload:
        payload["path"] = os.path.abspath(payload["path"])  # the game runs in another directory
    try:
        reply = request(payload, options.socket)
    except (OSError, ValueError) as error:
        reply = {"ok": False, "error": f"cannot reach the game at {socket_path(options.socket)}: {error}"}
    print(json.dumps(reply, separators=(",", ":")))
    return 0 if reply.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
