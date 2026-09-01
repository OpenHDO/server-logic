#include "openhdo/logic/flow.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace openhdo::logic {
namespace {

using NodeIndex = std::map<std::string, std::size_t>;

Error error(ErrorCode code, std::string path, std::string message, std::string node_id = {}) {
    return Error{code, std::move(path), std::move(message), std::move(node_id)};
}

void add_error(ValidationResult& result, ErrorCode code, std::string path, std::string message,
               std::string node_id = {}) {
    result.errors.push_back(error(code, std::move(path), std::move(message), std::move(node_id)));
}

bool valid_identifier(std::string_view value, std::size_t max_length = kMaxIdentifierLength) {
    return !value.empty() && value.size() <= max_length;
}

bool valid_uuid(std::string_view value) {
    if (value.size() != 36) {
        return false;
    }

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_message_type(std::string_view value) {
    if (value.empty() || value.size() > 64 || !std::islower(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::islower(byte) || std::isdigit(byte) || character == '.' || character == '_' || character == '-';
    });
}

bool valid_timestamp(std::string_view value) {
    if (value.size() < 20 || value.back() != 'Z' || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':') {
        return false;
    }
    for (std::size_t index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U, 12U, 14U, 15U, 17U, 18U}) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    if (value.size() == 20) {
        return true;
    }
    return value[19] == '.' && value.size() >= 22 && value.size() <= 31 &&
           std::all_of(value.begin() + 20, value.end() - 1, [](char character) {
               return std::isdigit(static_cast<unsigned char>(character));
           });
}

bool supported_kind(std::string_view kind) {
    return kind == kEventTriggerKind || kind == kConstantKind || kind == kCommandEmitKind;
}

bool valid_value(const Value& value) {
    if (value.type() == ValueType::number) {
        return std::isfinite(std::get<double>(value.storage()));
    }
    if (value.type() == ValueType::text) {
        return std::get<std::string>(value.storage()).size() <= kMaxTextLength;
    }
    return true;
}

void validate_payload(const Payload& payload, std::string_view path, ValidationResult& result) {
    if (payload.size() > kMaxPayloadFields) {
        add_error(result, ErrorCode::size_limit_exceeded, std::string{path}, "payload has too many fields");
    }
    for (const auto& [key, value] : payload) {
        if (!valid_identifier(key)) {
            add_error(result, ErrorCode::invalid_payload, std::string{path} + "." + key,
                      "payload keys must be non-empty and at most 128 characters");
        }
        if (!valid_value(value)) {
            add_error(result, ErrorCode::invalid_payload, std::string{path} + "." + key,
                      "payload values must be finite numbers and bounded text");
        }
    }
}

const Port* find_port(const std::vector<Port>& ports, std::string_view id) {
    const auto found = std::find_if(ports.begin(), ports.end(), [id](const Port& port) { return port.id == id; });
    return found == ports.end() ? nullptr : &*found;
}

const Value* config_value(const Node& node, std::string_view key) {
    const auto found = node.config.find(std::string{key});
    return found == node.config.end() ? nullptr : &found->second;
}

bool config_text(const Node& node, std::string_view key, std::string& value) {
    const auto configured = config_value(node, key);
    if (configured == nullptr || configured->type() != ValueType::text) {
        return false;
    }
    value = std::get<std::string>(configured->storage());
    return !value.empty();
}

void validate_ports(const Node& node, const std::vector<Port>& ports, PortDirection direction,
                   std::string_view path, ValidationResult& result) {
    if (ports.size() > kMaxPortsPerNode) {
        add_error(result, ErrorCode::size_limit_exceeded, std::string{path}, "node has too many ports", node.id);
    }

    std::set<std::string> identifiers;
    for (std::size_t index = 0; index < ports.size(); ++index) {
        const auto& port = ports[index];
        const auto port_path = std::string{path} + "[" + std::to_string(index) + "]";
        if (!valid_identifier(port.id, 64)) {
            add_error(result, ErrorCode::invalid_port, port_path, "port id must be non-empty and at most 64 characters",
                      node.id);
        }
        if (!identifiers.insert(port.id).second) {
            add_error(result, ErrorCode::duplicate_identifier, port_path, "port ids must be unique", node.id);
        }
        if (port.direction != direction) {
            add_error(result, ErrorCode::wrong_port_direction, port_path, "port direction does not match its collection",
                      node.id);
        }
    }
}

bool has_required_port(const std::vector<Port>& ports, std::string_view id, ValueType type) {
    const auto* port = find_port(ports, id);
    return port != nullptr && port->required && port->type == type;
}

void validate_node_shape(const Node& node, ValidationResult& result) {
    validate_ports(node, node.inputs, PortDirection::input, "inputs", result);
    validate_ports(node, node.outputs, PortDirection::output, "outputs", result);
    validate_payload(node.config, "nodes." + node.id + ".config", result);

    if (!supported_kind(node.kind)) {
        add_error(result, ErrorCode::unsupported_node_kind, "nodes." + node.id + ".kind",
                  "node kind is not supported by this engine", node.id);
        return;
    }

    if (node.kind == kEventTriggerKind) {
        if (!node.inputs.empty() || node.outputs.size() != 1 ||
            find_port(node.outputs, "matched") == nullptr ||
            find_port(node.outputs, "matched")->type != ValueType::boolean) {
            add_error(result, ErrorCode::invalid_port, "nodes." + node.id,
                      "event.trigger requires one boolean matched output and no inputs", node.id);
        }
        std::string event_type;
        if (!config_text(node, "event_type", event_type) || !valid_message_type(event_type)) {
            add_error(result, ErrorCode::invalid_configuration, "nodes." + node.id + ".config.event_type",
                      "event_type must be a valid message type", node.id);
        }
    } else if (node.kind == kConstantKind) {
        if (!node.inputs.empty() || node.outputs.size() != 1 || find_port(node.outputs, "value") == nullptr) {
            add_error(result, ErrorCode::invalid_port, "nodes." + node.id,
                      "value.constant requires one value output and no inputs", node.id);
        }
        if (config_value(node, "value") == nullptr) {
            add_error(result, ErrorCode::invalid_configuration, "nodes." + node.id + ".config.value",
                      "value.constant requires a value", node.id);
        } else if (find_port(node.outputs, "value") != nullptr &&
                   config_value(node, "value")->type() != find_port(node.outputs, "value")->type) {
            add_error(result, ErrorCode::type_mismatch, "nodes." + node.id + ".config.value",
                      "constant value type must match its output port", node.id);
        }
    } else if (node.kind == kCommandEmitKind) {
        if (node.outputs.size() != 0 || !has_required_port(node.inputs, "when", ValueType::boolean) ||
            find_port(node.inputs, "value") == nullptr || node.inputs.size() != 2) {
            add_error(result, ErrorCode::invalid_port, "nodes." + node.id,
                      "command.emit requires boolean when and scalar value inputs", node.id);
        }
        std::string command_type;
        if (!config_text(node, "command_type", command_type) || !valid_message_type(command_type)) {
            add_error(result, ErrorCode::invalid_configuration, "nodes." + node.id + ".config.command_type",
                      "command_type must be a valid message type", node.id);
        }
        std::string payload_key;
        if (!config_text(node, "payload_key", payload_key) || !valid_identifier(payload_key, 64)) {
            add_error(result, ErrorCode::invalid_configuration, "nodes." + node.id + ".config.payload_key",
                      "payload_key must be non-empty and at most 64 characters", node.id);
        }
    }
}

NodeIndex index_nodes(const Flow& flow, ValidationResult& result) {
    NodeIndex index;
    for (std::size_t position = 0; position < flow.nodes.size(); ++position) {
        const auto& node = flow.nodes[position];
        if (!valid_identifier(node.id)) {
            add_error(result, ErrorCode::invalid_identifier, "nodes[" + std::to_string(position) + "].id",
                      "node id must be non-empty and at most 128 characters", node.id);
        }
        if (!valid_identifier(node.kind, 64)) {
            add_error(result, ErrorCode::invalid_identifier, "nodes[" + std::to_string(position) + "].kind",
                      "node kind must be non-empty and at most 64 characters", node.id);
        }
        if (!index.emplace(node.id, position).second) {
            add_error(result, ErrorCode::duplicate_identifier, "nodes[" + std::to_string(position) + "].id",
                      "node ids must be unique", node.id);
        }
        validate_node_shape(node, result);
    }
    return index;
}

void validate_graph(const Flow& flow, const NodeIndex& node_index, ValidationResult& result) {
    std::map<std::string, std::size_t> incoming_counts;
    std::set<std::string> connections;
    std::map<std::string, std::vector<std::string>> outgoing;
    std::map<std::string, std::size_t> indegree;
    for (const auto& node : flow.nodes) {
        indegree.emplace(node.id, 0);
    }

    for (std::size_t position = 0; position < flow.connections.size(); ++position) {
        const auto& connection = flow.connections[position];
        const auto path = "connections[" + std::to_string(position) + "]";
        const auto key = connection.from_node + ":" + connection.from_port + "->" + connection.to_node + ":" +
                         connection.to_port;
        if (!connections.insert(key).second) {
            add_error(result, ErrorCode::duplicate_connection, path, "connection must be unique");
        }

        const auto from = node_index.find(connection.from_node);
        const auto to = node_index.find(connection.to_node);
        if (from == node_index.end()) {
            add_error(result, ErrorCode::missing_node, path + ".from_node", "source node does not exist");
            continue;
        }
        if (to == node_index.end()) {
            add_error(result, ErrorCode::missing_node, path + ".to_node", "destination node does not exist");
            continue;
        }

        const auto& from_node = flow.nodes[from->second];
        const auto& to_node = flow.nodes[to->second];
        const auto* from_port = find_port(from_node.outputs, connection.from_port);
        const auto* to_port = find_port(to_node.inputs, connection.to_port);
        if (from_port == nullptr) {
            add_error(result, ErrorCode::missing_port, path + ".from_port", "source output port does not exist");
            continue;
        }
        if (to_port == nullptr) {
            add_error(result, ErrorCode::missing_port, path + ".to_port", "destination input port does not exist");
            continue;
        }
        if (from_port->direction != PortDirection::output || to_port->direction != PortDirection::input) {
            add_error(result, ErrorCode::wrong_port_direction, path, "connections must join an output to an input");
        }
        if (from_port->type != to_port->type) {
            add_error(result, ErrorCode::type_mismatch, path, "connected ports must have the same type");
        }

        const auto input_key = connection.to_node + ":" + connection.to_port;
        if (++incoming_counts[input_key] > 1) {
            add_error(result, ErrorCode::multiple_input_connections, path,
                      "an input port may have only one incoming connection");
        }
        if (connection.from_node == connection.to_node) {
            add_error(result, ErrorCode::cycle_detected, path, "a node cannot connect to itself");
        }

        ++indegree[connection.to_node];
        outgoing[connection.from_node].push_back(connection.to_node);
    }

    for (const auto& node : flow.nodes) {
        for (const auto& port : node.inputs) {
            if (port.required && incoming_counts[node.id + ":" + port.id] == 0) {
                add_error(result, ErrorCode::missing_required_input, "nodes." + node.id + ".inputs." + port.id,
                          "required input has no connection", node.id);
            }
        }
    }

    std::queue<std::string> ready;
    for (const auto& [node_id, degree] : indegree) {
        if (degree == 0) {
            ready.push(node_id);
        }
    }
    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto node_id = ready.front();
        ready.pop();
        ++visited;
        for (const auto& next : outgoing[node_id]) {
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }
    if (visited != flow.nodes.size()) {
        add_error(result, ErrorCode::cycle_detected, "connections", "flow graph must be acyclic");
    }
}

ValidationResult validate_message_fields(const EventMessage& message) {
    ValidationResult result;
    if (message.version != kContractVersion) {
        add_error(result, ErrorCode::invalid_contract_version, "version", "unsupported message contract version");
    }
    if (!valid_uuid(message.id)) {
        add_error(result, ErrorCode::invalid_message_id, "id", "message id must be a canonical UUID");
    }
    if (!valid_message_type(message.type)) {
        add_error(result, ErrorCode::invalid_message_type, "type", "message type is invalid");
    }
    if (!valid_timestamp(message.timestamp)) {
        add_error(result, ErrorCode::invalid_timestamp, "timestamp", "timestamp must be an ISO-8601 UTC value");
    }
    if (!valid_identifier(message.source)) {
        add_error(result, ErrorCode::invalid_source, "source", "source must be non-empty and at most 128 characters");
    }
    validate_payload(message.payload, "payload", result);
    return result;
}

std::vector<const Node*> execution_order(const Flow& flow) {
    std::map<std::string, std::size_t> positions;
    std::map<std::string, std::size_t> indegree;
    std::map<std::string, std::vector<std::string>> outgoing;
    for (std::size_t position = 0; position < flow.nodes.size(); ++position) {
        positions.emplace(flow.nodes[position].id, position);
        indegree.emplace(flow.nodes[position].id, 0);
    }
    for (const auto& connection : flow.connections) {
        ++indegree[connection.to_node];
        outgoing[connection.from_node].push_back(connection.to_node);
    }

    std::set<std::pair<std::size_t, std::string>> ready;
    for (const auto& [node_id, degree] : indegree) {
        if (degree == 0) {
            ready.emplace(positions.at(node_id), node_id);
        }
    }

    std::vector<const Node*> order;
    while (!ready.empty()) {
        const auto [position, node_id] = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(&flow.nodes[position]);
        for (const auto& next : outgoing[node_id]) {
            if (--indegree[next] == 0) {
                ready.emplace(positions.at(next), next);
            }
        }
    }
    return order;
}

const Value* input_value(const std::map<std::string, Payload>& inputs, const Node& node, std::string_view port) {
    const auto node_inputs = inputs.find(node.id);
    if (node_inputs == inputs.end()) {
        return nullptr;
    }
    const auto found = node_inputs->second.find(std::string{port});
    return found == node_inputs->second.end() ? nullptr : &found->second;
}

void propagate(const Flow& flow, const Node& node, const Payload& outputs,
               std::map<std::string, Payload>& inputs) {
    for (const auto& connection : flow.connections) {
        if (connection.from_node != node.id) {
            continue;
        }
        const auto value = outputs.find(connection.from_port);
        if (value != outputs.end()) {
            inputs[connection.to_node].insert_or_assign(connection.to_port, value->second);
        }
    }
}

} // namespace

