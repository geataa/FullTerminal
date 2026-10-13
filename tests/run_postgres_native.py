"""Opt-in live test. Credentials stay in the child environment, never argv/logs.

Only a session-local temporary table is modified. No server is installed.
"""
import argparse
import os
import pathlib
import socket
import struct
import subprocess
import shlex

parser = argparse.ArgumentParser()
source = parser.add_mutually_exclusive_group(required=True)
source.add_argument('--env-file', type=pathlib.Path)
source.add_argument('--ssh-env', help='Read just POSTGRES_PASSWORD using an existing verified SSH login')
parser.add_argument('--ssh-env-path', help='Explicit path relative to the SSH account home')
parser.add_argument('--host', required=True)
parser.add_argument('--port', type=int, default=5432)
parser.add_argument('--database', required=True)
parser.add_argument('--user', required=True)
parser.add_argument('--allow-plaintext', action='store_true')
parser.add_argument('--exe', type=pathlib.Path, default=pathlib.Path('build/tests/test-runs/database_native/test_database_native.exe'))
args = parser.parse_args()
values = {}
if args.ssh_env:
    if not args.ssh_env_path:
        parser.error('--ssh-env requires an explicit --ssh-env-path')
    # No host-key bypass, password prompts, file writes or deployment commands.
    code = ('import pathlib; p=pathlib.Path.home()/' + repr(args.ssh_env_path) +
            '; print(next(x for x in p.read_text().splitlines() if x.startswith("POSTGRES_PASSWORD=")))')
    fetched = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
                              '-o', 'ConnectTimeout=5', args.ssh_env, 'python3 -c ' + shlex.quote(code)],
                             capture_output=True, timeout=15)
    if fetched.returncode:
        raise SystemExit('Could not read PostgreSQL configuration through the existing SSH login. No credentials were logged.')
    config = fetched.stdout.decode('utf-8-sig')
else:
    config = args.env_file.read_text(encoding='utf-8-sig')
for line in config.splitlines():
    line = line.strip()
    if line and not line.startswith('#') and '=' in line:
        key, value = line.split('=', 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key.strip()] = value
secret = values.get('POSTGRES_PASSWORD')
if not secret:
    raise SystemExit('No configured POSTGRES_PASSWORD found. Values were not logged.')
with socket.create_connection((args.host, args.port), timeout=5) as probe:
    probe.sendall(struct.pack('!II', 8, 80877103))
    tls = probe.recv(1)
if tls not in (b'S', b'N'):
    raise SystemExit('Endpoint did not respond as PostgreSQL.')
if tls == b'N' and not args.allow_plaintext:
    raise SystemExit('The server does not offer TLS; plaintext test requires --allow-plaintext.')
env = os.environ.copy()
env.update(PGHOST=args.host, PGPORT=str(args.port), PGDATABASE=args.database,
           PGUSER=args.user, PGPASSWORD=secret, PGSSLMODE='require' if tls == b'S' else 'disable')
print('PostgreSQL endpoint reachable. TLS available:', tls == b'S', flush=True)
result = subprocess.run([str(args.exe.resolve()), '--postgres-live'], env=env,
                        capture_output=True, timeout=60)
output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
print(output.replace(secret, '<redacted>'), end='')
raise SystemExit(result.returncode)
