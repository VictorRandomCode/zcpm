#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace zcpm
{

class Disk final
{
public:
    // Reads from cwd on instantiation.
    Disk();

    Disk(const Disk&) = delete;
    Disk& operator=(const Disk&) = delete;
    Disk(Disk&&) = delete;
    Disk& operator=(Disk&&) = delete;

    ~Disk();

    // Returns number of entries in this instance.
    size_t size() const;

    static const inline std::uint16_t SectorSize{ 0x0080 };
    using SectorData = std::array<std::uint8_t, SectorSize>; // 128 bytes each sector

    static const inline std::uint8_t BSH{ 0x04 }; // Block shift: 16 sectors per block
    static const inline std::uint8_t BLM{ 0x0F }; // Block mask
    // Given the disk geometry that we've specified in the DPH, this means that track 0 (sectors 00..7F)
    // and track 1 (sectors 00..7F) are directory entries, anything else is file data.

    // Read data from the disk (via a cache) into the supplied sector buffer
    void read(SectorData& buffer, std::uint16_t track, std::uint16_t sector) const;

    // Write data from the supplied sector buffer to the disk (via a cache)
    void write(const SectorData& buffer, std::uint16_t track, std::uint16_t sector);

private:
    class Private;
    std::unique_ptr<Private> m_private;
};

} // namespace zcpm
