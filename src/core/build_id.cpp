#include "build_id.h"

#ifndef OT_BUILD_HASH
#define OT_BUILD_HASH "dev"
#endif

namespace ot {
const char kBuildHash[] = OT_BUILD_HASH;
const char kBuildDate[] = __DATE__;
}  // namespace ot
