#include "disk.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <system_error>

namespace
{

    // Convert e.g. 'foo','txt' to 'FOO     TXT'
    std::string convert(const std::string& filename, const std::string& extension)
    {
        const size_t n_name = 8;
        const size_t n_extension = 3;
        // Make a copy of the filename which is limited to 8 characters and is uppercase
        const auto f(boost::to_upper_copy(filename.substr(0, n_name)));
        // Make a copy of the extension which is limited to 3 characters and is uppercase, taking
        // care to skip the leading '.' if it is present.
        const auto has_dot = !extension.empty() && extension[0] == '.';
        const auto e(boost::to_upper_copy(extension.substr(has_dot ? 1 : 0, n_extension)));

        // Build and return a 11-character space-padded version
        std::string result(n_name + n_extension, ' ');
        std::memcpy(&(result[0]), f.c_str(), f.size());
        std::memcpy(&(result[n_name]), e.c_str(), e.size());
        return result;
    }

    const inline std::uint16_t EntrySize = 0x0020;
    const inline std::uint16_t BlockSize = 0x0800;
    const inline std::uint16_t SectorsPerBlock = BlockSize / zcpm::Disk::SectorSize;

    using Location =
        std::tuple<std::uint16_t, std::uint16_t>; // Track/Sector which identify a particular sector on the disk

    // Given a block number and a sector offset, works out the actual track/sector. Assumes that sector_offset is
    // strictly within that same block, not overflowing into an adjacent block!
    Location find_location_within_block(std::uint16_t block, std::uint16_t sector_offset)
    {
        const auto s = block * SectorsPerBlock + sector_offset;
        const auto track_index = s / zcpm::Disk::SectorSize;
        const auto sector_index = s - track_index * zcpm::Disk::SectorSize;
        return { track_index, sector_index };
    }

    // Map a track/sector to a block number and offset, where the offset is the index of the sequence within the block
    // (first sector is zero, second sector is one, etc)
    std::tuple<std::uint16_t, std::uint8_t> track_sector_to_block_and_offset(std::uint16_t track, std::uint16_t sector)
    {
        const auto n = track * zcpm::Disk::SectorSize + sector;
        const auto block = n >> zcpm::Disk::BSH;
        const auto offset = n & zcpm::Disk::BLM;
        return { block, offset };
    }

} // namespace

namespace zcpm
{

    // A directory entry.  Keep in mind that a given file can have more than one entry.
    struct Entry
    {
    public:
        // Construct from a host filesystem file
        Entry(const std::filesystem::directory_entry& e,
              std::uint8_t extent,
              std::uint16_t sectors,
              std::uint16_t first_block)
            : m_raw_name(e.path().filename()),
              m_name(convert(e.path().stem(), e.path().extension())),
              m_exists(true),
              m_size(std::filesystem::file_size(e)),
              m_sectors(std::min<size_t>(sectors, 0x0080)),
              m_extent(extent),
              m_first_block(first_block)
        {
        }

        // Construct from a CP/M directory entry (16 bytes)
        explicit Entry(const std::uint8_t* buffer)
            : m_modified(true) // Needs to be flushed on shutdown because it is not (originally) a host filesystem file
        {
            m_exists = (buffer[0x00] != 0xE5);
            m_name.assign(reinterpret_cast<const char*>(buffer + 1), 11);
            m_raw_name = boost::to_lower_copy(boost::trim_right_copy(m_name.substr(0, 8)) + "." +
                                              boost::trim_right_copy(m_name.substr(8, 3)));
            m_extent = buffer[0x0C];
            m_sectors = buffer[0x0F];
            for (size_t i = 0; i < 8; i++)
            {
                const std::uint16_t block = buffer[0x10 + i * 2 + 0] | (buffer[0x10 + i * 2 + 1] << 8);
                if (block > 0)
                {
                    m_blocks.push_back(block);
                }
            }
        }

