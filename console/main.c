#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ShellLib.h>
#include <Register/Intel/Cpuid.h>
#include <Guid/Acpi.h>

#define efiAssert(code) { EFI_STATUS res = code; if (res != EFI_SUCCESS) { return res; } }

#define ASSERT(Expression)      \
  do {                          \
    if (!(Expression)) {        \
      CpuDeadLoop ();           \
    }                           \
  } while (FALSE)


/**
  CPUID Leaf 0x15 for Core Crystal Clock Frequency.

  The TSC counting frequency is determined by using CPUID leaf 0x15. Frequency in MHz = Core XTAL frequency * EBX/EAX.
  In newer flavors of the CPU, core xtal frequency is returned in ECX or 0 if not supported.
  @return The number of TSC counts per second.

**/
static UINT64
CpuidCoreClockCalculateTscFrequency (
  VOID
  )
{
	UINT64  TscFrequency;
	UINT64  CoreXtalFrequency;
	UINT32  RegEax;
	UINT32  RegEbx;
	UINT32  RegEcx;

	//
	// Use CPUID leaf 0x15 Time Stamp Counter and Nominal Core Crystal Clock Information
	// EBX returns 0 if not supported. ECX, if non zero, provides Core Xtal Frequency in hertz.
	// TSC frequency = (ECX, Core Xtal Frequency) * EBX/EAX.
	//
	AsmCpuid (CPUID_TIME_STAMP_COUNTER, &RegEax, &RegEbx, &RegEcx, NULL);

	//
	// If ECX returns 0, the XTAL frequency is not enumerated.
	// And PcdCpuCoreCrystalClockFrequency defined should base on processor series.
	//
	if (RegEcx == 0) {
		CoreXtalFrequency = PcdGet64 (PcdCpuCoreCrystalClockFrequency);
	} else {
		CoreXtalFrequency = (UINT64)RegEcx;
	}

	//
	// If EAX or EBX returns 0, the XTAL ratio is not enumerated.
	//
	if ((RegEax == 0) || (RegEbx == 0)) {
		return CoreXtalFrequency;
	}

	//
	// Calculate TSC frequency = (ECX, Core Xtal Frequency) * EBX/EAX
	//
	TscFrequency = DivU64x32 (MultU64x32 (CoreXtalFrequency, RegEbx) + (UINT64)(RegEax >> 1), RegEax);

	return TscFrequency;
}

/**
  Stalls the CPU for at least the given number of ticks.

  Stalls the CPU for at least the given number of ticks. It's invoked by
  MicroSecondDelay() and NanoSecondDelay().

  @param  Delay     A period of time to delay in ticks.

**/
static VOID CpuDelay (
  IN UINT64  Delay
  )
{
  UINT64  Ticks;

  //
  // The target timer count is calculated here
  //
  Ticks = AsmReadTsc () + Delay;

  //
  // Wait until time out
  // Timer wrap-arounds are NOT handled correctly by this function.
  // Thus, this function must be called within 10 years of reset since
  // Intel guarantees a minimum of 10 years before the TSC wraps.
  //
  while (AsmReadTsc () <= Ticks) {
    CpuPause ();
  }
}

/**
  Stalls the CPU for at least the given number of microseconds.

  Stalls the CPU for the number of microseconds specified by MicroSeconds.

  @param[in]  MicroSeconds  The minimum number of microseconds to delay.

  @return MicroSeconds

**/
static UINTN
EFIAPI
MicroSecondDelay (
  IN UINTN  MicroSeconds
  )
{
  CpuDelay (
    DivU64x32 (
      MultU64x64 (
        MicroSeconds,
        CpuidCoreClockCalculateTscFrequency ()
        ),
      1000000u
      )
    );

  return MicroSeconds;
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

	Print(L"Hello!!!\n");

	for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
		GUID curGuid = SystemTable->ConfigurationTable[i].VendorGuid;
		Print(L"#%Lu: %x %x %x (%x %x %x %x %x %x %x %x)\n", i, curGuid.Data1, curGuid.Data2, curGuid.Data3,
			curGuid.Data4[0], curGuid.Data4[1], curGuid.Data4[2], curGuid.Data4[3], curGuid.Data4[4], curGuid.Data4[5], curGuid.Data4[6], curGuid.Data4[7]);
		if (CompareMem(&curGuid, &gEfiAcpiTableGuid, sizeof(GUID)) == 0) {
			Print(L"Found the ACPI table\n");
		}
	}

	Print(L"Core TSC freq: %Lu\n", CpuidCoreClockCalculateTscFrequency());
	for (int i = 0; i < 16; i++) {
		Print(L"Iteration #%d, TSC = %Lu\n", i, AsmReadTsc());

		//ShellPromptForResponse(ShellPromptResponseTypeAnyKeyContinue, NULL, NULL);
		// Works on Intel systems only
		MicroSecondDelay(1e6);
	}
	return EFI_SUCCESS;
}