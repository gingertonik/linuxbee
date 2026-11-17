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
    allowed='matrix\.step|github\.ref'
    [ "$w" = release.yml ] && allowed='github\.token' # release.md, R-RIGHTS
    grep -o '\${{[^}]*}}' "$W" | grep -vxE "\\\$\{\{ ($allowed) \}\}" | head -n 3 | grep . && no "$w: an expression that is not allowed there"
    # C-RIGHTS
    grep -q 'secrets\.' "$W" && no "$w: a secret is used"
    [ "$(grep -cE '^permissions:' "$W")" = 1 ] || no "$w: no single top-level permissions"
    awk '/^permissions:/{f=1;next} f&&/^[^ ]/{f=0} f' "$W" | tr -d ' ' | grep -vx 'contents:read' | grep . && no "$w: rights beyond reading the source"
    if [ "$w" = release.yml ]; then # R-RIGHTS: one job, which may write the repository's contents, and no more
        [ "$(grep -cE '^[[:space:]]+permissions:' "$W")" = 1 ] || no "$w: not exactly one job with rights of its own"
        awk '/^[[:space:]]+permissions:/{f=1;next} f&&!/^      /{f=0} f' "$W" | tr -d ' ' | grep -vx 'contents:write' | grep . && no "$w: the job asks for more than writing a release"
        [ "$(grep -c 'github\.token' "$W")" = 1 ] || no "$w: the token is not named exactly once"
        awk '/- name: Publish/{f=1} /github\.token/&&!f{print}' "$W" | grep . && no "$w: the token is given to a step before the last"
        [ "$(grep -c '^      - name:' "$W")" -ge 1 ] && [ "$(grep '^      - name:' "$W" | tail -n 1)" = '      - name: Publish' ] || no "$w: publishing is not the last step"
        grep -qE "^    tags: \['v\*'\]" "$W" || no "$w: not on tags v*"
        grep -qE '^[[:space:]]+(branches|pull_request|schedule|workflow_dispatch)' "$W" && no "$w: runs on something besides a tag"
        grep -q 'GITHUB_REF_NAME' "$W" && { grep 'GITHUB_REF_NAME' "$W" | grep -v "su ci -c '" | grep . && no "$w: the tag's name is pasted into a script"; }
        grep -n 'release.sh check' "$W" | head -n 1 | cut -d: -f1 > /dev/null
        c1=$(grep -n 'release.sh check' "$W" | head -n 1 | cut -d: -f1); c2=$(grep -n 'ci.sh install' "$W" | head -n 1 | cut -d: -f1); c3=$(grep -n 'release.sh publish' "$W" | head -n 1 | cut -d: -f1)
        [ -n "$c1" ] && [ -n "$c2" ] && [ -n "$c3" ] && [ "$c1" -lt "$c2" ] && [ "$c2" -lt "$c3" ] || no "$w: not check, then install, then publish"
    else
        grep -qE '^[[:space:]]+permissions:' "$W" && no "$w: a job asks for rights of its own"
    fi
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
for step in packages user gate sanitize release install; do grep -q "^$step)" "$S" || no "tools/ci.sh has no step $step"; done
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