        void show() const
        {
            std::stringstream blocks;
            for (const auto& b : m_blocks)
            {
                blocks << ' ' << b;
            }
            BOOST_LOG_TRIVIAL(trace) << "  '" << m_raw_name << "' '" << m_name << "' Size=" << m_size
                                     << " Sectors=" << m_sectors << " Extent=" << m_extent
                                     << " FirstBlock=" << m_first_block << " [" << blocks.str()
                                     << " ] Exists:" << (m_exists ? 'Y' : 'N');
        }

        std::string m_raw_name; // e.g. "file.txt"
        std::string m_name;     // e.g. "FILE    TXT"
        bool m_exists = false;  // True normally, false if the file has been deleted
        size_t m_size = 0;      // Size in bytes; for the *whole* file
        size_t m_sectors = 0;   // Number of sectors in *this* extent
        size_t m_extent = 0;    // Extent number (always zero for small files, can be other for bigger files)
        std::uint16_t m_first_block =
            0; // What is the first block number of this file (across *all* of its extents/entries)
        std::vector<std::uint16_t>
            m_blocks;            // Block indexes allocated for this entry for this file.  Always contiguous!!!
        bool m_modified = false; // Does this need to be "flushed" to the host filesystem on completion?
    };

    struct SectorInfo
    {
        explicit SectorInfo(const Disk::SectorData& buffer) : m_data(buffer)
        {
        }

        void get_data(Disk::SectorData& buffer) const // Copy from cache into buffer
        {
            buffer = m_data;
        }

        void put_data(const Disk::SectorData& buffer) // Copy from buffer into cache
        {
            m_data = buffer;
            m_dirty = true;
        }

        Disk::SectorData m_data;
        bool m_dirty{ false };
    };

    // Implementation

    class Disk::Private final
    {
        std::vector<Entry> m_entries;
        // TODO: To speed up searches for files, consider adding a map of block number to Entry instance,
        // so that way we don't need to do a linear search on every block query.

        mutable std::map<Location, SectorInfo> m_sector_cache; // Cache of sectors that we know about

        std::uint16_t m_next_block = 0x0010;

    public:
        Private()
        {
            build_directory();
        }

        Private(const Private&) = delete;
        Private& operator=(const Private&) = delete;
        Private(Private&&) = delete;
        Private& operator=(Private&&) = delete;

        ~Private()
        {
            try
            {
                flush_to_host_filesystem();
            }
            catch (const std::exception& e)
            {
                std::cerr << "Exception during flush: " << e.what() << std::endl;
            }
        }

        size_t size() const
        {
            return m_entries.size();
        }

        void read(SectorData& buffer, std::uint16_t track, std::uint16_t sector) const
        {
            // First see if the specific sector is in the sector cache
            const Location location{ track, sector };
            const auto it = m_sector_cache.find(location);
            if (it != m_sector_cache.end())
            {
                it->second.get_data(buffer);
                return;
            }

            // It's not in the cache, so we need to go to the underlying (host) disk

            // Is it a directory entry or general data?
            if ((track == 0) || (track == 1))
            {
                // Construct 4 directory entries in the buffer; the choice of *which* directory
                // entries is determined by the track/sector values.
                create_directory_entries(buffer, track, sector);
            }
            else
            {
                // File data.  We use the directory entries to work out which bit of which file corresponds
                // to the requested track/sector
                read_disk_data(buffer, track, sector);
            }

            // Add the sector we've read (or synthesised) into the sector cache.  Fortunately CP/M disks
            // are not large, so we can comfortably have this in memory.
            SectorInfo sector_info(buffer);
            m_sector_cache.emplace(location, sector_info);
        }

        void write(const SectorData& buffer, std::uint16_t track, std::uint16_t sector)
        {
            // Is it a directory entry or general data?
            if ((track == 0) || (track == 1))
            {
                // BDOS is modifying directory entries.
                // Work out what has changed and then update m_entries accordingly
                check_for_directory_changes(buffer);
            }

            // Write the sector to the cache
            write_disk_data(buffer, track, sector);
        }

