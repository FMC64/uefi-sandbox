extern "C" {

#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ShellLib.h>
#include <Library/PrintLib.h>
#include <Register/Intel/Cpuid.h>
#include <Guid/Acpi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>

}

#include <optional>

#define uToC16(uStr) reinterpret_cast<const CHAR16*>(uStr)

[[noreturn]] static void bootFatalError(const CHAR16 *domain, UINT64 code) {
	Print(uToC16(u"FATAL ERROR: %s: code 0x%x\nRestart your machine."), domain, code);
	while (true) {
		CpuPause();
	}
}

[[maybe_unused]] [[noreturn]] static void runtimeFatalError(void) {
	while (true) {
		CpuPause();
	}
}

#define bootEfiAssert(code) { auto bootEfiAssert__res = code; if (bootEfiAssert__res != EFI_SUCCESS) { bootFatalError(uToC16(u"bootEfiAssert"), bootEfiAssert__res); } }
#define runtimeEfiAssert(code) { auto runtimeEfiAssert__res = code; if (runtimeEfiAssert__res != EFI_SUCCESS) { runtimeFatalError(); } }

[[maybe_unused]] static void bootPrintControlRegisters(void) {
		static constexpr UINT32 msrEferAddr = 0xC0000080;

		auto cr0 = AsmReadCr0();
		auto cr2 = AsmReadCr2();
		auto cr3 = AsmReadCr3();
		auto cr4 = AsmReadCr4();
		auto efer = AsmReadMsr64(msrEferAddr);
		Print(uToC16(u"CR0 = 0x%Lx, CR2 = 0x%Lx, CR3 = 0x%Lx, CR4 = 0x%Lx, EFER = 0x%Lx\n"), cr0, cr2, cr3, cr4, efer);
}

