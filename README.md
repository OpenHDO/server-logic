# OpenHDO Logic

## HDO plugin v1

This repository now ships an optional HDO plugin, not a built-in module. Its
`hdo.json`, `plugin.py` and `web/plugin.js` are ready to package with the separate
[plugin CLI](https://github.com/OpenHDO/plugin-cli):

```sh
hdop validate .
hdop pack . --out server-logic.hdop
```

Install the archive in HDO's Plugins tab and enable it. The host provides the
Python SDK, authentication, device/room services, automatic lifecycle cleanup
and private persistent storage in `/data/plugin-data/<plugin-id>`.


`server-logic` contains the optional Logic plugin and a reusable Python flow engine. It validates a small
typed DAG, evaluates an incoming event, and emits validated command intent for
the server application. The server remains responsible for authorization,
delivery, retries, persistence, and transport.

## Boundary

The module is stdlib-only and has no device-driver, network, database, or UI
dependency. It is not a standalone gateway or runtime. Vendor details stay in
provider plugins; canonical state stays in the server.

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

## Plugin automations

The plugin's Logic view edits a persisted rules list. Each rule references an
async `flowNodes` action supplied by an enabled plugin. Example:

```json
[{"id":"socket-on","event":"device.changed","match":{"id":"source-device-id"},
  "cooldown":5,"action":{"type":"openhdo.logic:device-action",
  "config":{"device_id":"target-device-id","code":"switch","value":true}}}]
```

GET/PUT `rules` reads or replaces rules; POST `run` with `{"id":"socket-on"}`
executes one rule explicitly. Mutation is admin-only. At least one second of
cooldown prevents a rule immediately triggering itself through command
readback. Custom action plugins register `ctx.extension("flowNodes", id,
async_handler)` where `async_handler(config, event)` performs the action. The
original deterministic DAG engine remains available as a standalone library.
