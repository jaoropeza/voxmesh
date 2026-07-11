#include "voxmesh/audio/config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace voxmesh::audio {

namespace {

using nlohmann::json;

// Accumulates the first validation failure; parsing stops being applied but the
// walk stays simple and linear.
struct Validator {
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }

    void fail(std::string message)
    {
        if (error.empty()) {
            error = std::move(message);
        }
    }
};

std::optional<SampleFormat> parseFormat(const std::string& text)
{
    if (text == "pcm_s16le") {
        return SampleFormat::PcmS16Le;
    }
    if (text == "float32le") {
        return SampleFormat::Float32Le;
    }
    return std::nullopt;
}

void readTrackFormat(const json& node, std::string_view name, TrackFormat& out, Validator& v)
{
    if (!node.is_object()) {
        v.fail(std::string{name} + " must be an object");
        return;
    }
    if (node.contains("sample_rate_hz")) {
        const auto rate = node["sample_rate_hz"];
        if (!rate.is_number_unsigned() || rate.get<std::uint64_t>() < 4000 || rate.get<std::uint64_t>() > 384000) {
            v.fail(std::string{name} + ".sample_rate_hz must be an integer in [4000, 384000]");
            return;
        }
        out.sampleRate = SampleRate{rate.get<std::uint32_t>()};
    }
    if (node.contains("channels")) {
        const auto channels = node["channels"];
        if (!channels.is_number_unsigned() || channels.get<std::uint64_t>() < 1 || channels.get<std::uint64_t>() > 8) {
            v.fail(std::string{name} + ".channels must be an integer in [1, 8]");
            return;
        }
        out.channels = ChannelCount{channels.get<std::uint16_t>()};
    }
    if (node.contains("format")) {
        const auto format = node["format"];
        if (!format.is_string()) {
            v.fail(std::string{name} + ".format must be a string");
            return;
        }
        const auto parsed = parseFormat(format.get<std::string>());
        if (!parsed.has_value()) {
            v.fail(std::string{name} + ".format must be \"pcm_s16le\" or \"float32le\"");
            return;
        }
        out.format = *parsed;
    }
}

void readDurationMs(const json& node, std::string_view key, std::string_view name, std::chrono::milliseconds& out,
                    Validator& v)
{
    if (!node.contains(key)) {
        return;
    }
    const auto value = node[std::string{key}];
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() < 1 || value.get<std::uint64_t>() > 1000) {
        v.fail(std::string{name} + " must be an integer in [1, 1000]");
        return;
    }
    out = std::chrono::milliseconds{value.get<std::uint64_t>()};
}

} // namespace

ConfigParseResult parseRecorderConfig(std::string_view json_text)
{
    const json root = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        return {std::nullopt, "invalid JSON"};
    }
    if (!root.is_object()) {
        return {std::nullopt, "top-level JSON value must be an object"};
    }

    RecorderConfig config;
    Validator v;

    if (root.contains("archival")) {
        const json& archival = root["archival"];
        if (!archival.is_object()) {
            v.fail("archival must be an object");
        } else {
            if (archival.contains("microphone")) {
                readTrackFormat(archival["microphone"], "archival.microphone", config.archivalMicrophone, v);
            }
            if (archival.contains("system_output")) {
                readTrackFormat(archival["system_output"], "archival.system_output", config.archivalSystemOutput, v);
            }
        }
    }
    if (v.ok() && root.contains("stt")) {
        const json& stt = root["stt"];
        readTrackFormat(stt, "stt", config.sttStream, v);
        if (v.ok()) {
            readDurationMs(stt, "frame_duration_ms", "stt.frame_duration_ms", config.sttFrameDuration, v);
        }
    }
    if (v.ok() && root.contains("capture")) {
        const json& capture = root["capture"];
        if (!capture.is_object()) {
            v.fail("capture must be an object");
        } else {
            readDurationMs(capture, "frame_duration_ms", "capture.frame_duration_ms", config.captureFrameDuration, v);
            if (v.ok() && capture.contains("ring_buffer_capacity_frames")) {
                const auto capacity = capture["ring_buffer_capacity_frames"];
                if (!capacity.is_number_unsigned() || capacity.get<std::uint64_t>() < 1 ||
                    capacity.get<std::uint64_t>() > 65536) {
                    v.fail("capture.ring_buffer_capacity_frames must be an integer in [1, 65536]");
                } else {
                    config.ringBufferCapacityFrames = capacity.get<std::size_t>();
                }
            }
        }
    }

    if (!v.ok()) {
        return {std::nullopt, std::move(v.error)};
    }
    return {config, {}};
}

ConfigParseResult loadRecorderConfig(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return {std::nullopt, "cannot open config file: " + file.string()};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return parseRecorderConfig(contents.str());
}

} // namespace voxmesh::audio
