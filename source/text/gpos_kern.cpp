#include "text/gpos_kern.h"

#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace zui {
namespace text {
namespace {

struct KernPairs {
    // Format 1: first glyph -> (second glyph -> xAdvance in design units).
    std::unordered_map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::int16_t>>> pairs;
    struct ClassPairs {
        std::unordered_map<std::uint32_t, std::uint16_t> first_class;
        std::unordered_map<std::uint32_t, std::uint16_t> second_class;
        // The Coverage table of this subtable. Class 0 is a real class - a glyph that has no entry
        // in the ClassDef belongs to it - so class membership cannot double as the coverage test.
        std::unordered_set<std::uint32_t> covered;
        std::uint16_t class1_count = 0;
        std::uint16_t class2_count = 0;
        std::vector<std::int16_t> value;
    };
    std::vector<ClassPairs> classes;
};

std::uint16_t be16(const std::vector<std::uint8_t>& data, std::size_t at) {
    if (at + 2 > data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>((data[at] << 8) | data[at + 1]);
}

std::uint32_t be32(const std::vector<std::uint8_t>& data, std::size_t at) {
    if (at + 4 > data.size()) {
        return 0;
    }
    return (static_cast<std::uint32_t>(data[at]) << 24) |
           (static_cast<std::uint32_t>(data[at + 1]) << 16) |
           (static_cast<std::uint32_t>(data[at + 2]) << 8) | data[at + 3];
}

std::uint32_t tag_of(const char* text) {
    return (static_cast<std::uint32_t>(text[0]) << 24) |
           (static_cast<std::uint32_t>(text[1]) << 16) |
           (static_cast<std::uint32_t>(text[2]) << 8) | static_cast<std::uint32_t>(text[3]);
}

// Coverage table -> covered glyphs, in coverage-index order.
std::vector<std::uint32_t> coverage_glyphs(const std::vector<std::uint8_t>& data, std::size_t at) {
    std::vector<std::uint32_t> out;
    const std::uint16_t format = be16(data, at);
    if (format == 1) {
        const std::uint16_t count = be16(data, at + 2);
        for (std::uint16_t i = 0; i < count; ++i) {
            out.push_back(be16(data, at + 4 + static_cast<std::size_t>(i) * 2));
        }
        return out;
    }
    if (format != 2) {
        return out;
    }
    const std::uint16_t ranges = be16(data, at + 2);
    for (std::uint16_t i = 0; i < ranges; ++i) {
        const std::size_t record = at + 4 + static_cast<std::size_t>(i) * 6;
        const std::uint32_t start = be16(data, record);
        const std::uint32_t end = be16(data, record + 2);
        for (std::uint32_t glyph = start; glyph <= end; ++glyph) {
            out.push_back(glyph);
        }
    }
    return out;
}

void read_class_def(const std::vector<std::uint8_t>& data, std::size_t at,
                    std::unordered_map<std::uint32_t, std::uint16_t>& out) {
    const std::uint16_t format = be16(data, at);
    if (format == 1) {
        const std::uint32_t start = be16(data, at + 2);
        const std::uint16_t count = be16(data, at + 4);
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint16_t value = be16(data, at + 6 + static_cast<std::size_t>(i) * 2);
            if (value != 0) {
                out[start + i] = value;
            }
        }
        return;
    }
    if (format != 2) {
        return;
    }
    const std::uint16_t ranges = be16(data, at + 2);
    for (std::uint16_t i = 0; i < ranges; ++i) {
        const std::size_t record = at + 4 + static_cast<std::size_t>(i) * 6;
        const std::uint32_t start = be16(data, record);
        const std::uint32_t end = be16(data, record + 2);
        const std::uint16_t value = be16(data, record + 4);
        if (value == 0) {
            continue;
        }
        for (std::uint32_t glyph = start; glyph <= end; ++glyph) {
            out[glyph] = value;
        }
    }
}

// ---- Variation deltas on the pair values -----------------------------------------------------
//
// A variable font expresses per-instance adjustments through VariationIndex "device" tables whose
// deltas live in the GDEF ItemVariationStore. Noto Sans SC declares its wght default as 100, so
// instancing it at 400 moves those deltas away from zero: reading the pair values raw is what left
// `sans-serif` a fraction of a pixel per pair wider than the browser while static faces matched.
double f2dot14(std::uint16_t raw);

struct VariationStore {
    std::vector<std::uint8_t> data;  // the GDEF table; the store is a window into it
    std::size_t store = 0;
    std::vector<double> coords;      // normalized instance coordinates
    std::vector<double> scalars;     // one scalar per variation region, for this instance
};

// One region's contribution factor for the instance (OpenType ItemVariationStore).
double region_scalar(const std::vector<std::uint8_t>& data, std::size_t region,
                     std::uint16_t axis_count, const std::vector<double>& coords) {
    double scalar = 1.0;
    for (std::uint16_t axis = 0; axis < axis_count; ++axis) {
        const std::size_t at = region + static_cast<std::size_t>(axis) * 6;
        const double start = f2dot14(be16(data, at));
        const double peak = f2dot14(be16(data, at + 2));
        const double end = f2dot14(be16(data, at + 4));
        const double value = axis < coords.size() ? coords[axis] : 0.0;
        if (start > peak || peak > end) {
            continue;
        }
        if (start == peak && peak == end) {
            continue;
        }
        if (peak == 0.0) {
            continue;
        }
        if (value < start || value > end) {
            return 0.0;
        }
        if (value == peak) {
            continue;
        }
        if (value < peak) {
            scalar *= (value - start) / (peak - start);
        } else {
            scalar *= (end - value) / (end - peak);
        }
    }
    return scalar;
}

bool read_variation_store(FT_Face face, const std::vector<double>& coords, VariationStore& out) {
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, tag_of("GDEF"), 0, nullptr, &length) != 0 || length < 12) {
        return false;
    }
    out.data.assign(static_cast<std::size_t>(length), 0);
    if (FT_Load_Sfnt_Table(face, tag_of("GDEF"), 0, out.data.data(), &length) != 0) {
        return false;
    }
    out.data.resize(static_cast<std::size_t>(length));
    // GDEF 1.3 adds the ItemVariationStore; earlier versions carry no deltas at all.
    if (be16(out.data, 0) < 1 || be16(out.data, 2) < 3) {
        return false;
    }
    // GDEF 1.3: glyf class def, attach list, lig caret list, mark attach class def, mark glyph sets
    // (all Offset16) and then the ItemVariationStore (Offset32) at byte 14.
    const std::uint32_t at = be32(out.data, 14);
    if (at == 0 || at + 8 > out.data.size() || be16(out.data, at) != 1) {
        return false;
    }
    out.store = at;
    out.coords = coords;
    const std::size_t regions = at + be32(out.data, at + 2);
    const std::uint16_t axis_count = be16(out.data, regions);
    const std::uint16_t region_count = be16(out.data, regions + 2);
    out.scalars.resize(region_count, 0.0);
    for (std::uint16_t r = 0; r < region_count; ++r) {
        out.scalars[r] =
            region_scalar(out.data, regions + 4 + static_cast<std::size_t>(r) * axis_count * 6,
                          axis_count, coords);
    }
    return true;
}

