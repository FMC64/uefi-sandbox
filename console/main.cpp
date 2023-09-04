extern "C" {

#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ShellLib.h>
#include <Register/Intel/Cpuid.h>
#include <Guid/Acpi.h>
#include <Universal/Console/TerminalDxe/Terminal.h>

}

#define efiAssert(code) { EFI_STATUS res = code; if (res != EFI_SUCCESS) { return res; } }
#define uToC16(uStr) reinterpret_cast<const CHAR16*>(uStr)

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

	Print(uToC16(u"Hello!!!\n"));

	for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
		auto curGuid = SystemTable->ConfigurationTable[i].VendorGuid;
		Print(uToC16(u"#%Lu: %x %x %x (%x %x %x %x %x %x %x %x)\n"), i, curGuid.Data1, curGuid.Data2, curGuid.Data3,
			curGuid.Data4[0], curGuid.Data4[1], curGuid.Data4[2], curGuid.Data4[3], curGuid.Data4[4], curGuid.Data4[5], curGuid.Data4[6], curGuid.Data4[7]);
		if (CompareMem(&curGuid, &gEfiAcpiTableGuid, sizeof(GUID)) == 0) {
			Print(uToC16(u"Found the ACPI table\n"));
		}
	}

	auto prevTsc = AsmReadTsc();
	for (UINTN i = 0; i < 4; i++) {
		auto curTsc = AsmReadTsc();
		Print(uToC16(u"Iteration #%u, TSC = %Lu, cycle diff = %Lu\n"), i, curTsc, curTsc - prevTsc);
		prevTsc = curTsc;

		//ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
		efiAssert(SystemTable->BootServices->Stall(1e6));
	}
	Print(uToC16(u"Will now read ConIn indefinitely until Return is pressed. Feel free to type whatever in there:\n"));
	while (true) {
		EFI_INPUT_KEY key{};
		auto status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &key);
		if (status == EFI_SUCCESS) {
			Print(uToC16(u"Keystroke: %x, unicode %x = '%c'\n"), key.ScanCode, key.UnicodeChar, key.UnicodeChar);
			if (key.UnicodeChar == CHAR_CARRIAGE_RETURN)
				break;
		} else if (status != EFI_NOT_READY) {
			Print(uToC16(u"Error on ReadKeyStroke: %Ld\n"), status);
			break;
		}
		efiAssert(SystemTable->BootServices->Stall(64));
	}
	Print(uToC16(u"Done! Press any key to get back to setup..\n"));
	ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, nullptr, nullptr);
	return EFI_SUCCESS;
}