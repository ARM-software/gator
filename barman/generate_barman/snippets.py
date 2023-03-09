# Copyright (C) 2023 by Arm Limited
#
# SPDX-License-Identifier: BSD-3-Clause

barman_c_heading_snippet = """/* Copyright (C) 2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "barman.h"

#if (!defined(BARMAN_DISABLED)) || (!BARMAN_DISABLED)
#include "barman-internal.h"
#include "barman-api.c"
#include "barman-atomics.c"
#include "barman-custom-counter-definitions.c"
#include "barman-initialize.c"
#include "barman-protocol.c"
#include "barman-memutils.c"
#include "multicore/barman-multicore-mpcore.c"
"""

barman_c_footer_snippet = """
#endif /* (!defined(BARMAN_DISABLED)) || (!BARMAN_DISABLED) */
"""

streaming_iface_snippet = """#if BM_DATASTORE_USES_STREAMING_INTERFACE
#include "data-store/barman-streaming-interface.c"
#endif
"""

pmu_driver_snippet = """#if BM_ARM_64BIT_STATE
#include "aarch64/barman-cache.c"
#include "aarch64/barman-external-dependencies.c"
#elif BM_ARM_32BIT_STATE
#include "arm/barman-cache.c"
#include "arm/barman-external-dependencies.c"
#endif
"""