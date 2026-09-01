# OpenHDO Logic

`server-logic` is the server-side module for data-driven automation. It owns
flows, nodes, ports, connections, triggers, conditions, transformations,
actions, schedules, and execution results.

## Boundary

Logic consumes events and emits validated commands through the server core. It
does not access device drivers directly, duplicate the registry, or become a
separate source of truth for dashboard state.

## Status

The first vertical slice is implemented as a dependency-free C++20 library.
It validates typed DAGs, consumes a versioned server event DTO, and returns a
versioned command DTO plus a deterministic node trace and structured errors.

```text
event.trigger ── matched ──┐
                           ├── command.emit ── command result
value.constant ── value ───┘
```

Build and test:

```text
cmake -S . -B build -DOPENHDO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The engine does not access hardware or deliver commands. The server core owns
authorization, delivery, retries, and transport adaptation. See
[`docs/ADR-0001-minimal-flow-engine.md`](docs/ADR-0001-minimal-flow-engine.md)
for the boundary and intentional limits.

See the [project architecture](https://github.com/OpenHDO/about/blob/main/ARCHITECTURE.md)
and [server contracts](https://github.com/OpenHDO/server/tree/master/contracts/v1).
