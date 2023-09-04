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

		UINTN getPositionCount(void) const {
			return m_positionCount;
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

	bool m_gameOver;
	UINTN m_score;
	UINTN m_completedLineTicks;

	UINTN m_currentPiece;
	UINTN m_currentPiecePosition;
	INTN m_currentPieceX;
	INTN m_currentPieceY;
	UINTN m_currentPieceFall;
	UINTN m_currentPieceFastFall;
	UINTN m_currentPieceMove;
	INTN m_currentPieceLastTickRot;
	UINTN m_nextPiece;

	UINTN m_lastRandom = 0xBAADBEEF;
	// To not use as a face value, always modulate this in some manner
	UINTN random(void) {
		auto cur = AsmReadTsc() >> 7;
		auto res = cur ^ m_lastRandom;
		m_lastRandom = res;
		return res;
	}

	void resetGame(void) {
		m_gameOver = false;
		m_score = 0;
		m_completedLineTicks = 0;
		m_currentPieceLastTickRot = 0;
		resetField();
		for (UINTN i = 0; i < 2; i++)
			genNextPiece();
	}

	void resetPieceTransient(void) {
		m_currentPieceFall = 0;
		m_currentPieceFastFall = 0;
		m_currentPieceMove = 0;
	}

	void genNextPiece(void) {
		m_currentPiece = m_nextPiece;
		m_currentPiecePosition = 0;
		m_currentPieceX = 3;
		m_currentPieceY = 0;
		resetPieceTransient();
		m_nextPiece = random() % pieceCount;

		if (isPieceIntersectingField(m_currentPiece, m_currentPiecePosition, m_currentPieceX, m_currentPieceY)) {
			m_currentPieceX = -64;
			m_gameOver = true;
			drawGameOver();
		}

		drawNext();
	}

	static inline constexpr UINTN framerate = 60;

	UINTN getDifficulty(UINTN tick) const {
		if (tick < framerate * 60)
			return 0;
		else if (tick < framerate * 60 * 3)
			return 1;
		else if (tick < framerate * 60 * 5)
			return 2;
		else if (tick < framerate * 60 * 10)
			return 3;
		else if (tick < framerate * 60 * 20)
			return 4;
		else if (tick < framerate * 60 * 45)
			return 5;
		else
			return 6;
	}

	UINTN getFallingSpeed(UINTN difficulty) const {
		if (difficulty == 0)
			return 50;
		else if (difficulty == 1)
			return 40;
		else if (difficulty == 2)
			return 30;
		else if (difficulty == 3)
			return 20;
		else if (difficulty == 4)
			return 10;
		else if (difficulty == 5)
			return 5;
		else
			return 4;
	}

	UINTN getScorePerLine(UINTN difficulty) const {
		if (difficulty == 0)
			return 100;
		else if (difficulty == 1)
			return 250;
		else if (difficulty == 2)
			return 500;
		else if (difficulty == 3)
			return 1000;
		else if (difficulty == 4)
			return 2500;
		else if (difficulty == 5)
			return 5000;
		else
			return 10000;
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

	bool moveBy(INTN rot, INTN x, INTN y) {
		auto &currentPiece = m_pieces[m_currentPiece];

		INTN nextPosition = m_currentPiecePosition + rot;
		if (nextPosition < 0)
			nextPosition = static_cast<INTN>(currentPiece.getPositionCount() - 1);
		if (nextPosition >= static_cast<INTN>(currentPiece.getPositionCount()))
			nextPosition = 0;
		INTN nextX = m_currentPieceX + x;
		INTN nextY = m_currentPieceY + y;

		if (isPieceIntersectingField(m_currentPiece, nextPosition, nextX, nextY)) {
			return false;
		} else {
			m_currentPiecePosition = nextPosition;
			m_currentPieceX = nextX;
			m_currentPieceY = nextY;
			return true;
		}
	}

	void emplaceCurrentPiece(void) {
		auto &currentPiece = m_pieces[m_currentPiece];
		auto currentPieceDisplay = currentPiece.getDisplay();
		for (UINTN i = 0; i < Piece::height; i++)
			for (UINTN j = 0; j < Piece::width; j++) {
				if (currentPiece.at(m_currentPiecePosition, j, i)) {
					// Every dot of the piece is guaranteed to be within the field, so no boundary checking
					m_field[m_currentPieceY + static_cast<INTN>(i)][m_currentPieceX + static_cast<INTN>(j)] = currentPieceDisplay;
				}
			}
	}

	static inline constexpr UINTN completedLineIterationCount = 6;
	static inline constexpr UINTN completedLineIterationLength = framerate / 3;

	bool isLineCompleted(UINTN y) const {
		for (UINTN i = 0; i < fieldWidth; i++) {
			if (m_field[y][i] == u'\0')
				return false;
		}
		return true;
	}

	UINTN getCompletedLineCount(void) const {
		UINTN res = 0;
		for (UINTN i = 0; i < fieldHeight; i++) {
			if (isLineCompleted(i))
				res++;
		}
		return res;
	}

	bool hasAnyCompletedLine(void) const {
		return getCompletedLineCount() > 0;
	}

	UINTN getCompletelineIteration(void) const {
		return m_completedLineTicks / completedLineIterationLength;
	}

	void deleteLine(UINTN y) {
		for (UINTN i = y; i > 0; i--)
			for (UINTN j = 0; j < fieldWidth; j++) {
				m_field[i][j] = m_field[i - 1][j];
			}
		for (UINTN i = 0; i < fieldWidth; i++)
			m_field[0][i] = u'\0';
	}

	void flushCompletedLines(UINTN difficulty) {
		for (UINTN i = 0; i < fieldHeight; i++) {
			if (isLineCompleted(i)) {
				m_score += getScorePerLine(difficulty);
				deleteLine(i);
			}
		}
	}

	void processTick(UINTN tick, INTN x, INTN y, INTN rot) {
		if (m_gameOver)
			return;

		auto difficulty = getDifficulty(tick);

		if (hasAnyCompletedLine()) {
			if (getCompletelineIteration() < completedLineIterationCount) {
				m_completedLineTicks++;
			} else {
				flushCompletedLines(difficulty);
				m_completedLineTicks = 0;
			}
			return;
		}

		if (y == 0)
			m_currentPieceFall++;
		if (m_currentPieceFall >= getFallingSpeed(difficulty)) {
			m_currentPieceFall = 0;
			if (!moveBy(0, 0, 1)) {
				emplaceCurrentPiece();
				genNextPiece();
			}
		}
		auto didPlayMoveSucceed = moveBy(rot, x, y);
		if (didPlayMoveSucceed && y != 0) {
			// Prevent quick gravity fall if player wants to move faster
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

		// Matrix, static elements
		{
			bool isCompletingBlank = getCompletelineIteration() & 1;
			for (UINTN i = 0; i < fieldHeight; i++) {
				auto isComplete = isLineCompleted(i);
				for (UINTN j = 0; j < fieldWidth; j++) {
					if (m_field[i][j] != u'\0') {
						auto display = m_field[i][j];
						if (isComplete)
							display = isCompletingBlank ? ' ' : '-';
						drawFieldDot(display, j, i);
					}
				}
			}
		}

		// Current piece
		{
			auto &currentPiece = m_pieces[m_currentPiece];
			auto currentPieceDisplay = currentPiece.getDisplay();
			for (UINTN i = 0; i < Piece::height; i++)
				for (UINTN j = 0; j < Piece::width; j++) {
					if (currentPiece.at(m_currentPiecePosition, j, i)) {
						drawFieldDot(currentPieceDisplay, m_currentPieceX + static_cast<INTN>(j), m_currentPieceY + static_cast<INTN>(i));
					}
				}
		}
	}

	void blit(UINTN x, UINTN y, const CHAR16 *str) {
		for (UINTN i = 0; x + i < (framebufferWidth - 1) && str[i] != u'\0'; i++)
			m_framebuffer[y][x + i] = str[i];
	}

	void drawNext(void) {
		CHAR16 pieceFramebuffer[Piece::height][Piece::width + 1];

		SetMem16(pieceFramebuffer, sizeof(pieceFramebuffer), u' ');
		for (UINTN i = 0; i < Piece::height; i++)
			pieceFramebuffer[i][Piece::width] = u'\0';

		auto &currentPiece = m_pieces[m_nextPiece];
		auto currentPieceDisplay = currentPiece.getDisplay();
		for (UINTN i = 0; i < Piece::height; i++)
			for (UINTN j = 0; j < Piece::width; j++) {
				if (currentPiece.at(0, j, i))
					pieceFramebuffer[i][j] = currentPieceDisplay;
			}

		blit(14, 2, uToC16(u"NEXT:"));
		for (UINTN i = 0; i < Piece::height; i++) {
			blit(15, 4 + i, pieceFramebuffer[i]);
		}
	}

	void drawScore(void) {
		CHAR16 buffer[128];
		UnicodeSPrint(buffer, sizeof(buffer), uToC16(u"Score: %08u"), m_score);
		blit(14, 10, buffer);
	}

	void drawGameOver(void) {
		if (m_gameOver) {
			blit(15, 14, uToC16(u"[GAME OVER!]"));
		}
	}

	void drawStats(UINTN frametime) {
		CHAR16 buffer[128];
		UnicodeSPrint(buffer, sizeof(buffer), uToC16(u"Frametime: %Lu / %Lu (nom) us"), frametime, static_cast<UINTN>(1e6) / framerate);
		blit(14, 0, buffer);
	}

	UINTN getTscFrequency(void) {
		auto begin = AsmReadTsc();
		sleep(static_cast<UINTN>(1e6));
		auto end = AsmReadTsc();
		return end - begin;
	}

public:
	inline Tetris(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *output);

	void run(void) {
		resetGame();
		UINTN currentTick = 0;

		resetFramebuffer();
		m_output.clear();
		auto tscFreq = getTscFrequency();
		UINTN avgFrametime = 0;
		UINTN frametimeAcc = 0;
		UINTN frametimeCount = 0;

		bool isDone = false;
		while (!isDone) {
			auto beginTsc = AsmReadTsc();

			UINTN keyCount = 0;
			INTN x = 0, y = 0, rot = 0;
			while (auto key = m_input.readKey()) {
				keyCount++;
				if (key->ScanCode == SCAN_ESC) {
					isDone = true;
				}
				if (key->ScanCode == SCAN_LEFT)
					x--;
				if (key->ScanCode == SCAN_RIGHT)
					x++;
				if (key->ScanCode == SCAN_DOWN)
					y++;
				if (key->UnicodeChar == u'z' || key->UnicodeChar == u'Z')
					rot--;
				if (key->UnicodeChar == u'x' || key->UnicodeChar == u'X')
					rot++;
			}

			processTick(currentTick, x, y, rot);

			resetFramebuffer();
			drawField();
			drawNext();
			drawScore();
			drawGameOver();
			drawStats(avgFrametime);

			for (UINTN i = 0; i < framebufferHeight; i++) {
				m_output.locate(0, i);
				m_output.print(m_framebuffer[i]);
			}

			auto endTsc = AsmReadTsc();
			auto tscDelta = endTsc - beginTsc;

			INTN microsecondsDelta = static_cast<UINTN>(1e6) * tscDelta / tscFreq;
			frametimeAcc += microsecondsDelta;
			frametimeCount++;

			static constexpr UINTN frametimePeriod = framerate / 4;
			if (frametimeCount > frametimePeriod) {
				avgFrametime = frametimeAcc / frametimeCount;
				frametimeAcc = 0;
				frametimeCount = 0;
			}

			INTN toSleep = static_cast<UINTN>(1e6) / framerate - avgFrametime;
			if (toSleep > 0)
				sleep(toSleep);
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