// The delta a VariationIndex "device" table contributes at this instance, in design units.
//
// `device` points into `gpos`, not into the store: the two halves of a variable adjustment live in
// different tables. The VariationIndex device table - outer and inner index - sits next to the
// value record in GPOS, while the ItemVariationStore it indexes into sits in GDEF. Reading both out
// of GDEF silently found nothing for a face whose GDEF is only a few hundred bytes long (Noto Sans
// SC: 240), which is why the pair values stayed at their base instance.
double variation_delta(const VariationStore& store, const std::vector<std::uint8_t>& gpos,
                       std::size_t device) {
    if (store.data.empty() || device + 6 > gpos.size() || be16(gpos, device + 4) != 0x8000) {
        return 0.0;
    }
    const std::uint16_t outer = be16(gpos, device);
    const std::uint16_t inner = be16(gpos, device + 2);
    const std::uint16_t data_count = be16(store.data, store.store + 6);
    if (outer >= data_count) {
        return 0.0;
    }
    const std::size_t item = store.store + be32(store.data, store.store + 8 + outer * 4);
    const std::uint16_t item_count = be16(store.data, item);
    if (inner >= item_count) {
        return 0.0;
    }
    const std::uint16_t word_count = be16(store.data, item + 2);
    const std::uint16_t region_count = be16(store.data, item + 4);
    const std::size_t indexes = item + 6;
    const std::size_t deltas = indexes + static_cast<std::size_t>(region_count) * 2;
    const std::size_t row = deltas + static_cast<std::size_t>(inner) * (word_count * 2 + region_count - word_count);
    double total = 0.0;
    for (std::uint16_t r = 0; r < region_count; ++r) {
        const std::uint16_t region = be16(store.data, indexes + static_cast<std::size_t>(r) * 2);
        if (region >= store.scalars.size()) {
            continue;
        }
        const double scalar = store.scalars[region];
        if (scalar == 0.0) {
            continue;
        }
        if (r < word_count) {
            total += scalar * static_cast<std::int16_t>(be16(store.data, row + static_cast<std::size_t>(r) * 2));
        } else {
            const std::size_t at = row + static_cast<std::size_t>(word_count) * 2 + (r - word_count);
            if (at < store.data.size()) {
                total += scalar * static_cast<std::int8_t>(store.data[at]);
            }
        }
    }
    return total;
}

