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

        ~Disk();

        // Returns number of entries in this instance.
        size_t size() const;

        static const inline uint16_t SectorSize = 0x0080;
        using SectorData = std::array<uint8_t, SectorSize>; // 128 bytes each sector

        static const inline uint8_t BSH = 0x04; // Block shift: 16 sectors per block
        static const inline uint8_t BLM = 0x0F; // Block mask
        // Given the disk geometry that we've specified in the DPH, this means that track 0 (sectors 00..7F)
        // and track 1 (sectors 00..7F) are directory entries, anything else is file data.

        // Read data from the disk (via a cache) into the supplied sector buffer
        void read(SectorData& buffer, uint16_t track, uint16_t sector) const;

        // Write data from the supplied sector buffer to the disk (via a cache)
        void write(const SectorData& buffer, uint16_t track, uint16_t sector);

    private:
        class Private;
        std::unique_ptr<Private> m_private;
    };

} // namespace zcpm