Value::Value(Storage value) : value_(std::move(value)) {}

Value Value::boolean(bool value) {
    return Value{Storage{value}};
}

Value Value::integer(std::int64_t value) {
    return Value{Storage{value}};
}

Value Value::number(double value) {
    return Value{Storage{value}};
}

Value Value::text(std::string value) {
    return Value{Storage{std::move(value)}};
}

ValueType Value::type() const noexcept {
    switch (value_.index()) {
    case 0:
        return ValueType::boolean;
    case 1:
        return ValueType::integer;
    case 2:
        return ValueType::number;
    default:
        return ValueType::text;
    }
}

const Value::Storage& Value::storage() const noexcept {
    return value_;
}

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::invalid_contract_version:
        return "invalid_contract_version";
    case ErrorCode::invalid_identifier:
        return "invalid_identifier";
    case ErrorCode::invalid_message_id:
        return "invalid_message_id";
    case ErrorCode::invalid_message_type:
        return "invalid_message_type";
    case ErrorCode::invalid_timestamp:
        return "invalid_timestamp";
    case ErrorCode::invalid_source:
        return "invalid_source";
    case ErrorCode::invalid_payload:
        return "invalid_payload";
    case ErrorCode::size_limit_exceeded:
        return "size_limit_exceeded";
    case ErrorCode::duplicate_identifier:
        return "duplicate_identifier";
    case ErrorCode::unsupported_node_kind:
        return "unsupported_node_kind";
    case ErrorCode::invalid_port:
        return "invalid_port";
    case ErrorCode::missing_node:
        return "missing_node";
    case ErrorCode::missing_port:
        return "missing_port";
    case ErrorCode::wrong_port_direction:
        return "wrong_port_direction";
    case ErrorCode::type_mismatch:
        return "type_mismatch";
    case ErrorCode::duplicate_connection:
        return "duplicate_connection";
    case ErrorCode::multiple_input_connections:
        return "multiple_input_connections";
    case ErrorCode::missing_required_input:
        return "missing_required_input";
    case ErrorCode::cycle_detected:
        return "cycle_detected";
    case ErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ErrorCode::multiple_command_sinks:
        return "multiple_command_sinks";
    case ErrorCode::execution_error:
        return "execution_error";
    }
    return "unknown";
}

