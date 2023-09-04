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

#include <array>
#include <optional>

#define uToC16(uStr) reinterpret_cast<const CHAR16*>(uStr)

[[noreturn]] static void fatalError(const CHAR16 *domain, UINT64 code) {
	Print(uToC16(u"FATAL ERROR: %s: code 0x%x\n"), domain, code);
	Print(uToC16("Restart your machine.\n"));
	while (true) {
		CpuPause();
	}
}

#define efiAssert(code) { auto res = code; if (res != EFI_SUCCESS) { fatalError(uToC16(u"efiAssert"), res); } }

class Input
{
	EFI_SIMPLE_TEXT_INPUT_PROTOCOL *m_input;

public:
	Input(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input) :
		m_input(input)
	{
	}

	std::optional<EFI_INPUT_KEY> readKey(void) const {
		EFI_INPUT_KEY key{};
		auto status = m_input->ReadKeyStroke(m_input, &key);
		if (status == EFI_SUCCESS) {
			return key;
		} else if (status != EFI_NOT_READY) {
			fatalError(uToC16(u"m_input->ReadKeyStroke"), status);
		}
		return std::nullopt;
	}
};

class Output
{
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *m_output;

public:
	Output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *output) :
		m_output(output)
	{
	}

	void clear(void) const {
		efiAssert(m_output->ClearScreen(m_output));
	}

	void locate(UINTN x, UINTN y) const {
		efiAssert(m_output->SetCursorPosition(m_output, x, y));
	}

	template <typename ...Args>
	void print(const CHAR16 *format, Args &&...args) {
		Print(format, std::forward<Args>(args)...);
	}
};

class Tetris
{
	Input m_input;
	Output m_output;


	static inline constexpr UINTN framebufferWidth = 80;
	static inline constexpr UINTN framebufferHeight = 24;
	CHAR16 framebuffer[framebufferHeight][framebufferWidth];

	static inline constexpr UINTN fieldWidth = 10;
	static inline constexpr UINTN fieldHeight = 18;

	void sleep(UINTN microseconds) const {
		efiAssert(gBS->Stall(microseconds));
	}

public:
	Tetris(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *output) :
		m_input(input),
		m_output(output)
	{
	}

	void run(void) {
		SetMem16(framebuffer, sizeof(framebuffer), u' ');
		for (UINTN i = 0; i < framebufferHeight; i++)
			framebuffer[i][framebufferWidth - 1] = u'\0';
		for (UINTN i = 0; i < fieldWidth + 2; i++) {
			framebuffer[fieldHeight][i] = u'#';
		}
		for (UINTN i = 0; i < fieldHeight + 1; i++) {
			framebuffer[i][0] = u'#';
			framebuffer[i][fieldWidth + 1] = u'#';
		}

		m_output.clear();

		bool isDone = false;
		while (!isDone) {
			UINTN keyCount = 0;
			while (auto key = m_input.readKey()) {
				keyCount++;
				if (key->UnicodeChar == CHAR_CARRIAGE_RETURN) {
					isDone = true;
				}
			}

			for (UINTN i = 0; i < framebufferHeight; i++) {
				m_output.locate(0, i);
				m_output.print(framebuffer[i]);
			}

			sleep(1e6 / 60);
		}
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
	efiAssert(ShellInitialize());

	auto tetris = Tetris(SystemTable->ConIn, SystemTable->ConOut);
	tetris.run();

	return EFI_SUCCESS;
}