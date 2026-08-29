#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
server_pid=
sentinel_pid=
sentinel_stop_path=

fail() {
    echo "git remote revision observer integration: $*" >&2
    exit 1
}

cleanup() {
    if [ -n "$sentinel_pid" ]; then
        if [ -n "$sentinel_stop_path" ]; then
            : > "$sentinel_stop_path"
        fi
        kill "$sentinel_pid" 2>/dev/null || true
        wait "$sentinel_pid" 2>/dev/null || true
    fi
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

control_root=$tmp_dir/control
server_root=$tmp_dir/server
repository_root=$tmp_dir/repositories
certificate_root=$tmp_dir/certificates
watched_root=$tmp_dir/watched
home_root=$watched_root/home
xdg_config_root=$watched_root/xdg-config
xdg_state_root=$watched_root/xdg-state
xdg_cache_root=$watched_root/xdg-cache
xdg_data_root=$watched_root/xdg-data
cwd_repository=$watched_root/cwd-repository
outside_root=$watched_root/outside
trace_root=$watched_root/trace-targets
marker_root=$watched_root/markers
clean_home=$control_root/clean-home
clean_xdg=$control_root/clean-xdg

mkdir -p \
    "$control_root" "$server_root" "$repository_root" \
    "$certificate_root" "$home_root" "$xdg_config_root/git" \
    "$xdg_state_root" "$xdg_cache_root" "$xdg_data_root" \
    "$outside_root" "$trace_root" "$marker_root" \
    "$clean_home" "$clean_xdg"
chmod 0700 \
    "$home_root" "$xdg_config_root" "$xdg_state_root" \
    "$xdg_cache_root" "$xdg_data_root"

ca_key=$certificate_root/ca.key
ca_certificate=$certificate_root/ca.pem
server_key=$certificate_root/server.key
server_request=$certificate_root/server.csr
server_certificate=$certificate_root/server.pem
openssl_log=$control_root/openssl.log

if ! /usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -sha256 \
    -days 2 -subj /CN=Moguet-Observer-Test-CA \
    -keyout "$ca_key" -out "$ca_certificate" \
    >"$openssl_log" 2>&1; then
    fail "test CA generation failed"
fi
if ! /usr/bin/openssl req -newkey rsa:2048 -nodes -sha256 \
    -subj /CN=127.0.0.1 \
    -addext 'subjectAltName=IP:127.0.0.1,DNS:localhost' \
    -keyout "$server_key" -out "$server_request" \
    >>"$openssl_log" 2>&1; then
    fail "test server certificate request generation failed"
fi
if ! /usr/bin/openssl x509 -req -sha256 -days 2 \
    -in "$server_request" -CA "$ca_certificate" -CAkey "$ca_key" \
    -set_serial 1 -copy_extensions copy -out "$server_certificate" \
    >>"$openssl_log" 2>&1; then
    fail "test server certificate signing failed"
fi
if ! /usr/bin/openssl x509 -in "$server_certificate" -noout \
    -checkip 127.0.0.1 >>"$openssl_log" 2>&1; then
    fail "test server certificate SAN does not cover loopback identity"
fi

create_served_repository() {
    object_format=$1
    work_repository=$2
    served_repository=$3
    tracked_contents=$4
    commit_date=$5

    if ! /usr/bin/git init -q --object-format="$object_format" \
        --initial-branch=main "$work_repository"; then
        fail "$object_format repository environment unsupported"
    fi
    printf '%s\n' "$tracked_contents" > "$work_repository/tracked.txt"
    /usr/bin/git -C "$work_repository" add tracked.txt
    GIT_AUTHOR_DATE="$commit_date" GIT_COMMITTER_DATE="$commit_date" \
        /usr/bin/git -C "$work_repository" \
        -c user.name='Moguet Observer Fixture' \
        -c user.email='observer-fixture@example.invalid' \
        commit -q -m initial
    /usr/bin/git -C "$work_repository" branch exact
    /usr/bin/git clone -q --bare "$work_repository" "$served_repository"
    /usr/bin/git --git-dir="$served_repository" update-server-info
}

sha1_work=$repository_root/sha1-work
sha1_served=$server_root/sha1.git
sha256_work=$repository_root/sha256-work
sha256_served=$server_root/sha256.git
escape_work=$repository_root/escape-work
escape_served=$repository_root/escape.git

create_served_repository \
    sha1 "$sha1_work" "$sha1_served" sha1-initial \
    '2026-01-01T00:00:00+0000'
create_served_repository \
    sha256 "$sha256_work" "$sha256_served" sha256-initial \
    '2026-01-02T00:00:00+0000'
create_served_repository \
    sha1 "$escape_work" "$escape_served" rewrite-escape \
    '2026-01-03T00:00:00+0000'

sha1_initial=$(/usr/bin/git -C "$sha1_work" rev-parse HEAD)
sha256_initial=$(/usr/bin/git -C "$sha256_work" rev-parse HEAD)
escape_oid=$(/usr/bin/git -C "$escape_work" rev-parse HEAD)
[ "${#sha1_initial}" -eq 40 ] || fail "SHA-1 fixture OID width differs"
[ "${#sha256_initial}" -eq 64 ] || fail "SHA-256 fixture OID width differs"
[ "$escape_oid" != "$sha1_initial" ] || fail "rewrite escape OID is not distinct"

direct_served=$server_root/direct.git
cp -a "$sha1_served" "$direct_served"
/usr/bin/git --git-dir="$direct_served" \
    update-ref --no-deref HEAD "$sha1_initial"
/usr/bin/git --git-dir="$direct_served" update-server-info

unborn_served=$server_root/unborn.git
/usr/bin/git init -q --bare --object-format=sha1 \
    --initial-branch=main "$unborn_served"
/usr/bin/git --git-dir="$unborn_served" update-server-info

port_file=$control_root/https-port
request_log=$control_root/https-requests.log
/usr/bin/python3 "$repo_root/tests/git_remote_revision_https_server.py" \
    "$server_root" "$server_certificate" "$server_key" \
    "$port_file" "$request_log" 127.0.0.1 &
server_pid=$!

attempt=0
while [ ! -s "$port_file" ]; do
    if ! kill -0 "$server_pid" 2>/dev/null; then
        fail "loopback HTTPS server exited before readiness handshake"
    fi
    attempt=$((attempt + 1))
    [ "$attempt" -le 200 ] || fail "loopback HTTPS server readiness timed out"
    sleep 0.02
done
port=$(tr -d '[:space:]' < "$port_file")
case "$port" in
    ''|*[!0-9]*) fail "loopback HTTPS server published an invalid port" ;;
