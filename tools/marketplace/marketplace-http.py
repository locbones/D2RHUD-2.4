"""Bounded JSON transport for the private companion. No save access or retries."""
import base64
import json
import re
import socket
import ssl
import time
import urllib.error
import os
import random
import tempfile
from pathlib import Path
from contextlib import contextmanager
from email.utils import parsedate_to_datetime


@contextmanager
def network_gate(directory):
    """One request per local profile; all workers share overload cooldowns.

    No save or credential data is stored here. A process exit releases the OS
    lock. Brief local contention never becomes an overlapping HTTP request.
    """
    if directory is None:
        yield None
        return
    root = Path(directory)
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'network.lock').open('a+b') as lock:
        deadline = time.monotonic() + 2
        while True:
            try:
                lock.seek(0)
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise ConnectionError('Marketplace connection is busy; queued work will retry automatically.') from None
                time.sleep(.025)
        try:
            yield root / 'network-state.json'
        finally:
            lock.seek(0)
            if os.name == 'nt':
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)


def network_state(path):
    if path is None:
        return {}
    try:
        value = json.loads(path.read_text(encoding='utf-8'))
        if (not isinstance(value, dict) or type(value.get('failures', 0)) is not int or
                not 0 <= value.get('failures', 0) <= 8 or type(value.get('retry_at', 0)) not in (int, float) or
                not 0 <= value.get('retry_at', 0) <= time.time() + 360):
            return {}
        return value
    except (OSError, ValueError):
        return {}


def in_cooldown(directory):
    return network_state(Path(directory) / 'network-state.json').get('retry_at', 0) > time.time()


def save_network_state(path, value):
    if path is None:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(json.dumps(value).encode('ascii'))
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def cooldown(path, previous, error):
    failures = min(8, max(0, int(previous.get('failures', 0))) + 1)
    delay = min(120, 5 * 2 ** (failures - 1))
    header = getattr(error, 'headers', None)
    retry = header.get('Retry-After') if header else None
    if retry:
        try:
            seconds = float(retry) if str(retry).isdigit() else parsedate_to_datetime(retry).timestamp() - time.time()
            delay = max(delay, min(300, max(0, seconds)))
        except (ValueError, TypeError, OverflowError):
            pass
    delay = min(300, delay * random.uniform(1, 1.25))
    save_network_state(path, dict(failures=failures, retry_at=time.time() + delay))


def encode(data, limit=8 * 1024 * 1024):
    raw = json.dumps(data, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
    if len(raw) > limit:
        raise ValueError('Marketplace request exceeds the size limit')
    # Some shared-host JSON inspectors stall on many nested inventory fields.
    # A versioned, flat envelope preserves exact data; the authenticated server
    # decodes it once and applies the original size/depth/semantic validation.
    return json.dumps({'market_transport': 'json-base64-v1',
                      'payload': base64.b64encode(raw).decode('ascii')}, separators=(',', ':')).encode('ascii')


def read_json(opener, request, operation, limit=16 * 1024 * 1024, timeout=30, state_directory=None):
    with network_gate(state_directory) as path:
        previous = network_state(path)
        remaining = min(300, max(0, float(previous.get('retry_at', 0)) - time.time()))
        if remaining:
            raise ConnectionError(f'Marketplace is cooling down after a connection failure; retrying automatically in {int(remaining)+1}s.')
        try:
            result = _read_json(opener, request, operation, limit, timeout)
        except urllib.error.HTTPError as error:
            content_type = (error.headers.get('Content-Type', '') if error.headers else '').lower()
            if error.code == 403 and not content_type.startswith('application/json'):
                cooldown(path, previous, error)
                raise ConnectionError('Marketplace hosting temporarily refused the connection; retrying automatically.') from None
            if error.code in {408, 429, 500, 502, 503, 504}:
                cooldown(path, previous, error)
            raise
        except (ConnectionError, ValueError) as error:
            cooldown(path, previous, error)
            raise
        if previous:
            save_network_state(path, {})
        return result


def _read_json(opener, request, operation, limit, timeout):
    began = time.monotonic()
    try:
        with opener.open(request, timeout=timeout) as response:
            raw = response.read(limit + 1)
    except urllib.error.HTTPError:
        raise  # Preserve validation/authentication/conflict responses.
    except (OSError, urllib.error.URLError) as error:
        reason = error.reason if isinstance(error, urllib.error.URLError) else error
        label = operation if re.fullmatch(r'[A-Za-z_ /()-]{1,80}', operation) else 'request'
        if isinstance(reason, (TimeoutError, socket.timeout)):
            detail = 'timed out'
        elif isinstance(reason, socket.gaierror):
            detail = 'could not resolve the server address'
        elif isinstance(reason, ssl.SSLCertVerificationError):
            detail = 'could not verify the HTTPS certificate'
        else:
            detail = 'could not complete the connection'
        # Never include headers, URLs, payloads or exception text containing
        # credentials. The existing watch loop retries durable operations.
        raise ConnectionError(f'Marketplace {label} {detail} after {time.monotonic()-began:.1f}s; retrying automatically.') from None
    if len(raw) > limit:
        raise ValueError('Marketplace response exceeds the size limit')
    return json.loads(raw)
