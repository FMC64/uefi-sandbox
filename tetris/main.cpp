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
	CHAR16 m_framebuffer[framebufferHeight][framebufferWidth];

	static inline constexpr UINTN fieldWidth = 10;
	static inline constexpr UINTN fieldHeight = 18;

	void resetFramebuffer(void) {
		SetMem16(m_framebuffer, sizeof(m_framebuffer), u' ');
		for (UINTN i = 0; i < framebufferHeight; i++)
			m_framebuffer[i][framebufferWidth - 1] = u'\0';
		for (UINTN i = 0; i < fieldWidth + 2; i++) {
			m_framebuffer[fieldHeight][i] = u'#';
		}
		for (UINTN i = 0; i < fieldHeight + 1; i++) {
			m_framebuffer[i][0] = u'#';
			m_framebuffer[i][fieldWidth + 1] = u'#';
		}
	}

	void sleep(UINTN microseconds) const {
		efiAssert(gBS->Stall(microseconds));
	}

	class Piece
	{
	public:
		static inline constexpr UINTN width = 4;
		static inline constexpr UINTN height = 4;
		static inline constexpr UINTN maxPosCount = 4;

	private:
		CHAR16 m_display;
		UINTN m_positionCount;
		bool m_positions[maxPosCount][height][width];

	public:
		Piece(CHAR16 display, UINTN maxPosCount, const bool (&positions)[][height][width]) :
			m_display(display),
			m_positionCount(maxPosCount) {
			CopyMem(m_positions, positions, width * height * maxPosCount);
		}

		template <UINTN PositionCount>
		static Piece build(CHAR16 display, const bool (&positions)[PositionCount][height][width]) {
			return Piece(display, PositionCount, positions);
		}

		CHAR16 getDisplay(void) const {
			return m_display;
		}

		bool at(UINTN position, UINTN x, UINTN y) const {
			return m_positions[position][y][x];
		}
	};

	static inline constexpr UINTN pieceCount = 7;
	const Piece m_pieces[pieceCount];
	CHAR16 m_field[fieldHeight][fieldWidth];

	void resetField(void) {
		SetMem16(m_field, sizeof(m_field), u'\0');
	}

	UINTN m_currentPiece;
	UINTN m_currentPiecePosition;
	INTN m_currentPieceX;
	INTN m_currentPieceY;
	UINTN m_currentPieceFall;
	UINTN m_nextPiece;

	UINTN m_lastRandom = 0xBAADBEEF;
	// To not use as a face value, always modulate this in some manner
	UINTN random(void) {
		auto cur = AsmReadTsc();
		auto res = cur ^ m_lastRandom;
		m_lastRandom = res;
		return res;
	}

	void genNextPiece(void) {
		m_currentPiece = m_nextPiece;
		m_currentPiecePosition = 0;
		m_currentPieceX = 3;
		m_currentPieceY = 0;
		m_currentPieceFall = 0;
		m_nextPiece = random() % pieceCount;
	}

	static inline constexpr UINTN framerate = 60;

	UINTN getFallingSpeed(UINTN tick) const {
		if (tick < framerate * 10)
			return 50;
		if (tick < framerate * 20)
			return 40;
		if (tick < framerate * 40)
			return 30;
		if (tick < framerate * 60)
			return 20;
		if (tick < framerate * 60 * 3)
			return 10;
		if (tick < framerate * 60 * 5)
			return 5;
		else
			return 4;
	}

	bool isPieceIntersectingField(UINTN pieceIndex, UINTN piecePosition, INTN pieceX, INTN pieceY) const {
		auto &piece = m_pieces[pieceIndex];
		for (UINTN i = 0; i < Piece::height; i++)
			for (UINTN j = 0; j < Piece::width; j++) {
				auto x = pieceX + static_cast<INTN>(j);
				auto y = pieceY + static_cast<INTN>(i);
				if (piece.at(piecePosition, j, i)) {
					if (
						x < 0 || x >= static_cast<INTN>(fieldWidth) ||
						y < 0 || y >= static_cast<INTN>(fieldHeight)
					)
						return true;
					if (m_field[y][x] != u'\0')
						return true;
				}
			}
		return false;
	}

	void moveBy(INTN x, INTN y) {
		INTN nextX = m_currentPieceX + x;
		INTN nextY = m_currentPieceY + y;
		if (isPieceIntersectingField(m_currentPiece, m_currentPiecePosition, nextX, nextY)) {
			auto &currentPiece = m_pieces[m_currentPiece];
			auto currentPieceDisplay = currentPiece.getDisplay();
			for (UINTN i = 0; i < Piece::height; i++)
				for (UINTN j = 0; j < Piece::width; j++) {
					if (currentPiece.at(m_currentPiecePosition, j, i)) {
						// Every dot of the piece is guaranteed to be within the field, so no boundary checking
						m_field[m_currentPieceY + static_cast<INTN>(i)][m_currentPieceX + static_cast<INTN>(j)] = currentPieceDisplay;
					}
				}
			genNextPiece();
		} else {
			m_currentPieceX = nextX;
			m_currentPieceY = nextY;
		}
	}

	void processTick(UINTN tick) {
		m_currentPieceFall++;
		if (m_currentPieceFall >= getFallingSpeed(tick)) {
			moveBy(0, 1);
			m_currentPieceFall = 0;
		}
	}

	void drawFieldDot(CHAR16 dot, INTN x, INTN y) {
		if (
			x >= 0 && x < static_cast<INTN>(framebufferWidth) &&
			y >= 0 && y < static_cast<INTN>(framebufferHeight)
		) {
			m_framebuffer[y][x + 1] = dot;
		}
	}

	void drawField(void) {

		for (UINTN i = 0; i < fieldHeight; i++)
			for (UINTN j = 0; j < fieldWidth; j++) {
				if (m_field[i][j] != u'\0')
					drawFieldDot(m_field[i][j], j, i);
			}

		auto &currentPiece = m_pieces[m_currentPiece];
		auto currentPieceDisplay = currentPiece.getDisplay();
		for (UINTN i = 0; i < Piece::height; i++)
			for (UINTN j = 0; j < Piece::width; j++) {
				if (currentPiece.at(m_currentPiecePosition, j, i)) {
					drawFieldDot(currentPieceDisplay, m_currentPieceX + static_cast<INTN>(j), m_currentPieceY + static_cast<INTN>(i));
				}
			}
	}

