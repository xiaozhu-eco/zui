#include "layout/cache/exclusion_space.h"

#include "layout/cache/hash_mix.h"
#include "layout/cache/quantum.h"

#include <cstdint>

namespace zui {
namespace layout {
namespace {

void push_bits(std::vector<std::uint64_t>& out, std::uint64_t value) { out.push_back(value); }

void push_quantum(std::vector<std::uint64_t>& out, float value) {
    out.push_back(static_cast<std::uint64_t>(quantum_key(value)));
}

std::vector<std::uint64_t> band_tokens(const std::vector<FloatRecord>& bands) {
    std::vector<std::uint64_t> out;
    push_bits(out, bands.size());
    for (const FloatRecord& band : bands) {
        push_bits(out, static_cast<std::uint64_t>(band.side));
        push_quantum(out, band.margin_box.x);
        push_quantum(out, band.margin_box.y);
        push_quantum(out, band.margin_box.w);
        push_quantum(out, band.margin_box.h);
        push_bits(out, band.pending ? 1 : 0);
        push_bits(out, static_cast<std::uint64_t>(band.shape.kind));
        push_quantum(out, band.shape.inset_t);
        push_quantum(out, band.shape.inset_r);
        push_quantum(out, band.shape.inset_b);
        push_quantum(out, band.shape.inset_l);
        push_quantum(out, band.shape.inset_round);
        push_quantum(out, band.shape.cx);
        push_quantum(out, band.shape.cy);
        push_quantum(out, band.shape.rx);
        push_quantum(out, band.shape.ry);
        push_quantum(out, band.shape_margin);
    }
    return out;
}

}  // namespace

ExclusionSpace::ExclusionSpace() = default;

ExclusionSpace::ExclusionSpace(std::vector<FloatRecord> bands) : bands_(std::move(bands)) {
    hash_ = bands_.empty() ? 0 : hash_tokens(band_tokens(bands_));
}

bool ExclusionSpace::equal(const ExclusionSpace& other) const {
    return band_tokens(bands_) == band_tokens(other.bands_);
}

std::shared_ptr<const ExclusionSpace> ExclusionSpace::append(const std::shared_ptr<const ExclusionSpace>& base,
                                                              FloatRecord band) {
    std::vector<FloatRecord> bands = base ? base->bands() : std::vector<FloatRecord>{};
    bands.push_back(band);
    return std::make_shared<const ExclusionSpace>(std::move(bands));
}

std::shared_ptr<const ExclusionSpace> ExclusionSpace::adopt(const std::shared_ptr<const ExclusionSpace>& base,
                                                             const std::vector<FloatRecord>& extra) {
    if (extra.empty()) {
        return base;
    }
    std::shared_ptr<const ExclusionSpace> current = base;
    for (const FloatRecord& band : extra) {
        current = append(current, band);
    }
    return current;
}

}  // namespace layout
}  // namespace zui
