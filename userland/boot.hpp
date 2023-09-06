#pragma once

extern "C" {

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/ShellLib.h>
#include <Uefi/UefiSpec.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

}

#include <optional>

#define bootUToC16(uStr) reinterpret_cast<const CHAR16*>(uStr)
#define bootEfiAssert(code) { auto bootEfiAssert__res = code; if (bootEfiAssert__res != EFI_SUCCESS) { boot::fatalError(bootUToC16(u"bootEfiAssert"), bootEfiAssert__res); } }

namespace boot {

[[noreturn]] static void fatalError(const CHAR16 *domain, UINT64 code) {
	Print(bootUToC16(u"FATAL ERROR: %s: code 0x%x\nRestart your machine."), domain, code);
	while (true) {
		CpuPause();
	}
}

[[maybe_unused]] static void printControlRegisters(void) {
		static constexpr UINT32 msrEferAddr = 0xC0000080;

		auto cr0 = AsmReadCr0();
		auto cr2 = AsmReadCr2();
		auto cr3 = AsmReadCr3();
		auto cr4 = AsmReadCr4();
		auto efer = AsmReadMsr64(msrEferAddr);
		Print(bootUToC16(u"CR0 = 0x%Lx, CR2 = 0x%Lx, CR3 = 0x%Lx, CR4 = 0x%Lx, EFER = 0x%Lx\n"), cr0, cr2, cr3, cr4, efer);
}

[[maybe_unused]] static UINTN getMemoryMapKey(void) {
	UINTN memoryMapSize = 0, mapKey, descriptorSize;
	UINT32 descriptorVersion;
	auto res = gBS->GetMemoryMap(&memoryMapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
	if (res != EFI_BUFFER_TOO_SMALL)
		bootEfiAssert(res);
	return mapKey;
}

// Fn is a `void (const EFI_MEMORY_DESCRIPTOR &currentMemoryDescriptor)`
template <typename Fn>
static UINTN iterateMemoryMap(Fn &&fn) {
	UINTN memoryMapSize = 0, mapKey, descriptorSize;
	UINT32 descriptorVersion;
	{
		auto res = gBS->GetMemoryMap(&memoryMapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
		if (res != EFI_BUFFER_TOO_SMALL)
			bootEfiAssert(res);
	}
	//Print(bootUToC16(u"Memory map size: 0x%Lx bytes, mapKey = 0x%Lx, descriptorSize = 0x%Lx, descriptorVersion = 0x%Lx\n"), memoryMapSize, mapKey, descriptorSize, descriptorVersion);

	if (descriptorVersion != EFI_MEMORY_DESCRIPTOR_VERSION)
		fatalError(bootUToC16(u"GetMemoryMap.descriptorVersion was expected to be EFI_MEMORY_DESCRIPTOR_VERSION"), descriptorVersion);

	UINT8 memoryMapBytes[memoryMapSize];
	bootEfiAssert(gBS->GetMemoryMap(&memoryMapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(memoryMapBytes), &mapKey, &descriptorSize, &descriptorVersion));

	UINTN descriptorCount = memoryMapSize / descriptorSize;
	for (UINTN i = 0; i < descriptorCount; i++) {
		auto &curDescriptor = *reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(memoryMapBytes + i * descriptorSize);
		fn(curDescriptor);
	}
	return descriptorCount;
}

[[maybe_unused]] static EFI_MEMORY_DESCRIPTOR findConventionalMemory(void) {
	std::optional<EFI_MEMORY_DESCRIPTOR> res;

	auto descriptorCount = iterateMemoryMap([&res](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		if (curDescriptor.Type != EfiConventionalMemory)
			return;

		if (!res || curDescriptor.NumberOfPages > res->NumberOfPages)
			res = curDescriptor;
	});
	if (!res)
		fatalError(bootUToC16(u"findConventionalMemory: no mapping found of type EfiConventionalMemory (code is memory descriptor count)"), descriptorCount);
	return *res;
}

[[maybe_unused]] static void printMemoryTotals(void) {
	UINTN totalPages[EfiMaxMemoryType] {};

	auto descriptorCount = iterateMemoryMap([&totalPages](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		totalPages[curDescriptor.Type] += curDescriptor.NumberOfPages;
	});

	Print(bootUToC16(u"Enumerating memory type totals, 0x%Lx descriptors:\n"), descriptorCount);
	for (UINTN i = 0; i < EfiMaxMemoryType; i++) {
		Print(bootUToC16(u"Type 0x%Lx: %,Ld bytes (0x%Lx pages)\n"), i, totalPages[i] * static_cast<UINTN>(1 << 12), totalPages[i]);
	}
}

[[maybe_unused]] static void printMemoryTypeDescriptors(EFI_MEMORY_TYPE memoryTypeBegin, EFI_MEMORY_TYPE memoryTypeEnd, UINTN minPageCount) {
	UINTN smallPageCount = 0;

	UINTN i = 0;
	Print(bootUToC16(u"Enumerating memory descriptors from type 0x%Lx to 0x%Lx (non inclusive):\n"), static_cast<UINTN>(memoryTypeBegin), static_cast<UINTN>(memoryTypeEnd));
	iterateMemoryMap([&i, memoryTypeBegin, memoryTypeEnd, minPageCount, &smallPageCount](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		if (!(curDescriptor.Type >= memoryTypeBegin && curDescriptor.Type < memoryTypeEnd))
			return;
		if (curDescriptor.NumberOfPages < minPageCount) {
			smallPageCount++;
			return;
		}

		Print(bootUToC16(u"#%Ld at [0x%Lx, 0x%Lx): %,Ld bytes (0x%Lx pages), attr = 0x%Lx\n"), i,
			curDescriptor.PhysicalStart, curDescriptor.PhysicalStart + curDescriptor.NumberOfPages * static_cast<UINTN>(1 << 12),
			curDescriptor.NumberOfPages * static_cast<UINTN>(1 << 12), curDescriptor.NumberOfPages, curDescriptor.Attribute
		);
		i++;

		if (i % 20 == 0) {
			Print(bootUToC16(u"Press any key to display next page..\n"));
			ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
		}
	});
	if (minPageCount > 0)
		Print(bootUToC16(u"0x%Lx memory descriptors of less than 0x%Lx pages were omitted\n"), smallPageCount, minPageCount);
}

[[maybe_unused]] static UINTN estimateTscFrequency(void) {
	auto begin = AsmReadTsc();
	bootEfiAssert(gBS->Stall(static_cast<UINTN>(1e6)));
	auto end = AsmReadTsc();
	return end - begin;
}

[[maybe_unused]] static void printGuid(const GUID &guid) {
	Print(bootUToC16(u"%x %x %x (%x %x %x %x %x %x %x %x)\n"), guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]
	);
}

// Fn is a `bool (EFI_HANDLE handle)`
// When fn returns false, the iterator aborts
template <typename Fn>
static void iterateHandles(EFI_LOCATE_SEARCH_TYPE searchType, EFI_GUID *protocol, VOID *searchKey, Fn &&fn) {
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

}