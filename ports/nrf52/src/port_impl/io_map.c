// Copyright 2019 jem@seethis.link
// Licensed under the MIT license (http://opensource.org/licenses/MIT)

#include "core/hardware.h"

io_port_t * const g_io_port_map[IO_PORT_COUNT] = {
    NRF_P0,
#if IO_PORT_COUNT > 1
    NRF_P1,
#endif
};
