#ifndef SHARE_BENCH_H
#define SHARE_BENCH_H

namespace share {

/// Headless port of xz-share's `xz-share-bench`: the same discovery + transfer code without the
/// UI, so the wire protocol can be exercised with `recv` / `send` (two machines, or two
/// processes on one) and with a self-check loopback on POSIX.
int bench_main(int argc, char** argv);

}  // namespace share

#endif  // SHARE_BENCH_H
