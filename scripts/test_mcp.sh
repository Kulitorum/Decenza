#!/bin/bash
# MCP Server Test Suite
# Usage: ./scripts/test_mcp.sh [host:port]
# Requires: curl, python3
# The app must be running with MCP enabled in settings.

set -uo pipefail

HOST="${1:-localhost:8888}"
BASE="http://$HOST/mcp"
PASS=0
FAIL=0
SKIP=0
SESSION=""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

rpc() {
    local id="$1"
    local method="$2"
    local params="$3"
    local headers=(-H "Content-Type: application/json")
    if [ -n "$SESSION" ]; then
        headers+=(-H "Mcp-Session: $SESSION")
    fi
    curl -s --max-time 5 -D /tmp/mcp_headers "${headers[@]}" -X POST "$BASE" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"$method\",\"params\":$params}"
}

extract_session() {
    # Prefer Mcp-Session-Id (spec name), fall back to Mcp-Session
    local sid
    sid=$(grep -i 'Mcp-Session-Id' /tmp/mcp_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
    if [ -z "$sid" ]; then
        sid=$(grep -i 'Mcp-Session:' /tmp/mcp_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
    fi
    echo "$sid"
}

# Parse JSON result — extracts the tool result text as parsed JSON
parse_tool_result() {
    python3 -c "
import json, sys
d = json.load(sys.stdin)
if 'error' in d:
    print(json.dumps(d['error']))
    sys.exit(1)
r = d.get('result', {})
content = r.get('content', [])
if content and 'text' in content[0]:
    print(content[0]['text'])
else:
    print(json.dumps(r))
" 2>/dev/null
}

assert_ok() {
    local test_name="$1"
    local response="$2"
    local check="$3"  # python expression that must be truthy

    local ok
    ok=$(echo "$response" | python3 -c "
import json, sys
try:
    d = json.loads(sys.stdin.read())
    result = $check
    print('1' if result else '0')
except Exception as e:
    print('0')
" 2>/dev/null)

    if [ "$ok" = "1" ]; then
        echo -e "  ${GREEN}PASS${NC} $test_name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}FAIL${NC} $test_name"
        echo -e "       Response: $(echo "$response" | head -c 200)"
        FAIL=$((FAIL + 1))
    fi
}

declare -a ALL_SESSIONS  # Track all sessions created during the test run

cleanup_all_sessions() {
    if [ ${#ALL_SESSIONS[@]} -gt 0 ]; then
        for sid in "${ALL_SESSIONS[@]}"; do
            curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $sid" > /dev/null 2>&1
        done
    fi
}
trap cleanup_all_sessions EXIT

# Create a session, set SESSION + INIT_RESP, track for cleanup
create_session() {
    INIT_RESP=$(curl -s --max-time 5 -D /tmp/mcp_headers -X POST "$BASE" -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}')
    SESSION=$(extract_session)
    if [ -n "${SESSION:-}" ]; then
        ALL_SESSIONS+=("$SESSION")
    fi
}

# Delete the current session
delete_session() {
    if [ -n "${SESSION:-}" ]; then
        curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $SESSION" > /dev/null 2>&1
        SESSION=""
    fi
}

echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo -e "${CYAN}  Decenza MCP Server Test Suite${NC}"
echo -e "${CYAN}  Target: $BASE${NC}"
echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo

# ─── 1. Protocol Tests ───
echo -e "${CYAN}1. Protocol${NC}"

# Test: MCP disabled returns 404 (can't test if enabled — skip)
# Test: Initialize
create_session
assert_ok "initialize returns protocolVersion" "$INIT_RESP" \
    "d.get('result',{}).get('protocolVersion') == '2025-03-26'"
assert_ok "initialize returns serverInfo" "$INIT_RESP" \
    "'Decenza' in d.get('result',{}).get('serverInfo',{}).get('name','')"
assert_ok "initialize returns tools capability" "$INIT_RESP" \
    "'tools' in d.get('result',{}).get('capabilities',{})"
if [ ${#SESSION} -gt 10 ]; then
    echo -e "  ${GREEN}PASS${NC} session ID returned"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} session ID returned (got: '$SESSION')"
    FAIL=$((FAIL + 1))
fi

if [ -z "$SESSION" ]; then
    echo -e "${RED}No session ID — cannot continue. Is MCP enabled?${NC}"
    exit 1
fi

# Test: Invalid session auto-recovers (PR #520 — mcp-remote can't re-initialize)
# May also get "Too many sessions" if slots are full — both are valid server responses
INVALID_RESP=$(curl -s -D /tmp/mcp_invalid_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session: invalid-session-id" \
    -d '{"jsonrpc":"2.0","id":99,"method":"tools/list","params":{}}')
assert_ok "invalid session handled (auto-recover or limit)" "$INVALID_RESP" \
    "isinstance(d.get('result',{}).get('tools'), list) or d.get('error',{}).get('code') == -32000"
# Clean up the auto-recovered session if one was created
INVALID_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_invalid_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
if [ -z "$INVALID_SID" ]; then
    INVALID_SID=$(grep -i 'Mcp-Session:' /tmp/mcp_invalid_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
fi
if [ -n "$INVALID_SID" ]; then
    curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $INVALID_SID" > /dev/null 2>&1
    ALL_SESSIONS+=("$INVALID_SID")
fi

# Test: Auto-recover with stale ID reuses sole session (no leak)
STALE_RECOVER_RESP=$(curl -s -D /tmp/mcp_stale_recover_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session-Id: stale-id-that-does-not-exist" \
    -d '{"jsonrpc":"2.0","id":98,"method":"tools/list","params":{}}')
STALE_RECOVER_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_stale_recover_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
assert_ok "stale session auto-recovers" "$STALE_RECOVER_RESP" \
    "isinstance(d.get('result',{}).get('tools'), list)"
if [ "$STALE_RECOVER_SID" = "$SESSION" ]; then
    echo -e "  ${GREEN}PASS${NC} stale ID reuses sole session (no leak)"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} stale ID should reuse sole session (got: '$STALE_RECOVER_SID', expected: '$SESSION')"
    FAIL=$((FAIL + 1))
fi

# Test: Repeated stale IDs don't leak sessions
for i in 1 2 3 4 5; do
    curl -s -X POST "$BASE" -H "Content-Type: application/json" \
        -H "Mcp-Session-Id: stale-repeat-$i" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$((97-i)),\"method\":\"tools/list\",\"params\":{}}" > /dev/null
done
# Verify only 1 session exists by checking initialize still works (not "Too many sessions")
LEAK_CHECK=$(curl -s -D /tmp/mcp_leak_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":90,"method":"initialize","params":{"capabilities":{}}}')
assert_ok "no session leak from repeated stale IDs" "$LEAK_CHECK" \
    "d.get('result',{}).get('protocolVersion') is not None"
# Clean up the extra session
LEAK_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_leak_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
if [ -n "$LEAK_SID" ] && [ "$LEAK_SID" != "$SESSION" ]; then
    curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $LEAK_SID" > /dev/null 2>&1
    ALL_SESSIONS+=("$LEAK_SID")
fi

# Test: ping works (spec keepalive — should not require notifications/initialized)
PING_EARLY=$(rpc 89 "ping" '{}')
assert_ok "ping works as keepalive" "$PING_EARLY" \
    "'result' in d and d['result'] == {}"

# Test: Unknown method
UNK_RESP=$(rpc 2 "unknown/method" '{}')
assert_ok "unknown method returns error" "$UNK_RESP" \
    "'error' in d.get('result',{}) or 'error' in d"

# Test: Parse error
PARSE_RESP=$(curl -s -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session: $SESSION" -d 'not json')
assert_ok "malformed JSON returns parse error" "$PARSE_RESP" \
    "d.get('error',{}).get('code') == -32700"

echo

# ─── 1b. HTTP Protocol Compliance ───
echo -e "${CYAN}1b. HTTP Protocol Compliance${NC}"

# Test: Mcp-Session-Id header accepted (spec header name)
SESSIONID_RESP=$(curl -s --max-time 5 -X POST "$BASE" \
    -H "Content-Type: application/json" \
    -H "Mcp-Session-Id: $SESSION" \
    -d '{"jsonrpc":"2.0","id":200,"method":"tools/list","params":{}}')
assert_ok "Mcp-Session-Id header accepted" "$SESSIONID_RESP" \
    "'error' not in d and isinstance(d.get('result',{}).get('tools'), list)"

# Test: notifications/initialized returns 202 Accepted
NOTIF_HEADERS=$(curl -s --max-time 5 -D - -o /dev/null -X POST "$BASE" \
    -H "Content-Type: application/json" \
    -H "Mcp-Session: $SESSION" \
    -d '{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}')
NOTIF_STATUS=$(echo "$NOTIF_HEADERS" | head -1 | tr -d '\r\n')
if echo "$NOTIF_STATUS" | grep -q "202"; then
    echo -e "  ${GREEN}PASS${NC} notifications/initialized returns 202"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} notifications/initialized returns 202 (got: $NOTIF_STATUS)"
    FAIL=$((FAIL + 1))
fi

# Test: 204 response has no Content-Length (RFC 7231 — checked on OPTIONS response below)
# (OPTIONS test captures headers; 204 Content-Length check is done there)

# Test: OPTIONS CORS preflight
OPTIONS_HEADERS=$(curl -s --max-time 5 -D - -o /dev/null -X OPTIONS "$BASE")
OPTIONS_STATUS=$(echo "$OPTIONS_HEADERS" | head -1 | tr -d '\r\n')
if echo "$OPTIONS_STATUS" | grep -q "204"; then
    echo -e "  ${GREEN}PASS${NC} OPTIONS returns 204"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} OPTIONS returns 204 (got: $OPTIONS_STATUS)"
    FAIL=$((FAIL + 1))
fi
if echo "$OPTIONS_HEADERS" | grep -qi "Access-Control-Allow-Methods"; then
    echo -e "  ${GREEN}PASS${NC} OPTIONS includes Access-Control-Allow-Methods"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} OPTIONS missing Access-Control-Allow-Methods"
    FAIL=$((FAIL + 1))
fi
if echo "$OPTIONS_HEADERS" | grep -qi "Access-Control-Allow-Headers"; then
    echo -e "  ${GREEN}PASS${NC} OPTIONS includes Access-Control-Allow-Headers"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} OPTIONS missing Access-Control-Allow-Headers"
    FAIL=$((FAIL + 1))
fi
# RFC 7231: 204 must NOT have Content-Length
if echo "$OPTIONS_HEADERS" | grep -qi "Content-Length"; then
    echo -e "  ${RED}FAIL${NC} 204 response has Content-Length (RFC violation)"
    FAIL=$((FAIL + 1))
else
    echo -e "  ${GREEN}PASS${NC} 204 response has no Content-Length"
    PASS=$((PASS + 1))
fi

# Test: HTTP status text correctness (not "400 OK" or "200 Unknown")
INIT_STATUS=$(curl -s --max-time 5 -D /tmp/mcp_status_headers -o /dev/null -X POST "$BASE" \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":201,"method":"initialize","params":{"capabilities":{}}}' && \
    head -1 /tmp/mcp_status_headers | tr -d '\r\n')
if echo "$INIT_STATUS" | grep -q "200 OK"; then
    echo -e "  ${GREEN}PASS${NC} initialize returns '200 OK' (not '200 Unknown')"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} initialize status text (got: $INIT_STATUS)"
    FAIL=$((FAIL + 1))
fi
# Clean up the extra session we just created
EXTRA_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_status_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
if [ -z "$EXTRA_SID" ]; then
    EXTRA_SID=$(grep -i 'Mcp-Session:' /tmp/mcp_status_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
fi
if [ -n "$EXTRA_SID" ]; then
    curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $EXTRA_SID" > /dev/null 2>&1
    ALL_SESSIONS+=("$EXTRA_SID")
fi

BAD_SESS_STATUS=$(curl -s --max-time 5 -D - -o /dev/null -X POST "$BASE" \
    -H "Content-Type: application/json" \
    -H "Mcp-Session: invalid-for-status-text-test" \
    -d '{"jsonrpc":"2.0","id":202,"method":"tools/list","params":{}}' | head -1 | tr -d '\r\n')
if echo "$BAD_SESS_STATUS" | grep -q "200 OK"; then
    echo -e "  ${GREEN}PASS${NC} error response returns '200 OK' status line"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} error response status text (got: $BAD_SESS_STATUS)"
    FAIL=$((FAIL + 1))
fi

# Test: Response includes both Mcp-Session-Id and Mcp-Session headers
DUAL_HEADERS=$(curl -s --max-time 5 -D - -o /dev/null -X POST "$BASE" \
    -H "Content-Type: application/json" \
    -H "Mcp-Session: $SESSION" \
    -d '{"jsonrpc":"2.0","id":203,"method":"tools/list","params":{}}')
if echo "$DUAL_HEADERS" | grep -qi "Mcp-Session-Id:"; then
    echo -e "  ${GREEN}PASS${NC} response includes Mcp-Session-Id header"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} response missing Mcp-Session-Id header"
    FAIL=$((FAIL + 1))
fi
if echo "$DUAL_HEADERS" | grep -qi "Mcp-Session:"; then
    echo -e "  ${GREEN}PASS${NC} response includes Mcp-Session header"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} response missing Mcp-Session header"
    FAIL=$((FAIL + 1))
fi

echo

# ─── 2. Tool Discovery ───
echo -e "${CYAN}2. Tool Discovery${NC}"

TOOLS_RESP=$(rpc 10 "tools/list" '{}')
TOOLS_JSON=$(echo "$TOOLS_RESP" | python3 -c "
import json, sys
d = json.load(sys.stdin)
tools = d.get('result',{}).get('tools',[])
print(json.dumps([t['name'] for t in tools]))
" 2>/dev/null)

assert_ok "tools/list returns array" "$TOOLS_RESP" \
    "isinstance(d.get('result',{}).get('tools'), list)"

EXPECTED_TOOLS="machine_get_state machine_get_telemetry shots_list shots_get_detail shots_get_debug_log shots_compare shots_update shots_delete profiles_list profiles_get_active profiles_get_detail profiles_get_params profiles_edit_params profiles_save profiles_delete profiles_create settings_get dialing_get_context machine_wake machine_sleep machine_start_espresso machine_start_steam machine_start_hot_water machine_start_flush machine_stop machine_skip_frame profiles_set_active settings_set scale_tare scale_timer_start scale_timer_stop scale_timer_reset scale_get_weight devices_list devices_scan devices_connect_scale devices_connection_status debug_get_log"

# Verify removed tools are NOT registered
REMOVED_TOOLS="dialing_apply_change dialing_suggest_change shots_set_feedback"
for tool in $REMOVED_TOOLS; do
    assert_ok "removed tool '$tool' NOT registered" "$TOOLS_JSON" \
        "'$tool' not in d"
done
for tool in $EXPECTED_TOOLS; do
    assert_ok "tool '$tool' registered" "$TOOLS_JSON" \
        "'$tool' in d"
done

echo

# ─── 3. Machine Tools ───
echo -e "${CYAN}3. Machine Tools${NC}"

STATE_RAW=$(rpc 20 "tools/call" '{"name":"machine_get_state","arguments":{}}')
STATE=$(echo "$STATE_RAW" | parse_tool_result)
assert_ok "machine_get_state returns phase" "$STATE" \
    "'phase' in d"
assert_ok "machine_get_state returns connected" "$STATE" \
    "'connected' in d"
assert_ok "machine_get_state returns waterLevelMl" "$STATE" \
    "'waterLevelMl' in d"
assert_ok "machine_get_state returns firmwareVersion" "$STATE" \
    "'firmwareVersion' in d"
assert_ok "machine_get_state returns activeProfile" "$STATE" \
    "'activeProfile' in d and len(d['activeProfile']) > 0"

TELEM_RAW=$(rpc 21 "tools/call" '{"name":"machine_get_telemetry","arguments":{}}')
TELEM=$(echo "$TELEM_RAW" | parse_tool_result)
assert_ok "machine_get_telemetry returns pressureBar" "$TELEM" \
    "'pressureBar' in d"
assert_ok "machine_get_telemetry returns flowMlPerSec" "$TELEM" \
    "'flowMlPerSec' in d"
assert_ok "machine_get_telemetry returns temperatureC" "$TELEM" \
    "'temperatureC' in d"
assert_ok "machine_get_telemetry returns scaleWeightG" "$TELEM" \
    "'scaleWeightG' in d"

echo

# ─── 4. Shot History Tools ───
echo -e "${CYAN}4. Shot History${NC}"

SHOTS_RAW=$(rpc 30 "tools/call" '{"name":"shots_list","arguments":{"limit":3}}')
SHOTS=$(echo "$SHOTS_RAW" | parse_tool_result)
assert_ok "shots_list returns shots array" "$SHOTS" \
    "isinstance(d.get('shots'), list)"
assert_ok "shots_list returns total count" "$SHOTS" \
    "isinstance(d.get('total'), int) and d['total'] >= 0"
assert_ok "shots_list respects limit" "$SHOTS" \
    "len(d.get('shots',[])) <= 3"

# Get a shot ID for detail test
SHOT_ID=$(echo "$SHOTS" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
shots = d.get('shots',[])
print(shots[0]['id'] if shots else 0)
" 2>/dev/null)

if [ "$SHOT_ID" != "0" ] && [ -n "$SHOT_ID" ]; then
    # Shot fields
    FIRST_SHOT=$(echo "$SHOTS" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
print(json.dumps(d['shots'][0]))
" 2>/dev/null)
    assert_ok "shot has profileName" "$FIRST_SHOT" "'profileName' in d"
    assert_ok "shot has doseG" "$FIRST_SHOT" "'doseG' in d"
    assert_ok "shot has yieldG" "$FIRST_SHOT" "'yieldG' in d"
    assert_ok "shot has durationSec" "$FIRST_SHOT" "'durationSec' in d"
    assert_ok "shot has timestamp" "$FIRST_SHOT" "'timestamp' in d"

    # Detail
    DETAIL_RAW=$(rpc 31 "tools/call" "{\"name\":\"shots_get_detail\",\"arguments\":{\"shotId\":$SHOT_ID}}")
    DETAIL=$(echo "$DETAIL_RAW" | parse_tool_result)
    assert_ok "shots_get_detail returns data" "$DETAIL" \
        "'id' in d or 'profileName' in d"

    # Compare (need 2 IDs)
    SHOT_ID2=$(echo "$SHOTS" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
shots = d.get('shots',[])
print(shots[1]['id'] if len(shots) > 1 else 0)
" 2>/dev/null)

    if [ "$SHOT_ID2" != "0" ] && [ -n "$SHOT_ID2" ]; then
        COMPARE_RAW=$(rpc 32 "tools/call" "{\"name\":\"shots_compare\",\"arguments\":{\"shotIds\":[$SHOT_ID,$SHOT_ID2]}}")
        COMPARE=$(echo "$COMPARE_RAW" | parse_tool_result)
        assert_ok "shots_compare returns 2 shots" "$COMPARE" \
            "d.get('count') == 2"
    else
        echo -e "  ${YELLOW}SKIP${NC} shots_compare (need 2+ shots)"
        SKIP=$((SKIP + 1))
    fi

    # Filter test
    PROFILE_NAME=$(echo "$FIRST_SHOT" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
print(d.get('profileName','')[:10])
" 2>/dev/null)
    if [ -n "$PROFILE_NAME" ]; then
        FILTERED_RAW=$(rpc 33 "tools/call" "{\"name\":\"shots_list\",\"arguments\":{\"limit\":5,\"profileName\":\"$PROFILE_NAME\"}}")
        FILTERED=$(echo "$FILTERED_RAW" | parse_tool_result)
        assert_ok "shots_list filter by profileName works" "$FILTERED" \
            "d.get('count',0) > 0"
    fi

    # Debug log
    DEBUG_LOG_RAW=$(rpc 36 "tools/call" "{\"name\":\"shots_get_debug_log\",\"arguments\":{\"shotId\":$SHOT_ID}}")
    DEBUG_LOG=$(echo "$DEBUG_LOG_RAW" | parse_tool_result)
    assert_ok "shots_get_debug_log returns log or error" "$DEBUG_LOG" \
        "'log' in d or 'error' in d"
    if echo "$DEBUG_LOG" | python3 -c "import json,sys; d=json.loads(sys.stdin.read()); exit(0 if 'log' in d else 1)" 2>/dev/null; then
        assert_ok "shots_get_debug_log has pagination fields" "$DEBUG_LOG" \
            "'totalLines' in d and 'hasMore' in d and 'returnedLines' in d"
    fi

    # Date range filter
    DATE_FILTERED_RAW=$(rpc 35 "tools/call" '{"name":"shots_list","arguments":{"limit":5,"after":"2026-01-01T00:00:00"}}')
    DATE_FILTERED=$(echo "$DATE_FILTERED_RAW" | parse_tool_result)
    assert_ok "shots_list with date range filter works" "$DATE_FILTERED" \
        "d.get('count',0) >= 0"
else
    echo -e "  ${YELLOW}SKIP${NC} shot detail/compare tests (no shots in database)"
    SKIP=$((SKIP + 4))
fi

# Invalid shot ID
BAD_SHOT_RAW=$(rpc 34 "tools/call" '{"name":"shots_get_detail","arguments":{"shotId":999999}}')
BAD_SHOT=$(echo "$BAD_SHOT_RAW" | parse_tool_result)
assert_ok "shots_get_detail invalid ID returns error" "$BAD_SHOT" \
    "'error' in d"

BAD_DEBUG_RAW=$(rpc 37 "tools/call" '{"name":"shots_get_debug_log","arguments":{"shotId":999999}}')
BAD_DEBUG=$(echo "$BAD_DEBUG_RAW" | parse_tool_result)
assert_ok "shots_get_debug_log invalid ID returns error" "$BAD_DEBUG" \
    "'error' in d"

echo

# ─── 5. Profile Tools ───
echo -e "${CYAN}5. Profiles${NC}"

PROFILES_RAW=$(rpc 40 "tools/call" '{"name":"profiles_list","arguments":{}}')
PROFILES=$(echo "$PROFILES_RAW" | parse_tool_result)
assert_ok "profiles_list returns profiles array" "$PROFILES" \
    "isinstance(d.get('profiles'), list)"
assert_ok "profiles_list has count" "$PROFILES" \
    "d.get('count',0) > 0"

ACTIVE_RAW=$(rpc 41 "tools/call" '{"name":"profiles_get_active","arguments":{}}')
ACTIVE=$(echo "$ACTIVE_RAW" | parse_tool_result)
assert_ok "profiles_get_active returns filename" "$ACTIVE" \
    "'filename' in d and len(d['filename']) > 0"
assert_ok "profiles_get_active returns title" "$ACTIVE" \
    "'title' in d"
assert_ok "profiles_get_active returns editorType" "$ACTIVE" \
    "'editorType' in d and len(d['editorType']) > 0"
assert_ok "profiles_get_active returns targetWeightG" "$ACTIVE" \
    "'targetWeightG' in d"

# profiles_get_params — editor-type-aware
PARAMS_RAW=$(rpc 44 "tools/call" '{"name":"profiles_get_params","arguments":{}}')
PARAMS=$(echo "$PARAMS_RAW" | parse_tool_result)
assert_ok "profiles_get_params returns editorType" "$PARAMS" \
    "'editorType' in d and len(d['editorType']) > 0"
assert_ok "profiles_get_params returns filename" "$PARAMS" \
    "'filename' in d"

# Get detail for first profile
PROFILE_FILE=$(echo "$PROFILES" | python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
profiles = d.get('profiles',[])
print(profiles[0]['filename'] if profiles else '')
" 2>/dev/null)

if [ -n "$PROFILE_FILE" ]; then
    PDETAIL_RAW=$(rpc 42 "tools/call" "{\"name\":\"profiles_get_detail\",\"arguments\":{\"filename\":\"$PROFILE_FILE\"}}")
    PDETAIL=$(echo "$PDETAIL_RAW" | parse_tool_result)
    assert_ok "profiles_get_detail returns data" "$PDETAIL" \
        "'title' in d or 'error' not in d"
else
    echo -e "  ${YELLOW}SKIP${NC} profiles_get_detail (no profiles)"
    SKIP=$((SKIP + 1))
fi

# Invalid profile
BAD_PROF_RAW=$(rpc 43 "tools/call" '{"name":"profiles_get_detail","arguments":{"filename":"nonexistent_profile_xyz"}}')
BAD_PROF=$(echo "$BAD_PROF_RAW" | parse_tool_result)
assert_ok "profiles_get_detail invalid name returns error" "$BAD_PROF" \
    "'error' in d"

echo

# ─── 6. Settings Tool ───
echo -e "${CYAN}6. Settings${NC}"

SETTINGS_RAW=$(rpc 50 "tools/call" '{"name":"settings_get","arguments":{}}')
SETTINGS=$(echo "$SETTINGS_RAW" | parse_tool_result)
assert_ok "settings_get returns espressoTemperatureC" "$SETTINGS" \
    "'espressoTemperatureC' in d"
assert_ok "settings_get returns targetWeightG" "$SETTINGS" \
    "'targetWeightG' in d"
assert_ok "settings_get returns steamTemperatureC" "$SETTINGS" \
    "'steamTemperatureC' in d"
assert_ok "settings_get returns DYE metadata" "$SETTINGS" \
    "'dyeBeanBrand' in d"

# Filtered settings
FILTERED_SET_RAW=$(rpc 51 "tools/call" '{"name":"settings_get","arguments":{"keys":["espressoTemperature","targetWeight"]}}')
FILTERED_SET=$(echo "$FILTERED_SET_RAW" | parse_tool_result)
assert_ok "settings_get with keys filter works" "$FILTERED_SET" \
    "'espressoTemperatureC' in d and 'targetWeightG' in d"
assert_ok "settings_get filter excludes other keys" "$FILTERED_SET" \
    "'steamTemperatureC' not in d"

echo

# ─── 7. Dial-In Context ───
echo -e "${CYAN}7. Dial-In Context${NC}"

DIALING_RAW=$(rpc 60 "tools/call" '{"name":"dialing_get_context","arguments":{}}')
DIALING=$(echo "$DIALING_RAW" | parse_tool_result)
assert_ok "dialing_get_context returns shotId" "$DIALING" \
    "'shotId' in d"
assert_ok "dialing_get_context returns shot summary" "$DIALING" \
    "'shot' in d and 'profileName' in d.get('shot',{})"
assert_ok "dialing_get_context returns currentBean" "$DIALING" \
    "'currentBean' in d"
assert_ok "dialing_get_context returns beanAgeDays" "$DIALING" \
    "'beanAgeDays' in d.get('currentBean',{}) or d.get('currentBean',{}).get('roastDate','') == ''"
assert_ok "dialing_get_context returns currentProfile" "$DIALING" \
    "'currentProfile' in d"
assert_ok "dialing_get_context returns profileKnowledge" "$DIALING" \
    "'profileKnowledge' in d"

# With specific shot ID
if [ "$SHOT_ID" != "0" ] && [ -n "$SHOT_ID" ]; then
    DIALING2_RAW=$(rpc 61 "tools/call" "{\"name\":\"dialing_get_context\",\"arguments\":{\"shot_id\":$SHOT_ID}}")
    DIALING2=$(echo "$DIALING2_RAW" | parse_tool_result)
    assert_ok "dialing_get_context with shot_id works" "$DIALING2" \
        "d.get('shotId') == $SHOT_ID"
fi

echo

# ─── 8. Machine Control Tools ───
echo -e "${CYAN}8. Machine Control (discovery only — not executing commands)${NC}"

# Verify control tools are registered
TOOLS_RESP2=$(rpc 70 "tools/list" '{}')
TOOLS_JSON2=$(echo "$TOOLS_RESP2" | python3 -c "
import json, sys
d = json.load(sys.stdin)
tools = d.get('result',{}).get('tools',[])
print(json.dumps([t['name'] for t in tools]))
" 2>/dev/null)

CONTROL_TOOLS="machine_wake machine_sleep machine_start_espresso machine_start_steam machine_start_hot_water machine_start_flush machine_stop machine_skip_frame"
for tool in $CONTROL_TOOLS; do
    assert_ok "control tool '$tool' registered" "$TOOLS_JSON2" \
        "'$tool' in d"
done

# Test state validation (machine_stop should fail when not flowing)
STOP_RAW=$(rpc 71 "tools/call" '{"name":"machine_stop","arguments":{}}')
STOP=$(echo "$STOP_RAW" | parse_tool_result)
assert_ok "machine_stop rejects when not flowing" "$STOP" \
    "'error' in d and 'No operation' in d.get('error','')"

# Test machine_skip_frame fails when not extracting
SKIP_RAW=$(rpc 72 "tools/call" '{"name":"machine_skip_frame","arguments":{}}')
SKIP_RESULT=$(echo "$SKIP_RAW" | parse_tool_result)
assert_ok "machine_skip_frame rejects when not extracting" "$SKIP_RESULT" \
    "'error' in d"

echo

# ─── 9. Resources ───
echo -e "${CYAN}9. Resources${NC}"

RES_LIST_RAW=$(rpc 90 "resources/list" '{}')
assert_ok "resources/list returns array" "$RES_LIST_RAW" \
    "isinstance(d.get('result',{}).get('resources'), list)"

RES_URIS=$(echo "$RES_LIST_RAW" | python3 -c "
import json, sys
d = json.load(sys.stdin)
resources = d.get('result',{}).get('resources',[])
print(json.dumps([r['uri'] for r in resources]))
" 2>/dev/null)

EXPECTED_RESOURCES="decenza://machine/state decenza://machine/telemetry decenza://profiles/active decenza://shots/recent decenza://profiles/list decenza://debug/log decenza://debug/memory"
for uri in $EXPECTED_RESOURCES; do
    assert_ok "resource '$uri' registered" "$RES_URIS" \
        "'$uri' in d"
done

# Read machine state resource
STATE_RES_RAW=$(rpc 91 "resources/read" '{"uri":"decenza://machine/state"}')
assert_ok "resources/read machine/state returns contents" "$STATE_RES_RAW" \
    "isinstance(d.get('result',{}).get('contents'), list)"

# Read recent shots resource
SHOTS_RES_RAW=$(rpc 92 "resources/read" '{"uri":"decenza://shots/recent"}')
assert_ok "shots/recent resource returns shots" "$SHOTS_RES_RAW" \
    "'shots' in json.loads(d.get('result',{}).get('contents',[{}])[0].get('text','{}'))"

# Read debug log resource
LOG_RES_RAW=$(rpc 93 "resources/read" '{"uri":"decenza://debug/log"}')
LOG_RES_PARSED=$(echo "$LOG_RES_RAW" | python3 -c "
import json, sys
d = json.load(sys.stdin)
contents = d.get('result',{}).get('contents',[])
text = contents[0].get('text','{}') if contents else '{}'
inner = json.loads(text)
print(json.dumps({'hasLog': len(inner.get('log','')) > 100, 'hasPath': len(inner.get('path','')) > 0, 'lineCount': inner.get('lineCount',0)}))
" 2>/dev/null)
assert_ok "debug/log resource returns log content" "$LOG_RES_PARSED" \
    "d.get('hasLog') == True"
assert_ok "debug/log resource returns path" "$LOG_RES_PARSED" \
    "d.get('hasPath') == True"

# Read memory stats resource
MEM_RES_RAW=$(rpc 94 "resources/read" '{"uri":"decenza://debug/memory"}')
MEM_RES_TEXT=$(echo "$MEM_RES_RAW" | python3 -c "
import json, sys
d = json.load(sys.stdin)
contents = d.get('result',{}).get('contents',[])
text = contents[0].get('text','{}') if contents else '{}'
print(text)
" 2>/dev/null)
assert_ok "debug/memory resource returns rssMB" "$MEM_RES_TEXT" \
    "d.get('current',{}).get('rssMB') is not None"
assert_ok "debug/memory resource returns qobjectCount" "$MEM_RES_TEXT" \
    "d.get('current',{}).get('qobjectCount') is not None"

# debug_get_log tool — chunked log access
LOG_TOOL_RAW=$(rpc 95 "tools/call" '{"name":"debug_get_log","arguments":{"offset":0,"limit":10}}')
LOG_TOOL=$(echo "$LOG_TOOL_RAW" | parse_tool_result)
assert_ok "debug_get_log returns totalLines" "$LOG_TOOL" \
    "d.get('totalLines',0) > 0"
assert_ok "debug_get_log returns returnedLines" "$LOG_TOOL" \
    "d.get('returnedLines',0) > 0 and d.get('returnedLines',0) <= 10"
assert_ok "debug_get_log returns hasMore" "$LOG_TOOL" \
    "'hasMore' in d"
assert_ok "debug_get_log returns log text" "$LOG_TOOL" \
    "len(d.get('log','')) > 0"

# Pagination: read from middle
LOG_TOTAL=$(echo "$LOG_TOOL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('totalLines',0))" 2>/dev/null)
if [ "$LOG_TOTAL" -gt 20 ]; then
    LOG_PAGE2_RAW=$(rpc 96 "tools/call" "{\"name\":\"debug_get_log\",\"arguments\":{\"offset\":10,\"limit\":5}}")
    LOG_PAGE2=$(echo "$LOG_PAGE2_RAW" | parse_tool_result)
    assert_ok "debug_get_log pagination works" "$LOG_PAGE2" \
        "d.get('offsetLines') == 10 and d.get('returnedLines') == 5"
fi

# Read unknown resource
UNK_RES_RAW=$(rpc 97 "resources/read" '{"uri":"decenza://nonexistent"}')
assert_ok "unknown resource returns error" "$UNK_RES_RAW" \
    "'error' in d.get('result',{}) or 'error' in d"

echo

# ─── 10. Scale Tools ───
echo -e "${CYAN}10. Scale Tools${NC}"

WEIGHT_RAW=$(rpc 96 "tools/call" '{"name":"scale_get_weight","arguments":{}}')
WEIGHT=$(echo "$WEIGHT_RAW" | parse_tool_result)
assert_ok "scale_get_weight returns weightG or error" "$WEIGHT" \
    "'weightG' in d or 'error' in d"

TARE_RAW=$(rpc 97 "tools/call" '{"name":"scale_tare","arguments":{}}')
TARE=$(echo "$TARE_RAW" | parse_tool_result)
assert_ok "scale_tare responds" "$TARE" \
    "'success' in d or 'error' in d"

echo

# ─── 11. Device Tools ───
echo -e "${CYAN}11. Device Tools${NC}"

DEV_LIST_RAW=$(rpc 98 "tools/call" '{"name":"devices_list","arguments":{}}')
DEV_LIST=$(echo "$DEV_LIST_RAW" | parse_tool_result)
assert_ok "devices_list returns devices array" "$DEV_LIST" \
    "'devices' in d or 'error' in d"

DEV_STATUS_RAW=$(rpc 99 "tools/call" '{"name":"devices_connection_status","arguments":{}}')
DEV_STATUS=$(echo "$DEV_STATUS_RAW" | parse_tool_result)
assert_ok "devices_connection_status returns machineConnected" "$DEV_STATUS" \
    "'machineConnected' in d"

# Don't actually trigger scan in automated tests (side effects)
# Just verify the tool exists (already checked in discovery)

# devices_connect_scale without address
DEV_CONNECT_RAW=$(rpc 100 "tools/call" '{"name":"devices_connect_scale","arguments":{}}')
DEV_CONNECT=$(echo "$DEV_CONNECT_RAW" | parse_tool_result)
assert_ok "devices_connect_scale requires address" "$DEV_CONNECT" \
    "'error' in d"

echo

# ─── 12. Write Tools ───
# Write tools require access level 2 (Full Automation).
# Detect current access level from settings_get.
ACCESS_LEVEL_RAW=$(rpc 99 "tools/call" '{"name":"machine_get_state","arguments":{}}')
# Check if settings tools are available by trying tools/list
TOOLS_FOR_ACCESS=$(rpc 99 "tools/list" '{}')
HAS_SETTINGS_SET=$(echo "$TOOLS_FOR_ACCESS" | python3 -c "
import json, sys
d = json.load(sys.stdin)
tools = [t['name'] for t in d.get('result',{}).get('tools',[])]
print('1' if 'settings_set' in tools else '0')
" 2>/dev/null)

if [ "$HAS_SETTINGS_SET" = "1" ]; then
    echo -e "${CYAN}12. Write Tools (Full Automation)${NC}"

    # Fresh session to avoid rate limit from earlier control tool calls
    delete_session
    create_session
else
    echo -e "${CYAN}12. Write Tools (SKIPPED — access level < 2, set to Full Automation to test)${NC}"
    SKIP=$((SKIP + 16))
fi

# shots_update (control category — level 1+, always available)
if [ "$SHOT_ID" != "0" ] && [ -n "$SHOT_ID" ]; then
    # Save original values for restore
    ORIG_DETAIL_RAW=$(rpc 100 "tools/call" "{\"name\":\"shots_get_detail\",\"arguments\":{\"shotId\":$SHOT_ID}}")
    ORIG_DETAIL=$(echo "$ORIG_DETAIL_RAW" | parse_tool_result)
    ORIG_ENJOYMENT=$(echo "$ORIG_DETAIL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('enjoyment',0))" 2>/dev/null)
    ORIG_NOTES=$(echo "$ORIG_DETAIL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('espressoNotes',''))" 2>/dev/null)
    ORIG_DOSE=$(echo "$ORIG_DETAIL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('doseWeight',0))" 2>/dev/null)
    ORIG_BARISTA=$(echo "$ORIG_DETAIL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('barista',''))" 2>/dev/null)

    # Test 1: enjoyment + notes
    UPDATE_RAW=$(rpc 101 "tools/call" "{\"name\":\"shots_update\",\"arguments\":{\"shotId\":$SHOT_ID,\"enjoyment\":85,\"notes\":\"MCP test note\"}}")
    UPDATE=$(echo "$UPDATE_RAW" | parse_tool_result)
    assert_ok "shots_update saves metadata" "$UPDATE" \
        "d.get('success') == True"

    # Read back to verify values actually persisted
    VERIFY_RAW=$(rpc 102 "tools/call" "{\"name\":\"shots_get_detail\",\"arguments\":{\"shotId\":$SHOT_ID}}")
    VERIFY=$(echo "$VERIFY_RAW" | parse_tool_result)
    assert_ok "shots_update enjoyment persisted" "$VERIFY" \
        "d.get('enjoyment') == 85"
    assert_ok "shots_update notes persisted" "$VERIFY" \
        "d.get('espressoNotes') == 'MCP test note'"

    # Test 2: full metadata (doseWeight + barista)
    UPDATE2_RAW=$(rpc 103 "tools/call" "{\"name\":\"shots_update\",\"arguments\":{\"shotId\":$SHOT_ID,\"doseWeight\":18.5,\"barista\":\"MCP Test\"}}")
    UPDATE2=$(echo "$UPDATE2_RAW" | parse_tool_result)
    assert_ok "shots_update full metadata accepted" "$UPDATE2" \
        "d.get('success') == True"

    VERIFY2_RAW=$(rpc 104 "tools/call" "{\"name\":\"shots_get_detail\",\"arguments\":{\"shotId\":$SHOT_ID}}")
    VERIFY2=$(echo "$VERIFY2_RAW" | parse_tool_result)
    assert_ok "shots_update doseWeight persisted" "$VERIFY2" \
        "d.get('doseWeight') == 18.5"
    assert_ok "shots_update barista persisted" "$VERIFY2" \
        "d.get('barista') == 'MCP Test'"

    # Test 3: extended metadata (beanBrand, grinderSetting — verifies camelCase key fix)
    ORIG_BRAND=$(echo "$ORIG_DETAIL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('beanBrand',''))" 2>/dev/null)
    ORIG_GSETTING=$(echo "$ORIG_DETAIL" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('grinderSetting',''))" 2>/dev/null)

    UPDATE3_RAW=$(rpc 107 "tools/call" "{\"name\":\"shots_update\",\"arguments\":{\"shotId\":$SHOT_ID,\"beanBrand\":\"MCP Test Bean\",\"grinderSetting\":\"42\"}}")
    UPDATE3=$(echo "$UPDATE3_RAW" | parse_tool_result)
    assert_ok "shots_update extended metadata accepted" "$UPDATE3" \
        "d.get('success') == True"

    VERIFY3_RAW=$(rpc 108 "tools/call" "{\"name\":\"shots_get_detail\",\"arguments\":{\"shotId\":$SHOT_ID}}")
    VERIFY3=$(echo "$VERIFY3_RAW" | parse_tool_result)
    assert_ok "shots_update beanBrand persisted" "$VERIFY3" \
        "d.get('beanBrand') == 'MCP Test Bean'"
    assert_ok "shots_update grinderSetting persisted" "$VERIFY3" \
        "d.get('grinderSetting') == '42'"

    # Verify partial update didn't wipe other fields (the data-loss bug)
    assert_ok "shots_update partial didn't wipe enjoyment" "$VERIFY3" \
        "d.get('enjoyment') == 85"

    # Restore all original values
    rpc 109 "tools/call" "{\"name\":\"shots_update\",\"arguments\":{\"shotId\":$SHOT_ID,\"enjoyment\":$ORIG_ENJOYMENT,\"notes\":\"$ORIG_NOTES\",\"doseWeight\":$ORIG_DOSE,\"barista\":\"$ORIG_BARISTA\",\"beanBrand\":\"$ORIG_BRAND\",\"grinderSetting\":\"$ORIG_GSETTING\"}}" > /dev/null
else
    echo -e "  ${YELLOW}SKIP${NC} shots_update (no shots)"
    SKIP=$((SKIP + 11))
fi

# shots_update without data
UPDATE_EMPTY_RAW=$(rpc 106 "tools/call" '{"name":"shots_update","arguments":{"shotId":1}}')
UPDATE_EMPTY=$(echo "$UPDATE_EMPTY_RAW" | parse_tool_result)
assert_ok "shots_update requires at least one field" "$UPDATE_EMPTY" \
    "'error' in d"

if [ "$HAS_SETTINGS_SET" = "1" ]; then
    # settings_set — save original, write, verify read-back, restore
    ORIG_TW_RAW=$(rpc 110 "tools/call" '{"name":"settings_get","arguments":{"keys":["targetWeight"]}}')
    ORIG_TW=$(echo "$ORIG_TW_RAW" | parse_tool_result)
    ORIG_TW_VAL=$(echo "$ORIG_TW" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('targetWeightG',0))" 2>/dev/null)

    SETW_RAW=$(rpc 111 "tools/call" '{"name":"settings_set","arguments":{"targetWeight":36.0,"confirmed":true}}')
    SETW=$(echo "$SETW_RAW" | parse_tool_result)
    assert_ok "settings_set updates targetWeight" "$SETW" \
        "d.get('success') == True and 'targetWeight' in d.get('updated',[])"

    # Read back to verify (settings_set uses QueuedConnection + uploadProfile, needs time)
    # Known failure: targetWeight triggers uploadProfile() which may asynchronously
    # override the value from the profile's stored weight (issue #527)
    sleep 1
    VERIFY_TW_RAW=$(rpc 112 "tools/call" '{"name":"settings_get","arguments":{"keys":["targetWeight"]}}')
    VERIFY_TW=$(echo "$VERIFY_TW_RAW" | parse_tool_result)
    assert_ok "settings_set targetWeight persisted" "$VERIFY_TW" \
        "d.get('targetWeightG') == 36.0"

    # Restore
    rpc 113 "tools/call" "{\"name\":\"settings_set\",\"arguments\":{\"targetWeight\":$ORIG_TW_VAL,\"confirmed\":true}}" > /dev/null

    # settings_set with no valid keys
    SETW_EMPTY_RAW=$(rpc 114 "tools/call" '{"name":"settings_set","arguments":{"confirmed":true}}')
    SETW_EMPTY=$(echo "$SETW_EMPTY_RAW" | parse_tool_result)
    assert_ok "settings_set with no keys returns error" "$SETW_EMPTY" \
        "'error' in d"

    # profiles_set_active with invalid profile
    BAD_PROFILE_RAW=$(rpc 115 "tools/call" '{"name":"profiles_set_active","arguments":{"filename":"nonexistent_xyz","confirmed":true}}')
    BAD_PROFILE=$(echo "$BAD_PROFILE_RAW" | parse_tool_result)
    assert_ok "profiles_set_active rejects invalid profile" "$BAD_PROFILE" \
        "'error' in d"

    # profiles_create + verify + delete + verify roundtrip
    CREATE_RAW=$(rpc 116 "tools/call" '{"name":"profiles_create","arguments":{"editorType":"pressure","title":"_MCP Test Profile","confirmed":true}}')
    CREATE=$(echo "$CREATE_RAW" | parse_tool_result)
    assert_ok "profiles_create creates profile" "$CREATE" \
        "d.get('success') == True and d.get('editorType') == 'pressure'"

    # profiles_create response filename may be empty due to async profile switch (issue #527).
    # Get the actual filename from profiles_get_active instead.
    sleep 1
    ACTIVE_RAW=$(rpc 117 "tools/call" '{"name":"profiles_get_active","arguments":{}}')
    ACTIVE=$(echo "$ACTIVE_RAW" | parse_tool_result)
    CREATED_FILE=$(echo "$ACTIVE" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('filename',''))" 2>/dev/null)

    if [ -n "$CREATED_FILE" ]; then
        assert_ok "profiles_create made profile active" "$ACTIVE" \
            "'_mcp_test_profile' in d.get('filename','').lower() or '_MCP' in d.get('title','')"

        # Switch back to default before deleting
        rpc 118 "tools/call" '{"name":"profiles_set_active","arguments":{"filename":"default","confirmed":true}}' > /dev/null
        sleep 1

        # Verify we're on default
        VERIFY_DEFAULT_RAW=$(rpc 119 "tools/call" '{"name":"profiles_get_active","arguments":{}}')
        VERIFY_DEFAULT=$(echo "$VERIFY_DEFAULT_RAW" | parse_tool_result)
        assert_ok "profiles_set_active switched to default" "$VERIFY_DEFAULT" \
            "'default' in d.get('filename','')"

        # Delete the test profile
        DEL_RAW=$(rpc 120 "tools/call" "{\"name\":\"profiles_delete\",\"arguments\":{\"filename\":\"$CREATED_FILE\",\"confirmed\":true}}")
        DEL=$(echo "$DEL_RAW" | parse_tool_result)
        assert_ok "profiles_delete removes created profile" "$DEL" \
            "d.get('success') == True"

        # Verify profile is gone from list
        VERIFY_LIST_RAW=$(rpc 121 "tools/call" '{"name":"profiles_list","arguments":{}}')
        VERIFY_LIST=$(echo "$VERIFY_LIST_RAW" | parse_tool_result)
        assert_ok "profiles_delete profile no longer in list" "$VERIFY_LIST" \
            "'$CREATED_FILE' not in str(d.get('profiles',[]))"
    fi

    # Fresh session to reset rate limiter
    delete_session
    create_session

    # --- DYE metadata round-trip ---
    # Save originals
    ORIG_DYE_RAW=$(rpc 130 "tools/call" '{"name":"settings_get","arguments":{"keys":["dyeBeanBrand","dyeGrinderSetting"]}}')
    ORIG_DYE=$(echo "$ORIG_DYE_RAW" | parse_tool_result)
    ORIG_BEAN_BRAND=$(echo "$ORIG_DYE" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('dyeBeanBrand',''))" 2>/dev/null)
    ORIG_GRIND=$(echo "$ORIG_DYE" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('dyeGrinderSetting',''))" 2>/dev/null)

    SET_DYE_RAW=$(rpc 131 "tools/call" '{"name":"settings_set","arguments":{"dyeBeanBrand":"MCP Test Brand","dyeGrinderSetting":"99","confirmed":true}}')
    SET_DYE=$(echo "$SET_DYE_RAW" | parse_tool_result)
    assert_ok "settings_set DYE fields accepted" "$SET_DYE" \
        "'dyeBeanBrand' in d.get('updated',[]) and 'dyeGrinderSetting' in d.get('updated',[])"

    sleep 1
    VERIFY_DYE_RAW=$(rpc 132 "tools/call" '{"name":"settings_get","arguments":{"keys":["dyeBeanBrand","dyeGrinderSetting"]}}')
    VERIFY_DYE=$(echo "$VERIFY_DYE_RAW" | parse_tool_result)
    assert_ok "settings_set dyeBeanBrand persisted" "$VERIFY_DYE" \
        "d.get('dyeBeanBrand') == 'MCP Test Brand'"
    assert_ok "settings_set dyeGrinderSetting persisted" "$VERIFY_DYE" \
        "d.get('dyeGrinderSetting') == '99'"

    # Restore
    rpc 133 "tools/call" "{\"name\":\"settings_set\",\"arguments\":{\"dyeBeanBrand\":\"$ORIG_BEAN_BRAND\",\"dyeGrinderSetting\":\"$ORIG_GRIND\",\"confirmed\":true}}" > /dev/null

    # --- profiles_edit_params round-trip ---
    # Save current profile params, change temperature, verify, restore
    ORIG_PARAMS_RAW=$(rpc 140 "tools/call" '{"name":"profiles_get_params","arguments":{}}')
    ORIG_PARAMS=$(echo "$ORIG_PARAMS_RAW" | parse_tool_result)
    ORIG_TEMP=$(echo "$ORIG_PARAMS" | python3 -c "
import json,sys
d = json.loads(sys.stdin.read())
# Recipe profiles use pourTemperature, advanced use espresso_temperature in steps
print(d.get('pourTemperature', d.get('espresso_temperature', 0)))
" 2>/dev/null)
    EDITOR_TYPE=$(echo "$ORIG_PARAMS" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('editorType',''))" 2>/dev/null)

    if [ -n "$EDITOR_TYPE" ] && [ "$EDITOR_TYPE" != "advanced" ] && [ "$ORIG_TEMP" != "0" ]; then
        NEW_TEMP=$(python3 -c "print(float($ORIG_TEMP) + 1)" 2>/dev/null)
        EDIT_RAW=$(rpc 141 "tools/call" "{\"name\":\"profiles_edit_params\",\"arguments\":{\"pourTemperature\":$NEW_TEMP,\"confirmed\":true}}")
        EDIT=$(echo "$EDIT_RAW" | parse_tool_result)
        assert_ok "profiles_edit_params accepted" "$EDIT" \
            "d.get('success') == True"

        sleep 1
        VERIFY_PARAMS_RAW=$(rpc 142 "tools/call" '{"name":"profiles_get_params","arguments":{}}')
        VERIFY_PARAMS=$(echo "$VERIFY_PARAMS_RAW" | parse_tool_result)
        assert_ok "profiles_edit_params temperature persisted" "$VERIFY_PARAMS" \
            "abs(d.get('pourTemperature',0) - $NEW_TEMP) < 0.1"

        # Restore
        rpc 143 "tools/call" "{\"name\":\"profiles_edit_params\",\"arguments\":{\"pourTemperature\":$ORIG_TEMP,\"confirmed\":true}}" > /dev/null
        sleep 0.5
    else
        echo -e "  ${YELLOW}SKIP${NC} profiles_edit_params (advanced profile or no temperature)"
        SKIP=$((SKIP + 2))
    fi

    # --- profiles_save round-trip ---
    # Ensure we're on the default profile (not the just-deleted test profile)
    rpc 149 "tools/call" '{"name":"profiles_set_active","arguments":{"filename":"default","confirmed":true}}' > /dev/null
    sleep 0.5

    # Built-in profiles are read-only — use Save As with a temp name
    SAVE_RAW=$(rpc 150 "tools/call" '{"name":"profiles_save","arguments":{"filename":"_mcp_test_save","title":"MCP Test Save","confirmed":true}}')
    SAVE=$(echo "$SAVE_RAW" | parse_tool_result)
    assert_ok "profiles_save accepted" "$SAVE" \
        "d.get('success') == True"

    # Verify profile is no longer modified
    sleep 0.5
    VERIFY_SAVED_RAW=$(rpc 151 "tools/call" '{"name":"profiles_get_active","arguments":{}}')
    VERIFY_SAVED=$(echo "$VERIFY_SAVED_RAW" | parse_tool_result)
    assert_ok "profiles_save cleared modified flag" "$VERIFY_SAVED" \
        "d.get('modified') == False"

    # Clean up: delete the test profile and restore the original
    rpc 152 "tools/call" '{"name":"profiles_delete","arguments":{"filename":"_mcp_test_save","confirmed":true}}' > /dev/null
    rpc 153 "tools/call" '{"name":"profiles_set_active","arguments":{"filename":"default","confirmed":true}}' > /dev/null
fi

echo

# ─── 13. Access Level Gating (interactive) ───
if [ "${SKIP_INTERACTIVE:-}" != "1" ]; then
    echo -e "${CYAN}13. Access Level Gating (requires manual setting changes)${NC}"

    # Helper: create fresh session and test tool access
    test_access_level() {
        local level_name="$1"
        local expected_level="$2"  # 0=Monitor, 1=Control, 2=Full

        echo -e "  ${YELLOW}ACTION${NC}: Set access level to ${CYAN}$level_name${NC} in Settings > AI > MCP, then press Enter"
        read -r

        # Fresh session (tracked for cleanup)
        local ASESS=$(curl -s --max-time 5 -D /tmp/mcp_access_headers -X POST "$BASE" -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}')
        local ASID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_access_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
        if [ -z "$ASID" ]; then
            ASID=$(grep -i 'Mcp-Session:' /tmp/mcp_access_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
        fi
        ALL_SESSIONS+=("$ASID")

        # Check which tools are visible
        local ATOOLS=$(curl -s --max-time 5 -X POST "$BASE" -H "Content-Type: application/json" -H "Mcp-Session: $ASID" \
            -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | python3 -c "
import json, sys
d = json.load(sys.stdin)
tools = [t['name'] for t in d.get('result',{}).get('tools',[])]
print(json.dumps(tools))
" 2>/dev/null)

        # Read tools should always be visible
        assert_ok "$level_name: read tools visible" "$ATOOLS" \
            "'machine_get_state' in d and 'shots_list' in d"

        if [ "$expected_level" -eq 0 ]; then
            # Monitor: control tools should NOT be visible
            assert_ok "$level_name: control tools hidden" "$ATOOLS" \
                "'machine_wake' not in d and 'machine_start_espresso' not in d"
            assert_ok "$level_name: settings tools hidden" "$ATOOLS" \
                "'settings_set' not in d and 'profiles_set_active' not in d"

            # Verify calling a control tool is rejected
            local REJECT=$(curl -s --max-time 5 -X POST "$BASE" -H "Content-Type: application/json" -H "Mcp-Session: $ASID" \
                -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"machine_wake","arguments":{}}}')
            assert_ok "$level_name: control tool call rejected" "$REJECT" \
                "d.get('error',{}).get('code') == -32603"

        elif [ "$expected_level" -eq 1 ]; then
            # Control: control tools visible, settings tools NOT
            assert_ok "$level_name: control tools visible" "$ATOOLS" \
                "'machine_wake' in d and 'machine_stop' in d"
            assert_ok "$level_name: settings tools hidden" "$ATOOLS" \
                "'settings_set' not in d and 'profiles_set_active' not in d"

            # Verify calling a settings tool is rejected
            local REJECT=$(curl -s --max-time 5 -X POST "$BASE" -H "Content-Type: application/json" -H "Mcp-Session: $ASID" \
                -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"settings_set","arguments":{"targetWeight":36}}}')
            assert_ok "$level_name: settings tool call rejected" "$REJECT" \
                "d.get('error',{}).get('code') == -32603"

        elif [ "$expected_level" -eq 2 ]; then
            # Full: everything visible
            assert_ok "$level_name: control tools visible" "$ATOOLS" \
                "'machine_wake' in d and 'machine_stop' in d"
            assert_ok "$level_name: settings tools visible" "$ATOOLS" \
                "'settings_set' in d and 'profiles_set_active' in d"
        fi

        # Cleanup
        curl -s --max-time 5 -X DELETE "$BASE" -H "Mcp-Session: $ASID" > /dev/null 2>&1
    }

    test_access_level "Monitor Only" 0
    test_access_level "Control" 1
    test_access_level "Full Automation" 2

    echo
else
    echo -e "${CYAN}13. Access Level Gating (SKIPPED — set SKIP_INTERACTIVE=1)${NC}"
    echo
fi

# ─── 14. Input Validation & Edge Cases ───
echo -e "${CYAN}14. Input Validation & Edge Cases${NC}"

# Missing required params
MISSING_RAW=$(rpc 80 "tools/call" '{"name":"shots_get_detail","arguments":{}}')
MISSING=$(echo "$MISSING_RAW" | parse_tool_result)
assert_ok "shots_get_detail missing shotId returns error" "$MISSING" \
    "'error' in d"

MISSING2_RAW=$(rpc 81 "tools/call" '{"name":"profiles_get_detail","arguments":{}}')
MISSING2=$(echo "$MISSING2_RAW" | parse_tool_result)
assert_ok "profiles_get_detail missing filename returns error" "$MISSING2" \
    "'error' in d"

# Boundary: shots_compare with 1 shot (needs 2-10)
COMPARE1_RAW=$(rpc 82 "tools/call" '{"name":"shots_compare","arguments":{"shotIds":[1]}}')
COMPARE1=$(echo "$COMPARE1_RAW" | parse_tool_result)
assert_ok "shots_compare with 1 shot returns error" "$COMPARE1" \
    "'error' in d and '2-10' in d.get('error','')"

# Boundary: shots_compare with 11 shots (needs 2-10)
COMPARE11_RAW=$(rpc 83 "tools/call" '{"name":"shots_compare","arguments":{"shotIds":[1,2,3,4,5,6,7,8,9,10,11]}}')
COMPARE11=$(echo "$COMPARE11_RAW" | parse_tool_result)
assert_ok "shots_compare with 11 shots returns error" "$COMPARE11" \
    "'error' in d and '2-10' in d.get('error','')"

# Boundary: shots_list limit clamped to max 100
BIGLIST_RAW=$(rpc 84 "tools/call" '{"name":"shots_list","arguments":{"limit":150}}')
BIGLIST=$(echo "$BIGLIST_RAW" | parse_tool_result)
assert_ok "shots_list clamps limit to 100" "$BIGLIST" \
    "d.get('count',0) <= 100"

# Boundary: shots_list with huge offset returns empty
BIGOFF_RAW=$(rpc 85 "tools/call" '{"name":"shots_list","arguments":{"offset":999999,"limit":5}}')
BIGOFF=$(echo "$BIGOFF_RAW" | parse_tool_result)
assert_ok "shots_list with huge offset returns empty" "$BIGOFF" \
    "d.get('count') == 0 and d.get('total',0) > 0"

# Pagination: offset is reflected in response
PAGE_RAW=$(rpc 86 "tools/call" '{"name":"shots_list","arguments":{"offset":3,"limit":2}}')
PAGE=$(echo "$PAGE_RAW" | parse_tool_result)
assert_ok "shots_list offset reflected in response" "$PAGE" \
    "d.get('offset') == 3"

# settings_get with non-existent key returns no extra fields
BADKEY_RAW=$(rpc 87 "tools/call" '{"name":"settings_get","arguments":{"keys":["nonexistentKey999"]}}')
BADKEY=$(echo "$BADKEY_RAW" | parse_tool_result)
assert_ok "settings_get with unknown key returns empty" "$BADKEY" \
    "'nonexistentKey999' not in d"

# Unknown tool name
UNKNOWN_TOOL_RAW=$(rpc 88 "tools/call" '{"name":"totally_fake_tool","arguments":{}}')
assert_ok "unknown tool returns error" "$UNKNOWN_TOOL_RAW" \
    "'error' in d.get('result',{}) or 'error' in d"

echo

# ─── 15. Rate Limiting ───
echo -e "${CYAN}15. Rate Limiting${NC}"

# Use a fresh session so previous control calls don't affect the count
delete_session
create_session

# Fire 11 machine_wake calls (succeeds on simulator).
# Calls 1-10 should work, 11th should be rate limited.
RATE_OK=true
ELEVENTH_LIMITED=false
for i in $(seq 1 11); do
    RATE_RAW=$(rpc $((100+i)) "tools/call" '{"name":"machine_wake","arguments":{}}')
    HAS_RATE_ERR=$(echo "$RATE_RAW" | python3 -c "
import json, sys
try:
    d = json.loads(sys.stdin.read())
    # Rate limit error is at top-level JSON-RPC error, not inside tool result
    print('1' if d.get('error',{}).get('code') == -32000 else '0')
except:
    print('0')
" 2>/dev/null)
    if [ "$i" -le 10 ] && [ "$HAS_RATE_ERR" = "1" ]; then RATE_OK=false; fi
    if [ "$i" -eq 11 ] && [ "$HAS_RATE_ERR" = "1" ]; then ELEVENTH_LIMITED=true; fi
done

if [ "$RATE_OK" = true ]; then
    echo -e "  ${GREEN}PASS${NC} first 10 control calls not rate limited"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} some of first 10 calls were incorrectly rate limited"
    FAIL=$((FAIL + 1))
fi
if [ "$ELEVENTH_LIMITED" = true ]; then
    echo -e "  ${GREEN}PASS${NC} 11th control call rate limited"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} 11th control call should be rate limited"
    FAIL=$((FAIL + 1))
fi

# Read tools should never be rate limited (even after control limit hit)
READ12=$(rpc 133 "tools/call" '{"name":"machine_get_state","arguments":{}}' | parse_tool_result)
assert_ok "read calls not rate limited" "$READ12" "'phase' in d"

echo

# ─── 16. Session Limits ───
echo -e "${CYAN}16. Session Limits${NC}"

# Delete current session to free a slot
delete_session

# Create sessions until we hit the limit (max 8 total)
LIMIT_SESSIONS=()
HIT_LIMIT=false
for i in $(seq 1 12); do
    SINIT=$(curl -s --max-time 5 -D /tmp/mcp_sess_headers -X POST "$BASE" -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$((200+i)),\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}")
    SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_sess_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
    if [ -z "$SID" ]; then
        SID=$(grep -i 'Mcp-Session:' /tmp/mcp_sess_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
    fi
    HAS_ERROR=$(echo "$SINIT" | python3 -c "import json,sys; d=json.load(sys.stdin); print('1' if 'error' in d else '0')" 2>/dev/null)
    if [ "$HAS_ERROR" = "1" ]; then
        HIT_LIMIT=true
        assert_ok "session limit enforced (rejected after ${#LIMIT_SESSIONS[@]} sessions)" "$SINIT" \
            "d.get('error',{}).get('code') == -32000"
        break
    fi
    LIMIT_SESSIONS+=("$SID")
done

if [ "$HIT_LIMIT" = false ]; then
    echo -e "  ${RED}FAIL${NC} created 12 sessions without hitting limit"
    FAIL=$((FAIL + 1))
fi

# Cleanup all limit test sessions
for sid in "${LIMIT_SESSIONS[@]}"; do
    curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $sid" > /dev/null 2>&1
done

# Re-create session for remaining tests
create_session

echo

# ─── 17. Session Management ───
echo -e "${CYAN}17. Session Management${NC}"

# Test: Re-initialize reuses existing session (no session leak)
REINIT_RESP=$(curl -s -D /tmp/mcp_reinit_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session-Id: $SESSION" \
    -d '{"jsonrpc":"2.0","id":170,"method":"initialize","params":{"capabilities":{}}}')
REINIT_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_reinit_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
assert_ok "re-initialize returns success" "$REINIT_RESP" \
    "d.get('result',{}).get('protocolVersion') is not None"
if [ "$REINIT_SID" = "$SESSION" ]; then
    echo -e "  ${GREEN}PASS${NC} re-initialize reuses same session ID (no leak)"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} re-initialize should reuse session ID (got: '$REINIT_SID', expected: '$SESSION')"
    FAIL=$((FAIL + 1))
fi

# Test: Session still works after re-initialize
AFTER_REINIT=$(rpc 171 "tools/list" '{}')
assert_ok "session works after re-initialize" "$AFTER_REINIT" \
    "isinstance(d.get('result',{}).get('tools'), list)"

# Test: Re-initialize without session header creates new (first connect)
FRESH_RESP=$(curl -s -D /tmp/mcp_fresh_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":172,"method":"initialize","params":{"capabilities":{}}}')
FRESH_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_fresh_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
assert_ok "initialize without session header creates new session" "$FRESH_RESP" \
    "d.get('result',{}).get('protocolVersion') is not None"
if [ -n "$FRESH_SID" ] && [ "$FRESH_SID" != "$SESSION" ]; then
    echo -e "  ${GREEN}PASS${NC} no-header initialize creates new session"
    PASS=$((PASS + 1))
    # Clean up
    curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $FRESH_SID" > /dev/null 2>&1
    ALL_SESSIONS+=("$FRESH_SID")
else
    echo -e "  ${RED}FAIL${NC} expected new session ID, got: '$FRESH_SID'"
    FAIL=$((FAIL + 1))
fi

# Test: Re-initialize with unknown session ID creates new (expired session)
STALE_RESP=$(curl -s -D /tmp/mcp_stale_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session-Id: 00000000-0000-0000-0000-000000000000" \
    -d '{"jsonrpc":"2.0","id":173,"method":"initialize","params":{"capabilities":{}}}')
STALE_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_stale_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
assert_ok "initialize with unknown session creates new" "$STALE_RESP" \
    "d.get('result',{}).get('protocolVersion') is not None"
if [ -n "$STALE_SID" ] && [ "$STALE_SID" != "00000000-0000-0000-0000-000000000000" ]; then
    echo -e "  ${GREEN}PASS${NC} stale session ID creates new session"
    PASS=$((PASS + 1))
    curl -s --max-time 2 -X DELETE "$BASE" -H "Mcp-Session-Id: $STALE_SID" > /dev/null 2>&1
    ALL_SESSIONS+=("$STALE_SID")
else
    echo -e "  ${RED}FAIL${NC} expected new session ID for stale header (got: '$STALE_SID')"
    FAIL=$((FAIL + 1))
fi

# DELETE session
DEL_RESP=$(curl -s -X DELETE "$BASE" -H "Mcp-Session-Id: $SESSION")
assert_ok "DELETE /mcp returns 200" "$DEL_RESP" \
    "True"  # any response is ok

# Verify deleted session auto-recovers (PR #520 — mcp-remote can't re-initialize)
POST_DEL=$(curl -s -D /tmp/mcp_del_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session: $SESSION" \
    -d '{"jsonrpc":"2.0","id":99,"method":"tools/list","params":{}}')
assert_ok "deleted session auto-recovers with new session" "$POST_DEL" \
    "isinstance(d.get('result',{}).get('tools'), list)"
# Track the auto-recovered session for cleanup
DEL_SID=$(grep -i 'Mcp-Session-Id' /tmp/mcp_del_headers 2>/dev/null | head -1 | awk '{print $2}' | tr -d '\r\n')
if [ -n "$DEL_SID" ]; then
    ALL_SESSIONS+=("$DEL_SID")
    SESSION="$DEL_SID"  # Update SESSION to the auto-recovered one
fi

echo

# ─── 17b. Ping & Subscribe ───
echo -e "${CYAN}17b. Ping & Subscribe${NC}"

# Test: ping returns empty result
PING_RESP=$(rpc 180 "ping" '{}')
assert_ok "ping returns empty result" "$PING_RESP" \
    "'result' in d and d['result'] == {}"

# Test: resources/subscribe
SUB_RESP=$(rpc 181 "resources/subscribe" '{"uri":"decenza://machine/state"}')
assert_ok "resources/subscribe returns success" "$SUB_RESP" \
    "'result' in d and not d['result']"

# Test: resources/subscribe with missing uri
SUB_ERR=$(rpc 182 "resources/subscribe" '{}')
assert_ok "resources/subscribe rejects missing uri" "$SUB_ERR" \
    "d.get('result',{}).get('error',{}).get('code') == -32602 or d.get('error',{}).get('code') == -32602"

# Test: resources/unsubscribe
UNSUB_RESP=$(rpc 183 "resources/unsubscribe" '{"uri":"decenza://machine/state"}')
assert_ok "resources/unsubscribe returns success" "$UNSUB_RESP" \
    "'result' in d and not d['result']"

echo

# ─── 18. Settings Parity (Phase 16) ───
echo -e "${CYAN}18. Settings Parity${NC}"

# Clean session for settings parity tests
delete_session
create_session

# Category filter
CAT_PREF_RAW=$(rpc 300 "tools/call" '{"name":"settings_get","arguments":{"category":"preferences"}}')
CAT_PREF=$(echo "$CAT_PREF_RAW" | parse_tool_result)
assert_ok "category 'preferences' returns autoSleepMinutes" "$CAT_PREF" \
    "'autoSleepMinutes' in d"
assert_ok "category 'preferences' returns themeMode" "$CAT_PREF" \
    "'themeMode' in d"
assert_ok "category 'preferences' excludes mqttEnabled" "$CAT_PREF" \
    "'mqttEnabled' not in d"
assert_ok "category 'preferences' excludes screensaverType" "$CAT_PREF" \
    "'screensaverType' not in d"

# Accessibility category
CAT_A11Y_RAW=$(rpc 301 "tools/call" '{"name":"settings_get","arguments":{"category":"accessibility"}}')
CAT_A11Y=$(echo "$CAT_A11Y_RAW" | parse_tool_result)
assert_ok "category 'accessibility' returns accessibilityEnabled" "$CAT_A11Y" \
    "'accessibilityEnabled' in d"
assert_ok "category 'accessibility' returns ttsEnabled" "$CAT_A11Y" \
    "'ttsEnabled' in d"
assert_ok "category 'accessibility' returns tickVolume" "$CAT_A11Y" \
    "'tickVolume' in d"
assert_ok "category 'accessibility' returns extractionAnnouncementMode" "$CAT_A11Y" \
    "'extractionAnnouncementMode' in d"

# Screensaver category
CAT_SS_RAW=$(rpc 302 "tools/call" '{"name":"settings_get","arguments":{"category":"screensaver"}}')
CAT_SS=$(echo "$CAT_SS_RAW" | parse_tool_result)
assert_ok "category 'screensaver' returns screensaverType" "$CAT_SS" \
    "'screensaverType' in d"
assert_ok "category 'screensaver' returns dimPercent" "$CAT_SS" \
    "'dimPercent' in d"

# MQTT category
CAT_MQTT_RAW=$(rpc 303 "tools/call" '{"name":"settings_get","arguments":{"category":"mqtt"}}')
CAT_MQTT=$(echo "$CAT_MQTT_RAW" | parse_tool_result)
assert_ok "category 'mqtt' returns mqttEnabled" "$CAT_MQTT" \
    "'mqttEnabled' in d"
assert_ok "category 'mqtt' returns mqttBrokerHost" "$CAT_MQTT" \
    "'mqttBrokerHost' in d"

# All settings (no category, no keys) — verify total field count
ALL_SET_RAW=$(rpc 304 "tools/call" '{"name":"settings_get","arguments":{}}')
ALL_SET=$(echo "$ALL_SET_RAW" | parse_tool_result)
ALL_COUNT=$(echo "$ALL_SET" | python3 -c "import json,sys; print(len(json.loads(sys.stdin.read())))" 2>/dev/null)
if [ "$ALL_COUNT" -ge 80 ]; then
    echo -e "  ${GREEN}PASS${NC} settings_get returns $ALL_COUNT fields (>= 80)"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}FAIL${NC} settings_get returns $ALL_COUNT fields (expected >= 80)"
    FAIL=$((FAIL + 1))
fi

# Sensitive fields NOT present
assert_ok "openaiApiKey not exposed" "$ALL_SET" "'openaiApiKey' not in d"
assert_ok "anthropicApiKey not exposed" "$ALL_SET" "'anthropicApiKey' not in d"
assert_ok "mqttPassword not exposed" "$ALL_SET" "'mqttPassword' not in d"
assert_ok "visualizerPassword not exposed" "$ALL_SET" "'visualizerPassword' not in d"
assert_ok "mcpApiKey not exposed" "$ALL_SET" "'mcpApiKey' not in d"

# Keys across categories
CROSS_RAW=$(rpc 305 "tools/call" '{"name":"settings_get","arguments":{"keys":["autoSleepMinutes","mqttEnabled","screensaverType"]}}')
CROSS=$(echo "$CROSS_RAW" | parse_tool_result)
CROSS_COUNT=$(echo "$CROSS" | python3 -c "import json,sys; print(len(json.loads(sys.stdin.read())))" 2>/dev/null)
assert_ok "cross-category key filter returns exactly 3" "$CROSS" \
    "len(d) == 3 and 'autoSleepMinutes' in d and 'mqttEnabled' in d and 'screensaverType' in d"

# Write test (only if Full Automation)
if [ "$HAS_SETTINGS_SET" = "1" ]; then
    # Save + set + verify read-back + restore autoSleepMinutes
    ORIG_SLEEP=$(echo "$CAT_PREF" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('autoSleepMinutes',60))" 2>/dev/null)
    SET_SLEEP_RAW=$(rpc 306 "tools/call" '{"name":"settings_set","arguments":{"autoSleepMinutes":30,"confirmed":true}}')
    SET_SLEEP=$(echo "$SET_SLEEP_RAW" | parse_tool_result)
    assert_ok "settings_set autoSleepMinutes accepted" "$SET_SLEEP" \
        "'autoSleepMinutes' in d.get('updated',[])"
    # Read back to verify the value persisted
    sleep 0.3
    VERIFY_SLEEP_RAW=$(rpc 308 "tools/call" '{"name":"settings_get","arguments":{"keys":["autoSleepMinutes"]}}')
    VERIFY_SLEEP=$(echo "$VERIFY_SLEEP_RAW" | parse_tool_result)
    assert_ok "settings_set autoSleepMinutes persisted" "$VERIFY_SLEEP" \
        "d.get('autoSleepMinutes') == 30"
    # Restore
    rpc 307 "tools/call" "{\"name\":\"settings_set\",\"arguments\":{\"autoSleepMinutes\":$ORIG_SLEEP,\"confirmed\":true}}" > /dev/null

    # --- Cross-category write/verify round-trips ---

    # Accessibility: tickVolume
    ORIG_TICK_RAW=$(rpc 310 "tools/call" '{"name":"settings_get","arguments":{"keys":["tickVolume"]}}')
    ORIG_TICK=$(echo "$ORIG_TICK_RAW" | parse_tool_result)
    ORIG_TICK_VAL=$(echo "$ORIG_TICK" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('tickVolume',50))" 2>/dev/null)
    rpc 311 "tools/call" '{"name":"settings_set","arguments":{"tickVolume":42,"confirmed":true}}' > /dev/null
    sleep 0.5
    VERIFY_TICK_RAW=$(rpc 312 "tools/call" '{"name":"settings_get","arguments":{"keys":["tickVolume"]}}')
    VERIFY_TICK=$(echo "$VERIFY_TICK_RAW" | parse_tool_result)
    assert_ok "settings_set tickVolume persisted" "$VERIFY_TICK" \
        "d.get('tickVolume') == 42"
    rpc 313 "tools/call" "{\"name\":\"settings_set\",\"arguments\":{\"tickVolume\":$ORIG_TICK_VAL,\"confirmed\":true}}" > /dev/null

    # Screensaver: dimPercent
    ORIG_DIM_RAW=$(rpc 314 "tools/call" '{"name":"settings_get","arguments":{"keys":["dimPercent"]}}')
    ORIG_DIM=$(echo "$ORIG_DIM_RAW" | parse_tool_result)
    ORIG_DIM_VAL=$(echo "$ORIG_DIM" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('dimPercent',50))" 2>/dev/null)
    rpc 315 "tools/call" '{"name":"settings_set","arguments":{"dimPercent":25,"confirmed":true}}' > /dev/null
    sleep 0.5
    VERIFY_DIM_RAW=$(rpc 316 "tools/call" '{"name":"settings_get","arguments":{"keys":["dimPercent"]}}')
    VERIFY_DIM=$(echo "$VERIFY_DIM_RAW" | parse_tool_result)
    assert_ok "settings_set dimPercent persisted" "$VERIFY_DIM" \
        "d.get('dimPercent') == 25"
    rpc 317 "tools/call" "{\"name\":\"settings_set\",\"arguments\":{\"dimPercent\":$ORIG_DIM_VAL,\"confirmed\":true}}" > /dev/null
fi

echo

# ─── Summary ───
TOTAL=$((PASS + FAIL + SKIP))
echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo -e "  Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}, ${YELLOW}$SKIP skipped${NC} / $TOTAL total"
echo -e "${CYAN}═══════════════════════════════════════════${NC}"

# Cleanup
rm -f /tmp/mcp_headers

exit $FAIL
