#pragma once

#include "vtcore/Exporters.h"
#include "vtcore/ObjectPool.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

// Target terminal the designer previews the pool for (Data Mask area and Soft Key designators).
struct DesignerSettings {
    int dataMaskSize = 480;    // square Data Mask area in pixels (ISO 11783-6 4.5.2, minimum 200)
    int softKeyWidth = 80;     // Soft Key designator width (minimum 60)
    int softKeyHeight = 80;    // Soft Key designator height (minimum 32)
    int physicalSoftKeys = 6;  // keys per column in the preview

    bool operator==(const DesignerSettings&) const = default;
};

// A designer project: the object pool plus everything that is not part of the binary pool.
struct Project {
    Project() = default;
    explicit Project(ObjectPool objectPool)
        : pool(std::move(objectPool))
    {
    }

    ObjectPool pool;
    ExportOptions exportOptions;
    DesignerSettings designer;
};

class ProjectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Project files (.vtp) are UTF-8 JSON, so they diff well in version control.
// Text values of strings are stored readable: 8-bit strings byte-for-byte as ISO 8859-1 code points,
// WideStrings (UTF-16 with byte order mark) under "wideText". Other binary data is base64 encoded.
std::string projectToJson(const Project& project);
Project projectFromJson(std::string_view json); // throws ProjectError

void saveProject(const Project& project, const std::filesystem::path& path);
Project loadProject(const std::filesystem::path& path);

// Helpers shared with the CLI and the editor.
std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);
void writeBinaryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
void writeTextFile(const std::filesystem::path& path, std::string_view text);

} // namespace vt
