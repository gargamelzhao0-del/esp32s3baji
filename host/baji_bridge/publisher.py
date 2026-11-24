"""One input reader folds events in order. Network retries use only the latest state."""
import asyncio
from contextlib import nullcontext
import os
import secrets
import sys
import threading
import time

from .device import signed_snapshot
from .protocol import PREFIX, decode, decode_object


MAX_LINE = 1024 * 1024


def read_events(source, state, stopped, interval):
    while not stopped.is_set():
        line = source.readline(MAX_LINE + 1)
        if not line or stopped.is_set():
            break
        if len(line) > MAX_LINE:
            while line and not line.endswith(b"\n") and not stopped.is_set():
                line = source.readline(MAX_LINE + 1)
            continue
        try:
            msg = decode(line) if line.startswith(PREFIX) else decode_object(line.decode("utf-8-sig"))
            accepted = state.accept(msg)
        except (ValueError, UnicodeError, RecursionError):
            accepted = False
        if interval and accepted:
            stopped.wait(interval)


async def publish(state, transport, key, eof, *, heartbeat=2., retry_limit=18.):
    stream = secrets.token_bytes(8)
    sequence, sent_revision = 0, 0
    closed_at = None
    failures = 0
    while True:
        # EOF is published after the last accepted revision. Read it first so
        # an initially empty/expired sample cannot race a new final revision.
        finished = eof.is_set()
        if not state.revision or state.upstream_expired():
            if not finished:
                await asyncio.sleep(0.1)
                continue
            if not state.revision:
                raise ValueError("input contained no supported Agent state")
            if sent_revision != state.revision:
                raise ValueError("last upstream snapshot expired before delivery")
            return sent_revision
        try:
            async with transport() as link:
                nonce = await link.hello()
                last_send = None
                while True:
                    now = time.monotonic()
                    if state.upstream_expired():
                        # Release the BLE connection as well as the expired upstream lease.
                        break
                    # BAJI snapshots already carry a producer heartbeat. Repeating
                    # one here would extend a stalled upstream's 8-second lease.
                    heartbeat_due = state.source != "snapshot" and last_send is not None and now - last_send >= heartbeat
                    if last_send is None or state.revision != sent_revision or heartbeat_due:
                        sample = state.sample()
                        if sample is None:
                            break
                        sequence += 1
                        await link.send(signed_snapshot(key,nonce,stream,sequence,sample))
                        last_send, sent_revision, failures = time.monotonic(), sample[0], 0
                    if eof.is_set():
                        # EOF can arrive during a radio write. The reader has
                        # now finished, so verify the acknowledged revision.
                        if sent_revision == state.revision:
                            return sent_revision
                        if state.sample() is None:
                            raise ValueError("last upstream snapshot expired before delivery")
                    await asyncio.sleep(0.1)
        except (OSError, asyncio.TimeoutError) as error:
            if getattr(error, "pairing_rejected", False):
                raise ValueError("pairing key was rejected; pair this badge over USB") from error
            if eof.is_set():
                # The final state may already be acknowledged when closing a
                # Bluetooth session fails because the peripheral disconnected.
                if sent_revision and sent_revision == state.revision:
                    return sent_revision
                closed_at = time.monotonic() if closed_at is None else closed_at
                if time.monotonic()-closed_at >= retry_limit:
                    raise
            failures += 1
            if failures == 1:
                print("Waiting for the paired badge; retaining the latest state.", file=sys.stderr)
            await asyncio.sleep(min(0.25*2**min(failures-1,4),4))


async def stream_events(source, state, transport, key, interval=0):
    eof = threading.Event()
    stopped = threading.Event()
    input_errors = []
    # An open pipeline can still be waiting for another line when the device
    # rejects our key. Keep that blocking read off Python's global stdin lock,
    # which interpreter shutdown also needs.
    owned_source = (os.fdopen(os.dup(source.fileno()), "rb")
                    if source is getattr(sys.stdin, "buffer", None) else nullcontext(source))

    def consume():
        try:
            with owned_source as stream:
                read_events(stream, state, stopped, interval)
        except Exception as error:
            input_errors.append(error)
        finally:
            eof.set()

    reader = threading.Thread(target=consume, daemon=True)
    reader.start()
    try:
        return await publish(state, transport, key, eof)
    finally:
        stopped.set()
        if eof.is_set() and input_errors:
            raise input_errors[0]
