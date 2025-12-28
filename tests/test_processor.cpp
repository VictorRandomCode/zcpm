// #define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MAIN // in only one cpp file
#include <boost/test/unit_test.hpp>

#include <zcpm/core/processor.hpp>

// This module tests some CPU/register functionality. Note that it's not practical to test all combinations, this
// test code aims to cover a useful sample to test for breakage. If a *full* test is needed, execute the 'zexall.com'
// binary via the 'runner' tool, and be patient...

namespace
{

// Define a local mock to assist with testing

struct Hardware
    : public zcpm::IMemory
    , public zcpm::IProcessorObserver
{
    Hardware() : m_processor(std::make_unique<zcpm::Processor>(*this, *this))
    {
    }

    void load_memory_and_set_pc(std::uint16_t address, const std::vector<std::uint8_t>& bytes)
    {
        copy_to_ram(bytes.data(), bytes.size(), address);
        m_processor->reg_pc() = address;
    }

    // Implements IMemory

    std::uint8_t read_byte(std::uint16_t address) const override
    {
        const auto result = m_memory[address];
        return result;
    }

    std::uint8_t read_byte(std::uint16_t address, size_t& elapsed_cycles) const override
    {
        const auto result = read_byte(address);
        elapsed_cycles += 3;
        return result;
    }

    std::uint16_t read_word(std::uint16_t address) const override
    {
        const auto result_low = m_memory[address];
        const auto result_high = m_memory[(address + 1) & 0xffff];
        const auto result = result_low | (result_high << 8);
        return result;
    }

    std::uint16_t read_word(std::uint16_t address, size_t& elapsed_cycles) const override
    {
        const auto result = read_word(address);
        elapsed_cycles += 6;
        return result;
    }

    void write_byte(std::uint16_t address, std::uint8_t x) override
    {
        m_memory[address] = x;
    }

    void write_byte(std::uint16_t address, std::uint8_t x, size_t& elapsed_cycles) override
    {
        write_byte(address, x);
        elapsed_cycles += 3;
    }

    void write_word(std::uint16_t address, std::uint16_t x) override
    {
        m_memory[address] = x;
        m_memory[(address + 1) & 0xffff] = x >> 8;
    }

    void write_word(std::uint16_t address, std::uint16_t x, size_t& elapsed_cycles) override
    {
        write_word(address, x);
        elapsed_cycles += 6;
    }

    std::uint8_t read_byte_step(std::uint16_t& address, size_t& elapsed_cycles) const override
    {
        const auto result = read_byte(address);
        address++;
        elapsed_cycles += 3;
        return result;
    }

    std::uint16_t read_word_step(std::uint16_t& address, size_t& elapsed_cycles) const override
    {
        const auto result = read_word(address);
        address += 2;
        elapsed_cycles += 6;
        return result;
    }

    void push(std::uint16_t x, size_t& elapsed_cycles) override
    {
        m_processor->reg_sp() -= 2;
        write_word(m_processor->reg_sp(), x, elapsed_cycles);
    }

    std::uint16_t pop(size_t& elapsed_cycles) override
    {
        const auto result = read_word(m_processor->reg_sp(), elapsed_cycles);
        m_processor->reg_sp() += 2;
        return result;
    }

    std::uint8_t input_byte(int port) override
    {
        // TODO
        return 0;
    }

    void output_byte(int port, std::uint8_t x) override
    {
        // TODO
    }

    void copy_to_ram(const std::uint8_t* buffer, size_t count, std::uint16_t base) override
    {
        if (!buffer || !count)
        {
            return;
        }

        // Limit the number of bytes so that we don't try to write past the end of RAM
        count = std::min(count, m_memory.size() - base);

        // TODO: This is exceedingly ugly, find a cleaner (but efficient!) solution
        std::memcpy(m_memory.data() + base, buffer, count);
    }

    void copy_from_ram(std::uint8_t* buffer, size_t count, std::uint16_t base) const override
    {
        if (!buffer || !count)
        {
            return;
        }

        // Limit the number of bytes so that we don't try to read past the end of RAM
        count = std::min(count, m_memory.size() - base);

        // TODO: This is exceedingly ugly, find a cleaner (but efficient!) solution
        std::memcpy(buffer, m_memory.data() + base, count);
    }

    void dump(std::uint16_t base, size_t count) const override
    {
        // TODO
    }

    void check_memory_accesses(bool protect) override
    {
        // TODO
    }

    // Implements IProcessorObserver

    void set_finished(bool) override
    {
    }

    [[nodiscard]] bool running() const override
    {
        return true;
    }

    bool check_and_handle_bdos_and_bios(std::uint16_t address) const override
    {
        return false; // TODO
    }

    std::unique_ptr<zcpm::Processor> m_processor;

    std::array<std::uint8_t, 0x10000> m_memory{};
};

// Perform a single instruction on the A register
void test_8bit_register_instruction(std::uint8_t instruction,
                                    size_t expected_cycles,
                                    std::uint8_t initial_value,
                                    std::uint8_t expected_value,
                                    std::uint8_t expected_flags)
{
    Hardware hardware;

    hardware.m_processor->reg_a() = initial_value;
    hardware.m_processor->reg_f() = 0x00;

    hardware.load_memory_and_set_pc(0x0005, { instruction });

    auto cycles = hardware.m_processor->emulate_instruction();
    BOOST_CHECK_EQUAL(cycles, expected_cycles);

    // The only change should be an incremented PC
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_a(), expected_value);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_f(), expected_flags);
}
} // namespace

