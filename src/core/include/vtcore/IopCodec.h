#pragma once

#include "vtcore/ObjectPool.h"
#include "vtcore/VtObject.h"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace vt {

// Raised when binary pool data cannot be parsed. offset() is the byte position in the input.
class IopDecodeError : public std::runtime_error {
public:
    IopDecodeError(const std::string& message, std::size_t offset);
    std::size_t offset() const { return m_offset; }

private:
    std::size_t m_offset;
};

// Raised when an object cannot be represented in the record format (e.g. too many list entries).
class IopEncodeError : public std::runtime_error {
public:
    IopEncodeError(const std::string& message, ObjectId object);
    ObjectId object() const { return m_object; }

private:
    ObjectId m_object;
};

// Serialises one object as a variable length record (ISO 11783-6 Annex B, Figure C.1).
std::vector<std::uint8_t> encodeObject(const VtObject& object);

// Serialises the whole pool: all object records concatenated in pool order. This is the content
// of an .iop file and of the Object Pool Transfer messages (without the function code byte).
std::vector<std::uint8_t> encodePool(const ObjectPool& pool);

// Parses one object record starting at offset; offset is advanced past the record.
VtObject decodeObject(std::span<const std::uint8_t> bytes, std::size_t& offset);

// Parses a complete pool. The VT version is not stored in the binary data and must be given.
ObjectPool decodePool(std::span<const std::uint8_t> bytes, VtVersion version);

} // namespace vt