        void build_directory(const std::string& dir = ".")
        {
            const auto extent_size = 0x0080 * 0x0080; // 16KB

            // Iterate through cwd and store file info (not directories) into m_entries; there can be
            // more than one entry per file if the file is large.
            // Note that we deliberately ignore zcpm.log because that can get confusing.
            for (const auto& item : std::filesystem::directory_iterator(dir))
            {
                if (std::filesystem::is_regular_file(item.path()))
                {
                    const auto bytes = std::filesystem::file_size(item);
                    if (item.path().filename() != "zcpm.log") // TODO: Yuk
                    {
                        const auto num_entries = (bytes + extent_size - 1) / extent_size;
                        const auto first_block =
                            m_next_block; // Need to remember first block across *all* entries/extents
                        auto remaining_sectors = (bytes + SectorSize - 1) / SectorSize;
                        for (size_t i = 0; i < num_entries; i++)
                        {
                            Entry e(item, i, remaining_sectors, first_block);
                            const auto num_blocks_this_entry = (e.m_sectors + SectorsPerBlock - 1) / SectorsPerBlock;
                            for (size_t j = 0; j < num_blocks_this_entry; ++j)
                            {
                                e.m_blocks.push_back(m_next_block++);
                            }
                            remaining_sectors -= e.m_sectors;
                            m_entries.push_back(e);
                        }
                    }
                }
            }

            BOOST_LOG_TRIVIAL(trace) << m_entries.size() << " directory entries:";
            for (const auto& e : m_entries)
            {
                e.show();
            }
        }

        void create_directory_entries(SectorData& buffer, std::uint16_t track, std::uint16_t sector) const
        {
            const auto index = (track * SectorSize + sector) * 4;

            for (auto i = 0; i < SectorSize / EntrySize; i++)
            {
                format_directory_entry(buffer.data() + i * EntrySize, index + i);
            }
        }

        void read_disk_data(SectorData& buffer, std::uint16_t track, std::uint16_t sector) const
        {
            // TODO: Given that the most common use case is to read sector N+1 of a given file immediately after
            // sector N of that same file, we could optimise this by keeping the file handle open and the file
            // position maintained from call to call, and only close it if the active file/position changes.

            // Convert the track/sector into a block number.
            const auto [block, offset] = track_sector_to_block_and_offset(track, sector);

            // Find that block number in the directory structure.  TODO: This uses a brute-force
            // search, once this code matures we should add a new map from block number to File
            // instance to avoid the need for this brute-force search.
            for (const auto& f : m_entries)
            {
                for (const auto& b : f.m_blocks)
                {
                    if (block == b)
                    {
                        // We've found that the block we're looking for is in this file.  Open the file and
                        // read & return the right chunk from it.  We need to compare "this" block index against
                        // the first block index for this file (not just for this entry!)
                        const auto block_offset = block - f.m_first_block;
                        const auto chunk = (block_offset << BSH) + offset;
                        if (auto fp = std::fopen(f.m_raw_name.c_str(), "r"); fp)
                        {
                            std::fseek(fp, chunk * SectorSize, SEEK_SET);
                            std::fread(buffer.data(), 01, buffer.size(), fp);
                            std::fclose(fp);
                            BOOST_LOG_TRIVIAL(trace) << "Reading chunk #" << chunk << " from " << f.m_raw_name;
                        }
                        else
                        {
                            throw std::system_error(errno, std::generic_category(), "File open failed");
                        }
                        return;
                    }
                }
            }

            BOOST_LOG_TRIVIAL(trace) << "WARNING: Can't find file for this sector";
        }

