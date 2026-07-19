/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "SDLApplication.h"
#include "SysInitImpl.h"

extern "C" int main(int argc, char **argv)
{
	try
	{
		krkrsdl2_pre_init_platform();

		krkrsdl2_convert_set_args(argc, argv);

		if (krkrsdl2_init_platform())
		{
			TVPTerminateCode = 0;
			return TVPTerminateCode;
		}

		krkrsdl2_run_main_loop();

		krkrsdl2_cleanup();
	}
	catch (...)
	{
		TVPTerminateCode = 2;
		return TVPTerminateCode;
	}
	return TVPTerminateCode;
}
