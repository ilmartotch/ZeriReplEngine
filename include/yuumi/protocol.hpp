#pragma once
#include <yuumi/types.hpp>
#include <nlohmann/json.hpp>
#include <bit>
#include <span>
#include <vector>
#include <cstring>
#include <string>

namespace yuumi {

    using Json = nlohmann::json;

    /**
     * @brief Gestore del formato dei pacchetti Yuumi.
     * Header: [Length (4B Little Endian)][Channel (1B)][Flags (1B)][Payload (NB UTF-8 JSON)]
     */
    class Protocol {
    public:
        // Flags per funzionalità avanzate
        enum Flags : uint8_t {
            None = 0,
            Compressed = 1 << 0, // Indica se il payload è compresso
            Encrypted  = 1 << 1  // Per utilizzi futuri
        };

        /**
         * @brief Serializza Json -> UTF-8 JSON -> Packet.
         */
        static std::vector<std::byte> encode(const Json& j, Channel ch = Channel::Command) {
            const std::string raw_data = j.dump();
            uint32_t length = static_cast<uint32_t>(raw_data.size());
            uint8_t flags = Flags::None;

            // Logica di compressione (Placeholder: qui andrebbe la chiamata a LZ4/Zstd)
            // if (length > 4096) { flags |= Flags::Compressed; ... }

            // Protocol Order (Little Endian)
            uint32_t wire_length = (std::endian::native == std::endian::big) ? std::byteswap(length) : length;

            std::vector<std::byte> packet(6 + raw_data.size());
            std::memcpy(packet.data(), &wire_length, 4);
            packet[4] = static_cast<std::byte>(ch);
            packet[5] = static_cast<std::byte>(flags);
            std::memcpy(packet.data() + 6, raw_data.data(), raw_data.size());

            return packet;
        }

        /**
         * @brief Decodifica un buffer UTF-8 JSON in un oggetto JSON.
         */
        static Result<Json> decode(std::span<const std::byte> buffer) {
            try {
                const auto* data = reinterpret_cast<const char*>(buffer.data());
                const std::string raw_json(data, buffer.size());
                return Json::parse(raw_json);
            } catch (...) {
                return std::unexpected(Error::ProtocolViolation);
            }
        }

        /**
         * @brief Validatore leggero per il JSON in ingresso.
         * Verifica la presenza dei campi necessari per la logica di business.
         */
        static bool validate_schema(const Json& j, std::initializer_list<std::string_view> required_keys) {
            for (auto key : required_keys) {
                if (!j.contains(key)) return false;
            }
            return true;
        }
    };
}