esac

base_url=https://127.0.0.1:$port
sha1_url=$base_url/sha1.git
sha256_url=$base_url/sha256.git
direct_url=$base_url/direct.git
unborn_url=$base_url/unborn.git
redirect_url=$base_url/redirect.git
auth_url=$base_url/auth.git

/usr/bin/git init -q --initial-branch=main "$cwd_repository"
printf '%s\n' cwd-tracked > "$cwd_repository/tracked.txt"
/usr/bin/git -C "$cwd_repository" add tracked.txt
GIT_AUTHOR_DATE='2026-01-04T00:00:00+0000' \
GIT_COMMITTER_DATE='2026-01-04T00:00:00+0000' \
    /usr/bin/git -C "$cwd_repository" \
    -c user.name='Moguet Observer Fixture' \
    -c user.email='observer-fixture@example.invalid' \
    commit -q -m initial
printf '%s\n' cwd-untracked > "$cwd_repository/untracked.txt"

credential_marker=$marker_root/credential-helper-executed
askpass_marker=$marker_root/askpass-executed
ssh_askpass_marker=$marker_root/ssh-askpass-executed
remote_helper_marker=$marker_root/remote-helper-executed
path_git_marker=$marker_root/path-git-executed
cookie_target=$marker_root/cookies.txt
printf '%s\n' fixture-cookie-baseline > "$cookie_target"

