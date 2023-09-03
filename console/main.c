#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ShellLib.h>
#include <Register/Intel/Cpuid.h>
#include <Guid/Acpi.h>

#define efiAssert(code) { EFI_STATUS res = code; if (res != EFI_SUCCESS) { return res; } }

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

	Print(L"Hello!!!\n");

	for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
		GUID curGuid = SystemTable->ConfigurationTable[i].VendorGuid;
		Print(L"#%Lu: %x %x %x (%x %x %x %x %x %x %x %x)\n", i, curGuid.Data1, curGuid.Data2, curGuid.Data3,
			curGuid.Data4[0], curGuid.Data4[1], curGuid.Data4[2], curGuid.Data4[3], curGuid.Data4[4], curGuid.Data4[5], curGuid.Data4[6], curGuid.Data4[7]);
		if (CompareMem(&curGuid, &gEfiAcpiTableGuid, sizeof(GUID)) == 0) {
			Print(L"Found the ACPI table\n");
		}
	}

	UINT64 prevTsc = AsmReadTsc();
	for (int i = 0; i < 16; i++) {
		UINT64 curTsc = AsmReadTsc();
		Print(L"Iteration #%d, TSC = %Lu, cycle diff = %Lu\n", i, curTsc, curTsc - prevTsc);
		prevTsc = curTsc;

		//ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, NULL, NULL);
		efiAssert(SystemTable->BootServices->Stall(1e6));
	}
	return EFI_SUCCESS;
}