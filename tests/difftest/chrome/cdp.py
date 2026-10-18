"""Minimal Chrome DevTools Protocol client.

The harness must run without adding a Python dependency, and the repository vendors no
WebSocket library for Python, so this file implements the small slice of RFC 6455 the CDP
transport needs: a client handshake, masked text frames out, unmasked frames in, ping/pong,
and continuation frames. Binary frames are decoded too because `Page.captureScreenshot` can
come back that way.

Scope note: CDP is only used to *drive* the browser deterministically. Nothing here is part of
the engine.
"""

from __future__ import annotations

import base64
import json
import os
import socket
import struct
import time
import urllib.error
import urllib.request

_WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class CdpError(RuntimeError):
    pass


class WebSocket:
    def __init__(self, url: str, timeout: float = 30.0):
        if not url.startswith("ws://"):
            raise CdpError(f"only ws:// is supported, got {url}")
        rest = url[len("ws://"):]
        host_port, _, path = rest.partition("/")
        path = "/" + path
        host, _, port = host_port.partition(":")
        self._sock = socket.create_connection((host, int(port or "80")), timeout=timeout)
        self._sock.settimeout(timeout)
        self._buffer = bytearray()
        self._handshake(host, port, path)

    def _handshake(self, host: str, port: str, path: str) -> None:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        self._sock.sendall(request.encode("ascii"))
        header = bytearray()
        while b"\r\n\r\n" not in header:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise CdpError("websocket handshake: connection closed")
            header.extend(chunk)
        head, _, tail = bytes(header).partition(b"\r\n\r\n")
        self._buffer.extend(tail)
        status = head.split(b"\r\n", 1)[0]
        if b"101" not in status:
            raise CdpError(f"websocket handshake failed: {status!r}")

    # -- framing -----------------------------------------------------------

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        header = bytearray()
        header.append(0x80 | opcode)
        mask_key = os.urandom(4)
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header.extend(struct.pack(">H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack(">Q", length))
        header.extend(mask_key)
        masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
        self._sock.sendall(bytes(header) + masked)

    def _read_exact(self, count: int) -> bytes:
        while len(self._buffer) < count:
            chunk = self._sock.recv(max(4096, count - len(self._buffer)))
            if not chunk:
                raise CdpError("websocket connection closed")
            self._buffer.extend(chunk)
        out = bytes(self._buffer[:count])
        del self._buffer[:count]
        return out

    def _read_frame(self) -> tuple[int, bytes]:
        first, second = self._read_exact(2)
        fin = (first & 0x80) != 0
        opcode = first & 0x0F
        masked = (second & 0x80) != 0
        length = second & 0x7F
        if length == 126:
            length = struct.unpack(">H", self._read_exact(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self._read_exact(8))[0]
        mask_key = self._read_exact(4) if masked else b""
        payload = self._read_exact(length)
        if masked:
            payload = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
        if not fin:
            _, rest = self._read_frame()
            payload += rest
        return opcode, payload

    def send_text(self, text: str) -> None:
        self._send_frame(0x1, text.encode("utf-8"))

    def recv_text(self) -> str:
        while True:
            opcode, payload = self._read_frame()
            if opcode in (0x1, 0x2):
                return payload.decode("utf-8", errors="replace")
            if opcode == 0x9:  # ping
                self._send_frame(0xA, payload)
                continue
            if opcode == 0x8:
                raise CdpError("websocket closed by peer")

    def close(self) -> None:
        try:
            self._send_frame(0x8, b"")
        except OSError:
            pass
        try:
            self._sock.close()
        except OSError:
            pass


class CdpSession:
    """One page target, with the request/response pairing CDP needs."""

    def __init__(self, ws_url: str, timeout: float = 30.0):
        self._ws = WebSocket(ws_url, timeout=timeout)
        self._next_id = 1
        self._events: list[tuple[str, dict]] = []

    def send(self, method: str, params: dict | None = None, session_id: str | None = None) -> int:
        message = {"id": self._next_id, "method": method}
        if params is not None:
            message["params"] = params
        if session_id:
            message["sessionId"] = session_id
        self._next_id += 1
        self._ws.send_text(json.dumps(message))
        return self._next_id - 1

    def wait(self, message_id: int, timeout: float = 30.0) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            if time.monotonic() > deadline:
                raise CdpError(f"timeout waiting for CDP response {message_id}")
            message = json.loads(self._ws.recv_text())
            if message.get("id") == message_id:
                if "error" in message:
                    raise CdpError(f"{message['error'].get('message')} (id {message_id})")
                return message.get("result", {})
            if "method" in message:
                self._events.append((message["method"], message.get("params", {})))

    def call(self, method: str, params: dict | None = None, timeout: float = 30.0) -> dict:
        return self.wait(self.send(method, params), timeout=timeout)

    def drain_events(self, name: str) -> list[dict]:
        found = [params for method, params in self._events if method == name]
        self._events = [(m, p) for m, p in self._events if m != name]
        return found

    def wait_event(self, name: str, timeout: float = 30.0) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            for i, (method, params) in enumerate(self._events):
                if method == name:
                    del self._events[i]
                    return params
            if time.monotonic() > deadline:
                raise CdpError(f"timeout waiting for CDP event {name}")
            message = json.loads(self._ws.recv_text())
            if "method" in message:
                self._events.append((message["method"], message.get("params", {})))

    def evaluate(self, expression: str, await_promise: bool = False, timeout: float = 60.0):
        result = self.call(
            "Runtime.evaluate",
            {
                "expression": expression,
                "returnByValue": True,
                "awaitPromise": await_promise,
                "userGesture": False,
            },
            timeout=timeout,
        )
        if result.get("exceptionDetails"):
            details = result["exceptionDetails"]
            exc = details.get("exception") or {}
            desc = exc.get("description") or details.get("text") or "unknown"
            line = details.get("lineNumber")
            col = details.get("columnNumber")
            raise CdpError(f"evaluate failed: {desc} at {line}:{col}")
        return result.get("result", {}).get("value")

    def close(self) -> None:
        self._ws.close()


def http_json(url: str, timeout: float = 10.0) -> object:
    with urllib.request.urlopen(url, timeout=timeout) as response:  # noqa: S310 - localhost only
        return json.loads(response.read().decode("utf-8"))


def find_page_target(port: int, timeout: float = 30.0) -> str:
    """Waits for the browser's debugging endpoint and returns a page target's WS URL."""
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            targets = http_json(f"http://127.0.0.1:{port}/json/list")
        except (urllib.error.URLError, ConnectionError, OSError) as exc:
            last_error = exc
            time.sleep(0.2)
            continue
        pages = [t for t in targets if t.get("type") == "page"]
        if pages:
            return pages[0]["webSocketDebuggerUrl"]
        time.sleep(0.2)
    raise CdpError(f"no page target on port {port} ({last_error})")
