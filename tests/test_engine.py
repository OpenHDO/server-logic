from __future__ import annotations

import unittest
from openhdo_logic import Connection, EventMessage, ExecutionRequest, ExecutionStatus, Flow, FlowEngine, Node, Port, PortDirection, Value, ValueType, validate_flow

EVENT_ID = "11111111-1111-4111-8111-111111111111"
EXECUTION_ID = "22222222-2222-4222-8222-222222222222"
COMMAND_ID = "33333333-3333-4333-8333-333333333333"


def input_port(identifier: str, kind: ValueType) -> Port: return Port(identifier, PortDirection.INPUT, kind, True)
def output_port(identifier: str, kind: ValueType) -> Port: return Port(identifier, PortDirection.OUTPUT, kind, False)


def valid_flow() -> Flow:
    return Flow(id="flow-1", nodes=[
        Node("trigger", "event.trigger", outputs=(output_port("matched", ValueType.BOOLEAN),), config={"event_type": Value.text("sensor.changed")}),
        Node("constant", "value.constant", outputs=(output_port("value", ValueType.TEXT),), config={"value": Value.text("on")}),
        Node("command", "command.emit", inputs=(input_port("when", ValueType.BOOLEAN), input_port("value", ValueType.TEXT)), config={"command_type": Value.text("device.set"), "payload_key": Value.text("state")}),
    ], connections=[Connection("trigger", "matched", "command", "when"), Connection("constant", "value", "command", "value")])


def request(event_type: str = "sensor.changed") -> ExecutionRequest:
    return ExecutionRequest(1, EXECUTION_ID, COMMAND_ID, EventMessage(1, EVENT_ID, event_type, "2026-09-01T10:00:00Z", "server-linker", {"temperature": Value.number(21.5)}))


class FlowEngineTests(unittest.TestCase):
    def test_matching_event_emits_deterministic_correlated_command(self) -> None:
        first, second = FlowEngine().execute(valid_flow(), request()), FlowEngine().execute(valid_flow(), request())
        self.assertEqual(first.status, ExecutionStatus.COMPLETED); self.assertEqual(first.errors, ()); self.assertEqual(first.commands, second.commands)
        self.assertEqual(len(first.commands), 1); command = first.commands[0]
        self.assertEqual((command.id, command.correlation_id, command.type), (COMMAND_ID, EVENT_ID, "device.set"))
        self.assertEqual(command.payload, {"state": Value.text("on")})
        self.assertEqual([trace.node_id for trace in first.trace], ["trigger", "constant", "command"])

    def test_non_matching_event_is_filtered_without_command(self) -> None:
        result = FlowEngine().execute(valid_flow(), request("other.event")); self.assertEqual(result.status, ExecutionStatus.FILTERED); self.assertTrue(result.succeeded); self.assertEqual(result.commands, ())

    def test_validation_rejects_cycle_and_type_mismatch(self) -> None:
        flow = valid_flow(); mismatch = Flow(flow.version, flow.id, flow.nodes, [Connection("constant", "value", "command", "when"), flow.connections[0]])
        self.assertTrue(any(error.code.value == "type_mismatch" for error in validate_flow(mismatch).errors))
        cycle_node = Node("constant", "value.constant", inputs=(input_port("loop", ValueType.TEXT),), outputs=(output_port("value", ValueType.TEXT),), config={"value": Value.text("on")})
        cycle = Flow(flow.version, flow.id, [flow.nodes[0], cycle_node, flow.nodes[2]], [Connection("constant", "value", "constant", "loop"), flow.connections[0]])
        self.assertTrue(any(error.code.value == "cycle_detected" for error in validate_flow(cycle).errors))

    def test_invalid_request_is_rejected(self) -> None:
        event = EventMessage(2, "not-a-uuid", "Bad Type", "2026-09-01T10:00:00Z", "server-linker", {})
        result = FlowEngine().execute(valid_flow(), ExecutionRequest(1, EXECUTION_ID, COMMAND_ID, event))
        self.assertEqual(result.status, ExecutionStatus.REJECTED); self.assertFalse(result.succeeded)


if __name__ == "__main__": unittest.main()
