#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openhdo::logic {

inline constexpr std::int32_t kContractVersion = 1;
inline constexpr std::string_view kLogicSource = "server-logic";

inline constexpr std::size_t kMaxNodes = 256;
inline constexpr std::size_t kMaxConnections = 512;
inline constexpr std::size_t kMaxPortsPerNode = 32;
inline constexpr std::size_t kMaxPayloadFields = 64;
inline constexpr std::size_t kMaxIdentifierLength = 128;
inline constexpr std::size_t kMaxTextLength = 4096;

enum class ValueType {
    boolean,
    integer,
    number,
    text,
};

class Value {
public:
    using Storage = std::variant<bool, std::int64_t, double, std::string>;

    static Value boolean(bool value);
    static Value integer(std::int64_t value);
    static Value number(double value);
    static Value text(std::string value);

    [[nodiscard]] ValueType type() const noexcept;
    [[nodiscard]] const Storage& storage() const noexcept;

    friend bool operator==(const Value&, const Value&) = default;

private:
    explicit Value(Storage value);

    Storage value_;
};

using Payload = std::map<std::string, Value>;

enum class PortDirection {
    input,
    output,
};

struct Port {
    std::string id;
    PortDirection direction;
    ValueType type;
    bool required = true;
};

struct Node {
    std::string id;
    std::string kind;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    Payload config;
};

struct Connection {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
};

struct Flow {
    std::int32_t version = kContractVersion;
    std::string id;
    std::vector<Node> nodes;
    std::vector<Connection> connections;
};

struct EventMessage {
    std::int32_t version = kContractVersion;
    std::string id;
    std::string type;
    std::string timestamp;
    std::string source;
    Payload payload;
};

struct CommandMessage {
    std::int32_t version = kContractVersion;
    std::string id;
    std::string type;
    std::string timestamp;
    std::string source = std::string{kLogicSource};
    std::string correlation_id;
    Payload payload;
};

struct ExecutionRequest {
    std::int32_t version = kContractVersion;
    std::string execution_id;
    std::string command_id;
    EventMessage event;
};

enum class ErrorCode {
    invalid_contract_version,
    invalid_identifier,
    invalid_message_id,
    invalid_message_type,
    invalid_timestamp,
    invalid_source,
    invalid_payload,
    size_limit_exceeded,
    duplicate_identifier,
    unsupported_node_kind,
    invalid_port,
    missing_node,
    missing_port,
    wrong_port_direction,
    type_mismatch,
    duplicate_connection,
    multiple_input_connections,
    missing_required_input,
    cycle_detected,
    invalid_configuration,
    multiple_command_sinks,
    execution_error,
};

struct Error {
    ErrorCode code;
    std::string path;
    std::string message;
    std::string node_id;
};

struct ValidationResult {
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

enum class NodeStatus {
    executed,
    filtered,
};

struct NodeTrace {
    std::string node_id;
    NodeStatus status;
    Payload outputs;
};

enum class ExecutionStatus {
    completed,
    filtered,
    rejected,
    failed,
};

struct ExecutionResult {
    std::int32_t version = kContractVersion;
    std::string execution_id;
    ExecutionStatus status = ExecutionStatus::failed;
    std::vector<NodeTrace> trace;
    std::vector<CommandMessage> commands;
    std::vector<Error> errors;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == ExecutionStatus::completed || status == ExecutionStatus::filtered;
    }
};

inline constexpr std::string_view kEventTriggerKind = "event.trigger";
inline constexpr std::string_view kConstantKind = "value.constant";
inline constexpr std::string_view kCommandEmitKind = "command.emit";

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(ExecutionStatus status) noexcept;
[[nodiscard]] std::string_view to_string(NodeStatus status) noexcept;

[[nodiscard]] ValidationResult validate_flow(const Flow& flow);
[[nodiscard]] ValidationResult validate_event(const EventMessage& event);
[[nodiscard]] ValidationResult validate_request(const ExecutionRequest& request);

class FlowEngine {
public:
    [[nodiscard]] ExecutionResult execute(const Flow& flow, const ExecutionRequest& request) const;
};

} // namespace openhdo::logic
