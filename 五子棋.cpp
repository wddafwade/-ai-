#include "framework.h"
#include "WindowsProject1.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
constexpr int BoardSize = 15;
constexpr int CellSize = 36;
constexpr int BoardLeft = 48;
constexpr int BoardTop = 90;
constexpr int StoneRadius = 14;
constexpr int BoardEmpty = 0;
constexpr int RedStone = 1;
constexpr int GreenStone = 2;
constexpr UINT_PTR BotTimerId = 1;
constexpr UINT BotDelayMs = 700;

enum class BotDifficulty
{
	Beginner,
	Normal,
	Expert
};

enum class GameResult
{
	RedWon,
	GreenWon,
	Draw,
	Abandoned
};

struct Move
{
	int row;
	int column;
	int player;
};

struct GameRecord
{
	BotDifficulty difficulty;
	int moveCount;
	GameResult result;
};

std::array<std::array<int, BoardSize>, BoardSize> board{};
std::vector<Move> moveHistory;
std::vector<GameRecord> completedGames;
int currentPlayer = RedStone;
bool gameOver = false;
bool currentGameRecorded = false;
BotDifficulty botDifficulty = BotDifficulty::Normal;
std::filesystem::path saveFilePath;

bool InitializeSavePath()
{
	DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
	if (required == 0)
	{
		return false;
	}
	std::wstring localAppData(required, L'\0');
	DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required);
	if (length == 0 || length >= required)
	{
		return false;
	}
	localAppData.resize(length);
	std::filesystem::path directory(localAppData);
	directory /= L"FiveInARow";
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error)
	{
		return false;
	}
	saveFilePath = directory / L"save.dat";
	return true;
}