std::size_t value_record_size(std::uint16_t format) {
    std::size_t fields = 0;
    for (int bit = 0; bit < 8; ++bit) {
        if ((format & (1u << bit)) != 0) {
            ++fields;
        }
    }
    return fields * 2;
}

// xAdvance of a ValueRecord, in design units: the format decides which fields are present, and
// XAdvance is the third of them. `parent` is the table the record lives in, which is what a
// VariationIndex "device" offset is measured from.
double value_record_x_advance(const std::vector<std::uint8_t>& data, std::size_t at,
                              std::uint16_t format, std::size_t parent,
                              const VariationStore& store) {
    if ((format & 0x0004u) == 0) {
        return 0;
    }
    std::size_t offset = at;
    for (int bit = 0; bit < 2; ++bit) {
        if ((format & (1u << bit)) != 0) {
            offset += 2;
        }
    }
    double value = static_cast<std::int16_t>(be16(data, offset));
    if (store.data.empty() || (format & 0x0040u) == 0) {
        return value;
    }
    // The device offsets follow the value fields, in bit order: [X, Y][Placement, Advance]Device.
    std::size_t device = at;
    for (int bit = 0; bit < 4; ++bit) {
        if ((format & (1u << bit)) != 0) {
            device += 2;
        }
    }
    for (int bit = 4; bit < 6; ++bit) {
        if ((format & (1u << bit)) != 0) {
            device += 2;
        }
    }
    const std::uint16_t device_offset = be16(data, device);
    if (device_offset != 0) {
        value += variation_delta(store, data, parent + device_offset);
    }
    return value;
}

void read_pair_pos(const std::vector<std::uint8_t>& data, std::size_t at, KernPairs& out,
                   const VariationStore& store) {
    const std::uint16_t format = be16(data, at);
    const std::uint16_t value_format1 = be16(data, at + 4);
    const std::size_t value_size1 = value_record_size(value_format1);
    if (format == 1) {
        const std::size_t coverage = at + be16(data, at + 2);
        const std::uint16_t count = be16(data, at + 8);
        const std::vector<std::uint32_t> glyphs = coverage_glyphs(data, coverage);
        // A PairValueRecord is [secondGlyph][valueRecord1][valueRecord2]; both value formats decide
        // the stride, and a font that adjusts the second glyph of the pair sets the second one.
        const std::size_t record_size =
            2 + value_size1 + value_record_size(be16(data, at + 6));
        for (std::uint16_t i = 0; i < count && i < glyphs.size(); ++i) {
            const std::size_t set = at + be16(data, at + 10 + static_cast<std::size_t>(i) * 2);
            const std::uint16_t records = be16(data, set);
            std::vector<std::pair<std::uint32_t, std::int16_t>>& list = out.pairs[glyphs[i]];
            for (std::uint16_t r = 0; r < records; ++r) {
                const std::size_t record = set + 2 + static_cast<std::size_t>(r) * record_size;
                const std::uint32_t second = be16(data, record);
                const double advance = value_size1 == 0
                                           ? 0.0
                                           : value_record_x_advance(data, record + 2, value_format1,
                                                                    set, store);
                if (advance != 0) {
                    list.emplace_back(second, advance);
                }
            }
        }
        return;
    }
    if (format != 2) {
        return;
    }
    KernPairs::ClassPairs classes;
    for (std::uint32_t glyph : coverage_glyphs(data, at + be16(data, at + 2))) {
        classes.covered.insert(glyph);
    }
    classes.class1_count = be16(data, at + 12);
    classes.class2_count = be16(data, at + 14);
    read_class_def(data, at + be16(data, at + 8), classes.first_class);
    read_class_def(data, at + be16(data, at + 10), classes.second_class);
    classes.value.assign(static_cast<std::size_t>(classes.class1_count) * classes.class2_count, 0);
    const std::size_t record_size = value_size1 + value_record_size(be16(data, at + 6));
    for (std::uint16_t c1 = 0; c1 < classes.class1_count; ++c1) {
        for (std::uint16_t c2 = 0; c2 < classes.class2_count; ++c2) {
            const std::size_t record =
                at + 16 +
                (static_cast<std::size_t>(c1) * classes.class2_count + c2) * record_size;
            const double value =
                value_size1 == 0 ? 0.0 : value_record_x_advance(data, record, value_format1, at, store);
            classes.value[static_cast<std::size_t>(c1) * classes.class2_count + c2] =
                static_cast<std::int16_t>(std::lround(value));
        }
    }
    out.classes.push_back(std::move(classes));
}

