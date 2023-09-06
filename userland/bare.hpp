#pragma once

extern "C" {

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>

}

#define bareEfiAssert(code) { auto runtimeEfiAssert__res = code; if (runtimeEfiAssert__res != EFI_SUCCESS) { bare::fatalError(); } }

namespace bare {

[[maybe_unused]] [[noreturn]] static void fatalError(void) {
	while (true) {
		CpuPause();
	}
}

[[maybe_unused]] static void sleep(UINTN tscFrequency, UINTN microseconds) {
	auto begin = AsmReadTsc();

	auto tscFullSleepDelay = microseconds * tscFrequency / 1e6;

	while (true) {
		auto cur = AsmReadTsc();
		if (cur - begin > tscFullSleepDelay)
			break;
	}
}

class GraphicsOutput
{
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION m_modeInfo;
	void *m_displayFramebuffer;
	void *m_drawFramebuffer;
	UINTN m_lineStride;

public:
	static inline constexpr UINTN pixelStride = 4;

	// `modeInfo.PixelFormat` must be `PixelRedGreenBlueReserved8BitPerColor` or `PixelBlueGreenRedReserved8BitPerColor`
	// displayFramebuffer is the actual matrix that will display the picture to the user when updated
	// drawFramebuffer is optional, if not `nullptr` it will be used for rendering. The picture will be
	// made visible only after a call to the `present` method.
	GraphicsOutput(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION modeInfo, void *displayFramebuffer, void *drawFramebuffer) :
		m_modeInfo(modeInfo),
		m_displayFramebuffer(displayFramebuffer),
		m_drawFramebuffer(drawFramebuffer),
		m_lineStride(modeInfo.PixelsPerScanLine * pixelStride)
	{
	}

	UINTN getWidth(void) const {
		return m_modeInfo.HorizontalResolution;
	}

	UINTN getHeight(void) const {
		return m_modeInfo.VerticalResolution;
	}

	union Pixel {
		struct {
			UINT8 r, g, b;
		} comps;
		struct {
			UINT8 values[3];
		} vector;
	};

	EFI_GRAPHICS_PIXEL_FORMAT getPixelFormat(void) const {
		return m_modeInfo.PixelFormat;
	}

	UINT8* getPixelOffset(UINTN x, UINTN y) {
		auto scanline = reinterpret_cast<UINT8*>(m_drawFramebuffer) + y * m_lineStride;
		return scanline + x * pixelStride;
	}

	// pixel must be in the ordering defined by `modeInfo.PixelFormat`
	void draw(UINTN x, UINTN y, UINT8 pixel[3]) {
		auto offset = getPixelOffset(x, y);
		for (UINTN k = 0; k < 3; k++)
			offset[k] = pixel[k];
	}

	void present(void) {
		CopyMem(m_displayFramebuffer, m_drawFramebuffer, m_lineStride * m_modeInfo.VerticalResolution);
	}
};

}