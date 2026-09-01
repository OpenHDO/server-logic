"""Validated, deterministic flow execution with a small standard-library core."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
import math
import re
from uuid import UUID

CONTRACT_VERSION = 1
LOGIC_SOURCE = "server-logic"
MAX_NODES = 256
MAX_CONNECTIONS = 512
MAX_PORTS = 32
MAX_PAYLOAD_FIELDS = 64
MAX_IDENTIFIER_LENGTH = 128
MAX_TEXT_LENGTH = 4096
MESSAGE_TYPE = re.compile(r"^[a-z][a-z0-9._-]{0,63}$")


class ValueType(str, Enum):
    BOOLEAN = "boolean"
    INTEGER = "integer"
    NUMBER = "number"
    TEXT = "text"


@dataclass(frozen=True, slots=True)
class Value:
    value: bool | int | float | str

    @classmethod
    def boolean(cls, value: bool) -> "Value": return cls(value)
    @classmethod
    def integer(cls, value: int) -> "Value": return cls(value)
    @classmethod
    def number(cls, value: float) -> "Value": return cls(value)
    @classmethod
    def text(cls, value: str) -> "Value": return cls(value)

    @property
    def type(self) -> ValueType:
        if type(self.value) is bool: return ValueType.BOOLEAN
        if type(self.value) is int: return ValueType.INTEGER
        if type(self.value) is float: return ValueType.NUMBER
        if type(self.value) is str: return ValueType.TEXT
        raise TypeError("Value must contain bool, int, float, or str")


Payload = dict[str, Value]


class PortDirection(str, Enum):
    INPUT = "input"
    OUTPUT = "output"


@dataclass(frozen=True, slots=True)
class Port:
    id: str
    direction: PortDirection
    type: ValueType
    required: bool = True


@dataclass(frozen=True, slots=True)
class Node:
    id: str
    kind: str
    inputs: tuple[Port, ...] = ()
    outputs: tuple[Port, ...] = ()
    config: Payload = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class Connection:
    from_node: str
    from_port: str
    to_node: str
    to_port: str


@dataclass(frozen=True, slots=True)
class Flow:
    version: int = CONTRACT_VERSION
    id: str = ""
    nodes: tuple[Node, ...] = ()
    connections: tuple[Connection, ...] = ()

    def __init__(self, version: int = CONTRACT_VERSION, id: str = "", nodes: tuple[Node, ...] | list[Node] = (), connections: tuple[Connection, ...] | list[Connection] = ()) -> None:
        object.__setattr__(self, "version", version)
        object.__setattr__(self, "id", id)
        object.__setattr__(self, "nodes", tuple(nodes))
        object.__setattr__(self, "connections", tuple(connections))


@dataclass(frozen=True, slots=True)
class EventMessage:
    version: int
    id: str
    type: str
    timestamp: str
    source: str
    payload: Payload = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class CommandMessage:
    version: int
    id: str
    type: str
    timestamp: str
    source: str
    correlation_id: str
    payload: Payload = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class ExecutionRequest:
    version: int
    execution_id: str
    command_id: str
    event: EventMessage


class ErrorCode(str, Enum):
    INVALID_CONTRACT_VERSION = "invalid_contract_version"
    INVALID_IDENTIFIER = "invalid_identifier"
    INVALID_MESSAGE_ID = "invalid_message_id"
    INVALID_MESSAGE_TYPE = "invalid_message_type"
    INVALID_TIMESTAMP = "invalid_timestamp"
    INVALID_SOURCE = "invalid_source"
    INVALID_PAYLOAD = "invalid_payload"
    SIZE_LIMIT_EXCEEDED = "size_limit_exceeded"
    DUPLICATE_IDENTIFIER = "duplicate_identifier"
    UNSUPPORTED_NODE_KIND = "unsupported_node_kind"
    INVALID_PORT = "invalid_port"
    MISSING_NODE = "missing_node"
    MISSING_PORT = "missing_port"
    WRONG_PORT_DIRECTION = "wrong_port_direction"
    TYPE_MISMATCH = "type_mismatch"
    DUPLICATE_CONNECTION = "duplicate_connection"
    MULTIPLE_INPUT_CONNECTIONS = "multiple_input_connections"
    MISSING_REQUIRED_INPUT = "missing_required_input"
    CYCLE_DETECTED = "cycle_detected"
    INVALID_CONFIGURATION = "invalid_configuration"
    EXECUTION_ERROR = "execution_error"


@dataclass(frozen=True, slots=True)
class Error:
    code: ErrorCode
    path: str
    message: str
    node_id: str = ""


@dataclass(frozen=True, slots=True)
class ValidationResult:
    errors: tuple[Error, ...] = ()
    @property
    def ok(self) -> bool: return not self.errors


class NodeStatus(str, Enum):
    EXECUTED = "executed"
    FILTERED = "filtered"


@dataclass(frozen=True, slots=True)
class NodeTrace:
    node_id: str
    status: NodeStatus
    outputs: Payload = field(default_factory=dict)


class ExecutionStatus(str, Enum):
    COMPLETED = "completed"
    FILTERED = "filtered"
    REJECTED = "rejected"
    FAILED = "failed"


@dataclass(frozen=True, slots=True)
class ExecutionResult:
    version: int
    execution_id: str
    status: ExecutionStatus
    trace: tuple[NodeTrace, ...] = ()
    commands: tuple[CommandMessage, ...] = ()
    errors: tuple[Error, ...] = ()
    @property
    def succeeded(self) -> bool: return self.status in {ExecutionStatus.COMPLETED, ExecutionStatus.FILTERED}


EVENT_TRIGGER = "event.trigger"
CONSTANT = "value.constant"
COMMAND_EMIT = "command.emit"
SUPPORTED_KINDS = {EVENT_TRIGGER, CONSTANT, COMMAND_EMIT}


def _valid_identifier(value: str, limit: int = MAX_IDENTIFIER_LENGTH) -> bool:
    return isinstance(value, str) and 0 < len(value) <= limit


def _valid_uuid(value: str) -> bool:
    try: UUID(value)
    except (ValueError, AttributeError, TypeError): return False
    return len(value) == 36 and all(value[index] == "-" for index in (8, 13, 18, 23))


def _valid_timestamp(value: str) -> bool:
    if not isinstance(value, str) or not value.endswith("Z"): return False
    try: parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError: return False
    return parsed.utcoffset() is not None


def _valid_value(value: Value) -> bool:
    if not isinstance(value, Value): return False
    return (value.type is not ValueType.NUMBER or math.isfinite(value.value)) and (value.type is not ValueType.TEXT or len(value.value) <= MAX_TEXT_LENGTH)


def _append_payload_errors(payload: Payload, path: str, errors: list[Error]) -> None:
    if len(payload) > MAX_PAYLOAD_FIELDS: errors.append(Error(ErrorCode.SIZE_LIMIT_EXCEEDED, path, "payload has too many fields"))
    for key, value in payload.items():
        if not _valid_identifier(key): errors.append(Error(ErrorCode.INVALID_PAYLOAD, f"{path}.{key}", "payload keys must be bounded identifiers"))
        if not _valid_value(value): errors.append(Error(ErrorCode.INVALID_PAYLOAD, f"{path}.{key}", "payload value is invalid"))


def _find_port(ports: tuple[Port, ...], port_id: str) -> Port | None:
    return next((port for port in ports if port.id == port_id), None)


def _config_text(node: Node, key: str) -> str | None:
    value = node.config.get(key)
    return value.value if value is not None and value.type is ValueType.TEXT and value.value else None


def _validate_ports(node: Node, ports: tuple[Port, ...], direction: PortDirection, path: str, errors: list[Error]) -> None:
    if len(ports) > MAX_PORTS: errors.append(Error(ErrorCode.SIZE_LIMIT_EXCEEDED, path, "node has too many ports", node.id))
    seen: set[str] = set()
    for index, port in enumerate(ports):
        port_path = f"{path}[{index}]"
        if not _valid_identifier(port.id, 64): errors.append(Error(ErrorCode.INVALID_PORT, port_path, "port id is invalid", node.id))
        if port.id in seen: errors.append(Error(ErrorCode.DUPLICATE_IDENTIFIER, port_path, "port ids must be unique", node.id))
        seen.add(port.id)
        if port.direction is not direction: errors.append(Error(ErrorCode.WRONG_PORT_DIRECTION, port_path, "port direction is invalid", node.id))


def _validate_node(node: Node, errors: list[Error]) -> None:
    _validate_ports(node, node.inputs, PortDirection.INPUT, f"nodes.{node.id}.inputs", errors)
    _validate_ports(node, node.outputs, PortDirection.OUTPUT, f"nodes.{node.id}.outputs", errors)
    _append_payload_errors(node.config, f"nodes.{node.id}.config", errors)
    if node.kind not in SUPPORTED_KINDS:
        errors.append(Error(ErrorCode.UNSUPPORTED_NODE_KIND, f"nodes.{node.id}.kind", "node kind is unsupported", node.id)); return
    if node.kind == EVENT_TRIGGER:
        matched = _find_port(node.outputs, "matched")
        if node.inputs or len(node.outputs) != 1 or matched is None or matched.type is not ValueType.BOOLEAN: errors.append(Error(ErrorCode.INVALID_PORT, f"nodes.{node.id}", "event.trigger requires one boolean matched output", node.id))
        event_type = _config_text(node, "event_type")
        if event_type is None or MESSAGE_TYPE.fullmatch(event_type) is None: errors.append(Error(ErrorCode.INVALID_CONFIGURATION, f"nodes.{node.id}.config.event_type", "event_type is invalid", node.id))
    elif node.kind == CONSTANT:
        output, value = _find_port(node.outputs, "value"), node.config.get("value")
        if node.inputs or len(node.outputs) != 1 or output is None: errors.append(Error(ErrorCode.INVALID_PORT, f"nodes.{node.id}", "value.constant requires one value output", node.id))
        if value is None: errors.append(Error(ErrorCode.INVALID_CONFIGURATION, f"nodes.{node.id}.config.value", "constant value is required", node.id))
        elif output is not None and value.type is not output.type: errors.append(Error(ErrorCode.TYPE_MISMATCH, f"nodes.{node.id}.config.value", "constant type does not match output", node.id))
    else:
        when, value = _find_port(node.inputs, "when"), _find_port(node.inputs, "value")
        command_type, payload_key = _config_text(node, "command_type"), _config_text(node, "payload_key")
        if node.outputs or len(node.inputs) != 2 or when is None or when.type is not ValueType.BOOLEAN or value is None: errors.append(Error(ErrorCode.INVALID_PORT, f"nodes.{node.id}", "command.emit requires when and value inputs", node.id))
        if command_type is None or MESSAGE_TYPE.fullmatch(command_type) is None: errors.append(Error(ErrorCode.INVALID_CONFIGURATION, f"nodes.{node.id}.config.command_type", "command_type is invalid", node.id))
        if payload_key is None or not _valid_identifier(payload_key, 64): errors.append(Error(ErrorCode.INVALID_CONFIGURATION, f"nodes.{node.id}.config.payload_key", "payload_key is invalid", node.id))


def validate_flow(flow: Flow) -> ValidationResult:
    errors: list[Error] = []
    if flow.version != CONTRACT_VERSION: errors.append(Error(ErrorCode.INVALID_CONTRACT_VERSION, "version", "unsupported flow contract version"))
    if not _valid_identifier(flow.id): errors.append(Error(ErrorCode.INVALID_IDENTIFIER, "id", "flow id is invalid"))
    if len(flow.nodes) > MAX_NODES: errors.append(Error(ErrorCode.SIZE_LIMIT_EXCEEDED, "nodes", "flow has too many nodes"))
    if len(flow.connections) > MAX_CONNECTIONS: errors.append(Error(ErrorCode.SIZE_LIMIT_EXCEEDED, "connections", "flow has too many connections"))
    positions: dict[str, int] = {}
    for index, node in enumerate(flow.nodes):
        if not _valid_identifier(node.id): errors.append(Error(ErrorCode.INVALID_IDENTIFIER, f"nodes[{index}].id", "node id is invalid", node.id))
        if node.id in positions: errors.append(Error(ErrorCode.DUPLICATE_IDENTIFIER, f"nodes[{index}].id", "node ids must be unique", node.id))
        positions.setdefault(node.id, index); _validate_node(node, errors)
    incoming: dict[tuple[str, str], int] = {}
    outgoing: dict[str, list[str]] = {node.id: [] for node in flow.nodes}
    indegree: dict[str, int] = {node.id: 0 for node in flow.nodes}
    connections: set[tuple[str, str, str, str]] = set()
    for index, connection in enumerate(flow.connections):
        path, key = f"connections[{index}]", (connection.from_node, connection.from_port, connection.to_node, connection.to_port)
        if key in connections: errors.append(Error(ErrorCode.DUPLICATE_CONNECTION, path, "connection must be unique"))
        connections.add(key)
        source = flow.nodes[positions[connection.from_node]] if connection.from_node in positions else None
        target = flow.nodes[positions[connection.to_node]] if connection.to_node in positions else None
        if source is None: errors.append(Error(ErrorCode.MISSING_NODE, f"{path}.from_node", "source node does not exist")); continue
        if target is None: errors.append(Error(ErrorCode.MISSING_NODE, f"{path}.to_node", "destination node does not exist")); continue
        from_port, to_port = _find_port(source.outputs, connection.from_port), _find_port(target.inputs, connection.to_port)
        if from_port is None: errors.append(Error(ErrorCode.MISSING_PORT, f"{path}.from_port", "source output does not exist")); continue
        if to_port is None: errors.append(Error(ErrorCode.MISSING_PORT, f"{path}.to_port", "destination input does not exist")); continue
        if from_port.direction is not PortDirection.OUTPUT or to_port.direction is not PortDirection.INPUT: errors.append(Error(ErrorCode.WRONG_PORT_DIRECTION, path, "connection must join output to input"))
        if from_port.type is not to_port.type: errors.append(Error(ErrorCode.TYPE_MISMATCH, path, "connected port types must match"))
        input_key = (connection.to_node, connection.to_port); incoming[input_key] = incoming.get(input_key, 0) + 1
        if incoming[input_key] > 1: errors.append(Error(ErrorCode.MULTIPLE_INPUT_CONNECTIONS, path, "input has multiple connections"))
        if connection.from_node == connection.to_node: errors.append(Error(ErrorCode.CYCLE_DETECTED, path, "node cannot connect to itself"))
        indegree[connection.to_node] += 1; outgoing[connection.from_node].append(connection.to_node)
    for node in flow.nodes:
        for port in node.inputs:
            if port.required and incoming.get((node.id, port.id), 0) == 0: errors.append(Error(ErrorCode.MISSING_REQUIRED_INPUT, f"nodes.{node.id}.inputs.{port.id}", "required input is not connected", node.id))
    ready = deque(node.id for node in flow.nodes if indegree[node.id] == 0); visited = 0
    while ready:
        node_id = ready.popleft(); visited += 1
        for next_node in outgoing[node_id]:
            indegree[next_node] -= 1
            if indegree[next_node] == 0: ready.append(next_node)
    if visited != len(flow.nodes): errors.append(Error(ErrorCode.CYCLE_DETECTED, "connections", "flow graph must be acyclic"))
    return ValidationResult(tuple(errors))


def _validate_message(message: EventMessage) -> list[Error]:
    errors: list[Error] = []
    if message.version != CONTRACT_VERSION: errors.append(Error(ErrorCode.INVALID_CONTRACT_VERSION, "version", "unsupported message contract version"))
    if not _valid_uuid(message.id): errors.append(Error(ErrorCode.INVALID_MESSAGE_ID, "id", "message id must be a UUID"))
    if MESSAGE_TYPE.fullmatch(message.type or "") is None: errors.append(Error(ErrorCode.INVALID_MESSAGE_TYPE, "type", "message type is invalid"))
    if not _valid_timestamp(message.timestamp): errors.append(Error(ErrorCode.INVALID_TIMESTAMP, "timestamp", "timestamp must be UTC ISO-8601"))
    if not _valid_identifier(message.source): errors.append(Error(ErrorCode.INVALID_SOURCE, "source", "source is invalid"))
    _append_payload_errors(message.payload, "payload", errors); return errors


def validate_event(event: EventMessage) -> ValidationResult: return ValidationResult(tuple(_validate_message(event)))


def validate_request(request: ExecutionRequest) -> ValidationResult:
    errors = _validate_message(request.event)
    if request.version != CONTRACT_VERSION: errors.append(Error(ErrorCode.INVALID_CONTRACT_VERSION, "version", "unsupported request contract version"))
    if not _valid_uuid(request.execution_id): errors.append(Error(ErrorCode.INVALID_MESSAGE_ID, "execution_id", "execution id must be a UUID"))
    if not _valid_uuid(request.command_id): errors.append(Error(ErrorCode.INVALID_MESSAGE_ID, "command_id", "command id must be a UUID"))
    return ValidationResult(tuple(errors))


def _execution_order(flow: Flow) -> tuple[Node, ...]:
    positions = {node.id: index for index, node in enumerate(flow.nodes)}; indegree = {node.id: 0 for node in flow.nodes}; outgoing: dict[str, list[str]] = {node.id: [] for node in flow.nodes}
    for connection in flow.connections: indegree[connection.to_node] += 1; outgoing[connection.from_node].append(connection.to_node)
    ready = [(index, node.id) for index, node in enumerate(flow.nodes) if indegree[node.id] == 0]; order: list[Node] = []
    while ready:
        _, node_id = min(ready); ready.remove((positions[node_id], node_id)); order.append(flow.nodes[positions[node_id]])
        for next_node in outgoing[node_id]:
            indegree[next_node] -= 1
            if indegree[next_node] == 0: ready.append((positions[next_node], next_node))
    return tuple(order)


class FlowEngine:
    """Service-layer facade: validate once, then execute deterministic nodes."""

    def execute(self, flow: Flow, request: ExecutionRequest) -> ExecutionResult:
        flow_validation, request_validation = validate_flow(flow), validate_request(request)
        if not flow_validation.ok or not request_validation.ok:
            return ExecutionResult(CONTRACT_VERSION, request.execution_id, ExecutionStatus.REJECTED, errors=flow_validation.errors + request_validation.errors)
        inputs: dict[str, Payload] = {}; traces: list[NodeTrace] = []; commands: list[CommandMessage] = []; filtered = False
        try:
            for node in _execution_order(flow):
                outputs: Payload = {}; status = NodeStatus.EXECUTED
                if node.kind == EVENT_TRIGGER:
                    outputs["matched"] = Value.boolean(request.event.type == _config_text(node, "event_type"))
                elif node.kind == CONSTANT:
                    outputs["value"] = node.config["value"]
                else:
                    node_inputs = inputs.get(node.id, {}); when, value = node_inputs.get("when"), node_inputs.get("value")
                    if when is None or when.type is not ValueType.BOOLEAN: raise ValueError(f"{node.id}.when is not boolean")
                    if not when.value: filtered, status = True, NodeStatus.FILTERED
                    else:
                        command_type, payload_key = _config_text(node, "command_type"), _config_text(node, "payload_key")
                        if command_type is None or payload_key is None or value is None: raise ValueError(f"{node.id} configuration or inputs are incomplete")
                        commands.append(CommandMessage(CONTRACT_VERSION, request.command_id, command_type, request.event.timestamp, LOGIC_SOURCE, request.event.id, {payload_key: value}))
                traces.append(NodeTrace(node.id, status, dict(outputs)))
                for connection in flow.connections:
                    if connection.from_node == node.id and connection.from_port in outputs: inputs.setdefault(connection.to_node, {})[connection.to_port] = outputs[connection.from_port]
            return ExecutionResult(CONTRACT_VERSION, request.execution_id, ExecutionStatus.FILTERED if filtered and not commands else ExecutionStatus.COMPLETED, tuple(traces), tuple(commands))
        except (KeyError, ValueError, TypeError) as error:
            return ExecutionResult(CONTRACT_VERSION, request.execution_id, ExecutionStatus.FAILED, tuple(traces), tuple(commands), (Error(ErrorCode.EXECUTION_ERROR, "execution", str(error)),))