std::string_view to_string(ExecutionStatus status) noexcept {
    switch (status) {
    case ExecutionStatus::completed:
        return "completed";
    case ExecutionStatus::filtered:
        return "filtered";
    case ExecutionStatus::rejected:
        return "rejected";
    case ExecutionStatus::failed:
        return "failed";
    }
    return "unknown";
}

std::string_view to_string(NodeStatus status) noexcept {
    switch (status) {
    case NodeStatus::executed:
        return "executed";
    case NodeStatus::filtered:
        return "filtered";
    }
    return "unknown";
}

ValidationResult validate_flow(const Flow& flow) {
    ValidationResult result;
    if (flow.version != kContractVersion) {
        add_error(result, ErrorCode::invalid_contract_version, "version", "unsupported flow contract version");
    }
    if (!valid_identifier(flow.id)) {
        add_error(result, ErrorCode::invalid_identifier, "id", "flow id must be non-empty and at most 128 characters");
    }
    if (flow.nodes.empty()) {
        add_error(result, ErrorCode::size_limit_exceeded, "nodes", "flow must contain at least one node");
    }
    if (flow.nodes.size() > kMaxNodes) {
        add_error(result, ErrorCode::size_limit_exceeded, "nodes", "flow has too many nodes");
    }
    if (flow.connections.size() > kMaxConnections) {
        add_error(result, ErrorCode::size_limit_exceeded, "connections", "flow has too many connections");
    }

    const auto node_index = index_nodes(flow, result);
    std::size_t command_sinks = 0;
    for (const auto& node : flow.nodes) {
        if (node.kind == kCommandEmitKind) {
            ++command_sinks;
        }
    }
    if (command_sinks > 1) {
        add_error(result, ErrorCode::multiple_command_sinks, "nodes", "the first slice supports at most one command sink");
    }
    validate_graph(flow, node_index, result);
    return result;
}

