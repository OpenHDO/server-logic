# OpenHDO Logic

`server-logic` is the server-side module for data-driven automation. It owns
flows, nodes, ports, connections, triggers, conditions, transformations,
actions, schedules, and execution results.

## Boundary

Logic consumes events and emits validated commands through the server core. It
does not access device drivers directly, duplicate the registry, or become a
separate source of truth for dashboard state.

## Status

Repository scaffold. The first vertical slice should validate and execute one
flow in memory, with explicit errors and an audit-friendly execution result.

See the [project architecture](https://github.com/OpenHDO/about/blob/main/ARCHITECTURE.md)
and [server contracts](https://github.com/OpenHDO/server/tree/master/contracts/v1).