// avar remaps an axis's normalized coordinate before anything else reads it: gvar, the
// ItemVariationStore and the FeatureVariations conditions are all written against the remapped
// value. Noto Sans SC maps the normalized 0.375 of `wght: 400` to 0.39, so skipping the remap left
// every delta this file computes a few percent short of the browser's.
std::vector<double> apply_avar(FT_Face face, std::vector<double> coords) {
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, tag_of("avar"), 0, nullptr, &length) != 0 || length < 12) {
        return coords;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    if (FT_Load_Sfnt_Table(face, tag_of("avar"), 0, data.data(), &length) != 0) {
        return coords;
    }
    data.resize(static_cast<std::size_t>(length));
    const std::uint16_t major = be16(data, 0);
    const std::uint16_t axes = be16(data, 6);
    // AxisValueMap is a pair of F2Dot14 coordinates: four bytes per segment, not the six of an
    // ItemVariationStore region record.
    constexpr std::size_t kSegmentSize = 4;
    std::size_t at = 8;
    for (std::uint16_t axis = 0; axis < axes && at + 2 <= data.size(); ++axis) {
        const std::uint16_t segments = be16(data, at);
        at += 2;
        if (segments < 2 || axis >= coords.size()) {
            at += static_cast<std::size_t>(segments) * kSegmentSize;
            continue;
        }
        const double value = std::max(-1.0, std::min(1.0, coords[axis]));
        const std::size_t map = at;
        at += static_cast<std::size_t>(segments) * kSegmentSize;
        for (std::uint16_t segment = 1; segment < segments; ++segment) {
            const std::size_t entry = map + static_cast<std::size_t>(segment) * kSegmentSize;
            const double from = f2dot14(be16(data, entry));
            const double to = f2dot14(be16(data, entry + 2));
            if (value > from) {
                continue;
            }
            const std::size_t previous = entry - kSegmentSize;
            const double previous_from =
                f2dot14(be16(data, previous));
            const double previous_to = f2dot14(be16(data, previous + 2));
            coords[axis] = from == previous_from
                               ? to
                               : previous_to + (value - previous_from) * (to - previous_to) /
                                                    (from - previous_from);
            break;
        }
        // avar 2 adds an ItemVariationStore on top of these segment maps; a face that ships one
        // declares major version 2, and the segment maps above already carry its base mapping.
        if (major >= 2) {
            break;
        }
    }
    return coords;
}

// The normalized coordinates of the instance this face was loaded at: the weight axis carries the
// requested weight, every other axis stays at its default - the same instance `apply_weight` sets.
// Condition tables and variation deltas are written against these, not against design units.
std::vector<double> normalized_coordinates(FT_Face face, int weight) {
    std::vector<double> out;
    FT_MM_Var* var = nullptr;
    if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) == 0 ||
        FT_Get_MM_Var(face, &var) != 0 || var == nullptr) {
        return out;
    }
    out.assign(static_cast<std::size_t>(var->num_axis), 0.0);
    for (FT_UInt i = 0; i < var->num_axis; ++i) {
        const FT_Var_Axis& axis = var->axis[i];
        double value = static_cast<double>(axis.def) / 65536.0;
        if (axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
            value = weight;
        }
        const double minimum = static_cast<double>(axis.minimum) / 65536.0;
        const double def = static_cast<double>(axis.def) / 65536.0;
        const double maximum = static_cast<double>(axis.maximum) / 65536.0;
        if (value < def && def > minimum) {
            out[i] = -(def - value) / (def - minimum);
        } else if (value > def && maximum > def) {
            out[i] = (value - def) / (maximum - def);
        }
    }
    FT_Done_MM_Var(face->glyph->library, var);
    return apply_avar(face, std::move(out));
}

