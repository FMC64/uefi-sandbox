#include "boot.hpp"
#include "bare.hpp"

extern "C" {

#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>

}

/**
	as the real entry point for the application.

	@param[in] ImageHandle    The firmware allocated handle for the EFI image.  
	@param[in] SystemTable    A pointer to the EFI System Table.

	@retval EFI_SUCCESS       The entry point is executed successfully.
	@retval other             Some error occurs when executing this entry point.
**/
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
	bootEfiAssert(ShellInitialize());

	boot::printControlRegisters();
	auto conventionalMemory = boot::findConventionalMemory();
	Print(bootUToC16(u"Conventional memory found at 0x%Lx: %,Ld bytes, attributes = 0x%Lx\n"),
		conventionalMemory.PhysicalStart, conventionalMemory.NumberOfPages * static_cast<UINTN>(1 << 12), conventionalMemory.Attribute
	);

	boot::printMemoryTotals();

	//Print(bootUToC16(u"Press any key to show conventional memory descriptors..\n"));
	//ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
	// Pruning less than 1MiB descriptors
	//bootPrintMemoryTypeDescriptors(EfiConventionalMemory, static_cast<EFI_MEMORY_TYPE>(EfiConventionalMemory + 1), 0xFF);

	//bootPrintMemoryTypeDescriptors(static_cast<EFI_MEMORY_TYPE>(0), EfiMaxMemoryType, 0x04);

	auto tscFreq = boot::estimateTscFrequency();

	Print(bootUToC16(u"Press any key to move ahead with graphical setup..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION graphicsModeInfo;
	EFI_PHYSICAL_ADDRESS gpuFramebuffer;
	{
		EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsOutputProtocolPtr = nullptr;

		{
			boot::printGuid(gEfiGraphicsOutputProtocolGuid);
			boot::iterateHandles(ByProtocol, &gEfiGraphicsOutputProtocolGuid, nullptr, [&graphicsOutputProtocolPtr, ImageHandle](EFI_HANDLE handle) {
				Print(bootUToC16(u"gEfiGraphicsOutputProtocolGuid handle %p\n"), handle);
				bootEfiAssert(
						gBS->OpenProtocol(
						handle, &gEfiGraphicsOutputProtocolGuid, reinterpret_cast<void**>(&graphicsOutputProtocolPtr),
						ImageHandle, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL
					)
				);
				return true;
			});
			if (graphicsOutputProtocolPtr == nullptr)
				boot::fatalError(bootUToC16(u"gEfiGraphicsOutputProtocolGuid is not supported"), EFI_UNSUPPORTED);
		}

		Print(bootUToC16(u"Graphics output protocol = %p\n"), graphicsOutputProtocolPtr);

		auto graphicsOutputProtocol = boot::GraphicsOutputProtocol(graphicsOutputProtocolPtr);
		struct Best {
			UINT32 modeNumber;
			EFI_GRAPHICS_OUTPUT_MODE_INFORMATION modeInfo;
		};

		std::optional<Best> best;
		graphicsOutputProtocol.iterateModes([&best](UINT32 modeNumber, const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION &modeInfo) {
			/*Print(bootUToC16(u"Mode #%u: width = %u, height = %u, format = 0x%x, pixelsPerScanline = %u, rMask = %x, gMask = %x, bMask = %u\n"), modeNumber,
				modeInfo.HorizontalResolution, modeInfo.VerticalResolution, modeInfo.PixelFormat, modeInfo.PixelsPerScanLine,
				modeInfo.PixelInformation.RedMask, modeInfo.PixelInformation.GreenMask, modeInfo.PixelInformation.BlueMask
			);*/
			if (!(modeInfo.PixelFormat == PixelRedGreenBlueReserved8BitPerColor || modeInfo.PixelFormat == PixelBlueGreenRedReserved8BitPerColor))
				return;

			if (!best || modeInfo.HorizontalResolution * modeInfo.VerticalResolution > best->modeInfo.HorizontalResolution * best->modeInfo.VerticalResolution) {
				best = Best {
					.modeNumber = modeNumber,
					.modeInfo = modeInfo
				};
			}
		});
		if (!best)
			boot::fatalError(bootUToC16(u"graphicsOutputProtocol: no compatible mode found (code is the number of modes available)"), graphicsOutputProtocolPtr->Mode->MaxMode);
		graphicsOutputProtocol.setMode(best->modeNumber);
		graphicsModeInfo = *graphicsOutputProtocolPtr->Mode->Info;
		gpuFramebuffer = graphicsOutputProtocolPtr->Mode->FrameBufferBase;
	}

	Print(bootUToC16(u"Done! Press any key to test out runtime rendering, then shut down your machine in 15 seconds..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	bootEfiAssert(SystemTable->BootServices->ExitBootServices(ImageHandle, boot::getMemoryMapKey()));

	auto framebuffer = reinterpret_cast<UINT8*>(conventionalMemory.PhysicalStart);
	for (UINTN it = 0; it < 60 * 15; it++) {

		union Pixel {
			struct {
				UINT8 r, g, b;
			} comps;
			struct {
				UINT8 values[3];
			} vector;
		};

		auto getPixel = [](UINTN it, UINTN x, UINTN y) -> Pixel {
			return (((x + it) / 8) ^ ((y + it * 3) / 16)) & 1 ?
				Pixel{
					.comps{
						.r = 0xFF,
						.g = static_cast<UINT8>((x + it) & 0xFF),
						.b = 0xFF
					}
				} : Pixel{
					.comps{
						.r = 0x80,
						.g = static_cast<UINT8>(0x80 + (y + it * 3) / 64),
						.b = 0xFF
					}
				};
		};

		if (graphicsModeInfo.PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
			for (UINTN i = 0; i < graphicsModeInfo.VerticalResolution; i++) {
				auto scanline = reinterpret_cast<UINT8*>(framebuffer) + i * graphicsModeInfo.PixelsPerScanLine * 4;
				for (UINTN j = 0; j < graphicsModeInfo.HorizontalResolution; j++) {
					auto pixel = getPixel(it, j, i);
					for (UINTN k = 0; k < 3; k++)
						scanline[j * 4 + k] = pixel.vector.values[k];
				}
			}
		} else {
			for (UINTN i = 0; i < graphicsModeInfo.VerticalResolution; i++) {
				auto scanline = reinterpret_cast<UINT8*>(framebuffer) + i * graphicsModeInfo.PixelsPerScanLine * 4;
				for (UINTN j = 0; j < graphicsModeInfo.HorizontalResolution; j++) {
					auto pixel = getPixel(it, j, i);
					for (UINTN k = 0; k < 3; k++)
						scanline[j * 4 + k] = pixel.vector.values[3 - 1 - k];
				}
			}
		}
		CopyMem(reinterpret_cast<void*>(gpuFramebuffer), framebuffer, graphicsModeInfo.PixelsPerScanLine * 4 * graphicsModeInfo.VerticalResolution);

		bare::sleep(tscFreq, static_cast<UINTN>(1e6 / 60));
	}
	SystemTable->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, nullptr);

	return EFI_SUCCESS;
}