credential_helper=$outside_root/credential-helper
askpass_helper=$outside_root/askpass
ssh_askpass_helper=$outside_root/ssh-askpass
git_exec_path=$outside_root/git-exec
path_bin=$outside_root/path-bin
mkdir -p "$git_exec_path"
mkdir -p "$path_bin"
printf '%s\n' '#!/bin/sh' \
    "/usr/bin/touch '$credential_marker'" 'exit 0' > "$credential_helper"
printf '%s\n' '#!/bin/sh' \
    "/usr/bin/touch '$askpass_marker'" 'exit 0' > "$askpass_helper"
printf '%s\n' '#!/bin/sh' \
    "/usr/bin/touch '$ssh_askpass_marker'" 'exit 0' > "$ssh_askpass_helper"
printf '%s\n' '#!/bin/sh' \
    "/usr/bin/touch '$remote_helper_marker'" 'exit 125' \
    > "$git_exec_path/git-remote-https"
printf '%s\n' '#!/bin/sh' \
    "/usr/bin/touch '$path_git_marker'" 'exit 125' \
    > "$path_bin/git"
chmod 0700 \
    "$credential_helper" "$askpass_helper" "$ssh_askpass_helper" \
    "$git_exec_path/git-remote-https" "$path_bin/git"

configure_malicious_git_config() {
    config_path=$1
    /usr/bin/git config --file "$config_path" \
        "url.file://$escape_served.insteadOf" "$sha1_url"
    /usr/bin/git config --file "$config_path" \
        credential.helper "!$credential_helper"
    /usr/bin/git config --file "$config_path" \
        core.askPass "$askpass_helper"
    /usr/bin/git config --file "$config_path" \
        http.extraHeader 'Authorization: observer-fixture-secret'
    /usr/bin/git config --file "$config_path" \
        http.cookieFile "$cookie_target"
    /usr/bin/git config --file "$config_path" http.saveCookies true
}

configure_malicious_git_config "$home_root/.gitconfig"
configure_malicious_git_config "$xdg_config_root/git/config"
configure_malicious_git_config "$cwd_repository/.git/config"
for malicious_config in \
    "$home_root/.gitconfig" "$xdg_config_root/git/config" \
    "$cwd_repository/.git/config"; do
    /usr/bin/git config --file "$malicious_config" \
        'url.http://127.0.0.1:9/downgrade.insteadOf' "$direct_url"
done

HOME=$home_root
XDG_CONFIG_HOME=$xdg_config_root
XDG_STATE_HOME=$xdg_state_root
XDG_CACHE_HOME=$xdg_cache_root
XDG_DATA_HOME=$xdg_data_root
GIT_DIR=$cwd_repository/.git
GIT_WORK_TREE=$cwd_repository
GIT_CONFIG=$home_root/.gitconfig
GIT_CONFIG_PARAMETERS="'http.extraHeader=Authorization: observer-parameter-secret'"
GIT_CONFIG_COUNT=4
GIT_CONFIG_KEY_0="url.file://$escape_served.insteadOf"
GIT_CONFIG_VALUE_0=$sha1_url
GIT_CONFIG_KEY_1=http.extraHeader
GIT_CONFIG_VALUE_1='Authorization: observer-count-secret'
GIT_CONFIG_KEY_2=http.cookieFile
GIT_CONFIG_VALUE_2=$cookie_target
GIT_CONFIG_KEY_3=credential.helper
GIT_CONFIG_VALUE_3="!$credential_helper"
GIT_ASKPASS=$askpass_helper
SSH_ASKPASS=$ssh_askpass_helper
GIT_TERMINAL_PROMPT=1
GIT_EXEC_PATH=$git_exec_path
GIT_TRACE=$trace_root/git-trace
GIT_TRACE_CURL=$trace_root/git-trace-curl
GIT_SSL_NO_VERIFY=true
PATH=$path_bin:/usr/bin:/bin
export \
    HOME XDG_CONFIG_HOME XDG_STATE_HOME XDG_CACHE_HOME XDG_DATA_HOME \
    GIT_DIR GIT_WORK_TREE GIT_CONFIG GIT_CONFIG_PARAMETERS \
    GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0 \
    GIT_CONFIG_KEY_1 GIT_CONFIG_VALUE_1 GIT_CONFIG_KEY_2 \
    GIT_CONFIG_VALUE_2 GIT_CONFIG_KEY_3 GIT_CONFIG_VALUE_3 \
    GIT_ASKPASS SSH_ASKPASS GIT_TERMINAL_PROMPT GIT_EXEC_PATH \
    GIT_TRACE GIT_TRACE_CURL GIT_SSL_NO_VERIFY PATH