bool SaveData()
{
	if (saveFilePath.empty())
	{
		return false;
	}
	std::filesystem::path temporaryPath = saveFilePath;
	temporaryPath += L".tmp";
	std::wofstream file(temporaryPath, std::ios::trunc);
	if (!file)
	{
		return false;
	}
	file << L"FIVEINAROW 1\n"
		<< static_cast<int>(botDifficulty) << L' '
		<< currentPlayer << L' '
		<< (gameOver ? 1 : 0) << L' '
		<< (currentGameRecorded ? 1 : 0) << L'\n'
		<< moveHistory.size() << L'\n';
	for (const Move& move : moveHistory)
	{
		file << move.row << L' ' << move.column << L' ' << move.player << L'\n';
	}
	file << completedGames.size() << L'\n';
	for (const GameRecord& record : completedGames)
	{
		file << static_cast<int>(record.difficulty) << L' '
			<< record.moveCount << L' '
			<< static_cast<int>(record.result) << L'\n';
	}
	file.close();
	if (!file)
	{
		DeleteFileW(temporaryPath.c_str());
		return false;
	}
	if (!MoveFileExW(temporaryPath.c_str(), saveFilePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(temporaryPath.c_str());
		return false;
	}
	return true;
}

bool LoadData()
{
	if (saveFilePath.empty())
	{
		return false;
	}
	std::wifstream file(saveFilePath);
	std::wstring signature;
	int version = 0;
	int difficulty = 0;
	int savedPlayer = 0;
	int savedGameOver = 0;
	int savedRecorded = 0;
	if (!(file >> signature >> version) || signature != L"FIVEINAROW" || version != 1 ||
		!(file >> difficulty >> savedPlayer >> savedGameOver >> savedRecorded) ||
		difficulty < 0 || difficulty > 2 ||
		(savedPlayer != RedStone && savedPlayer != GreenStone) ||
		(savedGameOver != 0 && savedGameOver != 1) ||
		(savedRecorded != 0 && savedRecorded != 1) || savedGameOver != savedRecorded)
	{
		return false;
	}

	size_t moveCount = 0;
	if (!(file >> moveCount) || moveCount > BoardSize * BoardSize)
	{
		return false;
	}
	std::array<std::array<int, BoardSize>, BoardSize> loadedBoard{};
	std::vector<Move> loadedMoves;
	loadedMoves.reserve(moveCount);
	for (size_t i = 0; i < moveCount; ++i)
	{
		Move move{};
		if (!(file >> move.row >> move.column >> move.player) ||
			move.row < 0 || move.row >= BoardSize ||
			move.column < 0 || move.column >= BoardSize ||
			move.player != (i % 2 == 0 ? RedStone : GreenStone) ||
			loadedBoard[move.row][move.column] != BoardEmpty)
		{
			return false;
		}
		loadedBoard[move.row][move.column] = move.player;
		loadedMoves.push_back(move);
	}
	if (!savedGameOver && savedPlayer != (moveCount % 2 == 0 ? RedStone : GreenStone))
	{
		return false;
	}

	size_t recordCount = 0;
	if (!(file >> recordCount) || recordCount > 10000)
	{
		return false;
	}
	std::vector<GameRecord> loadedGames;
	loadedGames.reserve(recordCount);
	for (size_t i = 0; i < recordCount; ++i)
	{
		int recordDifficulty = 0;
		int recordMoves = 0;
		int result = 0;
		if (!(file >> recordDifficulty >> recordMoves >> result) ||
			recordDifficulty < 0 || recordDifficulty > 2 ||
			recordMoves < 0 || recordMoves > BoardSize * BoardSize ||
			result < 0 || result > 3)
		{
			return false;
		}
		loadedGames.push_back({ static_cast<BotDifficulty>(recordDifficulty), recordMoves, static_cast<GameResult>(result) });
	}

	board = loadedBoard;
	moveHistory = std::move(loadedMoves);
	completedGames = std::move(loadedGames);
	botDifficulty = static_cast<BotDifficulty>(difficulty);
	currentPlayer = savedPlayer;
	gameOver = savedGameOver != 0;
	currentGameRecorded = savedRecorded != 0;
	return true;
}

void ShowGameRecords(HWND window)
{
	if (completedGames.empty())
	{
		MessageBoxW(window, L"目前还没有已归档的对局。", L"对局记录", MB_OK | MB_ICONINFORMATION);
		return;
	}

	std::wstring report = L"累计对局数：" + std::to_wstring(completedGames.size()) + L"\r\n最近对局（最多显示 15 盘）：\r\n";
	size_t first = completedGames.size() > 15 ? completedGames.size() - 15 : 0;
	for (size_t index = completedGames.size(); index > first; --index)
	{
		const GameRecord& record = completedGames[index - 1];
		const wchar_t* difficultyName = record.difficulty == BotDifficulty::Beginner ? L"初级" :
			(record.difficulty == BotDifficulty::Normal ? L"中级" : L"高级");
		const wchar_t* resultName = L"中止";
		switch (record.result)
		{
		case GameResult::RedWon:
			resultName = L"红棋胜";
			break;
		case GameResult::GreenWon:
			resultName = L"绿棋胜";
			break;
		case GameResult::Draw:
			resultName = L"平局";
			break;
		case GameResult::Abandoned:
			break;
		}
		report += std::to_wstring(index) + L". " + difficultyName + L"，" +
			std::to_wstring(record.moveCount) + L" 步，" + resultName + L"\r\n";
	}
	MessageBoxW(window, report.c_str(), L"对局记录", MB_OK | MB_ICONINFORMATION);
}

void ArchiveCurrentGame(GameResult result)
{
	if (moveHistory.empty() || currentGameRecorded)
	{
		return;
	}
	completedGames.push_back({ botDifficulty, static_cast<int>(moveHistory.size()), result });
	currentGameRecorded = true;
}

void ResetGame(HWND window)
{
	KillTimer(window, BotTimerId);
	if (!gameOver)
	{
		ArchiveCurrentGame(GameResult::Abandoned);
	}
	for (auto& row : board)
	{
		row.fill(BoardEmpty);
	}
	moveHistory.clear();
	currentPlayer = RedStone;
	gameOver = false;
	currentGameRecorded = false;
	SaveData();
	InvalidateRect(window, nullptr, TRUE);
}

void UndoGameTurn(HWND window)
{
	KillTimer(window, BotTimerId);
	if (moveHistory.empty())
	{
		return;
	}
	if (currentGameRecorded)
	{
		if (!completedGames.empty())
		{
			completedGames.pop_back();
		}
		currentGameRecorded = false;
	}

	if (moveHistory.back().player == GreenStone)
	{
		const Move move = moveHistory.back();
		board[move.row][move.column] = BoardEmpty;
		moveHistory.pop_back();
	}
	if (!moveHistory.empty() && moveHistory.back().player == RedStone)
	{
		const Move move = moveHistory.back();
		board[move.row][move.column] = BoardEmpty;
		moveHistory.pop_back();
	}

	currentPlayer = RedStone;
	gameOver = false;
	SaveData();
	InvalidateRect(window, nullptr, TRUE);
}

bool HasFiveInRow(int row, int column, int player)
{
	constexpr int directions[][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
	for (const auto& direction : directions)
	{
		int count = 1;
		for (int sign : { -1, 1 })
		{
			int r = row + direction[0] * sign;
			int c = column + direction[1] * sign;
			while (r >= 0 && r < BoardSize && c >= 0 && c < BoardSize && board[r][c] == player)
			{
				++count;
				r += direction[0] * sign;
				c += direction[1] * sign;
			}
		}
		if (count >= 5)
		{
			return true;
		}
	}
	return false;
}

int ScoreMove(int row, int column, int player)
{
	constexpr int directions[][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
	board[row][column] = player;
	int score = 0;
	for (const auto& direction : directions)
	{
		int count = 1;
		int openEnds = 0;
		for (int sign : { -1, 1 })
		{
			int r = row + direction[0] * sign;
			int c = column + direction[1] * sign;
			while (r >= 0 && r < BoardSize && c >= 0 && c < BoardSize && board[r][c] == player)
			{
				++count;
				r += direction[0] * sign;
				c += direction[1] * sign;
			}
			if (r >= 0 && r < BoardSize && c >= 0 && c < BoardSize && board[r][c] == BoardEmpty)
			{
				++openEnds;
			}
		}

		if (count >= 5)
		{
			score += 1000000;
		}
		else if (count == 4)
		{
			score += openEnds == 2 ? 100000 : (openEnds == 1 ? 10000 : 0);
		}
		else if (count == 3)
		{
			score += openEnds == 2 ? 5000 : (openEnds == 1 ? 500 : 0);
		}
		else if (count == 2)
		{
			score += openEnds == 2 ? 200 : (openEnds == 1 ? 50 : 0);
		}
	}
	board[row][column] = BoardEmpty;
	return score;
}

bool FindImmediateMove(int player, int& bestRow, int& bestColumn)
{
	for (int row = 0; row < BoardSize; ++row)
	{
		for (int column = 0; column < BoardSize; ++column)
		{
			if (board[row][column] != BoardEmpty)
			{
				continue;
			}
			board[row][column] = player;
			bool wins = HasFiveInRow(row, column, player);
			board[row][column] = BoardEmpty;
			if (wins)
			{
				bestRow = row;
				bestColumn = column;
				return true;
			}
		}
	}
	return false;
}

bool ChooseBotMove(int& bestRow, int& bestColumn)
{
	bool boardIsEmpty = true;
	for (const auto& row : board)
	{
		for (int stone : row)
		{
			if (stone != BoardEmpty)
			{
				boardIsEmpty = false;
				break;
			}
		}
	}
	if (boardIsEmpty)
	{
		bestRow = BoardSize / 2;
		bestColumn = BoardSize / 2;
		return true;
	}

	if (FindImmediateMove(GreenStone, bestRow, bestColumn))
	{
		return true;
	}
	if (botDifficulty != BotDifficulty::Beginner && FindImmediateMove(RedStone, bestRow, bestColumn))
	{
		return true;
	}

	long long bestScore = -1;
	for (int row = 0; row < BoardSize; ++row)
	{
		for (int column = 0; column < BoardSize; ++column)
		{
			if (board[row][column] != BoardEmpty)
			{
				continue;
			}
			long long ownScore = ScoreMove(row, column, GreenStone);
			long long opponentScore = ScoreMove(row, column, RedStone);
			long long score = ownScore;
			if (botDifficulty == BotDifficulty::Normal)
			{
				score += opponentScore * 9 / 10;
			}
			else if (botDifficulty == BotDifficulty::Expert)
			{
				score += opponentScore * 12 / 10;
				board[row][column] = GreenStone;
				int threatRow = 0;
				int threatColumn = 0;
				bool leavesWinningMove = FindImmediateMove(RedStone, threatRow, threatColumn);
				board[row][column] = BoardEmpty;
				if (leavesWinningMove)
				{
					score -= 1000000000;
				}
			}
			int centerDistance = std::abs(row - BoardSize / 2) + std::abs(column - BoardSize / 2);
			score = score * 100 - centerDistance;
			if (score > bestScore)
			{
				bestScore = score;
				bestRow = row;
				bestColumn = column;
			}
		}
	}
	return bestScore >= 0;
}

bool MakeBotMove(HWND window)
{
	int row = 0;
	int column = 0;
	if (!ChooseBotMove(row, column))
	{
		return false;
	}
	board[row][column] = GreenStone;
	moveHistory.push_back({ row, column, GreenStone });
	if (HasFiveInRow(row, column, GreenStone))
	{
		gameOver = true;
		ArchiveCurrentGame(GameResult::GreenWon);
		MessageBoxW(window, L"绿棋（AI）获胜！", L"五子棋", MB_OK | MB_ICONINFORMATION);
	}
	else
	{
		currentPlayer = RedStone;
	}
	SaveData();
	return true;
}

void DrawBoard(HWND window, HDC dc)
{
	RECT client{};
	GetClientRect(window, &client);
	HBRUSH background = CreateSolidBrush(RGB(238, 198, 129));
	FillRect(dc, &client, background);
	DeleteObject(background);

	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(55, 45, 35));
	const wchar_t* status = gameOver
		? L"对局结束，选择“重新开始”再来一局。"
		: (currentPlayer == RedStone ? L"红棋回合（你执红，AI执绿）" : L"绿棋 AI 回合");
	TextOutW(dc, BoardLeft, 18, status, static_cast<int>(lstrlenW(status)));
	const wchar_t* difficultyName = botDifficulty == BotDifficulty::Beginner ? L"难度：初级" :
		(botDifficulty == BotDifficulty::Normal ? L"难度：中级" : L"难度：高级");
	TextOutW(dc, 365, 18, difficultyName, static_cast<int>(lstrlenW(difficultyName)));
	const wchar_t* hint = L"点击棋盘落子 | 文件菜单可重新开始或退出";
	TextOutW(dc, BoardLeft, 45, hint, static_cast<int>(lstrlenW(hint)));

	HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(75, 55, 35));
	HGDIOBJ oldPen = SelectObject(dc, gridPen);
	for (int i = 0; i < BoardSize; ++i)
	{
		int offset = i * CellSize;
		MoveToEx(dc, BoardLeft + offset, BoardTop, nullptr);
		LineTo(dc, BoardLeft + offset, BoardTop + (BoardSize - 1) * CellSize);
		MoveToEx(dc, BoardLeft, BoardTop + offset, nullptr);
		LineTo(dc, BoardLeft + (BoardSize - 1) * CellSize, BoardTop + offset);
	}

	HBRUSH starBrush = CreateSolidBrush(RGB(75, 55, 35));
	HGDIOBJ oldBrush = SelectObject(dc, starBrush);
	for (int row : { 3, 7, 11 })
	{
		for (int column : { 3, 7, 11 })
		{
			int x = BoardLeft + column * CellSize;
			int y = BoardTop + row * CellSize;
			Ellipse(dc, x - 3, y - 3, x + 4, y + 4);
		}
	}
	SelectObject(dc, oldBrush);
	DeleteObject(starBrush);
	SelectObject(dc, oldPen);
	DeleteObject(gridPen);

	for (int row = 0; row < BoardSize; ++row)
	{
		for (int column = 0; column < BoardSize; ++column)
		{
			if (board[row][column] == BoardEmpty)
			{
				continue;
			}
			COLORREF color = board[row][column] == RedStone ? RGB(220, 45, 45) : RGB(35, 155, 70);
			HBRUSH stoneBrush = CreateSolidBrush(color);
			HPEN stonePen = CreatePen(PS_SOLID, 1, RGB(50, 45, 40));
			HGDIOBJ previousBrush = SelectObject(dc, stoneBrush);
			HGDIOBJ previousPen = SelectObject(dc, stonePen);
			int x = BoardLeft + column * CellSize;
			int y = BoardTop + row * CellSize;
			Ellipse(dc, x - StoneRadius, y - StoneRadius, x + StoneRadius + 1, y + StoneRadius + 1);
			SelectObject(dc, previousPen);
			SelectObject(dc, previousBrush);
			DeleteObject(stonePen);
			DeleteObject(stoneBrush);
		}
	}
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
		CreateWindowExW(
			0, L"BUTTON", L"悔棋", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			250, 620, 100, 32, window,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_GAME_UNDO)),
		GetModuleHandleW(nullptr), nullptr);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_GAME_RESTART:
			ResetGame(window);
			return 0;
		case ID_GAME_UNDO:
			UndoGameTurn(window);
			return 0;
		case ID_GAME_HISTORY:
			ShowGameRecords(window);
			return 0;
		case ID_DIFFICULTY_BEGINNER:
			botDifficulty = BotDifficulty::Beginner;
			SaveData();
			CheckMenuRadioItem(GetMenu(window), ID_DIFFICULTY_BEGINNER, ID_DIFFICULTY_EXPERT,
				ID_DIFFICULTY_BEGINNER, MF_BYCOMMAND);
			InvalidateRect(window, nullptr, TRUE);
			return 0;
		case ID_DIFFICULTY_NORMAL:
			botDifficulty = BotDifficulty::Normal;
			SaveData();
			CheckMenuRadioItem(GetMenu(window), ID_DIFFICULTY_BEGINNER, ID_DIFFICULTY_EXPERT,
				ID_DIFFICULTY_NORMAL, MF_BYCOMMAND);
			InvalidateRect(window, nullptr, TRUE);
			return 0;
		case ID_DIFFICULTY_EXPERT:
			botDifficulty = BotDifficulty::Expert;
			SaveData();
			CheckMenuRadioItem(GetMenu(window), ID_DIFFICULTY_BEGINNER, ID_DIFFICULTY_EXPERT,
				ID_DIFFICULTY_EXPERT, MF_BYCOMMAND);
			InvalidateRect(window, nullptr, TRUE);
			return 0;
		case IDM_ABOUT:
			MessageBoxW(window, L"五子棋\n你执红棋，AI执绿棋，可在“难度”菜单调整 AI。", L"关于", MB_OK | MB_ICONINFORMATION);
			return 0;
		case IDM_EXIT:
			DestroyWindow(window);
			return 0;
		}
		break;
	case WM_LBUTTONDOWN:
		if (gameOver || currentPlayer != RedStone)
		{
			return 0;
		}
		{
			int x = static_cast<short>(LOWORD(lParam)) - BoardLeft;
			int y = static_cast<short>(HIWORD(lParam)) - BoardTop;
			int column = (x + CellSize / 2) / CellSize;
			int row = (y + CellSize / 2) / CellSize;
			if (x >= -CellSize / 2 && y >= -CellSize / 2 &&
				column >= 0 && column < BoardSize && row >= 0 && row < BoardSize &&
				std::abs(x - column * CellSize) <= CellSize / 2 &&
				std::abs(y - row * CellSize) <= CellSize / 2 && board[row][column] == BoardEmpty)
			{
				board[row][column] = currentPlayer;
				moveHistory.push_back({ row, column, currentPlayer });
				if (HasFiveInRow(row, column, currentPlayer))
				{
					gameOver = true;
					ArchiveCurrentGame(GameResult::RedWon);
					const wchar_t* winner = currentPlayer == RedStone ? L"红棋（原白棋）获胜！" : L"绿棋（原黑棋）获胜！";
					InvalidateRect(window, nullptr, TRUE);
					MessageBoxW(window, winner, L"五子棋", MB_OK | MB_ICONINFORMATION);
				}
				else
				{
					currentPlayer = GreenStone;
					SetTimer(window, BotTimerId, BotDelayMs, nullptr);
				}
				SaveData();
				InvalidateRect(window, nullptr, TRUE);
			}
		}
		return 0;
	case WM_TIMER:
		if (wParam == BotTimerId)
		{
			KillTimer(window, BotTimerId);
			if (!gameOver && currentPlayer == GreenStone && !MakeBotMove(window))
			{
				gameOver = true;
				ArchiveCurrentGame(GameResult::Draw);
				SaveData();
				MessageBoxW(window, L"平局！", L"五子棋", MB_OK | MB_ICONINFORMATION);
			}
			InvalidateRect(window, nullptr, TRUE);
			return 0;
		}
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT paint{};
		HDC dc = BeginPaint(window, &paint);
		DrawBoard(window, dc);
		EndPaint(window, &paint);
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	const wchar_t className[] = L"FiveInARowWindow";
	WNDCLASSEXW windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = instance;
	windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_WINDOWSPROJECT1));
	windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	windowClass.lpszClassName = className;
	windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SMALL));
	if (!RegisterClassExW(&windowClass))
	{
		return 1;
	}

	InitializeSavePath();
	LoadData();

	HMENU menu = LoadMenuW(instance, MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1));
	if (menu != nullptr)
	{
		UINT checkedDifficulty = botDifficulty == BotDifficulty::Beginner ? ID_DIFFICULTY_BEGINNER :
			(botDifficulty == BotDifficulty::Normal ? ID_DIFFICULTY_NORMAL : ID_DIFFICULTY_EXPERT);
		CheckMenuRadioItem(menu, ID_DIFFICULTY_BEGINNER, ID_DIFFICULTY_EXPERT,
			checkedDifficulty, MF_BYCOMMAND);
	}
	RECT windowRect{ 0, 0, 600, 670 };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, menu != nullptr);
	HWND window = CreateWindowExW(
		0, className, L"五子棋", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
		nullptr, menu, instance, nullptr);
	if (window == nullptr)
	{
		return 1;
	}
	if (!gameOver && currentPlayer == GreenStone && SetTimer(window, BotTimerId, BotDelayMs, nullptr) == 0)
	{
		if (!MakeBotMove(window))
		{
			gameOver = true;
			ArchiveCurrentGame(GameResult::Draw);
		}
		SaveData();
	}

	ShowWindow(window, showCommand);
	UpdateWindow(window);

	HACCEL accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1));
	MSG message{};
	while (GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		if (accelerators == nullptr || !TranslateAcceleratorW(window, accelerators, &message))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	return static_cast<int>(message.wParam);
}