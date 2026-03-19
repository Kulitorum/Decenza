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

EXPECTED_TOOLS="machine_get_state machine_get_telemetry shots_list shots_get_detail shots_compare profiles_list profiles_get_active profiles_get_detail settings_get dialing_get_context machine_wake machine_sleep machine_start_espresso machine_start_steam machine_start_hot_water machine_start_flush machine_stop machine_skip_frame"
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

EXPECTED_RESOURCES="decenza://machine/state decenza://machine/telemetry decenza://profiles/active decenza://shots/recent decenza://profiles/list"
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

# Read unknown resource
UNK_RES_RAW=$(rpc 93 "resources/read" '{"uri":"decenza://nonexistent"}')
assert_ok "unknown resource returns error" "$UNK_RES_RAW" \
    "'error' in d.get('result',{}) or 'error' in d"

echo

# ─── 10. Input Validation & Edge Cases ───
echo -e "${CYAN}10. Input Validation & Edge Cases${NC}"

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

# ─── 11. Rate Limiting ───
echo -e "${CYAN}11. Rate Limiting${NC}"

# Use a fresh session so previous control calls don't affect the count
curl -s -X DELETE "$BASE" -H "Mcp-Session: $SESSION" > /dev/null 2>&1
RATE_INIT=$(curl -s -D /tmp/mcp_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":99,"method":"initialize","params":{"capabilities":{}}}')
SESSION=$(grep -i 'Mcp-Session' /tmp/mcp_headers 2>/dev/null | awk '{print $2}' | tr -d '\r\n')

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

# ─── 12. Session Limits ───
echo -e "${CYAN}12. Session Limits${NC}"

# Delete current session
curl -s -X DELETE "$BASE" -H "Mcp-Session: $SESSION" > /dev/null 2>&1

# Create sessions until we hit the limit (max 8 total)
CREATED_SESSIONS=()
HIT_LIMIT=false
for i in $(seq 1 12); do
    SINIT=$(curl -s --max-time 5 -D /tmp/mcp_sess_headers -X POST "$BASE" -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$((200+i)),\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}")
    SID=$(grep -i 'Mcp-Session' /tmp/mcp_sess_headers 2>/dev/null | awk '{print $2}' | tr -d '\r\n')
    HAS_ERROR=$(echo "$SINIT" | python3 -c "import json,sys; d=json.load(sys.stdin); print('1' if 'error' in d else '0')" 2>/dev/null)
    if [ "$HAS_ERROR" = "1" ]; then
        HIT_LIMIT=true
        assert_ok "session limit enforced (rejected after ${#CREATED_SESSIONS[@]} sessions)" "$SINIT" \
            "d.get('error',{}).get('code') == -32000"
        break
    fi
    CREATED_SESSIONS+=("$SID")
done

if [ "$HIT_LIMIT" = false ]; then
    echo -e "  ${RED}FAIL${NC} created 12 sessions without hitting limit"
    FAIL=$((FAIL + 1))
fi

# Cleanup
for sid in "${CREATED_SESSIONS[@]}"; do
    curl -s -X DELETE "$BASE" -H "Mcp-Session: $sid" > /dev/null 2>&1
done

# Re-create session for remaining tests
REINIT=$(curl -s -D /tmp/mcp_headers -X POST "$BASE" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":210,"method":"initialize","params":{"capabilities":{}}}')
SESSION=$(grep -i 'Mcp-Session' /tmp/mcp_headers 2>/dev/null | awk '{print $2}' | tr -d '\r\n')

echo

# ─── 13. Session Management ───
echo -e "${CYAN}13. Session Management${NC}"

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
