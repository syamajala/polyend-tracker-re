// Build identification: the git commit the image was built from.
#pragma once

namespace ot {
// `git describe --always --dirty` at build time (e.g. "e5bb173" or
// "e5bb173-dirty"); "unknown" without git, "dev" in host / simulator
// builds. Set by the Makefile on build_id.cpp only, which recompiles alone
// when the hash changes.
extern const char kBuildHash[];
// The date that file was compiled ("Oct 24 2026").
extern const char kBuildDate[];
}  // namespace ot