unset \
    http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY \
    SSL_CERT_FILE SSL_CERT_DIR GIT_SSL_CAPATH CURL_CA_BUNDLE
no_proxy=127.0.0.1,localhost
NO_PROXY=127.0.0.1,localhost
GIT_SSL_CAINFO=$ca_certificate
export no_proxy NO_PROXY GIT_SSL_CAINFO

clean_git() {
    /usr/bin/env \
        -u GIT_DIR -u GIT_WORK_TREE -u GIT_CONFIG \
        -u GIT_CONFIG_PARAMETERS -u GIT_CONFIG_COUNT \
        -u GIT_CONFIG_KEY_0 -u GIT_CONFIG_VALUE_0 \
        -u GIT_CONFIG_KEY_1 -u GIT_CONFIG_VALUE_1 \
        -u GIT_CONFIG_KEY_2 -u GIT_CONFIG_VALUE_2 \
        -u GIT_CONFIG_KEY_3 -u GIT_CONFIG_VALUE_3 \
        -u GIT_ASKPASS -u SSH_ASKPASS -u GIT_TERMINAL_PROMPT \
        -u GIT_EXEC_PATH -u GIT_TRACE -u GIT_TRACE_CURL \
        -u GIT_SSL_NO_VERIFY \
        HOME="$clean_home" XDG_CONFIG_HOME="$clean_xdg" \
        GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null \
        GIT_OPTIONAL_LOCKS=0 /usr/bin/git "$@"
}

snapshot_repository_state() {
    destination=$1
    {
        printf '%s\n' 'status:'
        clean_git -C "$cwd_repository" \
            status --porcelain=v2 --untracked-files=all
        printf '%s\n' 'head:'
        clean_git -C "$cwd_repository" rev-parse HEAD
        printf '%s\n' 'refs:'
        clean_git -C "$cwd_repository" for-each-ref \
            '--format=%(refname) %(objectname)'
        printf '%s\n' 'index:'
        clean_git -C "$cwd_repository" ls-files --stage
        printf '%s\n' 'objects:'
        find "$cwd_repository/.git/objects" -type f \
            -printf '%P %s\n' | LC_ALL=C sort
        printf '%s\n' 'refs-files:'
        find "$cwd_repository/.git/refs" -type f \
            -printf '%P %s\n' | LC_ALL=C sort
        printf '%s\n' 'logs-locks-and-transaction-files:'
        find "$cwd_repository/.git" \
            \( -path '*/logs/*' -o -name '*.lock' -o \
               -name FETCH_HEAD -o -name packed-refs \) \
            -printf '%P %s\n' | LC_ALL=C sort
    } > "$destination"
}

snapshot_watched_roots() {
    /usr/bin/python3 "$repo_root/tests/fs_tree_snapshot.py" \
        "$home_root" "$xdg_config_root" "$xdg_state_root" \
        "$xdg_cache_root" "$xdg_data_root" "$cwd_repository" \
        "$outside_root" "$trace_root" "$marker_root"
}

