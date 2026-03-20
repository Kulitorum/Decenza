#!/usr/bin/env python3
"""
Decenza MCP Bridge — connects Claude Desktop (stdio) to Decenza's HTTP MCP server.

Claude Desktop communicates via stdin/stdout JSON-RPC. This script translates
those messages to HTTP POST requests to the Decenza MCP server.

Usage in claude_desktop_config.json:
{
  "mcpServers": {
    "decenza": {
      "command": "python3",
      "args": ["/path/to/mcp_bridge.py", "http://192.168.1.x:8888/mcp"]
    }
  }
}

On Windows:
{
  "mcpServers": {
    "decenza": {
      "command": "python",
      "args": ["C:\\path\\to\\mcp_bridge.py", "http://192.168.1.x:8888/mcp"]
    }
  }
}
"""

import sys
import json
import urllib.request
import urllib.error

def main():
    if len(sys.argv) < 2:
        print("Usage: mcp_bridge.py <server-url> [api-key]", file=sys.stderr)
        print("Example: mcp_bridge.py http://localhost:8888/mcp", file=sys.stderr)
        sys.exit(1)

    server_url = sys.argv[1]
    api_key = sys.argv[2] if len(sys.argv) > 2 else None

    # Build headers (use list to allow mutation from nested scope)
    session = [None]

    def make_headers():
        h = {"Content-Type": "application/json"}
        if session[0]:
            # Send both header names for compatibility
            h["Mcp-Session-Id"] = session[0]
            h["Mcp-Session"] = session[0]
        if api_key:
            h["Authorization"] = f"Bearer {api_key}"
        return h

    # Read JSON-RPC messages from stdin, forward to HTTP, write responses to stdout
    # Stay alive — read line by line until stdin closes
    while True:
        try:
            line = sys.stdin.readline()
        except EOFError:
            break
        if not line:
            break  # EOF
        line = line.strip()
        if not line:
            continue

        try:
            request = json.loads(line)
        except json.JSONDecodeError as e:
            print(json.dumps({
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32700, "message": f"Parse error: {e}"}
            }), flush=True)
            continue

        try:
            # Notifications (no id) — fire and forget, don't wait for response
            is_notification = "id" not in request

            data = json.dumps(request).encode("utf-8")
            hdrs = make_headers()
            print(f"[bridge] Sending {request.get('method','?')} session={session[0]} notify={is_notification}", file=sys.stderr, flush=True)
            req = urllib.request.Request(server_url, data=data, headers=hdrs, method="POST")

            if is_notification:
                # Fire and forget — use short timeout, ignore errors
                try:
                    with urllib.request.urlopen(req, timeout=2) as resp:
                        resp.read()
                except Exception:
                    pass
                continue

            with urllib.request.urlopen(req, timeout=30) as resp:
                # Capture session ID from response headers (prefer spec name)
                new_session = resp.headers.get("Mcp-Session-Id") or resp.headers.get("Mcp-Session")
                if new_session:
                    session[0] = new_session

                response_body = resp.read().decode("utf-8")
                if response_body.strip():
                    # Forward response to stdout
                    print(response_body, flush=True)

        except urllib.error.URLError as e:
            error_msg = str(e.reason) if hasattr(e, 'reason') else str(e)
            if hasattr(e, 'read'):
                try:
                    error_body = e.read().decode("utf-8")
                    if error_body:
                        error_msg = error_body
                except Exception:
                    pass

            request_id = request.get("id")
            if request_id is not None:
                print(json.dumps({
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {"code": -32000, "message": f"Server error: {error_msg}"}
                }), flush=True)

        except Exception as e:
            request_id = request.get("id")
            if request_id is not None:
                print(json.dumps({
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {"code": -32000, "message": f"Bridge error: {e}"}
                }), flush=True)

if __name__ == "__main__":
    main()
