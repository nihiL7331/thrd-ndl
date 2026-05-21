#pragma once

#include "tcb.h"

tcb_t* get_curr_thrd(void);
void   resume_thrd(tcb_t* thrd);
