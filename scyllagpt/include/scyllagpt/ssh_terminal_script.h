#pragma once

namespace scyllagpt {
// Trusted WSL-side adapter. Secrets arrive on stdin and live in anonymous RAM-backed files.
inline constexpr const char* kSshTerminalScript = R"SCYLLA(
import json, os, re, selectors, signal, subprocess, sys, time

def execute(p):
    host, user = p['host'], p['user']
    if not re.fullmatch(r'[A-Za-z0-9_.:-]+', host) or not re.fullmatch(r'[A-Za-z0-9_.-]+', user):
        raise ValueError('Invalid SSH host or username')
    if '\n' in p['hostKey'] or '\r' in p['hostKey']:
        raise ValueError('Invalid pinned host public key')
    descriptors = []
    process = None
    def memory_file(text, mode):
        fd = os.memfd_create('scylla-ssh', os.MFD_CLOEXEC)
        descriptors.append(fd)
        os.fchmod(fd, mode)
        os.write(fd, text.encode('utf-8'))
        return '/proc/%s/fd/%s' % (os.getpid(), fd)
    try:
        key = memory_file(p['key'].replace('\r\n', '\n').rstrip('\n') + '\n', 0o600)
        host_label = host if p['port'] == 22 else '[%s]:%s' % (host, p['port'])
        hosts = memory_file(host_label + ' ' + p['hostKey'] + '\n', 0o600)
        askpass = memory_file('#!/bin/sh\nprintf "%s\\n" "$SCYLLA_KEY_PASSPHRASE"\n', 0o700)
        config = memory_file('Host broker-target\n HostName %s\n User %s\n Port %s\n IdentityFile %s\n UserKnownHostsFile %s\n GlobalKnownHostsFile /dev/null\n StrictHostKeyChecking yes\n IdentitiesOnly yes\n IdentityAgent none\n ClearAllForwardings yes\n PermitLocalCommand no\n ProxyCommand none\n ConnectTimeout 15\n' % (host, user, p['port'], key, hosts), 0o600)
        env = {'PATH': '/usr/bin:/bin', 'SSH_ASKPASS': askpass, 'SSH_ASKPASS_REQUIRE': 'force',
               'DISPLAY': ':0', 'SCYLLA_KEY_PASSPHRASE': p.get('passphrase', '')}
        process = subprocess.Popen(['/usr/bin/ssh', '-F', config, '-T', 'broker-target', 'sh', '-s'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, start_new_session=True)
        pending = (p['command'] + '\n').encode('utf-8')
        chunks = {'stdout': bytearray(), 'stderr': bytearray()}
        limit = p['maxBytes']
        deadline = time.monotonic() + p['timeout']
        timed_out = truncated = False
        with selectors.DefaultSelector() as events:
            for stream, label in [(process.stdout, 'stdout'), (process.stderr, 'stderr')]:
                os.set_blocking(stream.fileno(), False)
                events.register(stream, selectors.EVENT_READ, label)
            os.set_blocking(process.stdin.fileno(), False)
            events.register(process.stdin, selectors.EVENT_WRITE, 'stdin')
            while events.get_map():
                if time.monotonic() >= deadline:
                    timed_out = True
                    break
                for event, _ in events.select(0.1):
                    if event.data == 'stdin':
                        try: pending = pending[os.write(event.fileobj.fileno(), pending):]
                        except BrokenPipeError: pending = b''
                        if not pending:
                            events.unregister(event.fileobj)
                            event.fileobj.close()
                    else:
                        block = os.read(event.fileobj.fileno(), 8192)
                        if not block: events.unregister(event.fileobj)
                        else:
                            chunks[event.data].extend(block)
                            if sum(map(len, chunks.values())) > limit:
                                truncated = True
                                break
                if truncated: break
        if timed_out or truncated:
            os.killpg(process.pid, signal.SIGKILL)
        status = process.wait(timeout=5)
        # A capped stream can end halfway through a secret. Return no raw output in that case.
        if truncated or timed_out:
            return {'exitCode': status, 'stdout': '', 'stderr': '', 'truncated': truncated, 'timedOut': timed_out}
        def scrub(raw):
            text = raw.decode('utf-8', errors='replace')
            values = [p['key'], p.get('passphrase', ''), user, host]
            values += [line for line in p['key'].splitlines() if len(line) >= 8]
            for value in sorted(set(values), key=len, reverse=True):
                if value: text = text.replace(value, '[redacted]')
            return text
        return {'exitCode': status, 'stdout': scrub(chunks['stdout']), 'stderr': scrub(chunks['stderr']),
                'truncated': False, 'timedOut': False}
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        for fd in descriptors: os.close(fd)

if __name__ == '__main__':
    try: print(json.dumps(execute(json.load(sys.stdin))))
    except Exception: print(json.dumps({'error': 'The trusted SSH terminal could not complete the operation.'}))
)SCYLLA";
}
