extern "C" {

#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ShellLib.h>
#include <Library/PrintLib.h>
#include <Register/Intel/Cpuid.h>
#include <Guid/Acpi.h>
#include <Library/UefiBootServicesTableLib.h>

}

#include <optional>

#define uToC16(uStr) reinterpret_cast<const CHAR16*>(uStr)

[[noreturn]] static void fatalError(const CHAR16 *domain, UINT64 code) {
	Print(uToC16(u"FATAL ERROR: %s: code 0x%x\nRestart your machine."), domain, code);
	while (true) {
		CpuPause();
	}
}

#define efiAssert(code) { auto res = code; if (res != EFI_SUCCESS) { fatalError(uToC16(u"efiAssert"), res); } }

[[maybe_unused]] static void displayControlRegisters(void) {
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
	gBS->GetMemoryMap(&memoryMapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
	return mapKey;
}

// Fn is a `void (const EFI_MEMORY_DESCRIPTOR &currentMemoryDescriptor)`
template <typename Fn>
static UINTN bootIterateMemoryMap(Fn &&fn) {
	UINTN memoryMapSize = 0, mapKey, descriptorSize;
	UINT32 descriptorVersion;
	gBS->GetMemoryMap(&memoryMapSize, nullptr, &mapKey, &descriptorSize, &descriptorVersion);
	//Print(uToC16(u"Memory map size: 0x%Lx bytes, mapKey = 0x%Lx, descriptorSize = 0x%Lx, descriptorVersion = 0x%Lx\n"), memoryMapSize, mapKey, descriptorSize, descriptorVersion);

	if (descriptorVersion != EFI_MEMORY_DESCRIPTOR_VERSION)
		fatalError(uToC16(u"GetMemoryMap.descriptorVersion was expected to be EFI_MEMORY_DESCRIPTOR_VERSION"), descriptorVersion);

	UINT8 memoryMapBytes[memoryMapSize];
	gBS->GetMemoryMap(&memoryMapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(memoryMapBytes), &mapKey, &descriptorSize, &descriptorVersion);

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
		fatalError(uToC16(u"findConventionalMemory: no mapping found of type EfiConventionalMemory (code is memory descriptor count)"), descriptorCount);
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

[[maybe_unused]] static void bootPrintMemoryTypeDescriptors(EFI_MEMORY_TYPE memoryType, UINTN minPageCount) {
	UINTN smallPageCount = 0;

	UINTN i = 0;
	Print(uToC16(u"Enumerating memory descriptors of type 0x%Lx:\n"), static_cast<UINTN>(memoryType));
	bootIterateMemoryMap([&i, memoryType, minPageCount, &smallPageCount](const EFI_MEMORY_DESCRIPTOR &curDescriptor) {
		if (curDescriptor.Type != memoryType)
			return;
		if (curDescriptor.NumberOfPages < minPageCount) {
			smallPageCount++;
			return;
		}

		Print(uToC16(u"#%Ld at 0x%Lx: %,Ld bytes (0x%Lx pages), attr = 0x%Lx\n"), i, curDescriptor.PhysicalStart, curDescriptor.NumberOfPages * static_cast<UINTN>(1 << 12), curDescriptor.NumberOfPages, curDescriptor.Attribute);
		i++;
	});
	if (minPageCount > 0)
		Print(uToC16(u"0x%Lx memory descriptors of less than 0x%Lx pages were omitted\n"), smallPageCount, minPageCount);
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
	efiAssert(ShellInitialize());

	auto conventionalMemory = bootFindConventionalMemory();
	Print(uToC16(u"Conventional memory found at 0x%Lx: %,Ld bytes, attributes = 0x%Lx\n"),
		conventionalMemory.PhysicalStart, conventionalMemory.NumberOfPages * static_cast<UINTN>(1 << 12), conventionalMemory.Attribute
	);

	bootPrintMemoryTotals();

	Print(uToC16(u"Press any key to show conventional memory descriptors..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
	// Pruning less than 1MiB descriptors
	bootPrintMemoryTypeDescriptors(EfiConventionalMemory, 0xFF);

	Print(uToC16(u"Done! Press any key to get back to setup..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	return EFI_SUCCESS;
}