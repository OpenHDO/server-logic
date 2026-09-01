# ADR-0001: Minimal in-memory flow engine

## Status

Accepted

## Context

Logic needs a first executable slice without taking ownership of hardware,
device discovery, or server state. The server contract already gives modules a
versioned event/command envelope, so the module boundary must remain a small,
validated DTO boundary that the server can adapt to its transport.

## Decision

- Use a C++20 library with value types for flows, nodes, ports, connections,
  events, commands, and execution results.
- Keep the public contract at version 1 and validate versions, message fields,
  identifiers, bounded collections, node shapes, port directions, port types,
  required inputs, and graph acyclicity before execution.
- Start with three built-in node kinds: `event.trigger`, `value.constant`, and
  `command.emit`. The first slice permits one command sink so the host supplies
  one deterministic command id per execution.
- Execute a validated DAG in declaration-order tie breaks using an in-memory
  value map. No node accesses hardware, persistence, clocks, threads, or
  network transports.
- Return commands in the execution result. The server core owns delivery,
  retries, authorization, and any later command envelope adapter.
- Record a node trace and structured errors in every result so the host can
  audit, observe, and test evaluation without parsing log text.

## Consequences

The slice is deterministic and easy to embed in the modular monolith. It does
not yet provide persistence, retries, dynamic node plugins, branching beyond a
boolean command gate, JSON serialization, or multiple command sinks. Those
features should be added only when the server boundary and a concrete use case
require them.
