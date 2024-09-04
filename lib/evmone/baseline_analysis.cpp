// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "baseline.hpp"
#include "eof.hpp"
#include "instructions.hpp"
#include <memory>

namespace evmone::baseline
{
static_assert(std::is_move_constructible_v<CodeAnalysis>);
static_assert(std::is_move_assignable_v<CodeAnalysis>);
static_assert(!std::is_copy_constructible_v<CodeAnalysis>);
static_assert(!std::is_copy_assignable_v<CodeAnalysis>);

#ifdef QTUM_BUILD
// GCC 10 doesn't support std::make_unique_for_overwrite, support starts from GCC 11.1
// C++20 make_unique_for_overwrite implementation
template<class T>
    requires (!std::is_array_v<T>)
std::unique_ptr<T> make_unique_for_overwrite()
{
    return std::unique_ptr<T>(new T);
}
 
template<class T>
    requires std::is_unbounded_array_v<T>
std::unique_ptr<T> make_unique_for_overwrite(std::size_t n)
{
    return std::unique_ptr<T>(new std::remove_extent_t<T>[n]);
}
 
template<class T, class... Args>
    requires std::is_bounded_array_v<T>
void make_unique_for_overwrite(Args&&...) = delete;
#endif

namespace
{
CodeAnalysis::JumpdestMap analyze_jumpdests(bytes_view code)
{
    // To find if op is any PUSH opcode (OP_PUSH1 <= op <= OP_PUSH32)
    // it can be noticed that OP_PUSH32 is INT8_MAX (0x7f) therefore
    // static_cast<int8_t>(op) <= OP_PUSH32 is always true and can be skipped.
    static_assert(OP_PUSH32 == std::numeric_limits<int8_t>::max());

    CodeAnalysis::JumpdestMap map(code.size());  // Allocate and init bitmap with zeros.
    for (size_t i = 0; i < code.size(); ++i)
    {
        const auto op = code[i];
        if (static_cast<int8_t>(op) >= OP_PUSH1)  // If any PUSH opcode (see explanation above).
            i += op - size_t{OP_PUSH1 - 1};       // Skip PUSH data.
        else if (INTX_UNLIKELY(op == OP_JUMPDEST))
            map[i] = true;
    }

    return map;
}

std::unique_ptr<uint8_t[]> pad_code(bytes_view code)
{
    // We need at most 33 bytes of code padding: 32 for possible missing all data bytes of PUSH32
    // at the very end of the code; and one more byte for STOP to guarantee there is a terminating
    // instruction at the code end.
    constexpr auto padding = 32 + 1;

#ifdef QTUM_BUILD
    auto padded_code = evmone::baseline::make_unique_for_overwrite<uint8_t[]>(code.size() + padding);
#else
    auto padded_code = std::make_unique_for_overwrite<uint8_t[]>(code.size() + padding);
#endif
    std::copy(std::begin(code), std::end(code), padded_code.get());
    std::fill_n(&padded_code[code.size()], padding, uint8_t{OP_STOP});
    return padded_code;
}


CodeAnalysis analyze_legacy(bytes_view code)
{
    // TODO: The padded code buffer and jumpdest bitmap can be created with single allocation.
    return {pad_code(code), code.size(), analyze_jumpdests(code)};
}

CodeAnalysis analyze_eof1(bytes_view container)
{
    auto header = read_valid_eof1_header(container);

    // Extract all code sections as single buffer reference.
    // TODO: It would be much easier if header had code_sections_offset and data_section_offset
    //       with code_offsets[] being relative to code_sections_offset.
    const auto code_sections_offset = header.code_offsets[0];
    const auto code_sections_end = size_t{header.code_offsets.back()} + header.code_sizes.back();
    const auto executable_code =
        container.substr(code_sections_offset, code_sections_end - code_sections_offset);

    return CodeAnalysis{executable_code, std::move(header)};
}
}  // namespace

CodeAnalysis analyze(evmc_revision rev, bytes_view code)
{
    if (rev < EVMC_PRAGUE || !is_eof_container(code))
        return analyze_legacy(code);
    return analyze_eof1(code);
}
}  // namespace evmone::baseline
