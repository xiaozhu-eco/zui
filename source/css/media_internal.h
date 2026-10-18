#ifndef ZUI_SOURCE_CSS_MEDIA_INTERNAL_H
#define ZUI_SOURCE_CSS_MEDIA_INTERNAL_H

// Media-query parse/eval types shared by media_query.cpp and media_eval.cpp.

#include "css/values_internal.h"

namespace zui {
namespace css {
namespace values_detail {

enum class MfCmp { Lt, Le, Gt, Ge, Eq };

struct MfBound {
    enum class Kind { Length, Ratio, Bare, Resolution } kind = Kind::Length;
    float px = 0.f;
    float num = 0.f;
    float den = 1.f;
};

bool parse_mq_length(Scan& sc, const MediaState& media, float& px);
bool parse_mq_integer(std::string_view text, float& out);
bool parse_ratio_text(std::string_view text, float& num, float& den);
bool parse_resolution_text(std::string_view text, float& dppx);
bool parse_mf_bound(Scan& sc, const MediaState& media, MfBound& bound);
bool mf_lt_family(MfCmp cmp);
bool relate_mf(MfCmp cmp, float left, float right);
bool relate_ratio(MfCmp cmp, float left_num, float left_den, float right_num, float right_den);
bool feature_matches(std::string_view name, std::string_view value, bool boolean,
                     const MediaState& media);
bool eval_axis_cmp(std::string_view name, MfCmp cmp, bool feature_on_left, const MfBound& bound,
                   const MediaState& media, bool& value);

}  // namespace values_detail
}  // namespace css
}  // namespace zui

#endif