[[maybe_unused]] static UINTN bootGetMemoryMapKey(void) {
	UINTN memoryMapSize = 0, mapKey, descriptorSize;
	UINT32 descriptorVersion;
	auto res = gBS->GetMemoryMap(&memoryMapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
	if (res != EFI_BUFFER_TOO_SMALL)
		bootEfiAssert(res);
	return mapKey;
}

// Fn is a `void (const EFI_MEMORY_DESCRIPTOR &currentMemoryDescriptor)`
template <typename Fn>
static UINTN bootIterateMemoryMap(Fn &&fn) {
	UINTN memoryMapSize = 0, mapKey, descriptorSize;
	UINT32 descriptorVersion;
	{
		auto res = gBS->GetMemoryMap(&memoryMapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
		if (res != EFI_BUFFER_TOO_SMALL)
			bootEfiAssert(res);
	}
	//Print(uToC16(u"Memory map size: 0x%Lx bytes, mapKey = 0x%Lx, descriptorSize = 0x%Lx, descriptorVersion = 0x%Lx\n"), memoryMapSize, mapKey, descriptorSize, descriptorVersion);

	if (descriptorVersion != EFI_MEMORY_DESCRIPTOR_VERSION)
		bootFatalError(uToC16(u"GetMemoryMap.descriptorVersion was expected to be EFI_MEMORY_DESCRIPTOR_VERSION"), descriptorVersion);

	UINT8 memoryMapBytes[memoryMapSize];
	bootEfiAssert(gBS->GetMemoryMap(&memoryMapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(memoryMapBytes), &mapKey, &descriptorSize, &descriptorVersion));

	UINTN descriptorCount = memoryMapSize / descriptorSize;
	for (UINTN i = 0; i < descriptorCount; i++) {
		auto &curDescriptor = *reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(memoryMapBytes + i * descriptorSize);
		fn(curDescriptor);
	}
	return descriptorCount;
}

static EFI_MEMORY_DESCRIPTOR bootFindConventionalMemory(void) {
	std::optional<EFI_MEMORY_DESCRIPTOR> res;

	auto descriptorCount = bootIterateMemoryMap([&res](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		if (curDescriptor.Type != EfiConventionalMemory)
			return;

		if (!res || curDescriptor.NumberOfPages > res->NumberOfPages)
			res = curDescriptor;
	});
	if (!res)
		bootFatalError(uToC16(u"findConventionalMemory: no mapping found of type EfiConventionalMemory (code is memory descriptor count)"), descriptorCount);
	return *res;
}

[[maybe_unused]] static void bootPrintMemoryTotals(void) {
	UINTN totalPages[EfiMaxMemoryType] {};

	auto descriptorCount = bootIterateMemoryMap([&totalPages](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		totalPages[curDescriptor.Type] += curDescriptor.NumberOfPages;
	});

	Print(uToC16(u"Enumerating memory type totals, 0x%Lx descriptors:\n"), descriptorCount);
	for (UINTN i = 0; i < EfiMaxMemoryType; i++) {
		Print(uToC16(u"Type 0x%Lx: %,Ld bytes (0x%Lx pages)\n"), i, totalPages[i] * static_cast<UINTN>(1 << 12), totalPages[i]);
	}
}

[[maybe_unused]] static void bootPrintMemoryTypeDescriptors(EFI_MEMORY_TYPE memoryTypeBegin, EFI_MEMORY_TYPE memoryTypeEnd, UINTN minPageCount) {
	UINTN smallPageCount = 0;

	UINTN i = 0;
	Print(uToC16(u"Enumerating memory descriptors from type 0x%Lx to 0x%Lx (non inclusive):\n"), static_cast<UINTN>(memoryTypeBegin), static_cast<UINTN>(memoryTypeEnd));
	bootIterateMemoryMap([&i, memoryTypeBegin, memoryTypeEnd, minPageCount, &smallPageCount](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		if (!(curDescriptor.Type >= memoryTypeBegin && curDescriptor.Type < memoryTypeEnd))
			return;
		if (curDescriptor.NumberOfPages < minPageCount) {
			smallPageCount++;
			return;
		}

		Print(uToC16(u"#%Ld at [0x%Lx, 0x%Lx): %,Ld bytes (0x%Lx pages), attr = 0x%Lx\n"), i,
			curDescriptor.PhysicalStart, curDescriptor.PhysicalStart + curDescriptor.NumberOfPages * static_cast<UINTN>(1 << 12),
			curDescriptor.NumberOfPages * static_cast<UINTN>(1 << 12), curDescriptor.NumberOfPages, curDescriptor.Attribute
		);
		i++;

		if (i % 20 == 0) {
			Print(uToC16(u"Press any key to display next page..\n"));
			ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
		}
	});
	if (minPageCount > 0)
		Print(uToC16(u"0x%Lx memory descriptors of less than 0x%Lx pages were omitted\n"), smallPageCount, minPageCount);
}

[[maybe_unused]] static UINTN bootEstimateTscFrequency(void) {
	auto begin = AsmReadTsc();
	bootEfiAssert(gBS->Stall(static_cast<UINTN>(1e6)));
	auto end = AsmReadTsc();
	return end - begin;
}

[[maybe_unused]] static void runtimeSleep(UINTN tscFrequency, UINTN microseconds) {
	auto begin = AsmReadTsc();

	auto tscFullSleepDelay = microseconds * tscFrequency / 1e6;

	while (true) {
		auto cur = AsmReadTsc();
		if (cur - begin > tscFullSleepDelay)
			break;
	}
}

[[maybe_unused]] static void bootPrintGuid(const GUID &guid) {
	Print(uToC16(u"%x %x %x (%x %x %x %x %x %x %x %x)\n"), guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]
	);
}

// Fn is a `bool (EFI_HANDLE handle)`
// When fn returns false, the iterator aborts
template <typename Fn>
static void bootIterateHandles(EFI_LOCATE_SEARCH_TYPE searchType, EFI_GUID *protocol, VOID *searchKey, Fn &&fn) {
	UINTN bufferSize = 0;
	{
		auto res = gBS->LocateHandle(searchType, protocol, searchKey, &bufferSize, nullptr);
		if (res != EFI_BUFFER_TOO_SMALL)
			bootEfiAssert(res);
	}
	UINT8 buffer[bufferSize];
	bootEfiAssert(gBS->LocateHandle(searchType, protocol, searchKey, &bufferSize, reinterpret_cast<EFI_HANDLE*>(buffer)));

	UINTN handleCount = bufferSize / sizeof(EFI_HANDLE);
	for (UINTN i = 0; i < handleCount; i++) {
		auto currentHandle = *reinterpret_cast<const EFI_HANDLE*>(buffer + i * sizeof(EFI_HANDLE));
		if (!fn(currentHandle))
			break;
	}
}

class GraphicsOutputProtocol
{
	EFI_GRAPHICS_OUTPUT_PROTOCOL *m_graphicsOutputProtocol;

public:
	GraphicsOutputProtocol(EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsOutputProtocol) :
		m_graphicsOutputProtocol(graphicsOutputProtocol)
	{
	}

	// Fn is a `void (UINT32 modeNumber, const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION &modeInfo)`
	template <typename Fn>
	void iterateModes(Fn &&fn) const {
		for (UINT32 i = 0; i < m_graphicsOutputProtocol->Mode->MaxMode; i++) {
			UINTN sizeofInfo;
			EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
			bootEfiAssert(m_graphicsOutputProtocol->QueryMode(m_graphicsOutputProtocol, i, &sizeofInfo, &info));
			fn(i, *info);
		}
	}

	void setMode(UINT32 modeNumber) const {
		bootEfiAssert(m_graphicsOutputProtocol->SetMode(m_graphicsOutputProtocol, modeNumber));
	}
};

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

	bootPrintControlRegisters();
	auto conventionalMemory = bootFindConventionalMemory();
	Print(uToC16(u"Conventional memory found at 0x%Lx: %,Ld bytes, attributes = 0x%Lx\n"),
		conventionalMemory.PhysicalStart, conventionalMemory.NumberOfPages * static_cast<UINTN>(1 << 12), conventionalMemory.Attribute
	);

	bootPrintMemoryTotals();

	//Print(uToC16(u"Press any key to show conventional memory descriptors..\n"));
	//ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
	// Pruning less than 1MiB descriptors
	//bootPrintMemoryTypeDescriptors(EfiConventionalMemory, static_cast<EFI_MEMORY_TYPE>(EfiConventionalMemory + 1), 0xFF);

	//bootPrintMemoryTypeDescriptors(static_cast<EFI_MEMORY_TYPE>(0), EfiMaxMemoryType, 0x04);

	auto tscFreq = bootEstimateTscFrequency();

	Print(uToC16(u"Press any key to move ahead with graphical setup..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION graphicsModeInfo;
	EFI_PHYSICAL_ADDRESS framebuffer;
	{
		EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsOutputProtocolPtr = nullptr;

		{
			bootPrintGuid(gEfiGraphicsOutputProtocolGuid);
			bootIterateHandles(ByProtocol, &gEfiGraphicsOutputProtocolGuid, nullptr, [&graphicsOutputProtocolPtr, ImageHandle](EFI_HANDLE handle) {
				Print(uToC16(u"gEfiGraphicsOutputProtocolGuid handle %p\n"), handle);
				bootEfiAssert(
						gBS->OpenProtocol(
						handle, &gEfiGraphicsOutputProtocolGuid, reinterpret_cast<void**>(&graphicsOutputProtocolPtr),
						ImageHandle, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL
					)
				);
				return true;
			});
			if (graphicsOutputProtocolPtr == nullptr)
				bootFatalError(uToC16(u"gEfiGraphicsOutputProtocolGuid is not supported"), EFI_UNSUPPORTED);
		}

		Print(uToC16(u"Graphics output protocol = %p\n"), graphicsOutputProtocolPtr);

		auto graphicsOutputProtocol = GraphicsOutputProtocol(graphicsOutputProtocolPtr);
		struct Best {
			UINT32 modeNumber;
			EFI_GRAPHICS_OUTPUT_MODE_INFORMATION modeInfo;
		};

		std::optional<Best> best;
		graphicsOutputProtocol.iterateModes([&best](UINT32 modeNumber, const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION &modeInfo) {
			/*Print(uToC16(u"Mode #%u: width = %u, height = %u, format = 0x%x, pixelsPerScanline = %u, rMask = %x, gMask = %x, bMask = %u\n"), modeNumber,
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
			bootFatalError(uToC16(u"graphicsOutputProtocol: no compatible mode found (code is the number of modes available)"), graphicsOutputProtocolPtr->Mode->MaxMode);
		graphicsOutputProtocol.setMode(best->modeNumber);
		graphicsModeInfo = *graphicsOutputProtocolPtr->Mode->Info;
		framebuffer = graphicsOutputProtocolPtr->Mode->FrameBufferBase;
	}

	Print(uToC16(u"Done! Press any key to test out runtime rendering, then shut down your machine in 15 seconds..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	bootEfiAssert(SystemTable->BootServices->ExitBootServices(ImageHandle, bootGetMemoryMapKey()));

	for (UINTN it = 0; it < 30 * 15; it++) {

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

		runtimeSleep(tscFreq, static_cast<UINTN>(1e6 / 30));
	}
	SystemTable->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, nullptr);

	return EFI_SUCCESS;
}