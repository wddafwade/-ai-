#include "framework.h"
#include "WindowsProject1.h"

#include <array>
#include <cstdlib>

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

std::array<std::array<int, BoardSize>, BoardSize> board{};
int currentPlayer = RedStone;
bool gameOver = false;

void ResetGame(HWND window)
{
	for (auto& row : board)
	{
		row.fill(BoardEmpty);
	}
	currentPlayer = RedStone;
	gameOver = false;
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

	if (FindImmediateMove(GreenStone, bestRow, bestColumn) ||
		FindImmediateMove(RedStone, bestRow, bestColumn))
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
			long long score = ScoreMove(row, column, GreenStone) + ScoreMove(row, column, RedStone) * 9LL / 10;
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

bool MakeBotMove()
{
	int row = 0;
	int column = 0;
	if (!ChooseBotMove(row, column))
	{
		return false;
	}
	board[row][column] = GreenStone;
	if (HasFiveInRow(row, column, GreenStone))
	{
		gameOver = true;
		MessageBoxW(nullptr, L"绿棋（AI）获胜！", L"五子棋", MB_OK | MB_ICONINFORMATION);
	}
	else
	{
		currentPlayer = RedStone;
	}
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
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_GAME_RESTART:
			ResetGame(window);
			return 0;
		case IDM_ABOUT:
			MessageBoxW(window, L"五子棋\n你执红棋，AI执绿棋。", L"关于", MB_OK | MB_ICONINFORMATION);
			return 0;
		case IDM_EXIT:
			DestroyWindow(window);
			return 0;
		}
		break;
	case WM_LBUTTONDOWN:
		if (gameOver)
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
				if (HasFiveInRow(row, column, currentPlayer))
				{
					gameOver = true;
					const wchar_t* winner = currentPlayer == RedStone ? L"红棋（原白棋）获胜！" : L"绿棋（原黑棋）获胜！";
					InvalidateRect(window, nullptr, TRUE);
					MessageBoxW(window, winner, L"五子棋", MB_OK | MB_ICONINFORMATION);
				}
				else
				{
					currentPlayer = GreenStone;
					if (!MakeBotMove())
					{
						gameOver = true;
						MessageBoxW(window, L"平局！", L"五子棋", MB_OK | MB_ICONINFORMATION);
					}
				}
				InvalidateRect(window, nullptr, TRUE);
			}
		}
		return 0;
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

	HMENU menu = LoadMenuW(instance, MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1));
	RECT windowRect{ 0, 0, 600, 640 };
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

	ShowWindow(window, showCommand);
	UpdateWindow(window);

	MSG message{};
	while (GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return static_cast<int>(message.wParam);
}