#pragma once

#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <msgpack.hpp>
#include <google/protobuf/message.h>
#include <flatbuffers/flatbuffers.h>

namespace async_toolkit::rpc {

// Serialization format types
enum class SerializationType {
    JSON,
    PROTOBUF,
    MSGPACK,
    FLATBUFFERS
};

// Base serializer class
class Serializer {
public:
    virtual ~Serializer() = default;
    
    virtual std::string serialize(const void* data, size_t size) = 0;
    virtual bool deserialize(const std::string& data, void* result, size_t size) = 0;
    
    // Specialized serialization methods
    template<typename T>
    std::string serialize(const T& obj) {
        return serialize(&obj, sizeof(T));
    }
    
    template<typename T>
    bool deserialize(const std::string& data, T& obj) {
        return deserialize(data, &obj, sizeof(T));
    }
};

// MessagePack serializer
class MsgPackSerializer : public Serializer {
public:
    std::string serialize(const void* data, size_t) override {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, *static_cast<const msgpack::object*>(data));
        return std::string(sbuf.data(), sbuf.size());
    }

    bool deserialize(const std::string& data, void* result, size_t) override {
        try {
            msgpack::object_handle oh = 
                msgpack::unpack(data.data(), data.size());
            *static_cast<msgpack::object*>(result) = oh.get();
            return true;
        } catch (...) {
            return false;
        }
    }
};

// Protocol Buffers serializer
class ProtobufSerializer : public Serializer {
public:
    std::string serialize(const void* data, size_t) override {
        const auto* msg = static_cast<const google::protobuf::Message*>(data);
        std::string output;
        msg->SerializeToString(&output);
        return output;
    }

    bool deserialize(const std::string& data, void* result, size_t) override {
        auto* msg = static_cast<google::protobuf::Message*>(result);
        return msg->ParseFromString(data);
    }
};

// FlatBuffers serializer
class FlatBuffersSerializer : public Serializer {
public:
    std::string serialize(const void* data, size_t size) override {
        const auto* buf = static_cast<const uint8_t*>(data);
        return std::string(reinterpret_cast<const char*>(buf), size);
    }

    bool deserialize(const std::string& data, void* result, size_t) override {
        auto* buf = static_cast<flatbuffers::FlatBufferBuilder*>(result);
        buf->PushFlatBuffer(
            reinterpret_cast<const uint8_t*>(data.data()), 
            data.size()
        );
        return true;
    }
};

// JSON serializer
class JSONSerializer : public Serializer {
public:
    std::string serialize(const void* data, size_t) override {
        // Use nlohmann::json or other JSON library implementation
        return "{}";  // Placeholder implementation
    }

    bool deserialize(const std::string& data, void* result, size_t) override {
        // Use nlohmann::json or other JSON library implementation
        return true;  // Placeholder implementation
    }
};

// Serializer factory function
inline std::unique_ptr<Serializer> 
create_serializer(SerializationType type) {
    switch (type) {
        case SerializationType::MSGPACK:
            return std::make_unique<MsgPackSerializer>();
        case SerializationType::PROTOBUF:
            return std::make_unique<ProtobufSerializer>();
        case SerializationType::FLATBUFFERS:
            return std::make_unique<FlatBuffersSerializer>();
        case SerializationType::JSON:
            return std::make_unique<JSONSerializer>();
        default:
            return std::make_unique<ProtobufSerializer>();
    }
}

// RPC message header
struct RPCHeader {
    std::string service_name;
    uint32_t header_size;
    uint32_t body_size;
    uint32_t sequence_id;
    uint32_t timeout_ms;
};

// Serialize header
inline std::string serialize_header(const RPCHeader& header) {
    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, header);
    return std::string(sbuf.data(), sbuf.size());
}

// Deserialize header
inline bool deserialize_header(const std::string& data, RPCHeader& header) {
    try {
        msgpack::object_handle oh = 
            msgpack::unpack(data.data(), data.size());
        oh.get().convert(header);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace async_toolkit::rpc
