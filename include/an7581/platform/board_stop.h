/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_BOARD_STOP_H
#define AN7581_BOARD_STOP_H

#include "an7581/runtime/status.h"

typedef enum npu_runtime_result (*an7581_board_prepare_stop)(void *context);
typedef enum npu_runtime_result (*an7581_board_resume)(void *context);

#endif