wait_for_sentinel_ready() {
    ready_path=$1
    attempt=0
    while [ ! -s "$ready_path" ]; do
        if ! kill -0 "$sentinel_pid" 2>/dev/null; then
            fail "mutation sentinel exited before readiness handshake"
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 200 ] || fail "mutation sentinel readiness timed out"
        sleep 0.02
    done
}

run_mutation_case() {
    case_name=$1
    expected_result=$2
    shift 2
    case_root=$control_root/cases/$case_name
    mkdir -p "$case_root"
    : > "$request_log"
    snapshot_repository_state "$case_root/repository.before"
    snapshot_watched_roots > "$case_root/filesystem.before"

    ready_path=$case_root/sentinel.ready
    sentinel_stop_path=$case_root/sentinel.stop
    events_path=$case_root/sentinel.events
    /usr/bin/python3 "$repo_root/tests/fs_mutation_sentinel.py" \
        "$ready_path" "$sentinel_stop_path" "$events_path" \
        "$home_root" "$xdg_config_root" "$xdg_state_root" \
        "$xdg_cache_root" "$xdg_data_root" "$cwd_repository" \
        "$outside_root" "$trace_root" "$marker_root" &
    sentinel_pid=$!
    wait_for_sentinel_ready "$ready_path"

    if ! (cd "$cwd_repository" && "$@") \
        > "$case_root/observer.stdout" \
        2> "$case_root/observer.stderr"; then
        fail "$case_name observer driver failed"
    fi

    # Keep the inotify watcher active after return. Timeout/overflow fixtures
    # must not leave a same-group descendant that writes later.
    sleep 0.25
    : > "$sentinel_stop_path"
    wait "$sentinel_pid"
    sentinel_pid=
    sentinel_stop_path=

    snapshot_repository_state "$case_root/repository.after"
    snapshot_watched_roots > "$case_root/filesystem.after"
    cp "$request_log" "$case_root/requests.log"

    cmp -s "$case_root/filesystem.before" "$case_root/filesystem.after" ||
        fail "$case_name changed a watched filesystem identity"
    cmp -s "$case_root/repository.before" "$case_root/repository.after" ||
        fail "$case_name changed cwd repository status/HEAD/refs/index/objects"
    [ ! -s "$events_path" ] ||
        fail "$case_name produced an inotify mutation event"
    [ ! -s "$case_root/observer.stderr" ] ||
        fail "$case_name exposed an unexpected observer diagnostic"

    actual_result=$(cat "$case_root/observer.stdout")
    [ "$actual_result" = "$expected_result" ] ||
        fail "$case_name returned an unexpected typed result"
    if grep -F 'authorization=1' "$case_root/requests.log" \
        >/dev/null 2>&1; then
        fail "$case_name sent an ambient Authorization header"
    fi
    if [ -s "$case_root/requests.log" ]; then
        first_request=$(sed -n '1p' "$case_root/requests.log")
        case "$first_request" in
            *'cookie=0') ;;
            *) fail "$case_name read and sent an ambient cookie" ;;
        esac
    fi
    last_request_log=$case_root/requests.log
}

expected_sha1=$(printf 'Observed\t%s' "$sha1_initial")
expected_sha256=$(printf 'Observed\t%s' "$sha256_initial")
expected_git_exit=$(printf 'GitExitFailure\t128')

run_mutation_case \
    config-rewrite-observed "$expected_sha1" \
    "$test_binary" --observe-default "$sha1_url"
grep -F 'path=/sha1.git/' "$last_request_log" >/dev/null ||
    fail "canonical HTTPS endpoint was not contacted"

run_mutation_case \
    direct-head-observed "$expected_sha1" \
    "$test_binary" --observe-default "$direct_url"
