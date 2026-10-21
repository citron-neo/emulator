/* This file is part of the dynarmic project.
 * Copyright (c) 2023 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#if defined(ARCHITECTURE_x86_64)
#    include "dynarmic/backend/x64/mig/mach_exc_server.c"
#elif defined(ARCHITECTURE_arm64)
#    include "dynarmic/backend/arm64/mig/mach_exc_server.c"
#else
#    error "Invalid architecture"
#endif
