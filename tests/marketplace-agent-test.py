"""Actual PHP HTTP + two isolated save profiles + real crash/recovery boundaries."""
import importlib.util
import http.cookiejar
import json
import os
from pathlib import Path
import socket
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

repo = Path(__file__).resolve().parents[1]
source = repo / "tools/marketplace/custody-agent.py"
spec = importlib.util.spec_from_file_location("save_agent", source)
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def empty(raw):
    result, at = bytearray(), 0
    for _ in range(7):
        header = bytearray(raw[at:at+68])
        at += struct.unpack_from("<I", raw, at+16)[0]
        struct.pack_into("<I", header, 12, 0)
        struct.pack_into("<I", header, 16, 68)
        struct.pack_into("<H", header, 66, 0)
        result.extend(header)
    return bytes(result)


def transport(endpoint, token, mode, data, action="custody"):
    req = urllib.request.Request(endpoint + f"?action={action}&hardcore={int(mode == 'HC')}",
        None if data is None else json.dumps(data).encode(), {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            raw = response.read()
            try:
                return json.loads(raw)
            except json.JSONDecodeError:
                raise AssertionError('Non-JSON API response: ' + repr(raw[:600])) from None
    except urllib.error.HTTPError as error:
        body = json.load(error)
        message = f"Marketplace {error.code}: {body['error']}"
        if action == 'custody' and 'deposit_item_indices' in body:
            raise a.DepositItemsRefused(message, body['deposit_item_indices']) from None
        raise ValueError(message) from None


def main():
    import runpy
    source = Path(sys.argv[4])
    if source.is_dir():
        sys.argv[4] = str(source / 'Stash_SC_Page11.d2i')
    runpy.run_path(str(repo / 'tests/marketplace-single-page-test.py'))['main']()


if __name__ == '__main__':
    main()
