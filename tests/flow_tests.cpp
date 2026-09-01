#include "openhdo/logic/flow.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace openhdo::logic;

constexpr const char* kEventId = "11111111-1111-4111-8111-111111111111";
constexpr const char* kExecutionId = "22222222-2222-4222-8222-222222222222";
constexpr const char* kCommandId = "33333333-3333-4333-8333-333333333333";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Port input(std::string id, ValueType type) {
    return Port{std::move(id), PortDirection::input, type, true};
}

Port output(std::string id, ValueType type) {
    return Port{std::move(id), PortDirection::output, type, false};
}

Node trigger_node() {
    return Node{
        "trigger",
        std::string{kEventTriggerKind},
        {},
        {output("matched", ValueType::boolean)},
        {{"event_type", Value::text("sensor.changed")}},
    };
}

Node constant_node() {
    return Node{
        "constant",
        std::string{kConstantKind},
        {},
        {output("value", ValueType::text)},
        {{"value", Value::text("on")}},
    };
}

Node command_node() {
    return Node{
        "command",
        std::string{kCommandEmitKind},
        {input("when", ValueType::boolean), input("value", ValueType::text)},
        {},
        {{"command_type", Value::text("device.set")}, {"payload_key", Value::text("state")}},
    };
}

Flow valid_flow() {
    return Flow{
        kContractVersion,
        "flow-1",
        {trigger_node(), constant_node(), command_node()},
        {
            {"trigger", "matched", "command", "when"},
            {"constant", "value", "command", "value"},
        },
    };
}

EventMessage event(std::string type = "sensor.changed") {
    return EventMessage{
        kContractVersion,
        kEventId,
        std::move(type),
        "2026-09-01T10:00:00Z",
        "server-linker",
        {{"temperature", Value::number(21.5)}},
    };
}

ExecutionRequest request(EventMessage source = event()) {
    return ExecutionRequest{kContractVersion, kExecutionId, kCommandId, std::move(source)};
}

void valid_flow_emits_a_versioned_command() {
    const auto flow = valid_flow();
    require(validate_flow(flow).ok(), "valid flow should validate");

    const FlowEngine engine;
    const auto first = engine.execute(flow, request());
    const auto second = engine.execute(flow, request());

    require(first.status == ExecutionStatus::completed, "matching event should complete");
    require(first.errors.empty(), "matching event should have no errors");
    require(first.commands.size() == 1, "matching event should produce one command");
    require(first.commands.size() == second.commands.size() && first.commands.front().id == second.commands.front().id &&
                first.commands.front().type == second.commands.front().type &&
                first.commands.front().payload == second.commands.front().payload,
            "same request must produce the same command");
    require(first.trace.size() == 3, "all nodes should be traced");
    require(first.trace[0].node_id == "trigger" && first.trace[1].node_id == "constant" &&
                first.trace[2].node_id == "command",
            "independent nodes should follow declaration order");

    const auto& command = first.commands.front();
    require(command.version == kContractVersion, "command must use the v1 boundary");
    require(command.id == kCommandId && command.correlation_id == kEventId, "command ids must preserve correlation");
    require(command.type == "device.set" && command.timestamp == "2026-09-01T10:00:00Z",
            "command metadata must be derived from the request");
    require(command.payload.at("state") == Value::text("on"), "command payload must contain the propagated value");
}

void non_matching_event_is_filtered_without_a_command() {
    const FlowEngine engine;
    const auto result = engine.execute(valid_flow(), request(event("other.event")));

    require(result.status == ExecutionStatus::filtered, "non-matching event should be filtered");
    require(result.succeeded(), "filtered execution is a successful evaluation");
    require(result.commands.empty(), "filtered event must not emit a command");
    require(result.trace.back().status == NodeStatus::filtered, "command sink should be traced as filtered");
}

void validation_rejects_bad_connections_and_cycles() {
    auto flow = valid_flow();
    flow.connections[1].to_port = "when";
    const auto mismatch = validate_flow(flow);
    require(!mismatch.ok(), "mismatched connection should be rejected");
    require(mismatch.errors[0].code == ErrorCode::type_mismatch, "mismatched connection should be typed");

    flow = valid_flow();
    flow.nodes[1].inputs.push_back(input("loop", ValueType::text));
    flow.connections.push_back({"constant", "value", "constant", "loop"});
    const auto cycle = validate_flow(flow);
    require(!cycle.ok(), "cycle should be rejected");
    bool found_cycle = false;
    for (const auto& issue : cycle.errors) {
        found_cycle = found_cycle || issue.code == ErrorCode::cycle_detected;
    }
    require(found_cycle, "cycle rejection should be structured");

    flow = valid_flow();
    flow.connections.pop_back();
    const auto missing_input = validate_flow(flow);
    bool found_missing_input = false;
    for (const auto& issue : missing_input.errors) {
        found_missing_input = found_missing_input || issue.code == ErrorCode::missing_required_input;
    }
    require(found_missing_input, "required inputs should be validated");
}

void validation_rejects_bad_server_messages() {
    auto source = event();
    source.version = 2;
    source.id = "not-a-uuid";
    source.type = "Bad Type";
    const auto result = validate_request(request(std::move(source)));

    require(!result.ok(), "invalid event should be rejected at the boundary");
    require(result.errors.size() == 3, "invalid event should return each field error");
    require(result.errors[0].code == ErrorCode::invalid_contract_version, "version failure should be explicit");
}

} // namespace

int main() {
    try {
        valid_flow_emits_a_versioned_command();
        non_matching_event_is_filtered_without_a_command();
        validation_rejects_bad_connections_and_cycles();
        validation_rejects_bad_server_messages();
        std::cout << "openhdo logic tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "openhdo logic tests failed: " << exception.what() << '\n';
        return 1;
    }
}
