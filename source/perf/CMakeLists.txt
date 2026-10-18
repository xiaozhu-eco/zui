# Debug work-unit counters. Leaf instrumentation: html / css / layout / paint /
# input / app all include this header. It must not live under app/.
# perf_budget.cpp includes trace/trace.h only to mirror FrameWork at publish
# (inline no-ops unless the consumer defined ZUI_TRACE_PERFETTO). Do not
# INTERFACE-link zui_src_trace from here: zui already compiles that TU.

zui_add_interface_sources(zui_src_perf
  perf_budget.cpp
)
