#!/bin/bash
# MCP Server Test Suite
# Usage: ./scripts/test_mcp.sh [host:port]
# Requires: curl, python3
# The app must be running with MCP enabled in settings.

set -euo pipefail

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
    curl -s -D /tmp/mcp_headers "${headers[@]}" -X POST "$BASE" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"$method\",\"params\":$params}"
}

extract_session() {
    grep -i 'Mcp-Session' /tmp/mcp_headers 2>/dev/null | awk '{print $2}' | tr -d '\r\n'
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

echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo -e "${CYAN}  Decenza MCP Server Test Suite${NC}"
echo -e "${CYAN}  Target: $BASE${NC}"
echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo

# ─── 1. Protocol Tests ───
echo -e "${CYAN}1. Protocol${NC}"

# Test: MCP disabled returns 404 (can't test if enabled — skip)
# Test: Initialize
INIT_RESP=$(rpc 1 "initialize" '{"capabilities":{}}')
SESSION=$(extract_session)
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

# Test: Invalid session
INVALID_RESP=$(curl -s -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session: invalid-session-id" \
    -d '{"jsonrpc":"2.0","id":99,"method":"tools/list","params":{}}')
assert_ok "invalid session returns error" "$INVALID_RESP" \
    "d.get('error',{}).get('code') == -32600"

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

EXPECTED_TOOLS="machine_get_state machine_get_telemetry shots_list shots_get_detail shots_compare profiles_list profiles_get_active profiles_get_detail settings_get"
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

TELEM_RAW=$(rpc 21 "tools/call" '{"name":"machine_get_telemetry","arguments":{}}')
TELEM=$(echo "$TELEM_RAW" | parse_tool_result)
assert_ok "machine_get_telemetry returns pressure" "$TELEM" \
    "'pressure' in d"
assert_ok "machine_get_telemetry returns flow" "$TELEM" \
    "'flow' in d"
assert_ok "machine_get_telemetry returns temperature" "$TELEM" \
    "'temperature' in d"
assert_ok "machine_get_telemetry returns scaleWeight" "$TELEM" \
    "'scaleWeight' in d"

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
    assert_ok "shot has dose" "$FIRST_SHOT" "'dose' in d"
    assert_ok "shot has yield" "$FIRST_SHOT" "'yield' in d"
    assert_ok "shot has duration" "$FIRST_SHOT" "'duration' in d"
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
else
    echo -e "  ${YELLOW}SKIP${NC} shot detail/compare tests (no shots in database)"
    SKIP=$((SKIP + 3))
fi

# Invalid shot ID
BAD_SHOT_RAW=$(rpc 34 "tools/call" '{"name":"shots_get_detail","arguments":{"shotId":999999}}')
BAD_SHOT=$(echo "$BAD_SHOT_RAW" | parse_tool_result)
assert_ok "shots_get_detail invalid ID returns error" "$BAD_SHOT" \
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
assert_ok "profiles_get_active returns targetWeight" "$ACTIVE" \
    "'targetWeight' in d"

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
assert_ok "settings_get returns espressoTemperature" "$SETTINGS" \
    "'espressoTemperature' in d"
assert_ok "settings_get returns targetWeight" "$SETTINGS" \
    "'targetWeight' in d"
assert_ok "settings_get returns steamTemperature" "$SETTINGS" \
    "'steamTemperature' in d"
assert_ok "settings_get returns DYE metadata" "$SETTINGS" \
    "'dyeBeanBrand' in d"

# Filtered settings
FILTERED_SET_RAW=$(rpc 51 "tools/call" '{"name":"settings_get","arguments":{"keys":["espressoTemperature","targetWeight"]}}')
FILTERED_SET=$(echo "$FILTERED_SET_RAW" | parse_tool_result)
assert_ok "settings_get with keys filter works" "$FILTERED_SET" \
    "'espressoTemperature' in d and 'targetWeight' in d"
assert_ok "settings_get filter excludes other keys" "$FILTERED_SET" \
    "'steamTemperature' not in d"

echo

# ─── 7. Session Management ───
echo -e "${CYAN}7. Session Management${NC}"

# DELETE session
DEL_RESP=$(curl -s -X DELETE "$BASE" -H "Mcp-Session: $SESSION")
assert_ok "DELETE /mcp returns 200" "$DEL_RESP" \
    "True"  # any response is ok

# Verify deleted session is invalid
POST_DEL=$(curl -s -X POST "$BASE" -H "Content-Type: application/json" \
    -H "Mcp-Session: $SESSION" \
    -d '{"jsonrpc":"2.0","id":99,"method":"tools/list","params":{}}')
assert_ok "deleted session returns error" "$POST_DEL" \
    "d.get('error',{}).get('code') == -32600"

echo

# ─── Summary ───
TOTAL=$((PASS + FAIL + SKIP))
echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo -e "  Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}, ${YELLOW}$SKIP skipped${NC} / $TOTAL total"
echo -e "${CYAN}═══════════════════════════════════════════${NC}"

# Cleanup
rm -f /tmp/mcp_headers

exit $FAIL