        void write_disk_data(const SectorData& buffer, std::uint16_t track, std::uint16_t sector)
        {
            // Do we have a cache entry for this sector?
            const auto location(Location{ track, sector });

            const auto it = m_sector_cache.find(location);
            if (it != m_sector_cache.end())
            {
                // Yes, already in cache.  In that case we modify it
                it->second.put_data(buffer);
            }
            else
            {
                // No, new entry, create it.
                SectorInfo sector_info(buffer);
                sector_info.m_dirty = true;
                m_sector_cache.emplace(location, sector_info);
            }
        }

        // Given a memory location, formats the nth (counting from zero) directory entry (of size EntrySize) into that
        // location. Allows out of range values of 'n' which get mapped to E5 (inactive) file entries.  Note that this
        // may actually cause more than one entry to be created, in the case where a file requires multiple
        // entries/extents.
        void format_directory_entry(std::uint8_t* base, std::uint16_t n) const
        {
            if (n >= m_entries.size())
            {
                // An out of range directory entry gets created as 'inactive'
                for (auto i = 0; i < EntrySize; i++)
                {
                    base[i] = 0xE5;
                }

                return;
            }

            const auto& f(m_entries[n]);

            // See http://primrosebank.net/computers/cpm/cpm_structure.htm,
            // or David Coresi's "Inside CP/M" page 221, or http://www.seasip.info/Cpm/format22.html

            // Initially filled with nulls
            for (auto i = 0; i < EntrySize; i++)
            {
                base[i] = 0x00;
            }

            const std::uint8_t user = 0x00; // ZCPM only uses user 0 at this stage
            const bool exists = f.m_exists;

            // Byte 0: user code.  E5 means inactive (or deleted), 00..0F is a user code.
            base[0x00] = exists ? user : 0xe5;

            // filename.ext
            for (size_t i = 0; i < f.m_name.size(); i++)
            {
                base[0x01 + i] = f.m_name[i];
            }
            // TODO: top bits of the extension indicate these flags: read-only, invisible, backup.  We should expand
            // this method to allow them to be specified.

            base[0x0C] = f.m_extent &
                         0x1F; // EX (TODO: mask & shift determined empirically; but where do magic values come from?)
            base[0x0D] = 0x00; // S1 (Reserved, set to zero)
            base[0x0E] = (f.m_extent >> 5) &
                         0xFF; // S2 (TODO: mask & shift determined empirically; but where do magic values come from?)
            base[0x0F] = f.m_sectors; // RC (record count)

            // Disk map.  Each 2-byte entry is a block number (each block is 2KB aka BlockSize)
            for (size_t i = 0; i < f.m_blocks.size(); i++)
            {
                const auto block = f.m_blocks[i];
                base[0x10 + i * 2 + 0] = block & 0xFF;
                base[0x10 + i * 2 + 1] = (block >> 8) & 0xFF;
            }
        }

