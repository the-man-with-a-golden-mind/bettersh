import json
import sys

log_path = sys.argv[1]
log = open(log_path, "w", encoding="utf-8")
defs = 0


def log_line(text):
    log.write(text + "\n")
    log.flush()


def read_exact(n):
    buf = b""
    while len(buf) < n:
        chunk = sys.stdin.buffer.read(n - len(buf))
        if chunk == b"":
            return None
        buf += chunk
    return buf


def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if line == b"":
            return None
        if line in (b"\r\n", b"\n"):
            break
        if b":" in line:
            name, val = line.split(b":", 1)
            headers[name.decode("ascii", "replace").strip().lower()] = val.decode("ascii", "replace").strip()
    n = int(headers.get("content-length", "0"))
    body = read_exact(n)
    if body is None:
        return None
    return json.loads(body.decode("utf-8"))


def send(obj):
    data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(("Content-Length: %d\r\n\r\n" % len(data)).encode("ascii") + data)
    sys.stdout.buffer.flush()


def send_raw(data):
    sys.stdout.buffer.write(("Content-Length: %d\r\n\r\n" % len(data)).encode("ascii") + data)
    sys.stdout.buffer.flush()


uri = None
while True:
    msg = read_message()
    if msg is None:
        break
    method = msg.get("method")
    mid = msg.get("id", None)
    if method is None:
        continue
    if method == "initialize":
        log_line("initialize")
        send({
            "jsonrpc": "2.0",
            "id": 50,
            "method": "workspace/configuration",
            "params": {"items": []},
        })
        while True:
            reply = read_message()
            if reply is None:
                log_line("config-eof")
                sys.exit(0)
            if reply.get("id") == 50:
                log_line("config")
                break
        send({"jsonrpc": "2.0", "id": msg["id"], "result": {"capabilities": {}}})
    elif method == "initialized":
        log_line("initialized")
    elif method == "textDocument/didOpen":
        doc = msg["params"]["textDocument"]
        uri = doc["uri"]
        log_line("didOpen " + repr(doc["text"]))
        send({
            "jsonrpc": "2.0",
            "method": "textDocument/publishDiagnostics",
            "params": {
                "uri": uri,
                "diagnostics": [{
                    "range": {
                        "start": {"line": 0, "character": 0},
                        "end": {"line": 0, "character": 1},
                    },
                    "message": "unused",
                }],
            },
        })
        send_raw(b"{not json")
    elif method == "textDocument/didChange":
        text = msg["params"]["contentChanges"][0]["text"]
        log_line("didChange " + repr(text))
    elif method == "textDocument/didSave":
        log_line("didSave")
    elif method == "textDocument/didClose":
        log_line("didClose")
    elif method == "textDocument/completion":
        log_line("completion")
        send({
            "jsonrpc": "2.0",
            "id": mid,
            "result": {"items": [{"label": "alpha", "insertText": "alpha"}]},
        })
    elif method == "textDocument/hover":
        log_line("hover")
        send({
            "jsonrpc": "2.0",
            "id": mid,
            "result": {"contents": {"kind": "plaintext", "value": "a name"}},
        })
    elif method == "textDocument/definition":
        defs += 1
        log_line("definition")
        if defs == 1:
            send({
                "jsonrpc": "2.0",
                "id": mid,
                "result": {
                    "uri": uri,
                    "range": {
                        "start": {"line": 1, "character": 2},
                        "end": {"line": 1, "character": 3},
                    },
                },
            })
        else:
            send({
                "jsonrpc": "2.0",
                "id": mid,
                "result": [{
                    "targetUri": "file:///tmp/other.py",
                    "targetSelectionRange": {
                        "start": {"line": 4, "character": 5},
                        "end": {"line": 4, "character": 6},
                    },
                    "targetRange": {
                        "start": {"line": 4, "character": 0},
                        "end": {"line": 4, "character": 6},
                    },
                }],
            })
    elif method == "shutdown":
        log_line("shutdown")
        send({"jsonrpc": "2.0", "id": mid, "result": None})
    elif method == "exit":
        log_line("exit")
        break
    elif mid is not None:
        log_line("unknown " + method)
        send({
            "jsonrpc": "2.0",
            "id": mid,
            "error": {"code": -32601, "message": "method not found"},
        })
    else:
        log_line("note " + method)

log.flush()
log.close()