double f2dot14(std::uint16_t raw) {
    return static_cast<double>(static_cast<std::int16_t>(raw)) / 16384.0;
}

// A condition set matches when every condition in it does (OpenType FeatureVariations).
bool condition_set_matches(const std::vector<std::uint8_t>& data, std::size_t at,
                           const std::vector<double>& coords) {
    const std::uint16_t count = be16(data, at);
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t condition = at + be32(data, at + 2 + static_cast<std::size_t>(i) * 4);
        if (be16(data, condition) != 1) {
            return false;  // format 1 is the axis range; anything else is not understood
        }
        const std::uint16_t axis = be16(data, condition + 2);
        if (axis >= coords.size()) {
            return false;
        }
        const double low = f2dot14(be16(data, condition + 4));
        const double high = f2dot14(be16(data, condition + 6));
        if (coords[axis] < low || coords[axis] > high) {
            return false;
        }
    }
    return true;
}

bool read_gpos_kern(FT_Face face, int weight, KernPairs& out) {
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, tag_of("GPOS"), 0, nullptr, &length) != 0 || length < 10) {
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    if (FT_Load_Sfnt_Table(face, tag_of("GPOS"), 0, data.data(), &length) != 0) {
        return false;
    }
    data.resize(static_cast<std::size_t>(length));
    const std::size_t feature_list = be16(data, 6);
    const std::size_t lookup_list = be16(data, 8);
    if (feature_list == 0 || lookup_list == 0) {
        return false;
    }
    // The `kern` feature of the script the run is in - not every `kern` feature in the face. A font
    // carries one per script with different pair values, and merging them meant a Latin pair could
    // pick up another script's (smaller) adjustment.
    std::vector<std::uint32_t> wanted;
    {
        const std::size_t script_list = be16(data, 4);
        for (const char* script : {"latn", "DFLT"}) {
            if (script_list == 0) {
                break;
            }
            const std::uint16_t scripts = be16(data, script_list);
            for (std::uint16_t i = 0; i < scripts; ++i) {
                const std::size_t record = script_list + 2 + static_cast<std::size_t>(i) * 6;
                if (be32(data, record) != tag_of(script)) {
                    continue;
                }
                const std::size_t table = script_list + be16(data, record + 4);
                const std::uint16_t default_lang = be16(data, table);
                if (default_lang != 0) {
                    const std::size_t lang = table + default_lang;
                    const std::uint16_t indices = be16(data, lang + 4);
                    for (std::uint16_t k = 0; k < indices; ++k) {
                        wanted.push_back(be16(data, lang + 6 + static_cast<std::size_t>(k) * 2));
                    }
                }
                break;
            }
            if (!wanted.empty()) {
                break;
            }
        }
    }
    const std::uint16_t feature_count = be16(data, feature_list);
    std::vector<std::pair<std::uint16_t, std::size_t>> kern_features;
    for (std::uint16_t i = 0; i < feature_count; ++i) {
        const std::size_t record = feature_list + 2 + static_cast<std::size_t>(i) * 6;
        if (be32(data, record) != tag_of("kern")) {
            continue;
        }
        if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), i) == wanted.end()) {
            continue;
        }
        kern_features.emplace_back(i, feature_list + be16(data, record + 4));
    }
    if (kern_features.empty()) {
        return false;
    }
    // FeatureVariations: at this instance, the first record whose conditions all match replaces the
    // feature tables it names. Without this a variable font's kerning is the base table's, which is
    // what made `sans-serif` (Noto Sans SC VF on this host) measure wider than the browser while
    // static faces matched to a hundredth of a pixel.
    if (be16(data, 2) >= 1) {
        const std::size_t variations = be32(data, 10);
        if (variations != 0) {
            const std::size_t table = variations;
            const std::uint32_t records = be32(data, table + 4);
            const std::vector<double> coords = normalized_coordinates(face, weight);
            for (std::uint32_t r = 0; r < records; ++r) {
                const std::size_t record = table + 8 + static_cast<std::size_t>(r) * 8;
                const std::size_t conditions = table + be32(data, record);
                if (!condition_set_matches(data, conditions, coords)) {
                    continue;
                }
                const std::size_t substitution = table + be32(data, record + 4);
                const std::uint16_t substitutions = be16(data, substitution + 4);
                for (std::uint16_t s = 0; s < substitutions; ++s) {
                    const std::size_t entry =
                        substitution + 6 + static_cast<std::size_t>(s) * 6;
                    const std::uint16_t index = be16(data, entry);
                    const std::size_t alternate = substitution + be32(data, entry + 2);
                    for (auto& feature : kern_features) {
                        if (feature.first == index) {
                            feature.second = alternate;
                        }
                    }
                }
                break;  // the first matching record wins
            }
        }
    }
    VariationStore store;
    read_variation_store(face, normalized_coordinates(face, weight), store);
    std::vector<std::uint16_t> lookups;
    for (const auto& feature_entry : kern_features) {
        const std::size_t feature = feature_entry.second;
        const std::uint16_t indices = be16(data, feature + 2);
        for (std::uint16_t k = 0; k < indices; ++k) {
            lookups.push_back(be16(data, feature + 4 + static_cast<std::size_t>(k) * 2));
        }
    }
    if (lookups.empty()) {
        return false;
    }
    const std::uint16_t lookup_count = be16(data, lookup_list);
    for (std::uint16_t index : lookups) {
        if (index >= lookup_count) {
            continue;
        }
        const std::size_t lookup =
            lookup_list + be16(data, lookup_list + 2 + static_cast<std::size_t>(index) * 2);
        if (be16(data, lookup) != 2) {  // LookupType 2 is PairPos.
            continue;
        }
        const std::uint16_t subtables = be16(data, lookup + 4);
        for (std::uint16_t s = 0; s < subtables; ++s) {
            const std::size_t subtable =
                lookup + be16(data, lookup + 6 + static_cast<std::size_t>(s) * 2);
            read_pair_pos(data, subtable, out, store);
        }
    }
    return !out.pairs.empty() || !out.classes.empty();
}

