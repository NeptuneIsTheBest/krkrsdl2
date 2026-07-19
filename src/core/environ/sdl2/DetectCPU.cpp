//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Fixed CPU capabilities for the only supported target: macOS arm64.
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tvpgl_ia32_intf.h"
#include "SysInitIntf.h"

extern "C"
{
	tjs_uint32 TVPCPUType = 0;
}

static bool TVPCPUChecked = false;

static void TVPDisableCPU(tjs_uint32 featurebit, const tjs_char *name)
{
	tTJSVariant val;
	if(TVPGetCommandLine(name, &val))
	{
		ttstr str = val;
		if(str == TJS_W("no"))
			TVPCPUType &= ~featurebit;
		else if(str == TJS_W("force"))
			TVPCPUType |= featurebit;
	}
}

void TVPDetectCPU()
{
	if(TVPCPUChecked) return;
	TVPCPUChecked = true;

	// The SSE-family bits select implementations compiled through SIMDe. They
	// describe available compatibility code paths, not native x86 hardware.
	TVPCPUType =
		TVP_CPU_FAMILY_ARM64 |
		TVP_CPU_HAS_MMX |
		TVP_CPU_HAS_3DN |
		TVP_CPU_HAS_SSE |
		TVP_CPU_HAS_SSE2 |
		TVP_CPU_HAS_SSE3 |
		TVP_CPU_HAS_SSSE3 |
		TVP_CPU_HAS_SSE41 |
		TVP_CPU_HAS_SSE42 |
		TVP_CPU_HAS_SSE4a;

	TVPDisableCPU(TVP_CPU_HAS_MMX, TJS_W("-cpummx"));
	TVPDisableCPU(TVP_CPU_HAS_3DN, TJS_W("-cpu3dn"));
	TVPDisableCPU(TVP_CPU_HAS_SSE, TJS_W("-cpusse"));
	TVPDisableCPU(TVP_CPU_HAS_SSE2, TJS_W("-cpusse2"));
	TVPDisableCPU(TVP_CPU_HAS_SSE3, TJS_W("-cpusse3"));
	TVPDisableCPU(TVP_CPU_HAS_SSSE3, TJS_W("-cpussse3"));
	TVPDisableCPU(TVP_CPU_HAS_SSE41, TJS_W("-cpusse41"));
	TVPDisableCPU(TVP_CPU_HAS_SSE42, TJS_W("-cpusse42"));
	TVPDisableCPU(TVP_CPU_HAS_SSE4a, TJS_W("-cpusse4a"));
}

tjs_uint32 TVPGetCPUType()
{
	TVPDetectCPU();
	return TVPCPUType;
}
//---------------------------------------------------------------------------
