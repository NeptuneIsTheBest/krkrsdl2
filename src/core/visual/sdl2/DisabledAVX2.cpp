/*
 * The unmodified krkrz core still contains link-time references to its AVX2
 * entry points even when the CPU feature is disabled. Keep those entry points
 * inert on the macOS arm64 build without compiling the AVX2 implementations.
 */
#include "tjsCommHead.h"

#include <float.h>

#include "LayerBitmapIntf.h"
#include "LayerBitmapImpl.h"
#include "ResampleImageInternal.h"

extern void TVPResampleImageSSE2(
	const tTVPResampleClipping &clip,
	const tTVPImageCopyFuncBase *blendfunc,
	tTVPBaseBitmap *dest,
	const tTVPRect &destrect,
	const tTVPBaseBitmap *src,
	const tTVPRect &srcrect,
	tTVPBBStretchType type,
	tjs_real typeopt
);

void TVPResampleImageAVX2(
	const tTVPResampleClipping &clip,
	const tTVPImageCopyFuncBase *blendfunc,
	tTVPBaseBitmap *dest,
	const tTVPRect &destrect,
	const tTVPBaseBitmap *src,
	const tTVPRect &srcrect,
	tTVPBBStretchType type,
	tjs_real typeopt
)
{
	TVPResampleImageSSE2(clip, blendfunc, dest, destrect, src, srcrect, type, typeopt);
}

void TVPGL_AVX2_Init()
{
	// AVX2 is intentionally unavailable on the only supported architecture.
}