KernPairs& table_for(FT_Face face, int weight) {
    // Keyed by weight as well as face: the instance decides which feature tables apply.
    static std::map<std::pair<FT_Face, int>, KernPairs> tables;
    const auto key = std::make_pair(face, weight);
    const auto it = tables.find(key);
    if (it != tables.end()) {
        return it->second;
    }
    KernPairs pairs;
    read_gpos_kern(face, weight, pairs);
    return tables.emplace(key, std::move(pairs)).first->second;
}

}  // namespace

std::int16_t gpos_kern_units(FT_Face face, int weight, std::uint32_t left, std::uint32_t right) {
    if (face == nullptr || left == 0 || right == 0) {
        return 0;
    }
    const KernPairs& table = table_for(face, weight);
    const auto first = table.pairs.find(left);
    if (first != table.pairs.end()) {
        for (const auto& entry : first->second) {
            if (entry.first == right) {
                return entry.second;
            }
        }
    }
    for (const KernPairs::ClassPairs& classes : table.classes) {
        if (classes.covered.find(left) == classes.covered.end()) {
            continue;  // a first glyph outside the coverage table is not adjusted here
        }
        // A glyph with no explicit ClassDef entry is class 0, and class 0 carries real values: the
        // (A, V) pair of Noto Sans SC is class (0, 41) at -11 units. Treating "absent from the
        // ClassDef" as "not covered" dropped every pair whose first glyph is class 0.
        std::uint16_t c1 = 0;
        const auto first_class = classes.first_class.find(left);
        if (first_class != classes.first_class.end()) {
            c1 = first_class->second;
        }
        std::uint16_t c2 = 0;
        const auto right_class = classes.second_class.find(right);
        if (right_class != classes.second_class.end()) {
            c2 = right_class->second;
        }
        if (c1 >= classes.class1_count || c2 >= classes.class2_count) {
            continue;
        }
        const std::int16_t value =
            classes.value[static_cast<std::size_t>(c1) * classes.class2_count + c2];
        if (value != 0) {
            return value;
        }
    }
    return 0;
}

}  // namespace text
}  // namespace zui
