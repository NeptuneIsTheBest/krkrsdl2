/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "SDLApplication.h"
#include "SysInitImpl.h"

extern "C" int main(int argc, char **argv)
{
	try
	{
		krkrsdl3_pre_init_platform();

		krkrsdl3_convert_set_args(argc, argv);

		if (krkrsdl3_init_platform())
		{
			TVPTerminateCode = 0;
			krkrsdl3_cleanup();
			return TVPTerminateCode;
		}

		krkrsdl3_run_main_loop();

		krkrsdl3_cleanup();
	}
	catch (...)
	{
		krkrsdl3_cleanup();
		TVPTerminateCode = 2;
		return TVPTerminateCode;
	}
	return TVPTerminateCode;
}