ValidationResult validate_event(const EventMessage& event) {
    return validate_message_fields(event);
}

ValidationResult validate_request(const ExecutionRequest& request) {
    ValidationResult result;
    if (request.version != kContractVersion) {
        add_error(result, ErrorCode::invalid_contract_version, "version", "unsupported execution request version");
    }
    if (!valid_uuid(request.execution_id)) {
        add_error(result, ErrorCode::invalid_message_id, "execution_id", "execution id must be a canonical UUID");
    }
    if (!valid_uuid(request.command_id)) {
        add_error(result, ErrorCode::invalid_message_id, "command_id", "command id must be a canonical UUID");
    }
    const auto event_result = validate_event(request.event);
    result.errors.insert(result.errors.end(), event_result.errors.begin(), event_result.errors.end());
    return result;
}

ExecutionResult FlowEngine::execute(const Flow& flow, const ExecutionRequest& request) const {
    ExecutionResult result;
    result.execution_id = request.execution_id;

    const auto flow_result = validate_flow(flow);
    const auto request_result = validate_request(request);
    result.errors.insert(result.errors.end(), flow_result.errors.begin(), flow_result.errors.end());
    result.errors.insert(result.errors.end(), request_result.errors.begin(), request_result.errors.end());
    if (!result.errors.empty()) {
        result.status = ExecutionStatus::rejected;
        return result;
    }

    std::map<std::string, Payload> inputs;
    bool filtered = false;
    for (const auto* node : execution_order(flow)) {
        NodeTrace trace{node->id, NodeStatus::executed, {}};
        if (node->kind == kEventTriggerKind) {
            const auto configured = config_value(*node, "event_type");
            if (configured == nullptr || configured->type() != ValueType::text) {
                result.errors.push_back(error(ErrorCode::execution_error, "nodes." + node->id,
                                              "event.trigger configuration was not executable", node->id));
                trace.status = NodeStatus::filtered;
                result.trace.push_back(std::move(trace));
                result.status = ExecutionStatus::failed;
                return result;
            }
            const auto& event_type = std::get<std::string>(configured->storage());
            trace.outputs.emplace("matched", Value::boolean(request.event.type == event_type));
        } else if (node->kind == kConstantKind) {
            const auto configured = config_value(*node, "value");
            if (configured == nullptr) {
                result.errors.push_back(error(ErrorCode::execution_error, "nodes." + node->id,
                                              "constant configuration was not executable", node->id));
                trace.status = NodeStatus::filtered;
                result.trace.push_back(std::move(trace));
                result.status = ExecutionStatus::failed;
                return result;
            }
            trace.outputs.emplace("value", *configured);
        } else if (node->kind == kCommandEmitKind) {
            const auto* when = input_value(inputs, *node, "when");
            const auto* value = input_value(inputs, *node, "value");
            if (when == nullptr || when->type() != ValueType::boolean || value == nullptr) {
                result.errors.push_back(error(ErrorCode::execution_error, "nodes." + node->id,
                                              "command.emit inputs were not available", node->id));
                trace.status = NodeStatus::filtered;
                result.trace.push_back(std::move(trace));
                result.status = ExecutionStatus::failed;
                return result;
            }
            if (!std::get<bool>(when->storage())) {
                trace.status = NodeStatus::filtered;
                filtered = true;
            } else {
                std::string command_type;
                std::string payload_key;
                if (!config_text(*node, "command_type", command_type) ||
                    !config_text(*node, "payload_key", payload_key)) {
                    result.errors.push_back(error(ErrorCode::execution_error, "nodes." + node->id,
                                                  "command.emit configuration was not executable", node->id));
                    trace.status = NodeStatus::filtered;
                    result.trace.push_back(std::move(trace));
                    result.status = ExecutionStatus::failed;
                    return result;
                }
                CommandMessage command;
                command.id = request.command_id;
                command.type = std::move(command_type);
                command.timestamp = request.event.timestamp;
                command.correlation_id = request.event.id;
                command.payload.emplace(std::move(payload_key), *value);
                result.commands.push_back(std::move(command));
            }
        }
        propagate(flow, *node, trace.outputs, inputs);
        result.trace.push_back(std::move(trace));
    }

    result.status = filtered ? ExecutionStatus::filtered : ExecutionStatus::completed;
    return result;
}

} // namespace openhdo::logic
