#pragma once

#include "Util.hpp"

namespace flattriples {

// Rebuild projection matrix for one render target with current config values.
// Used per-frame so bezel/horizon adjustments take effect immediately.
void rebuild_flat_projection(size_t rt);

}
