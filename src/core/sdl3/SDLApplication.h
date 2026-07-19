/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#pragma once
#include "tjsCommHead.h"

extern void krkrsdl3_pre_init_platform(void);
extern void krkrsdl3_set_args(int argc, tjs_char **argv);
extern void krkrsdl3_convert_set_args(int argc, char **argv);
extern bool krkrsdl3_init_platform(void);
extern void krkrsdl3_run_main_loop(void);
extern void krkrsdl3_cleanup(void);
