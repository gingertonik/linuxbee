#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# The CI workflow, read as text: its rights, what it runs and where. Rules: docs/spec/ci.md.
# It cannot run the workflow; that is done in the image by hand (ci.md, C-STEPS) and by GitHub.
# usage: cli_ci.sh /path/to/linuxbeectl   (the argument is not used)
set -u
SRC=$(cd "$(dirname "$0")/../.." && pwd)
fail=0
no() { echo "FAIL: $1"; fail=$((fail + 1)); }
for W in "$SRC"/.github/workflows/*; do
    [ -f "$W" ] || { no "no workflow"; break; }
    w=$(basename "$W")
    # C-NOACTIONS
    grep -nE '^[[:space:]-]*uses[[:space:]]*:' "$W" && no "$w: an action is used"
    grep -o '\${{[^}]*}}' "$W" | grep -vxE '\$\{\{ (matrix\.step|github\.ref) \}\}' | head -n 3 | grep . && no "$w: an expression other than matrix.step and github.ref"
    # C-RIGHTS
    grep -q 'secrets\.' "$W" && no "$w: a secret is used"
    [ "$(grep -cE '^permissions:' "$W")" = 1 ] || no "$w: no single top-level permissions"
    awk '/^permissions:/{f=1;next} f&&/^[^ ]/{f=0} f' "$W" | tr -d ' ' | grep -vx 'contents:read' | grep . && no "$w: rights beyond reading the source"
    grep -qE '^[[:space:]]+permissions:' "$W" && no "$w: a job asks for rights of its own"
    # C-WHEN
    grep -q 'pull_request_target\|workflow_run\|issue_comment' "$W" && no "$w: an event that runs with the repository's rights on a stranger's code"
    # C-CONTAINER
    grep -E '^[[:space:]]*(container|image)[[:space:]]*:' "$W" | grep -vE 'archlinux:base-devel@sha256:[0-9a-f]{64}[[:space:]]*$' | grep . && no "$w: a container not named by digest"
    grep -q 'curl\|wget' "$W" && no "$w: something is downloaded in the workflow itself"
done
C="$SRC/.github/workflows/ci.yml"
if [ -f "$C" ]; then
    grep -q 'archlinux:base-devel@sha256:' "$C" || no "ci.yml: the jobs do not run in the Arch image"
    grep -qE 'step: \[gate, sanitize, release\]' "$C" || no "ci.yml: not the three steps"
    grep -q 'fail-fast: false' "$C" || no "ci.yml: one step failing stops the others"
    grep -q 'GITHUB_SHA' "$C" || no "ci.yml: the pushed commit is not what is fetched"
    grep -qE 'su ci -c "sh tools/ci.sh \$\{STEP\}"' "$C" || no "ci.yml: the step does not run as the ordinary user"
    grep -qE 'branches: \[main\]' "$C" || no "ci.yml: not on pushes to main"
    grep -qE '^  pull_request:' "$C" || no "ci.yml: not on pull requests"
else
    no "no .github/workflows/ci.yml"
fi
S="$SRC/tools/ci.sh"
sh -n "$S" 2> /dev/null || no "tools/ci.sh does not parse"
for step in packages user gate sanitize release; do grep -q "^$step)" "$S" || no "tools/ci.sh has no step $step"; done
grep -q 'sudo\|curl\|wget' "$S" && no "tools/ci.sh downloads or escalates"
sh "$S" nosuch > /dev/null 2>&1 && no "tools/ci.sh takes a step it does not have"
# The two steps that change a system do nothing unless told it is a throwaway one. (Stand-ins, so that
# nothing is installed and no user made if they do go on.)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
for x in pacman useradd chown id; do printf '#!/bin/sh\necho "$0" >> "%s/ran"\nexit 0\n' "$T" > "$T/$x"; chmod +x "$T/$x"; done
for step in packages user; do env -u CI PATH="$T:$PATH" sh "$S" "$step" > /dev/null 2>&1 && no "tools/ci.sh $step went on outside a throwaway container"; done
[ -e "$T/ran" ] && no "tools/ci.sh changed the system without CI=true: $(sort -u "$T/ran" | tr '\n' ' ')"
CI=true PATH="$T:$PATH" sh "$S" packages > /dev/null 2>&1; grep -q pacman "$T/ran" 2> /dev/null || no "tools/ci.sh packages does not install with CI=true"
echo "cli_ci: $fail failed"
[ "$fail" -eq 0 ]
