"""Bounded JSON transport for the private companion. No save access or retries."""
import base64
import json
import re
import socket
import ssl
import time
import urllib.error


def encode(data, limit=8 * 1024 * 1024):
    raw = json.dumps(data, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
    if len(raw) > limit:
        raise ValueError('Marketplace request exceeds the size limit')
    # Some shared-host JSON inspectors stall on many nested inventory fields.
    # A versioned, flat envelope preserves exact data; the authenticated server
    # decodes it once and applies the original size/depth/semantic validation.
    return json.dumps({'market_transport': 'json-base64-v1',
                      'payload': base64.b64encode(raw).decode('ascii')}, separators=(',', ':')).encode('ascii')


def read_json(opener, request, operation, limit=16 * 1024 * 1024, timeout=30):
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
