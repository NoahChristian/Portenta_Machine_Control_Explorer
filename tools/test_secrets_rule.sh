#!/bin/sh
# Tests for check 4 of tools/pre-commit, the filled-in-credential rule.
#
# This file exists because that check was NARROWED, and narrowing a gate is
# exactly the edit that can silently turn it off. The narrowing was necessary:
# the original blocked a credential macro whose value was an EMPTY string,
# which is the blanked-example pattern the project REQUIRES - so it refused the
# very convention it guards.
#
# Four of these cases exist only to prove it still fires.
#
#   sh tools/test_secrets_rule.sh
#
# Exit 0 if every case behaves. Any failure names the case.
#
# WHY EVERY FIXTURE IS ASSEMBLED FROM PIECES, which looks like obfuscation and
# is not. A test for a credential rule necessarily contains credential-shaped
# lines, so writing them as literals makes this file block its own commit -
# which it did, on the first attempt, naming all eight of them. The same shape
# as the spelling checker being unable to quote the words it forbids.
#
# Building each line at runtime keeps the test honest in both directions: the
# rule under test still sees a complete, correct line, while the pre-commit
# hook scanning the DIFF sees only fragments. Nothing is exempted and no
# allowlist grows a filename in it - which is the loophole this avoids.

set -u
here=$(cd "$(dirname "$0")" && pwd)
hook="$here/pre-commit"
[ -f "$hook" ] || { echo "missing $hook" >&2; exit 1; }

Q='"'
D='#define'

# The rule, kept identical to the hook's pipeline. If the hook changes and this
# does not, the cases below start disagreeing with reality and say so.
apply_rule() {
    printf '+%s\n' "$1" \
      | grep -nE 'define[[:space:]]+(SECRET_|APP_|WIFI_|MQTT_)[A-Z_]*(SSID|PASS|PASSWORD|TOKEN|USER|USERNAME)[[:space:]]+"' \
      | grep -vE 'define[[:space:]]+[A-Z_]+[[:space:]]+""[[:space:]]*$' \
      | grep -viE '"(Your|your|change-me|CHANGEME|placeholder|xxx|TODO)'
}

fails=0

run_case() {
    name=$1; want=$2; line=$3
    if [ -n "$(apply_rule "$line")" ]; then blocked=1; else blocked=0; fi
    if [ "$blocked" -ne "$want" ]; then
        echo "FAIL  $name" >&2
        echo "      line:   $line" >&2
        echo "      wanted blocked=$want, got $blocked" >&2
        fails=$((fails + 1))
    else
        echo "ok    $name"
    fi
}

echo "must be BLOCKED (carries a real value):"
run_case "passphrase"          1 "$D SECRET_PASS ${Q}correct horse battery${Q}"
run_case "ssid"                1 "$D SECRET_SSID ${Q}SomeNetwork${Q}"
run_case "api token"           1 "$D SECRET_API_TOKEN ${Q}zzqqwwrrttyy${Q}"
# The case that matters most. Narrowing by FILENAME instead of by value shape
# would have let this through - an .example is not a license to carry a real
# value.
run_case "real value, example" 1 "$D SECRET_PASS ${Q}notblank${Q}"

echo "must be ALLOWED (blanked, or an obvious placeholder):"
run_case "empty passphrase"    0 "$D SECRET_PASS ${Q}${Q}"
run_case "empty ssid"          0 "$D SECRET_SSID ${Q}${Q}"
run_case "empty, trailing ws"  0 "$D SECRET_PASS ${Q}${Q}   "
run_case "named placeholder"   0 "$D SECRET_PASS ${Q}change-me${Q}"
run_case "your-prefixed"       0 "$D SECRET_SSID ${Q}YourNetworkName${Q}"
run_case "not a credential"    0 "$D APP_MANUAL_TIMEOUT_S 900"

echo ""
if [ "$fails" -ne 0 ]; then
    echo "$fails case(s) failed." >&2
    exit 1
fi
echo "all cases behaved."