BOOST_AUTO_TEST_CASE(test_register_read_write)
{
    Hardware hardware;

    // Set registers as 16-bit values
    hardware.m_processor->reg_af() = 0x1234;
    hardware.m_processor->reg_bc() = 0x2345;
    hardware.m_processor->reg_de() = 0x3456;
    hardware.m_processor->reg_hl() = 0x4567;
    hardware.m_processor->reg_sp() = 0x5678;
    hardware.m_processor->reg_pc() = 0x6789;

    // Read them back 16-bits at a time
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_af(), 0x1234);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_bc(), 0x2345);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_de(), 0x3456);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_hl(), 0x4567);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_sp(), 0x5678);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_pc(), 0x6789);

    // Reading them back 8 bits at a time
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_a(), 0x12);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_f(), 0x34);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_b(), 0x23);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_c(), 0x45);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_d(), 0x34);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_e(), 0x56);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_h(), 0x45);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_l(), 0x67);

    // Set registers as 8-bit values
    hardware.m_processor->reg_a() = 0x21;
    hardware.m_processor->reg_f() = 0x32;
    hardware.m_processor->reg_b() = 0x43;
    hardware.m_processor->reg_c() = 0x54;
    hardware.m_processor->reg_h() = 0x65;
    hardware.m_processor->reg_l() = 0x76;

    // Reading them back 8 bits at a time
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_a(), 0x21);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_f(), 0x32);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_b(), 0x43);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_c(), 0x54);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_h(), 0x65);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_l(), 0x76);

    // Read them back 16-bits at a time
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_af(), 0x2132);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_bc(), 0x4354);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_hl(), 0x6576);
}

BOOST_AUTO_TEST_CASE(test_single_instruction_nop)
{
    Hardware hardware;

    // Set all registers to known values (although PC will be overwritten shortly)
    hardware.m_processor->reg_af() = 0x1234;
    hardware.m_processor->reg_bc() = 0x2345;
    hardware.m_processor->reg_de() = 0x3456;
    hardware.m_processor->reg_hl() = 0x4567;
    hardware.m_processor->reg_sp() = 0x5678;
    hardware.m_processor->reg_pc() = 0x6789;

    hardware.load_memory_and_set_pc(0x0005, { 0x00 }); // NOP

    const auto cycles = hardware.m_processor->emulate_instruction();
    BOOST_CHECK_EQUAL(cycles, 4);

    // The only change should be an incremented PC
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_af(), 0x1234);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_bc(), 0x2345);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_de(), 0x3456);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_hl(), 0x4567);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_sp(), 0x5678);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_pc(), 0x0006); // One single-byte instruction after 0x0005
}

// Test to make sure side effects are as expected
BOOST_AUTO_TEST_CASE(test_single_instruction_side_effects)
{
    Hardware hardware;

    // Set all registers to known values (although PC will be overwritten shortly)
    hardware.m_processor->reg_af() = 0x0000;
    hardware.m_processor->reg_bc() = 0x2345;
    hardware.m_processor->reg_de() = 0x3456;
    hardware.m_processor->reg_hl() = 0x4567;
    hardware.m_processor->reg_sp() = 0x5678;
    hardware.m_processor->reg_pc() = 0x6789;

    hardware.load_memory_and_set_pc(0x0005, { 0x3C }); // INC A

    auto cycles = hardware.m_processor->emulate_instruction();
    BOOST_CHECK_EQUAL(cycles, 4);

    // The only change should be an incremented PC
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_a(), 0x01); // Was incremented from zero
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_f(), 0x00); // No flags set
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_bc(), 0x2345);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_de(), 0x3456);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_hl(), 0x4567);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_sp(), 0x5678);
    BOOST_CHECK_EQUAL(hardware.m_processor->reg_pc(), 0x0006); // One single-byte instruction after 0x0005
}

// Test various 8-bit register operations
BOOST_AUTO_TEST_CASE(test_8bit_register_operations)
{
    // INC A of 0x00
    test_8bit_register_instruction(0x3C, 4, 0x00, 0x01, 0x00);
    // INC A of 0x7F
    test_8bit_register_instruction(
        0x3C, 4, 0x7F, 0x80, zcpm::Processor::S_FLAG_MASK | zcpm::Processor::H_FLAG_MASK | zcpm::Processor::PV_FLAG_MASK);
    // INC A of 0xFF
    test_8bit_register_instruction(0x3C, 4, 0xFF, 0x00, zcpm::Processor::Z_FLAG_MASK | zcpm::Processor::H_FLAG_MASK);

    // DEC A of 0x00
    test_8bit_register_instruction(0x3D, 4, 0x00, 0xFF, 0xBA); // TODO: Enumerate expected flags
    // DEC A of 0x7F
    test_8bit_register_instruction(0x3D, 4, 0x80, 0x7F, 0x3E); // TODO: Enumerate expected flags
    // DEC A of 0xFF
    test_8bit_register_instruction(0x3D, 4, 0x01, 0x00, 0x42); // TODO: Enumerate expected flags
}

BOOST_AUTO_TEST_CASE(test_branching)
{
    Hardware hardware;

    // TODO: load RAM with a slightly longer sequence involving branching, execute it, etc.
}