grep -F 'path=/direct.git/' "$last_request_log" >/dev/null ||
    fail "HTTPS-to-HTTP rewrite changed the canonical endpoint"
run_mutation_case \
    sha256-default-observed "$expected_sha256" \
    "$test_binary" --observe-default "$sha256_url"
run_mutation_case \
    sha256-exact-observed "$expected_sha256" \
    "$test_binary" --observe-branch "$sha256_url" exact
run_mutation_case \
    exact-branch-before-advance "$expected_sha1" \
    "$test_binary" --observe-branch "$sha1_url" exact
run_mutation_case \
    missing-exact-branch RefNotFound \
    "$test_binary" --observe-branch "$sha1_url" missing
run_mutation_case \
    unborn-head RefNotFound \
    "$test_binary" --observe-default "$unborn_url"

printf '%s\n' sha1-advanced >> "$sha1_work/tracked.txt"
clean_git -C "$sha1_work" add tracked.txt
GIT_AUTHOR_DATE='2026-01-05T00:00:00+0000' \
GIT_COMMITTER_DATE='2026-01-05T00:00:00+0000' \
    clean_git -C "$sha1_work" \
    -c user.name='Moguet Observer Fixture' \
    -c user.email='observer-fixture@example.invalid' \
    commit -q -m advance
clean_git -C "$sha1_work" push -q \
    "$sha1_served" HEAD:refs/heads/exact
clean_git --git-dir="$sha1_served" update-server-info
sha1_advanced=$(clean_git -C "$sha1_work" rev-parse HEAD)
[ "$sha1_advanced" != "$sha1_initial" ] ||
    fail "branch advance did not change the OID"
expected_sha1_advanced=$(printf 'Observed\t%s' "$sha1_advanced")
run_mutation_case \
    exact-branch-after-advance "$expected_sha1_advanced" \
    "$test_binary" --observe-branch "$sha1_url" exact

run_mutation_case \
    credential-401 "$expected_git_exit" \
    "$test_binary" --observe-default "$auth_url"
grep -F 'path=/auth.git/' "$last_request_log" >/dev/null ||
    fail "401 credential fixture was not contacted"

run_mutation_case \
    redirect-denied "$expected_git_exit" \
    "$test_binary" --observe-default "$redirect_url"
grep -F 'path=/redirect.git/' "$last_request_log" >/dev/null ||
    fail "redirect fixture was not contacted"
if grep -F 'path=/sha1.git/' "$last_request_log" >/dev/null; then
    fail "observer followed the redirect target"
fi

run_mutation_case \
    malformed-output MalformedOutput \
    "$test_binary" --observe-process-fixture bad-oid
run_mutation_case \
    ambiguous-output AmbiguousOutput \
    "$test_binary" --observe-process-fixture duplicate
run_mutation_case \
    capture-overflow CaptureLimitExceeded \
    "$test_binary" --observe-process-fixture capture-overflow
run_mutation_case \
    timeout Timeout \
    "$test_binary" --observe-process-fixture timeout

[ ! -e "$credential_marker" ] || fail "credential helper executed"
[ ! -e "$askpass_marker" ] || fail "GIT_ASKPASS executed"
[ ! -e "$ssh_askpass_marker" ] || fail "SSH_ASKPASS executed"
[ ! -e "$remote_helper_marker" ] || fail "ambient GIT_EXEC_PATH helper executed"
[ ! -e "$path_git_marker" ] || fail "observer used PATH to find Git"
[ ! -e "$trace_root/git-trace" ] || fail "ambient Git trace target was written"
[ ! -e "$trace_root/git-trace-curl" ] ||
    fail "ambient Git curl trace target was written"
[ "$(cat "$cookie_target")" = fixture-cookie-baseline ] ||
    fail "ambient cookie file was read or rewritten"

kill "$server_pid"
wait "$server_pid" 2>/dev/null || true
server_pid=

echo "Git remote revision observer HTTPS/mutation integration: all checks passed"
