#pragma once

extern "C" {

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>

}

#define bareEfiAssert(code) { auto runtimeEfiAssert__res = code; if (runtimeEfiAssert__res != EFI_SUCCESS) { bare::fatalError(); } }

namespace bare {

[[maybe_unused]] [[noreturn]] static void fatalError(void) {
	while (true) {
		CpuPause();
	}
}

[[maybe_unused]] static void sleep(UINTN tscFrequency, UINTN microseconds) {
	auto begin = AsmReadTsc();

	auto tscFullSleepDelay = microseconds * tscFrequency / 1e6;

	while (true) {
		auto cur = AsmReadTsc();
		if (cur - begin > tscFullSleepDelay)
			break;
	}
}

}