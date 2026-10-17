#pragma once
#include "RecordingTypes.hxx"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// A recording slot as a file, so a dummy recording can be kept by name and
// loaded again: {"format":"sf4e-recording","version":2,"frames":[[mapped,raw,wait,offset],...]}.
// Version 2 holds the frames as a slot does, for a fighter facing right.
// Version 1 held the pad's own directions and not the facing; it was
// recorded on the dummy, which starts on the right facing left, so that
// facing is assumed and its frames are turned on import.
namespace sf4e { namespace training {
constexpr const char* RecordingFormat = "sf4e-recording";
constexpr int RecordingVersion = 2;
// The most a named recording's file may hold: a full slot written as the
// panel writes it (indented) is over the 256 KiB other stores keep to.
constexpr std::size_t RecordingFileBytes = 4 * 1024 * 1024;
// One frame as the file holds it: [mapped, raw, wait, offset].
inline nlohmann::json EncodeFrame(const Input& frame) { return {frame.mapped, frame.raw, frame.wait, frame.offset}; }
// False when the value is not four such numbers. The wait keeps its two
// bits and the offset is held within its range.
inline bool DecodeFrame(const nlohmann::json& value, Input& frame) {
    if (!value.is_array() || value.size() != 4 || !value[0].is_number_unsigned() || !value[1].is_number_unsigned() ||
        !value[2].is_number_unsigned() || !value[3].is_number_integer()) return false;
    frame = {value[0].get<unsigned>(), value[1].get<unsigned>(), static_cast<unsigned char>(value[2].get<unsigned>() & 3),
        static_cast<signed char>((std::max)(MinOffset, (std::min)(MaxOffset, value[3].get<int>())))};
    return true;
}
inline std::string ExportRecording(const std::vector<Input>& frames) {
    nlohmann::json value = nlohmann::json::object();
    value["format"] = RecordingFormat; value["version"] = RecordingVersion;
    value["frames"] = nlohmann::json::array();
    for (const auto& frame : frames) value["frames"].push_back(EncodeFrame(frame));
    return value.dump();
}
inline bool ImportRecording(const std::string& text, std::vector<Input>& frames, std::string& error) {
    frames.clear();
    const auto value = nlohmann::json::parse(text, nullptr, false);
    if (!value.is_object()) { error = "this is not a recording"; return false; }
    const auto format = value.find("format");
    if (format == value.end() || !format->is_string() || format->get<std::string>() != RecordingFormat) { error = "this is not a recording"; return false; }
    // A file from before versions were read is a version 1 file.
    const auto version = value.value("version", nlohmann::json(1u));
    if (!version.is_number_unsigned() || version < 1) { error = "this is not a recording"; return false; }
    if (version > RecordingVersion) { error = "the recording is from a newer version"; return false; }
    const bool padDirections = version == 1;
    const auto list = value.find("frames");
    if (list == value.end() || !list->is_array() || list->empty()) { error = "the recording has no frames"; return false; }
    if (list->size() > static_cast<std::size_t>(MaxFrames)) { error = "the recording is too long"; return false; }
    for (const auto& item : *list) {
        Input frame;
        if (!DecodeFrame(item, frame)) { error = "a frame is not four numbers"; frames.clear(); return false; }
        frames.push_back(padDirections ? Mirror(frame) : frame);
    }
    return true;
}
} }