        // BDOS appears to be modifying a directory sector; work out what has changed and what we need to do
        void check_for_directory_changes(const SectorData& buffer)
        {
            // For each of the four entries in this directory sector build a 'pending' Entry for each,
            // and then compare these against our 'real' entries to see what needs changing.

            for (auto i = 0; i < SectorSize / EntrySize; i++)
            {
                Entry pending(buffer.data() + i * EntrySize);
                if (pending.m_exists)
                {
                    BOOST_LOG_TRIVIAL(trace) << "Considering pending entry:";
                    pending.show();

                    // Walk through m_entries to work out what action is required for this item, if any
                    bool found = false;
                    for (auto& e : m_entries)
                    {
                        if ((e.m_name == pending.m_name) && (e.m_extent == pending.m_extent) &&
                            (e.m_blocks == pending.m_blocks))
                        {
                            BOOST_LOG_TRIVIAL(trace) << "  (no action required)";
                            found = true;
                        }
                        else if ((e.m_name == pending.m_name) && (e.m_extent == pending.m_extent) &&
                                 (e.m_blocks != pending.m_blocks))
                        {
                            BOOST_LOG_TRIVIAL(trace) << "  (content modification)";
                            // NOTE: The following is a best-effort, there may be some tweaks needed here
                            e.m_sectors = pending.m_sectors;
                            e.m_blocks = pending.m_blocks;
                            e.m_size = e.m_sectors * SectorSize;
                            e.m_first_block = m_next_block++;
                            e.m_modified = true;
                            found = true;
                        }
                        else if (e.m_exists && (e.m_name != pending.m_name) && (e.m_extent == pending.m_extent) &&
                                 (e.m_blocks == pending.m_blocks) && !pending.m_blocks.empty())
                        {
                            BOOST_LOG_TRIVIAL(trace)
                                << "  (rename of '" << e.m_raw_name << "' to '" << pending.m_raw_name << "')";
                            e.m_name = pending.m_name;
                            e.m_raw_name = pending.m_raw_name;
                            e.m_modified = true;
                            found = true;
                        }
                        if (found)
                        {
                            break;
                        }
                    }

                    if (!found)
                    {
                        BOOST_LOG_TRIVIAL(trace) << "  (file creation)";
                        // Add this newly-created entry to our overall collection.
                        m_entries.push_back(pending);
                    }
                }
                else
                {
                    // Is it a file deletion?

                    // Walk through m_entries to find the matching item that needs to be deleted
                    for (auto& e : m_entries)
                    {
                        if ((e.m_name == pending.m_name) && e.m_exists && !pending.m_exists &&
                            (e.m_extent == pending.m_extent) && (e.m_blocks == pending.m_blocks))
                        {
                            BOOST_LOG_TRIVIAL(trace) << "  (deletion):";
                            pending.show();
                            e.m_exists = false;
                            e.m_modified = true;
                            break;
                        }
                    }
                }
            }
        }

        void flush_to_host_filesystem() const
        {
            // First take care of any directory-level changes; i.e., new files, deleted files, ...
            flush_file_changes_to_host_filesystem();

            // And then take care of any remaining dirty sectors in the cache; these typically are
            // the result of a WRITERAND to existing files.
            flush_changed_sectors_to_host_filesystem();
        }

        void flush_file_changes_to_host_filesystem() const
        {
            // TODO: We don't yet cater for a *renamed* host file

            for (const auto& e : m_entries)
            {
                if (e.m_modified)
                {
                    BOOST_LOG_TRIVIAL(trace) << "Flush '" << e.m_raw_name << "' to host filesystem:";
                    e.show();
                    if (e.m_exists)
                    {
                        if (auto fp = std::fopen(e.m_raw_name.c_str(), "w"); fp)
                        {
                            auto sectors_remaining = e.m_sectors;
                            for (const auto& b : e.m_blocks)
                            {
                                const auto sectors_this_block =
                                    std::min<std::uint16_t>(SectorsPerBlock, sectors_remaining);
                                BOOST_LOG_TRIVIAL(trace)
                                    << "Writing " << sectors_this_block << " sector from block #" << b;
                                for (auto i = 0; i < sectors_this_block; i++)
                                {
                                    const auto [track, sector] = find_location_within_block(b, i);
                                    BOOST_LOG_TRIVIAL(trace)
                                        << fmt::format("  Using data from TRACK:{:04X} SECTOR:{:04X}", track, sector);
                                    const auto it = m_sector_cache.find({ track, sector });
                                    if (it != m_sector_cache.end())
                                    {
                                        SectorData data{};
                                        it->second.get_data(data);
                                        if (std::fwrite(data.data(), sizeof(SectorData::value_type), data.size(), fp) <
                                            data.size())
                                        {
                                            BOOST_LOG_TRIVIAL(trace) << "TODO: File write error handling";
                                        }
                                        it->second.m_dirty =
                                            false; // No longer 'dirty' in the cache because it has now been flushed
                                    }
                                    else
                                    {
                                        BOOST_LOG_TRIVIAL(trace) << "TODO: file data not in cache, now what?";
                                    }
                                }
                            }
                            std::fclose(fp);
                        }
                        else
                        {
                            throw std::system_error(errno, std::generic_category(), "File open failed");
                        }
                    }
                    else
                    {
                        // The modified flag has the 'exists' flag set to false, which means that is a deletion.
                        // But we need to be careful where we have an entry for a newly-deleted file *and* one
                        // for an existing file with the same name.  If this is the case, we don't delete anything.
                        const auto has_existing_version =
                            std::any_of(m_entries.begin(),
                                        m_entries.end(),
                                        [e](auto f) { return f.m_exists && (f.m_raw_name == e.m_raw_name); });

                        if (has_existing_version)
                        {
                            BOOST_LOG_TRIVIAL(trace) << "(not erasing because an existing one is still present)";
                        }
                        else
                        {
                            BOOST_LOG_TRIVIAL(trace) << "(erasing it if it still exists)";
                            std::error_code ec;
                            std::filesystem::remove(e.m_raw_name, ec); // Ignore any error, doesn't really matter
                        }
                    }
                }
            }
        }

