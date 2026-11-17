#!/usr/bin/env python3
"""Test automatic and persisted PAPPL port selection."""

from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
APP = os.environ.get("BROTHER_TD_APP", str(ROOT / "build" / "brother-td-app"))


def skip(reason: str) -> None:
    print(f"pappl-port: skipping ({reason})")
    raise SystemExit(0)


if not pathlib.Path(APP).is_file():
    skip("brother-td-app is not built")


def read_port(path: pathlib.Path) -> int:
    try:
        return int(path.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return 0


def wait_port(path: pathlib.Path, previous: int, process: subprocess.Popen,
              timeout: float = 10.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        port = read_port(path)
        if port > 0 and port != previous:
            return port
        if process.poll() is not None:
            break
        time.sleep(0.05)
    raise AssertionError(f"no new port in {path}; app exited {process.returncode}")


def start(tmp: pathlib.Path, name: str) -> tuple[subprocess.Popen, pathlib.Path, pathlib.Path]:
    spool = tmp / "spool"
    spool.mkdir(exist_ok=True)
    portfile = spool / "port"
    log = tmp / f"{name}.log"

    environment = dict(os.environ)
    ldpath = environment.get("PAPPL_LD_LIBRARY_PATH")
    if ldpath:
        environment["LD_LIBRARY_PATH"] = ldpath + (
            ":" + environment["LD_LIBRARY_PATH"] if environment.get("LD_LIBRARY_PATH") else ""
        )

    with log.open("wb") as output:
        process = subprocess.Popen(
            [
                APP,
                "--model", "TD-4210D",
                "--device", f"file:{tmp / 'capture'}",
                "--printer", name,
                "--spool", str(spool),
                "--port", "auto",
                "--no-state",
            ],
            stdout=output,
            stderr=subprocess.STDOUT,
            env=environment,
        )
    return process, spool, portfile


def stop(process: subprocess.Popen) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="td-port-") as tmpname:
        tmp = pathlib.Path(tmpname)

        process, _, portfile = start(tmp, "TDPORTTEST1")
        try:
            first = wait_port(portfile, 0, process)
        finally:
            stop(process)

        process, _, portfile = start(tmp, "TDPORTTEST2")
        try:
            second = wait_port(portfile, 0, process)
            assert second == first, (first, second)
        finally:
            stop(process)

        # Occupy the saved port and verify the app moves to another one.
        holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        holder.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        holder.bind(("0.0.0.0", second))
        holder.listen(1)

        try:
            process, _, portfile = start(tmp, "TDPORTTEST3")
            try:
                third = wait_port(portfile, second, process)
                assert third != second, (second, third)
            finally:
                stop(process)
        finally:
            holder.close()

    print(f"pappl-port: OK ({first}, reused {second}, moved to {third})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
