# CSS is one module (LAYERS.md). Kernel tests pick subsets, so the files are
# grouped by cascade phase and exposed as INTERFACE collections.

zui_add_interface_sources(zui_src_css_engine
  engine/engine.cpp
  engine/facts.cpp
  engine/facts_neighbor.cpp
  engine/cascade.cpp
  engine/selector_match.cpp
  engine/selector_nth.cpp
  engine/selector_pseudo.cpp
  engine/rule_cascade.cpp
  engine/rule_expand.cpp
  engine/rule_index.cpp
  engine/computed_style.cpp
  engine/computed_custom.cpp
  engine/computed_used.cpp
  engine/style_diff.cpp
  engine/generated_content.cpp
  engine/selection_style.cpp
  engine/cascade_walk.cpp
  engine/cascade_children.cpp
  engine/cascade_apply.cpp
  engine/http_fetch.cpp
)

zui_add_interface_sources(zui_src_css_shorthand
  shorthand/shorthand.cpp
  shorthand/serialize.cpp
  shorthand/box.cpp
  shorthand/tokenize.cpp
  shorthand/background.cpp
  shorthand/grid_template.cpp
  shorthand/custom.cpp
)

zui_add_interface_sources(zui_src_css_values
  values.cpp
  media_query.cpp
  media_eval.cpp
  substitution.cpp
  supports.cpp
  math_length.cpp
  color.cpp
  color_parse.cpp
  color_convert.cpp
  color_mix.cpp
  image_set.cpp
)

zui_add_interface_sources(zui_src_css_transition
  transition/transition.cpp
  transition/timing.cpp
  transition/color.cpp
  transition/transform.cpp
  transition/value.cpp
  transition/easing.cpp
  transition/animation.cpp
)

add_library(zui_src_css INTERFACE)
target_link_libraries(zui_src_css INTERFACE
  zui_src_css_engine
  zui_src_css_shorthand
  zui_src_css_values
  zui_src_css_transition
)
