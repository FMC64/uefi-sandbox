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
	//Print(uToC16(u"Enumerating memory map, 0x%Lx descriptors:\n"), descriptorCount);
	for (UINTN i = 0; i < descriptorCount; i++) {
		auto &curDescriptor = *reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(memoryMapBytes + i * descriptorSize);
		/*Print(uToC16(u"#0x%Lx: type = 0x%x, physicalStart = 0x%Lx, virtualStart = 0x%Lx, pageCount = 0x%Lx, attributes = 0x%Lx\n"), i,
			curDescriptor.Type, curDescriptor.PhysicalStart, curDescriptor.VirtualStart, curDescriptor.NumberOfPages, curDescriptor.Attribute
		);
		ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);*/

		fn(curDescriptor);
	}
	return descriptorCount;
}

static EFI_MEMORY_DESCRIPTOR findConventionalMemory(void) {
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

	auto conventionalMemory = findConventionalMemory();
	Print(uToC16(u"Conventional memory found at 0x%Lx: %Lu bytes, attributes = 0x%Lx\n"),
		conventionalMemory.PhysicalStart, conventionalMemory.NumberOfPages * static_cast<UINTN>(1 << 12), conventionalMemory.Attribute
	);

	Print(uToC16(u"Done! Press any key to get back to setup..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);

	return EFI_SUCCESS;
}