# OpenHDO Logic

`server-logic` is the Python server-side flow module. It validates a small
typed DAG, evaluates an incoming event, and emits validated command intent for
the server application. The server remains responsible for authorization,
delivery, retries, persistence, and transport.

## Boundary

The module is stdlib-only and has no device-driver, network, database, or UI
dependency. It is not a standalone gateway or runtime. Vendor details stay in
the Python Linker; canonical state stays in the server.

The current node kinds are deliberately small:

```text
event.trigger ── matched ──┐
                           ├── command.emit
value.constant ── value ───┘
```

## Install and check

```powershell
python -m pip install -e .
python -m unittest discover -s tests -v
python -m compileall -q openhdo_logic
python -m pip wheel . --no-deps --no-build-isolation -w build/package
```

Commands preserve the incoming event correlation and are deterministic for the
same request. Flow validation fails closed on unsupported versions, malformed
messages, invalid ports, multiple input connections, missing required inputs,
and cyclic graphs.

See [`docs/ADR-0001-minimal-flow-engine.md`](docs/ADR-0001-minimal-flow-engine.md),
the [project architecture](https://github.com/OpenHDO/about/blob/main/ARCHITECTURE.md),
and [server contracts](https://github.com/OpenHDO/server/tree/master/contracts/v1).
