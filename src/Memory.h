// Memory.h - Pattern scanning for external processes.
//
// Use this to locate module-relative offsets at runtime when you only know a
// stable byte signature (IDA-style "48 8B 05 ? ? ? ?") rather than a hard
// offset. Lets the cheat survive small game patches.

#pragma once
#include "Process.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <optional>

namespace mem {

    struct SigByte {
        uint8_t value = 0;
        bool    wild  = false; // '?' wildcard
    };

    // Parses an IDA-style signature string: "48 8B 05 ? ? ? ? 48 85 C0".
    // Hex digits, pairs or singles; '?' or '??' means wildcard.
    inline std::vector<SigByte> parseSignature(const std::string& sig) {
        std::vector<SigByte> out;
        std::istringstream iss(sig);
        std::string token;
        while (iss >> token) {
            SigByte b;
            if (token == "?" || token == "??") {
                b.wild = true;
            } else {
                b.value = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
            }
            out.push_back(b);
        }
        return out;
    }

    // Scans the given module region in the attached process for the pattern.
    // Returns the absolute address of the first match, or 0 if none.
    inline uintptr_t findPattern(const Process& proc,
                                 const std::string& sig,
                                 size_t chunkSize = 0x10000) {
        if (!proc.isAttached() || !proc.base() || !proc.size()) return 0;

        const auto pattern = parseSignature(sig);
        if (pattern.empty()) return 0;

        std::vector<uint8_t> buffer(chunkSize);
        const uintptr_t start = proc.base();
        const uintptr_t end   = proc.base() + proc.size();

        for (uintptr_t cur = start; cur < end; cur += chunkSize - pattern.size()) {
            const size_t want = (cur + chunkSize > end) ? (end - cur) : chunkSize;
            if (!proc.readRaw(cur, buffer.data(), want)) continue;

            if (want < pattern.size()) break;
            const size_t limit = want - pattern.size();
            for (size_t i = 0; i <= limit; ++i) {
                bool match = true;
                for (size_t j = 0; j < pattern.size(); ++j) {
                    if (pattern[j].wild) continue;
                    if (buffer[i + j] != pattern[j].value) { match = false; break; }
                }
                if (match) return cur + i;
            }
        }
        return 0;
    }

    // Resolves a RIP-relative instruction operand at `insnAddr + insnOffset`
    // whose instruction length is `insnLen`. Useful for turning a signature
    // hit like "48 8B 0D ? ? ? ?" into the real target address.
    //
    //   target = insnAddr + insnLen + *(int32_t*)(insnAddr + insnOffset)
    inline uintptr_t resolveRip(const Process& proc,
                                uintptr_t insnAddr,
                                size_t insnOffset,
                                size_t insnLen) {
        int32_t rel = 0;
        if (!proc.read<int32_t>(insnAddr + insnOffset, rel)) return 0;
        return insnAddr + insnLen + rel;
    }
}
