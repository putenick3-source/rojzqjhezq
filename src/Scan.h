// Scan.h - Resumable value scanner + projection/pointer-array heuristics.
//
// Read-only: everything here goes through Process::readRaw (RPM). Scans are
// incremental (first/next) so the render loop can interleave them, and every
// walk is bounded by a byte budget and a hard hit cap.

#pragma once
#include "Process.h"
#include "Projection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scan {

    enum class Kind {
        Float,
        Int32,
    };

    struct Hit {
        uintptr_t addr = 0;
        double    f    = 0.0;
        int32_t   i    = 0;
    };

    // Stops accumulating after this many hits so a loose tolerance cannot
    // exhaust memory.
    inline constexpr size_t kMaxHits = 200000;

    class Scanner {
    public:
        // Starts a new scan and yields the first hit (if any). `budget` caps
        // the bytes enumerated, 0 = whole address space.
        bool first(const Process& proc, Kind kind, double value, float tol,
                   size_t budget = 0) {
            reset();
            kind_    = kind;
            value_   = value;
            tol_     = (tol < 0.f) ? 0.f : tol;
            regions_ = proc.regions(budget);
            block_.resize(kBlock);
            done_ = regions_.empty();
            return done_ ? false : step(proc);
        }

        // Continues the current scan towards the next hit.
        bool next(const Process& proc) {
            if (done_) return false;
            return step(proc);
        }

        void reset() {
            regions_.clear();
            hits_.clear();
            region_ = 0;
            offset_ = 0;
            done_   = true;
        }

        const std::vector<Hit>& hits() const { return hits_; }
        size_t                  count() const { return hits_.size(); }
        bool                    finished() const { return done_; }

    private:
        static constexpr size_t kBlock = 0x10000;

        bool step(const Process& proc) {
            while (region_ < regions_.size()) {
                const Process::Region& r = regions_[region_];
                if (offset_ >= r.size) {
                    ++region_;
                    offset_ = 0;
                    continue;
                }

                const size_t want = std::min(kBlock, r.size - offset_);
                const uintptr_t base = r.base + offset_;
                if (!proc.readRaw(base, block_.data(), want)) {
                    ++region_;
                    offset_ = 0;
                    continue;
                }

                for (size_t i = 0; i + 4 <= want; i += 4) {
                    if (!matches(i)) continue;

                    hits_.push_back(hitAt(base + i, i));
                    offset_ += i + 4;
                    if (hits_.size() >= kMaxHits) done_ = true;
                    return true;
                }

                offset_ += want;
            }
            done_ = true;
            return false;
        }

        bool matches(size_t i) const {
            if (kind_ == Kind::Float) {
                float v = 0.f;
                std::memcpy(&v, block_.data() + i, sizeof(v));
                if (!std::isfinite(v)) return false;
                return std::fabs((double)v - value_) <= (double)tol_;
            }
            int32_t v = 0;
            std::memcpy(&v, block_.data() + i, sizeof(v));
            return (double)v == value_;
        }

        Hit hitAt(uintptr_t at, size_t i) const {
            Hit h;
            h.addr = at;
            if (kind_ == Kind::Float) {
                float v = 0.f;
                std::memcpy(&v, block_.data() + i, sizeof(v));
                h.f = (double)v;
            } else {
                std::memcpy(&h.i, block_.data() + i, sizeof(h.i));
                h.f = (double)h.i;
            }
            return h;
        }

        std::vector<Process::Region> regions_;
        std::vector<uint8_t>         block_;
        std::vector<Hit>             hits_;
        size_t region_ = 0;
        size_t offset_ = 0;
        Kind   kind_   = Kind::Float;
        double value_  = 0.0;
        float  tol_    = 0.0f;
        bool   done_   = true;
    };

    // A run of consecutive valid pointers; `addr` is where the run starts.
    struct PointerRun {
        uintptr_t              addr = 0;
        std::vector<uintptr_t> values;
    };

    // Runs of aligned, readable pointers — IL2CPP entity lists look like this.
    inline std::vector<PointerRun>
    findPointerArrays(const Process& proc, size_t minCount, size_t budget = 0) {
        std::vector<PointerRun> arrays;
        if (!proc.isAttached()) return arrays;
        if (minCount < 2) minCount = 2;

        constexpr size_t kBlock     = 0x10000;
        constexpr size_t kMaxRun    = 4096;
        constexpr size_t kMaxArrays = 512;

        std::vector<uint8_t> block(kBlock);
        PointerRun run;

        auto flush = [&]() {
            if (run.values.size() >= minCount) arrays.push_back(run);
            run = PointerRun{};
        };

        for (const Process::Region& r : proc.regions(budget)) {
            for (size_t off = 0; off < r.size; off += kBlock) {
                const size_t want = std::min(kBlock, r.size - off);
                if (want < sizeof(uintptr_t)) break;
                if (!proc.readRaw(r.base + off, block.data(), want)) {
                    flush();
                    continue;
                }

                for (size_t i = 0; i + sizeof(uintptr_t) <= want;
                     i += sizeof(uintptr_t)) {
                    uintptr_t p = 0;
                    std::memcpy(&p, block.data() + i, sizeof(p));

                    if (p > 0x10000 && proc.validate(p, sizeof(uintptr_t))) {
                        if (run.values.empty()) run.addr = r.base + off + i;
                        if (run.values.size() < kMaxRun) run.values.push_back(p);
                    } else {
                        flush();
                        if (arrays.size() >= kMaxArrays) return arrays;
                    }
                }
                flush();
                if (arrays.size() >= kMaxArrays) return arrays;
            }
        }
        return arrays;
    }

    // Looks for a view-projection matrix: cheap row-3 norm prefilter, then full
    // validateProjection in both layouts. `checked` reports candidates tested.
    inline bool findViewProjection(const Process& proc, Projection& out,
                                   size_t budget, size_t& checked) {
        checked = 0;
        if (!proc.isAttached()) return false;

        constexpr size_t kBlock  = 0x10000;
        constexpr size_t kFloats = 16;
        constexpr size_t kStep   = 4; // one candidate every 16 bytes

        std::vector<float> block(kBlock / sizeof(float));

        for (const Process::Region& r : proc.regions(budget)) {
            for (size_t off = 0; off < r.size; off += kBlock) {
                const size_t floats =
                    std::min(block.size(), (r.size - off) / sizeof(float));
                if (floats < kFloats) break;
                if (!proc.readRaw(r.base + off, block.data(),
                                  floats * sizeof(float))) {
                    continue;
                }

                for (size_t i = 0; i + kFloats <= floats; i += kStep) {
                    const float sx   = block[i + 3];
                    const float sy   = block[i + 7];
                    const float sz   = block[i + 11];
                    const float norm = std::sqrt(sx * sx + sy * sy + sz * sz);
                    if (!std::isfinite(norm) || norm < 0.8f || norm > 1.25f) continue;

                    Mat4 raw{};
                    for (int rr = 0; rr < 4; ++rr) {
                        for (int cc = 0; cc < 4; ++cc) {
                            raw.m[rr][cc] = block[i + rr * 4 + cc];
                        }
                    }

                    ++checked;
                    float residual = 1.0f;
                    for (Layout layout : { Layout::RowMajor, Layout::ColumnMajor }) {
                        Mat4 candidate{};
                        for (int rr = 0; rr < 4; ++rr) {
                            for (int cc = 0; cc < 4; ++cc) {
                                candidate.m[rr][cc] = (layout == Layout::RowMajor)
                                    ? raw.m[rr][cc]
                                    : raw.m[cc][rr];
                            }
                        }
                        if (!validateProjection(candidate, residual)) continue;

                        out.mat  = candidate;
                        out.addr = r.base + off + i * sizeof(float);
                        out.invalidate();
                        return out.valid();
                    }
                }
            }
        }
        return false;
    }

}
