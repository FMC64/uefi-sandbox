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

	auto graphicsOutput = boot::GraphicsOutputProtocol::query().toBareGraphics(1 << 24, reinterpret_cast<void*>(conventionalMemory.PhysicalStart));

	Print(bootUToC16(u"Done! Press any key to test out runtime rendering, then shut down your machine in 15 seconds..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	bootEfiAssert(SystemTable->BootServices->ExitBootServices(ImageHandle, boot::getMemoryMapKey()));

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

		if (graphicsOutput.getPixelFormat() == PixelRedGreenBlueReserved8BitPerColor) {
			for (UINTN i = 0; i < graphicsOutput.getHeight(); i++) {
				auto scanline = graphicsOutput.getPixelOffset(0, i);
				for (UINTN j = 0; j < graphicsOutput.getWidth(); j++) {
					auto pixel = getPixel(it, j, i);
					for (UINTN k = 0; k < 3; k++)
						scanline[j * 4 + k] = pixel.vector.values[k];
				}
			}
		} else {
			for (UINTN i = 0; i < graphicsOutput.getHeight(); i++) {
				auto scanline = graphicsOutput.getPixelOffset(0, i);
				for (UINTN j = 0; j < graphicsOutput.getWidth(); j++) {
					auto pixel = getPixel(it, j, i);
					for (UINTN k = 0; k < 3; k++)
						scanline[j * 4 + k] = pixel.vector.values[3 - 1 - k];
				}
			}
		}
		graphicsOutput.present();

		bare::sleep(tscFreq, static_cast<UINTN>(1e6 / 60));
	}
	SystemTable->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, nullptr);

	return EFI_SUCCESS;
}