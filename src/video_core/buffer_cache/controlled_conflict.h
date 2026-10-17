// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdexcept>

namespace VideoCommon {

// A legacy producer violated controlled range isolation. This is not a retry result:
// the GPU loop stops dispatch and retires command fences through its failure path.
class ControlledProducerConflict : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace VideoCommon
