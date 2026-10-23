//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// CPU idetification / features detection routine
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tvpgl_ia32_intf.h"
#include "DebugIntf.h"
#include "SysInitIntf.h"

#include "ThreadIntf.h"

#include "Exception.h"
#include "MsgIntf.h"
#include <SDL_platform.h>

#if 0
#endif

/*
	Note: CPU clock measuring routine is in EmergencyExit.cpp, reusing
	hot-key watching thread.
*/

//---------------------------------------------------------------------------
extern "C"
{
	tjs_uint32 TVPCPUType = 0; // CPU type
}

static bool TVPCPUChecked = false;
//---------------------------------------------------------------------------










//---------------------------------------------------------------------------
// TVPDetectCPU
//---------------------------------------------------------------------------
static void TVPDisableCPU(tjs_uint32 featurebit, const tjs_char *name)
{
	tTJSVariant val;
	ttstr str;
	if(TVPGetCommandLine(name, &val))
	{
		str = val;
		if(str == TJS_W("no"))
			TVPCPUType &=~ featurebit;
		else if(str == TJS_W("force"))
			TVPCPUType |= featurebit;
	}
}
//---------------------------------------------------------------------------
void TVPDetectCPU()
{
	if(TVPCPUChecked) return;
	TVPCPUChecked = true;

	TVPCPUType &= ~ TVP_CPU_FEATURE_MASK;

#ifdef __i386__
	TVPCPUType |= TVP_CPU_FAMILY_X86;
#endif

#ifdef __x86_64__
	TVPCPUType |= TVP_CPU_FAMILY_X86_64;
#endif

#ifdef __MMX__
	TVPCPUType |= TVP_CPU_HAS_MMX;
#endif

#ifdef __3dNOW__
	TVPCPUType |= TVP_CPU_HAS_3DN;
#endif

#ifdef __SSE__
	TVPCPUType |= TVP_CPU_HAS_SSE;
#endif

#ifdef __SSE2__
	TVPCPUType |= TVP_CPU_HAS_SSE2;
#endif

#ifdef __SSE3__
	TVPCPUType |= TVP_CPU_HAS_SSE3;
#endif

#ifdef __SSSE3__
	TVPCPUType |= TVP_CPU_HAS_SSSE3;
#endif

#ifdef __SSE4_1__
	TVPCPUType |= TVP_CPU_HAS_SSE41;
#endif

#ifdef __SSE4_2__
	TVPCPUType |= TVP_CPU_HAS_SSE42;
#endif

#ifdef __SSE4a__
	TVPCPUType |= TVP_CPU_HAS_SSE4a;
#endif

#ifdef __AVX__
	TVPCPUType |= TVP_CPU_HAS_AVX;
#endif

#ifdef __AVX2__
	TVPCPUType |= TVP_CPU_HAS_AVX2;
#endif

#ifdef __FMA__
	TVPCPUType |= TVP_CPU_HAS_FMA3;
#endif

#ifdef __AES__
	TVPCPUType |= TVP_CPU_HAS_AES;
#endif

#ifndef DISABLE_USE_VECTOR_CODEPATHS
	TVPCPUType |= TVP_CPU_HAS_MMX;
	TVPCPUType |= TVP_CPU_HAS_3DN;
	TVPCPUType |= TVP_CPU_HAS_SSE;
	TVPCPUType |= TVP_CPU_HAS_SSE2;
	TVPCPUType |= TVP_CPU_HAS_SSE3;
	TVPCPUType |= TVP_CPU_HAS_SSSE3;
	TVPCPUType |= TVP_CPU_HAS_SSE41;
	TVPCPUType |= TVP_CPU_HAS_SSE42;
	TVPCPUType |= TVP_CPU_HAS_SSE4a;
#endif

	// Disable or enable cpu features by option
	TVPDisableCPU(TVP_CPU_HAS_MMX,  TJS_W("-cpummx"));
	TVPDisableCPU(TVP_CPU_HAS_3DN,  TJS_W("-cpu3dn"));
	TVPDisableCPU(TVP_CPU_HAS_SSE,  TJS_W("-cpusse"));
	TVPDisableCPU(TVP_CPU_HAS_CMOV, TJS_W("-cpucmov"));
	TVPDisableCPU(TVP_CPU_HAS_E3DN, TJS_W("-cpue3dn"));
	TVPDisableCPU(TVP_CPU_HAS_EMMX, TJS_W("-cpuemmx"));
	TVPDisableCPU(TVP_CPU_HAS_SSE2, TJS_W("-cpusse2"));

	TVPDisableCPU(TVP_CPU_HAS_SSE3, TJS_W("-cpusse3"));
	TVPDisableCPU(TVP_CPU_HAS_SSSE3, TJS_W("-cpussse3"));
	TVPDisableCPU(TVP_CPU_HAS_SSE41, TJS_W("-cpusse41"));
	TVPDisableCPU(TVP_CPU_HAS_SSE42, TJS_W("-cpusse42"));
	TVPDisableCPU(TVP_CPU_HAS_SSE4a, TJS_W("-cpusse4a"));
	TVPDisableCPU(TVP_CPU_HAS_AVX, TJS_W("-cpuavx"));
	TVPDisableCPU(TVP_CPU_HAS_AVX2, TJS_W("-cpuavx2"));
	TVPDisableCPU(TVP_CPU_HAS_FMA3, TJS_W("-cpufma3"));
	TVPDisableCPU(TVP_CPU_HAS_AES, TJS_W("-cpuaes"));

}
//---------------------------------------------------------------------------





#if 0
//---------------------------------------------------------------------------
// jpeg and png loader support functions
//---------------------------------------------------------------------------
unsigned long MMXReady = 0;
extern "C"
{
	void __fastcall CheckMMX(void)
	{
		TVPDetectCPU();
		MMXReady = TVPCPUType & TVP_CPU_HAS_MMX;
	}
}
//---------------------------------------------------------------------------
#endif





//---------------------------------------------------------------------------
// TVPGetCPUType
//---------------------------------------------------------------------------
tjs_uint32 TVPGetCPUType()
{
	TVPDetectCPU();
	return TVPCPUType;
}
//---------------------------------------------------------------------------