        void flush_changed_sectors_to_host_filesystem() const
        {
            for (const auto& [key, value] : m_sector_cache)
            {
                if (value.m_dirty)
                {
                    const auto& [track, sector] = key;
                    if (track > 1)
                    {
                        // Work out what file owns this sector
                        const auto [block, offset] = track_sector_to_block_and_offset(track, sector);
                        for (const auto& f : m_entries)
                        {
                            if (f.m_exists)
                            {
                                // TODO: Can we rewrite the following find/loop using an STL algorithm?
                                for (size_t i = 0; i < f.m_blocks.size(); i++)
                                {
                                    if (f.m_blocks[i] == block)
                                    {
                                        BOOST_LOG_TRIVIAL(trace) << fmt::format(
                                            "Sector {:02X}:{:02X} is block {:d} offset {:d} within file {}",
                                            track,
                                            sector,
                                            block,
                                            offset,
                                            f.m_raw_name);
                                        try
                                        {
                                            flush_changed_file(value, block, offset, f);
                                        }
                                        catch (const std::exception& e)
                                        {
                                            BOOST_LOG_TRIVIAL(trace) << "Exception during file flush: " << e.what();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        void flush_changed_file(const SectorInfo& value, std::uint16_t block, std::uint8_t offset, const Entry& f) const
        {
            const auto byte_offset = (((block - f.m_first_block) << BSH) + offset) * SectorSize;

            // Note that we open/modify/close the file for *each* modified sector.  In a big system with lots of changes
            // this is very inefficient, but in the typical use case for ZCPM we see just a handful of *modified*
            // sectors, most file I/O is new files which uses different logic.

            if (auto fp = std::fopen(f.m_raw_name.c_str(), "rb+"); fp)
            {
                std::fseek(fp, byte_offset, SEEK_SET);

                SectorData data{};
                value.get_data(data);
                if (std::fwrite(data.data(), sizeof(SectorData::value_type), data.size(), fp) < SectorSize)
                {
                    BOOST_LOG_TRIVIAL(trace) << "TODO: File write error handling";
                }
                std::fclose(fp);
            }
            else
            {
                // Note that throwing this would cause us to lose data from being flushed for subsequent modified files
                throw std::system_error(errno, std::generic_category(), "File open failed");
            }
        }
    };

    // Facade

    Disk::Disk() : m_private(std::make_unique<Private>())
    {
    }

    Disk::~Disk() = default;

    size_t Disk::size() const
    {
        return m_private->size();
    }

    void Disk::read(SectorData& buffer, std::uint16_t track, std::uint16_t sector) const
    {
        m_private->read(buffer, track, sector);
    }

    void Disk::write(const SectorData& buffer, std::uint16_t track, std::uint16_t sector)
    {
        m_private->write(buffer, track, sector);
    }

} // namespace zcpm