public:
	inline Tetris(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *output);

	void run(void) {
		resetField();
		for (UINTN i = 0; i < 2; i++)
			genNextPiece();
		resetFramebuffer();
		m_output.clear();

		UINTN currentTick = 0;

		bool isDone = false;
		while (!isDone) {
			UINTN keyCount = 0;
			while (auto key = m_input.readKey()) {
				keyCount++;
				if (key->ScanCode == SCAN_ESC) {
					isDone = true;
				}
			}

			processTick(currentTick);

			resetFramebuffer();
			drawField();

			for (UINTN i = 0; i < framebufferHeight; i++) {
				m_output.locate(0, i);
				m_output.print(m_framebuffer[i]);
			}

			sleep(1e6 / framerate);
			currentTick++;
		}
	}
};

Tetris::Tetris(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *output) :
	m_input(input),
	m_output(output),
	m_pieces{
		Piece::build<1>(u'@', {
			{
				{false, true, true, false},
				{false, true, true, false},
				{false, false, false, false},
				{false, false, false, false}
			}
		}),
		Piece::build<2>(u'H', {
			{
				{false, false, false, false},
				{true, true, true, true},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{false, true, false, false},
				{false, true, false, false},
				{false, true, false, false}
			}
		}),
		Piece::build<2>(u'W', {
			{
				{false, true, true, false},
				{true, true, false, false},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{false, true, true, false},
				{false, false, true, false},
				{false, false, false, false}
			}
		}),
		Piece::build<2>(u'Z', {
			{
				{false, true, true, false},
				{false, false, true, true},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, false, true, false},
				{false, true, true, false},
				{false, true, false, false},
				{false, false, false, false}
			}
		}),
		Piece::build<4>(u'L', {
			{
				{false, true, true, true},
				{false, false, false, true},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, false, true, false},
				{false, false, true, false},
				{false, true, true, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{false, true, true, true},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, true, true, false},
				{false, true, false, false},
				{false, true, false, false},
				{false, false, false, false}
			}
		}),
		Piece::build<4>(u'T', {
			{
				{true, true, true, false},
				{true, false, false, false},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, true, true, false},
				{false, false, true, false},
				{false, false, true, false},
				{false, false, false, false}
			},
			{
				{false, false, true, false},
				{true, true, true, false},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{false, true, false, false},
				{false, true, true, false},
				{false, false, false, false}
			}

		}),
		Piece::build<4>(u'X', {
			{
				{false, false, false, false},
				{true, true, true, false},
				{false, true, false, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{true, true, false, false},
				{false, true, false, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{true, true, true, false},
				{false, false, false, false},
				{false, false, false, false}
			},
			{
				{false, true, false, false},
				{false, true, true, false},
				{false, true, false, false},
				{false, false, false, false}
			}
		})
	}
{
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

	auto tetris = Tetris(SystemTable->ConIn, SystemTable->ConOut);
	tetris.run();

	return EFI_SUCCESS;
}