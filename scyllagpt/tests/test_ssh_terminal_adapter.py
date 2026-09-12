"""Offline tests for the embedded trusted adapter; never starts SSH or WSL."""
from pathlib import Path
from types import SimpleNamespace
from contextlib import contextmanager
import unittest

# Tiny synchronous fakes keep these tests independent of Windows asyncio/Winsock.
class Mock:
    def __init__(self, **fields):
        self.__dict__.update(fields)
        self.call_args_list = []
        self.return_value = None
    def __getattr__(self, name):
        value = Mock()
        setattr(self, name, value)
        return value
    def __call__(self, *args, **kwargs):
        self.call_args_list.append(SimpleNamespace(args=args, kwargs=kwargs))
        return self.return_value
    @property
    def called(self): return bool(self.call_args_list)
    @property
    def call_count(self): return len(self.call_args_list)
    @property
    def call_args(self): return self.call_args_list[-1]
    def assert_not_called(self): assert not self.called

class patch:
    @staticmethod
    @contextmanager
    def object(target, name, replacement=None, create=False, side_effect=None, return_value=None):
        missing = object()
        old = getattr(target, name, missing)
        recorder = Mock()
        recorder.return_value = return_value
        effects = iter(side_effect) if side_effect is not None and not callable(side_effect) else None
        def call(*args, **kwargs):
            recorder(*args, **kwargs)
            if effects is not None: return next(effects)
            if side_effect is not None: return side_effect(*args, **kwargs)
            return return_value
        setattr(target, name, replacement if replacement is not None else call)
        try: yield recorder
        finally:
            if old is missing: delattr(target, name)
            else: setattr(target, name, old)

source = (Path(__file__).parents[1] / 'include/scyllagpt/ssh_terminal_script.h').read_text()
script = source.split('R"SCYLLA(', 1)[1].split(')SCYLLA"', 1)[0]
adapter = {'__name__': 'adapter_test'}
exec(compile(script, 'ssh_terminal_adapter', 'exec'), adapter)


class AdapterTests(unittest.TestCase):
    def run_adapter(self, output=b'user-test host.test pass-test key-line-123\n', limit=4096, timeout=30):
        p = dict(host='host.test', user='user-test', port=22, hostKey='ssh-ed25519 public-key',
                 key='key-line-123', passphrase='pass-test', command='uname -a', maxBytes=limit, timeout=timeout)
        process = Mock(pid=123, stdin=Mock(), stdout=Mock(), stderr=Mock())
        process.stdin.fileno.return_value = 8
        process.stdout.fileno.return_value = 9
        process.stderr.fileno.return_value = 10
        process.wait.return_value = 7
        process.poll.return_value = 7
        blocks = {9: [output, b''], 10: [b'']}
        class Events:
            def __init__(self): self.entries = {}
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def register(self, stream, flags, label): self.entries[stream] = SimpleNamespace(fileobj=stream, data=label)
            def unregister(self, stream): del self.entries[stream]
            def get_map(self): return self.entries
            def select(self, wait): return [(event, 0) for event in list(self.entries.values())]
        os = adapter['os']
        with patch.object(os, 'memfd_create', create=True, side_effect=[20, 21, 22, 23]), \
             patch.object(os, 'MFD_CLOEXEC', 1, create=True), \
             patch.object(os, 'fchmod', create=True), patch.object(os, 'set_blocking'), \
             patch.object(os, 'write', side_effect=lambda fd, data: len(data)) as writes, \
             patch.object(os, 'read', side_effect=lambda fd, count: blocks[fd].pop(0)), \
             patch.object(os, 'close') as closes, patch.object(os, 'killpg', create=True) as kill, \
             patch.object(adapter['signal'], 'SIGKILL', 9, create=True), \
             patch.object(adapter['selectors'], 'DefaultSelector', Events), \
             patch.object(adapter['subprocess'], 'Popen', return_value=process) as launch:
            result = adapter['execute'](p)
            argv = launch.call_args.args[0]
            self.assertFalse(any(secret in ' '.join(argv) for secret in ['pass-test', 'key-line-123', 'user-test', 'host.test']))
            self.assertEqual(launch.call_args.kwargs['env']['SCYLLA_KEY_PASSPHRASE'], 'pass-test')
            staged = b''.join(call.args[1] for call in writes.call_args_list if call.args[0] != 8)
            self.assertIn(b'StrictHostKeyChecking yes', staged)
            self.assertEqual(closes.call_count, 4)
            return result, kill.called

    def test_output_and_exit_status(self):
        result, killed = self.run_adapter()
        self.assertEqual(result['exitCode'], 7)
        self.assertNotIn('pass-test', result['stdout'])
        self.assertNotIn('key-line-123', result['stdout'])
        self.assertNotIn('user-test', result['stdout'])
        self.assertFalse(killed)

    def test_cap_suppresses_partial_secrets(self):
        result, killed = self.run_adapter(limit=2)
        self.assertTrue(result['truncated'])
        self.assertEqual(result['stdout'], '')
        self.assertTrue(killed)

    def test_timeout_terminates_child(self):
        result, killed = self.run_adapter(timeout=0)
        self.assertTrue(result['timedOut'])
        self.assertTrue(killed)

    def test_host_injection_rejected_before_launch(self):
        with patch.object(adapter['subprocess'], 'Popen') as launch:
            with self.assertRaises(ValueError):
                adapter['execute']({'host': 'host\nProxyCommand evil', 'user': 'user'})
            launch.assert_not_called()

if __name__ == '__main__':
    unittest.main()
