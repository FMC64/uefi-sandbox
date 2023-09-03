#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
//#include <Library/TimerLib.h>
#include <Library/ShellLib.h>

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

	//EFI_STATUS printStatus = ShellPrintEx(1, 1, L"Hello!!!");
	efiAssert(ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, L"Press any key to return to UEFI setup", NULL));

	/*for (UINT32 i = 0; i < 32; i++) {
		ShellPrintEx(1, 1, L"Hello!!!");

		ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, L"Press any key for more greetings!!", NULL);
	}*/
	return EFI_SUCCESS;
}