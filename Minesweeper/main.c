#include <Windows.h>
#include <stdlib.h>
#include "resource.h"
#include "registry.h"
#include <commctrl.h>
#include <WinUser.h>
#include "enums.h"
#include "globals.h"
#include "signatures.h"

void InitializeA();

DWORD SimpleGetSystemMetrics(DWORD val) {
    DWORD result;

    switch (val) {
    case GET_SCREEN_WIDTH:
        result = GetSystemMetrics(SM_CXVIRTUALSCREEN);

        if (result == 0) {
            result = GetSystemMetrics(SM_CXSCREEN);
        }
        break;
    case GET_SCREEN_HEIGHT:
        result = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        if (result == 0) {
            result = GetSystemMetrics(SM_CYSCREEN);
        }
        break;
    default:
        result = GetSystemMetrics(val);
    }

    return result;
}

// .text:01003950
void DisplayErrorMessage(UINT uID) {
    WCHAR Buffer[128];
    WCHAR Caption[128];
    
    if (uID >= 999) {
        LoadStringW(hModule, ID_ERR_MSG_D, Caption, sizeof(Caption));
        wsprintfW(Buffer, Caption, uID);
    }
    else {
        LoadStringW(hModule, uID, Buffer, sizeof(Buffer));
    }

    // Show Title: Minesweeper Error, Content: Buffer from above
    LoadStringW(hModule, ID_ERROR, Caption, sizeof(Caption));
    MessageBoxW(NULL, Buffer, Caption, MB_ICONERROR);
}

VOID LoadResourceString(UINT uID, LPWSTR lpBuffer, DWORD cchBufferMax) {
    if (!LoadStringW(hModule, uID, lpBuffer, cchBufferMax)) {
        DisplayErrorMessage(1001);
    }
}

// Called on button release
// Called on MouseMove 
__inline void ReleaseMouseCapture() {
	HasMouseCapture = FALSE;
	ReleaseCapture();

	if (StateFlags & STATE_GAME_IS_ON) {
		ReleaseBlocksClick();		
	}
	else {
		DisplayResult();
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        
    // uMsg is between 0x201 to 0x212 (inclusive)
    switch (uMsg) {
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (HasMouseCapture) {
            ReleaseMouseCapture();
        }
        break;
    case WM_ENTERMENULOOP:
        IsMenuOpen = TRUE;
        break;
    case WM_EXITMENULOOP:
        IsMenuOpen = FALSE;
        break;
    case WM_MBUTTONDOWN:
		if (IgnoreSingleClick) {
			IgnoreSingleClick = FALSE;
			return FALSE;
		}

        if ((StateFlags & STATE_GAME_IS_ON) == 0) {
            Is3x3Click = TRUE;
            return CaptureMouseInput(hwnd, uMsg, wParam, lParam);
        }
        break;
    case WM_RBUTTONDOWN:
        if (IgnoreSingleClick) {
            IgnoreSingleClick = FALSE;
            return FALSE;
        }

        if ((StateFlags & STATE_GAME_IS_ON) == 0) {
            break;
        }

        if (HasMouseCapture) {
			ReleaseBlocksClick();
            Is3x3Click = TRUE;
            PostMessageW(hWnd, WM_MOUSEMOVE, wParam, lParam);
        }
        else if (wParam & 1) {
            CaptureMouseInput(hwnd, uMsg, wParam, lParam);
        }
        else if (!IsMenuOpen) {

            HandleRightClick((BoardPoint) {
				.Column = (LOWORD(lParam) + 4) / 16,
				.Row = (HIWORD(lParam) - 39) / 16
			});
        }

        return FALSE;

    case WM_LBUTTONDOWN:
        if (IgnoreSingleClick) {
			IgnoreSingleClick = FALSE;
            return FALSE;
        }
        if (HandleLeftClick((DWORD)lParam)) {
            return 0;
        }

        if ((StateFlags & STATE_GAME_IS_ON) == 0) {
            break;
        }

        Is3x3Click = (wParam & 6) ? TRUE : FALSE;
        return CaptureMouseInput(hwnd, uMsg, wParam, lParam);

    case WM_MOUSEMOVE:
        return MouseMoveHandler(hwnd, uMsg, wParam, lParam);

    case WM_KEYDOWN:
        KeyDownHandler(wParam);
        break;

    case WM_COMMAND:
        if (!MenuHandler(LOWORD(wParam))) {
            return FALSE;
        }

        break;

    case WM_SYSCOMMAND:
        
        switch (GET_SC_WPARAM(wParam)) {
        case SC_MINIMIZE:
            NotifyMinimize();
            // Maybe add minimize flags..
            StateFlags |= (STATE_WINDOW_MINIMIZED | STATE_WINDOW_MINIMIZED_2);
            break;
        case SC_RESTORE:
            // Clean those bits from earlier
			StateFlags &= ~(STATE_WINDOW_MINIMIZED | STATE_WINDOW_MINIMIZED_2);
            NotifyWindowRestore();
            IgnoreSingleClick = FALSE;
            break;
        }

        break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID);
        PostQuitMessage(0);
        break;
    case WM_ACTIVATE:
        if (wParam == WA_CLICKACTIVE) {
            IgnoreSingleClick = TRUE;
        }
        break;
    case WM_WINDOWPOSCHANGED:
        if ((StateFlags & STATE_WINDOW_MINIMIZED_2) == 0) {
            WINDOWPOS* pos = (WINDOWPOS*)lParam;
            Xpos_InitFile = pos->x;
            Ypos_InitFile = pos->y;
        }
        break;
    case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC hDC = BeginPaint(hWnd, &paint);
		RedrawUIOnDC(hDC);
		EndPaint(hWnd, &paint);
		return FALSE;
	}
    case WM_TIMER:
        TickSeconds();
        return FALSE;
    
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

typedef struct _DifficultyConfigItem {
	DWORD Mines;
	DWORD Height;
	DWORD Width;
} DifficultyConfigItem;

#define BEGINNER_MINES 10
#define BEGINEER_HEIGHT 9
#define BEGINNER_WIDTH 9

#define INTERMIDIATE_MINES 40
#define INTERMIDIATE_HEIGHT 16
#define INTERMIDIATE_WIDTH 16

#define EXPERT_MINES 99
#define EXPERT_HEIGHT 16
#define EXPERT_WIDTH 30

DifficultyConfigItem DifficultyConfigTable[] = {
	{ BEGINNER_MINES, BEGINEER_HEIGHT, BEGINNER_WIDTH },
	{ INTERMIDIATE_MINES, INTERMIDIATE_HEIGHT, INTERMIDIATE_WIDTH },
	{ EXPERT_MINES, EXPERT_HEIGHT, EXPERT_WIDTH }
};

__inline BOOL MenuHandler(WORD menuItem) {

	if (menuItem >= ID_MENUITEM_BEGINNER && menuItem <= ID_MENUITEM_EXPERT){
        Difficulty_InitFile = LOWORD(menuItem - ID_MENUITEM_BEGINNER);

        Mines_InitFile = DifficultyConfigTable[Difficulty_InitFile].Mines;
        Height_InitFile = DifficultyConfigTable[Difficulty_InitFile].Height;
        Width_InitFile = DifficultyConfigTable[Difficulty_InitFile].Width;
        InitializeNewGame();
        NeedToSaveConfigToRegistry = TRUE;
        InitializeMenu(Menu_InitFile);
        return TRUE;
    }

    switch (menuItem) {
    case ID_MENUITEM_COLOR:
		// Toogle Color
		Color_InitFile = !Color_InitFile;
        FreePenAndBlocks();
        
        if (LoadBitmaps()) {
            RedrawUI();
            NeedToSaveConfigToRegistry = TRUE;
            InitializeMenu(Menu_InitFile);
        }
        else {
            DisplayErrorMessage(ID_OUT_OF_MEM);
            
            // Send a close message
            SendMessageW(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            return FALSE;
        }
        break;
    case ID_MENUITEM_BEST_TIMES:
        WinnersDialogBox();
        break;
    case ID_MENUITEM_NEWGAME:
        InitializeNewGame();
        break;
    case ID_MENUITEM_CUSTOM:
        CustomFieldDialogBox();
        break;
    case ID_MENUITEM_SEARCH_FOR_HELP:
        ShowHelpHtml(1, 2);
        break;
    case ID_MENUITEM_USING_HELP:
        ShowHelpHtml(4, 0);
        break;
    case ID_MENUITEM_ABOUT:
        ShowAboutWindow();
        return FALSE;
        break;
    case ID_MENUITEM_CONTENTS:
        ShowHelpHtml(3, 0);
        break;
    case ID_MENUITEM_MARKS:
        Mark_InitFile = (Mark_InitFile) ? FALSE : TRUE;
        NeedToSaveConfigToRegistry = TRUE;
        InitializeMenu(Menu_InitFile);
        break;
    case ID_MENUITEM_SOUND:
		
        if (Sound_InitFile) {
            FreeSound();
            Sound_InitFile = 0;
        }
        else {
            Sound_InitFile = StopAllSound();
        }
        NeedToSaveConfigToRegistry = TRUE;
        InitializeMenu(Menu_InitFile);
        break;
    case ID_MENUITEM_EXIT:
        ShowWindow(hWnd, FALSE);
        SendMessageW(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        break;
    default:
        break;
    }

    return TRUE;
}


__inline void KeyDownHandler(WPARAM wParam) {
    switch (wParam) {
    case VK_SHIFT:
        if (CheatPasswordIndex >= 5) {
            // flip these bits
            CheatPasswordIndex ^= 0x14; // 0b10100
        }
        break;
    case VK_F4:
        if (Sound_InitFile > 1) {
            if (Sound_InitFile == 3) {
                FreeSound();
                Sound_InitFile = 2;
            }
            else {
                Sound_InitFile = StopAllSound();
            }
        }
        break;
    case VK_F5:
        if (Menu_InitFile) {
            InitializeMenu(1);
        }
        break;
    case VK_F6:
        if (Menu_InitFile) {
            InitializeMenu(2);
        }
        break;
    default: 
        if (CheatPasswordIndex < 5) { 
            if (CheatPassword[CheatPasswordIndex] == LOWORD(wParam)) {
                CheatPasswordIndex++;
            }
            else {
                CheatPasswordIndex = 0;
            }
        }
    }
}

__inline LRESULT CaptureMouseInput(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Shared code...
    SetCapture(hWnd);
	ClickedBlock = (BoardPoint) { -1, -1 };
    HasMouseCapture = TRUE;
    DisplaySmile(SMILE_WOW);
    return MouseMoveHandler(hwnd, uMsg, wParam, lParam);
}

__inline LRESULT MouseMoveHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // WM_MOUSEMOVE_Handler!
    if (!HasMouseCapture) {
        if (CheatPasswordIndex == 0) {
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }

        if (CheatPasswordIndex > 5 || (CheatPasswordIndex == 5 && wParam != MK_CONTROL)) {
			// Get Mouse Block
			ClickedBlock = (BoardPoint) {
				.Column = (LOWORD(lParam) + 4) / 16,
					.Row = (HIWORD(lParam) - 39) / 16
			};

			if (!IsInBoardRange(ClickedBlock)) {
				return DefWindowProcW(hWnd, uMsg, wParam, lParam);
			}

            HDC hDesktop = GetDC(NULL);
			BYTE block = ACCESS_BLOCK(ClickedBlock);
            SetPixel(hDesktop, 0, 0, (block & BLOCK_IS_BOMB) ? BLACK_COLOR : WHITE_COLOR);

            return FALSE;
        }
    }
    else if ((StateFlags & STATE_GAME_IS_ON) == 0) {
        ReleaseMouseCapture();
    }
    else {
		// Update Mouse Block
		UpdateClickedBlocksState((BoardPoint) {
			.Column = (LOWORD(lParam) + 4) / 16,
			.Row = (HIWORD(lParam) - 39) / 16
		});
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}


void HandleRightClick(BoardPoint point) {
	if (IsInBoardRange(point)) {
        BYTE block = ACCESS_BLOCK(point);

        if (!(block & BLOCK_IS_REVEALED)) {
            BYTE blockState = block & BLOCK_STATE_MASK;

            switch (blockState) {
            case BLOCK_STATE_FLAG:
                blockState = (Mark_InitFile) ? BLOCK_STATE_QUESTION_MARK : BLOCK_STATE_EMPTY_UNCLICKED;
                AddAndDisplayLeftFlags(1);
                break;
            case BLOCK_STATE_QUESTION_MARK:
                blockState = BLOCK_STATE_EMPTY_UNCLICKED;
                break;
            default: // Assume BLOCK_STATE_EMPTY_UNCLICKED
                blockState = BLOCK_STATE_FLAG;
                AddAndDisplayLeftFlags(-1);
            }

            ChangeBlockState(point, blockState);

            if (BLOCK_IS_STATE(block, BLOCK_STATE_FLAG) &&
                NumberOfRevealedBlocks == NumberOfEmptyBlocks) {
                FinishGame(TRUE);
            }
        }
    }
}


void DisplayResult() {
	if (IsInBoardRange(ClickedBlock)){
        if (NumberOfRevealedBlocks == 0 && TimerSeconds == 0) {
            // First Click! Initialize Timer 
            PlayGameSound(SOUNDTYPE_TICK);
            TimerSeconds++;
            DisplayTimerSeconds();
            IsTimerOnAndShowed = TRUE;
            
            if (!SetTimer(hWnd, TIMER_ID, 1000, NULL)){
                DisplayErrorMessage(ID_TIMER_ERROR);
            }
        }

        if (!(StateFlags & STATE_GAME_IS_ON)){
			// WIERD: Setting this to -2, -2 causes buffer overflow
			ClickedBlock = (BoardPoint) { -2, -2 };
        }

        if (Is3x3Click){
            Handle3x3Click(ClickedBlock);
        }
        else {
			// WIERD: The buffer overflow occurs here
			// ClickedBlock might be (-2, -2)
			BYTE blockValue = ACCESS_BLOCK(ClickedBlock);

            if (!(blockValue & BLOCK_IS_REVEALED) && !BLOCK_IS_STATE(blockValue, BLOCK_STATE_FLAG)) {
                HandleBlockClick(ClickedBlock);
            }
        }


    }

    DisplaySmile(GlobalSmileId);
}


void NotifyMinimize() {
    FreeSound();

    if ((StateFlags & STATE_WINDOW_MINIMIZED) == 0) {
        IsTimerOnTemp = IsTimerOnAndShowed;
    }

    if (StateFlags & STATE_GAME_IS_ON) {
        IsTimerOnAndShowed = FALSE;
    }

    StateFlags |= STATE_WINDOW_MINIMIZED;
}

void NotifyWindowRestore() {
    if (StateFlags & STATE_GAME_IS_ON) {
        IsTimerOnAndShowed = IsTimerOnTemp;
    }
    
	StateFlags &= ~(STATE_WINDOW_MINIMIZED);
}

INT_PTR CALLBACK CustomFieldDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_HELP:
	{
		HELPINFO* pHelpInfo = (HELPINFO*)lParam;
		WinHelpW((HWND)pHelpInfo->hItemHandle, L"winmine.hlp", HELP_WM_HELP, (ULONG_PTR)&winnersHelpData);
		return FALSE;
	}
    case WM_CONTEXTMENU:
	{
		WinHelpW((HWND)wParam, L"winmine.hlp", HELP_CONTEXTMENU, (ULONG_PTR)&winnersHelpData);
		return FALSE;
	}
    case WM_INITDIALOG:
	{
		SetDlgItemInt(hDlg, ID_DIALOG_CUSTOM_FIELD_HEIGHT, Height_InitFile, FALSE);
		SetDlgItemInt(hDlg, ID_DIALOG_CUSTOM_FIELD_WIDTH, Width_InitFile, FALSE);
		SetDlgItemInt(hDlg, ID_DIALOG_CUSTOM_FIELD_MINES, Mines_InitFile, FALSE);
		return TRUE;
	}
    case WM_COMMAND:
	{
		switch (LOWORD(wParam)) {
		case 1:
		case 100:
		{
			Height_InitFile = GetDlgIntOfRange(hDlg, ID_DIALOG_CUSTOM_FIELD_HEIGHT, 9, 24);
			Width_InitFile = GetDlgIntOfRange(hDlg, ID_DIALOG_CUSTOM_FIELD_WIDTH, 9, 30);
			int maxMines = min((Height_InitFile - 1) * (Width_InitFile - 1), 999);
			Mines_InitFile = GetDlgIntOfRange(hDlg, ID_DIALOG_CUSTOM_FIELD_MINES, 10, maxMines);
		}
		break;
		case 2:
			break;
		default:
			return FALSE;
		}

		EndDialog(hDlg, TRUE);
		return TRUE;
	}
    }

	return FALSE;
}

void CustomFieldDialogBox() {
    DialogBoxParamW(hModule, (LPCWSTR)ID_DIALOG_CUSTOM_FIELD, hWnd, CustomFieldDialogProc, 0);
    Difficulty_InitFile = 3;
    // WIERD: Unconditionally change the difficulty to 3...
    InitializeCheckedMenuItems();
    NeedToSaveConfigToRegistry = TRUE;
}

void Handle3x3Click(BoardPoint point){
	BYTE blockValue = ACCESS_BLOCK(point);

    if ((blockValue & BLOCK_IS_REVEALED) && GetFlagBlocksCount(point) == blockValue){
        BOOL lostGame = FALSE;

        for (DWORD loop_row=(point.Row-1); loop_row<=(point.Row+1); ++loop_row){
            for (DWORD loop_column=(point.Column-1); loop_column<=(point.Column+1); ++loop_column){
				BYTE blockValue = BlockArray[loop_row][loop_column];

                if (blockValue == BLOCK_STATE_FLAG || !(blockValue & BLOCK_IS_BOMB)) {
					ExpandEmptyBlock((BoardPoint) { loop_column, loop_row });
                }
                else { 
					// The user clicked a bomb
                    lostGame = TRUE;
					ChangeBlockState((BoardPoint) { loop_column, loop_row }, BLOCK_IS_REVEALED | BLOCK_STATE_BOMB_RED_BACKGROUND);
                }
            }       
        }

        if (lostGame) {
            FinishGame(FALSE);
        }
        else if (NumberOfEmptyBlocks == NumberOfRevealedBlocks) {
            FinishGame(TRUE);
        }

    } else {
		ReleaseBlocksClick();
         return;

    }

}


void TickSeconds() {
    if (IsTimerOnAndShowed && TimerSeconds < 999) {
        TimerSeconds++;
        DisplayTimerSeconds();
        PlayGameSound(SOUNDTYPE_TICK);
    }
}

void ChangeBlockState(BoardPoint point, BYTE blockState) {
	PBYTE pBlock = &ACCESS_BLOCK(point);

    *pBlock = (*pBlock & 0xE0) | blockState;

    DrawBlock(point);
}

__inline void ReplaceFirstNonBomb(BoardPoint point, PBYTE pFunctionBlock) {
	// The first block! Change a normal block to a bomb, 
	// Replace the current block into an empty block
	// Reveal the current block
	// WIERD: LOOP IS WITHOUT AN EQUAL SIGN
	for (DWORD current_row = 1; current_row < Height_InitFile2; ++current_row) {
		for (DWORD current_column = 1; current_column < Width_InitFile2; ++current_column) {
			PBYTE pLoopBlock = &BlockArray[current_row][current_column];

			// Find the first non-bomb
			if (!(*pLoopBlock & BLOCK_IS_BOMB)) {
				// Replace bomb place
				*pFunctionBlock = BLOCK_STATE_EMPTY_UNCLICKED;
				*pLoopBlock |= BLOCK_IS_BOMB;

				ExpandEmptyBlock(point);
				return;
			}
		}
	}
}

void HandleBlockClick(BoardPoint point) {
	PBYTE pFunctionBlock = &ACCESS_BLOCK(point);

	// Click an empty block
    if (!(*pFunctionBlock & BLOCK_IS_BOMB)) {
        ExpandEmptyBlock(point);

        if (NumberOfRevealedBlocks == NumberOfEmptyBlocks) {
            FinishGame(TRUE);
        }
    }
	// Clicked bomb and it's the first block 
    else if (NumberOfRevealedBlocks == 0) { 
		ReplaceFirstNonBomb(point, pFunctionBlock);
    }
	// Clicked A Bomb
    else {  
		ChangeBlockState(point, BLOCK_IS_REVEALED | BLOCK_STATE_BOMB_RED_BACKGROUND);
		FinishGame(FALSE);
    }
}

void ExpandEmptyBlock(BoardPoint point) {

    currentLocationIndex = 1;
    ShowBlockValue(point);

    int i = 1;

    while (i != currentLocationIndex) {
		DWORD row = RowsList[i];
        DWORD column = ColumnsList[i];

		ShowBlockValue((BoardPoint){ point.Column - 1, point.Row - 1 });
		ShowBlockValue((BoardPoint){ column, row - 1 });
		ShowBlockValue((BoardPoint) { column + 1, row - 1 });

		ShowBlockValue((BoardPoint) { column - 1, row });
		ShowBlockValue((BoardPoint) { column + 1, row });

		ShowBlockValue((BoardPoint) { column - 1, row + 1 });
		ShowBlockValue((BoardPoint) { column, row + 1 });
		ShowBlockValue((BoardPoint) { column + 1, row + 1 });
        i++;

        if (i == 100) {
            i = 0;
        }
    }
    
}

void ShowBlockValue(BoardPoint point){

	BYTE blockValue = ACCESS_BLOCK(point);

    if (blockValue & BLOCK_IS_REVEALED){
        return;
    }

    BYTE state = blockValue & BLOCK_STATE_MASK;

    if (state == 0x10 || state == BLOCK_STATE_FLAG) {
        return;
    }

    NumberOfRevealedBlocks++;

    int nearBombsCount = CountNearBombs(point);
    nearBombsCount |= BLOCK_IS_REVEALED;
	ACCESS_BLOCK(point) = nearBombsCount;
    DrawBlock(point);
    
    if (nearBombsCount == 0) {
        RowsList[currentLocationIndex] = point.Row;
        ColumnsList[currentLocationIndex] = point.Column;

        currentLocationIndex++;
        
        if (currentLocationIndex == 100) {
            currentLocationIndex = 0;
        }
    }    
}


void RedrawUI() {
    HDC hDC = GetDC(hWnd);
    RedrawUIOnDC(hDC);
    ReleaseDC(hWnd, hDC);
}

void RedrawUIOnDC(HDC hDC) {
    DrawHUDRectangles(hDC);
    DisplayLeftFlagsOnDC(hDC);
    DisplaySmileOnDC(hDC, GlobalSmileId);
    DisplayTimerSecondsOnDC(hDC);
    DisplayAllBlocksInDC(hDC);
}

void DrawHUDRectangles(HDC hDC) {
    RECT rect;
    
    // Draw Frame Bar
    // Does not use the lower-right lines
    rect.left = 0;
    rect.top = 0;
    rect.right = xRight - 1;
    rect.bottom = yBottom - 1;
    DrawHUDRectangle(hDC, rect, 3, 1);
    
    //  Blocks Board Rectangle
    rect.left = 9;
    rect.top = 52;
    rect.right = xRight - 10;
    rect.bottom = yBottom - 1 - 9;
    DrawHUDRectangle(hDC, rect, 3, 1);
    
    // Upper Rectangle - Contains: LeftMines | Smile | SecondsLeft
    rect.left = 9;
    rect.top = 9;
    rect.right = xRight - 10;
    rect.bottom = 45;
    DrawHUDRectangle(hDC, rect, 2, 0);

    // LeftMines Counter Rectangle
    rect.left = 16;
    rect.top = 16;
    rect.right = 56;
    rect.bottom = 39;
    DrawHUDRectangle(hDC, rect, 1, 0);

    // SecondsLeft Rectangle
    rect.left = xRight - WindowWidthInPixels - 57;
    rect.top = 15;
    rect.right = rect.left + 40;
    rect.bottom = 39;
    DrawHUDRectangle(hDC, rect, 1, 0);

    // Smile Rectangle
    rect.left = (xRight - 24) / 2 - 1;
    rect.top = 15;
    rect.right = rect.left + 25;
    rect.bottom = 40;
    DrawHUDRectangle(hDC, rect, 1, 2);
}


void SetROPWrapper(HDC hDC, BYTE white_or_copypen) {
    if (white_or_copypen & 1) {
        SetROP2(hDC, R2_WHITE);
    }
    else {
        SetROP2(hDC, R2_COPYPEN);
        SelectObject(hDC, hPen);
    }
}

/*
    If white_or_copypen is 1, the lower-right side is not drawn
*/
void DrawHUDRectangle(HDC hDC, RECT rect, DWORD lines_width, BYTE white_or_copypen) {
    
    SetROPWrapper(hDC, white_or_copypen);

    for (DWORD i = 0; i < lines_width; i++) {
        rect.bottom--;
        MoveToEx(hDC, rect.left, rect.bottom, NULL);
        
        // Draw left vertical line
        LineTo(hDC, rect.left, rect.top);
        rect.left++;

        // Draw top line
        LineTo(hDC, rect.right, rect.top);
        rect.right--;
        rect.top++;
    }

    if (white_or_copypen < 2) {
        SetROPWrapper(hDC, white_or_copypen ^ 1);
    }

    for (DWORD i = 0; i < lines_width; i++) {
        rect.bottom++;
        MoveToEx(hDC, rect.left, rect.bottom, NULL);
        rect.left--;
        rect.right++;

        // Draw lower line
        LineTo(hDC, rect.right, rect.right);
        rect.top--;

        // Draw right line
        LineTo(hDC, rect.right, rect.top);
    }
}


int CountNearBombs(BoardPoint point){
    int count = 0;

    for (DWORD loop_row = (point.Row-1); loop_row<=(point.Row+1); loop_row++){
        for (DWORD loop_column = (point.Column - 1); loop_column<=(point.Column+1); loop_column++){
            if (BlockArray[loop_row][loop_column] & BLOCK_IS_BOMB){
                count++;
            }
        }
    }

    return count;
}

void FinishGame(BOOL isWon){
    IsTimerOnAndShowed = FALSE;
    GlobalSmileId = (isWon) ? SMILE_WINNER : SMILE_LOST;
    DisplaySmile(GlobalSmileId);
    
    // If the player wins, bombs are changed into flags
    // If the player loses, bombs change into black bombs
    RevealAllBombs((isWon) ? BLOCK_STATE_FLAG : BLOCK_STATE_BLACK_BOMB);

    if (isWon && LeftFlags != 0){
        AddAndDisplayLeftFlags(-LeftFlags);
    }

    PlayGameSound(isWon ? SOUNDTYPE_WIN : SOUNDTYPE_LOSE);
    StateFlags = STATE_GAME_FINISHED;

    // Check if it is the best time
    if (isWon && Difficulty_InitFile != DIFFICULTY_CUSTOM){
        if (TimerSeconds < Time_InitFile[Difficulty_InitFile]){
            Time_InitFile[Difficulty_InitFile] = TimerSeconds;
            SaveWinnerNameDialogBox();
            WinnersDialogBox();
        }
    }
}


void ShowAboutWindow() {
    WCHAR szApp[128];
    WCHAR szCredits[128];

    LoadResourceString(ID_MINESWEEPER2, szApp, 128);
    LoadResourceString(ID_CREDITS, szCredits, 128);
    
    HICON hIcon = LoadIconW(hModule, (LPCWSTR)100);
    ShellAboutW(hWnd, szApp, szCredits, hIcon);
}

BOOL FindHtmlHelpDLL(PSTR outputLibraryName) {

    HKEY hKey;

    // This registry value actually contains the same ActiveX Control DLL "hhctrl.ocx"
    if (!RegOpenKeyExA(HKEY_CLASSES_ROOT,
        "CLSID\\{ADB880A6-D8FF-11CF-9377-00AA003B7A11}\\InprocServer32",
        0,
        KEY_READ,
        &hKey
    )) {
        return FALSE;
    }

    DWORD cbData = 260;
    BOOL result = FALSE;
    
    if (RegQueryValueExA(hKey, "", NULL, NULL, (LPBYTE)outputLibraryName, &cbData) == 0) {
        result = TRUE;
    }

    RegCloseKey(hKey);
    return TRUE;
}

void DisplayHelpWindow(HWND hDesktopWnd, LPCSTR lpChmFilename, UINT uCommand, DWORD_PTR dwData) {
    if (HtmlHelpModuleHandle == NULL && ErrorLoadingHelpFunc == FALSE) {
        CHAR libFileName[260];
        
        if (FindHtmlHelpDLL(libFileName)) {
            HtmlHelpModuleHandle = LoadLibraryA(libFileName);
        }

        if (HtmlHelpModuleHandle == NULL) {
            HtmlHelpModuleHandle = LoadLibraryA("hhctrl.ocx");
        }

        if (HtmlHelpModuleHandle == NULL) {
            ErrorLoadingHelpFunc = TRUE;
            return;
        }

        if (HtmlHelpWPtr == NULL) {
            HtmlHelpWPtr = (HTMLHELPWPROC)GetProcAddress(HtmlHelpModuleHandle, (LPCSTR)HtmlHelpW_ExportOrdinal);

            if (HtmlHelpWPtr == NULL) {
                ErrorLoadingHelpFunc = TRUE;
                return;
            }
        }

        // WIERD!
        // Using HtmlHelpW with ascii instead of unicode filename???
        HtmlHelpWPtr(hDesktopWnd, (LPCWSTR)lpChmFilename, uCommand, dwData);
    }
}

void ShowHelpHtml(DWORD arg0, UINT uCommand) {
    CHAR ChmFilename[250];

    if (arg0 == 4) {
        // WIERD: Missing NULL Terminator
        memcpy(ChmFilename, "NTHelp.chm", 10);
    }
    else {
        DWORD dwLength = GetModuleFileNameA(hModule, ChmFilename, sizeof(ChmFilename));
        
        // Point to the last char (the last e of the exe)
        PSTR copyDest = &ChmFilename[dwLength - 1];
        PSTR lastCharPointer = copyDest;

        if ((lastCharPointer - ChmFilename) > 4  // the file is at least 5 chars
            && *(lastCharPointer - 3) == '.') { // there is a dot before the 'exe'
            copyDest = lastCharPointer - 3;
        }

		// WIERD: Buffer overflow copying ".chm"
        memcpy(copyDest, ".chm", 4);
    }

    DisplayHelpWindow(GetDesktopWindow(), ChmFilename, uCommand, (DWORD_PTR)NULL);
}

int GetDlgIntOfRange(HWND hDlg, int nIDDlgItem, int min, int max) {
    
    int result = GetDlgItemInt(hDlg, nIDDlgItem, &nIDDlgItem, FALSE);
	
    if (result < min) {
        return min;
    }

    if (result > max) {
        return max;
    }

    return result;
}

void RevealAllBombs(BYTE revealedBombsState) {

    for (DWORD loop_row = 1; loop_row <= Height_InitFile2; ++loop_row) {
        for (DWORD loop_column = 1; loop_column <= Width_InitFile2; ++loop_column) {
            PBYTE pBlock = &BlockArray[loop_row][loop_column];

            if (*pBlock & BLOCK_IS_REVEALED) {
                continue;
            }

            BYTE blockState = *pBlock & BLOCK_STATE_MASK;

            if (*pBlock & BLOCK_IS_BOMB) {
                if (blockState != BLOCK_STATE_FLAG) {
                    *pBlock = (*pBlock & 0xe0) | revealedBombsState;
                }
            }
            else if (blockState == BLOCK_STATE_FLAG){
                // This is not a bomb, but flagged by the user
                *pBlock = (*pBlock & 0xeb) | BLOCK_STATE_BOMB_WITH_X;
            }
        }
    }

    DisplayAllBlocks();
}

void DisplayAllBlocks() {
    HDC hDC = GetDC(hWnd);
    DisplayAllBlocksInDC(hDC);
    ReleaseDC(hWnd, hDC);
}

void DisplayAllBlocksInDC(HDC hDC) {
    int y = 55;
    
    for (DWORD loop_row = 1; loop_row <= Height_InitFile2; ++loop_row) {
        int x = 12;

        for (DWORD loop_column = 1; loop_column <= Width_InitFile2; loop_column++) {
            // Get the current state of the block
            BYTE blockValue = BlockArray[loop_row][loop_column];
            HDC blockState = BlockStates[blockValue & BLOCK_STATE_MASK];
            
            // Draw the block
            BitBlt(hDC, x, y, 16, 16, blockState, 0, 0, SRCCOPY);
            x += 16;
        }

        y += 16;
    }
}

void SaveWinnerNameDialogBox(){
    DialogBoxParamW(hModule, (LPCWSTR)ID_DIALOG_SAVE_NAME, hWnd, SaveWinnerNameDialogProc, 0);
    NeedToSaveConfigToRegistry = TRUE;
}


INT_PTR CALLBACK SaveWinnerNameDialogProc(HWND hDialog, UINT uMsg, WPARAM wParam, LPARAM lParam){
     WCHAR winMsg[128];

    if (uMsg == WM_INITDIALOG){
        // Load you have the fastest time message
        LoadResourceString(Difficulty_InitFile+9, winMsg, _countof(winMsg));

        // Show the message on the 
        SetDlgItemTextW(hDialog, ID_DIALOG_SAVE_NAME_CONTROL_MSG, winMsg);

        SendMessageW(GetDlgItem(hDialog, ID_DIALOG_SAVE_NAME), 
            EM_SETLIMITTEXT, 32, 0);

        PWSTR pWinnerNamePtr = NULL;

        switch (Difficulty_InitFile){
            case DIFFICULTY_BEGINNER:
				pWinnerNamePtr = Name1_InitFile;
                break;
            case DIFFICULTY_INTERMEDIATE:
                pWinnerNamePtr = Name2_InitFile;
                break;
            default: 
                pWinnerNamePtr = Name3_InitFile;
                break;
        }

        SetDlgItemTextW(hDialog, ID_DIALOG_SAVE_NAME_CONTROL_NAME, pWinnerNamePtr);
        return TRUE;
    
    } else if (uMsg ==  WM_COMMAND && wParam > 0 && 
        (wParam <= 2 || wParam == 100 || wParam == 109)){

         PWSTR pWinnerNamePtr = NULL;

        switch (Difficulty_InitFile){
            case DIFFICULTY_BEGINNER:
				pWinnerNamePtr = Name1_InitFile;
                break;
            case DIFFICULTY_INTERMEDIATE:
                pWinnerNamePtr = Name2_InitFile;
                break;
            default: 
                pWinnerNamePtr = Name3_InitFile;
                break;
        }

        GetDlgItemTextW(hDialog, ID_DIALOG_SAVE_NAME_CONTROL_NAME, pWinnerNamePtr, 32);
        EndDialog(hDialog, 1);
        return TRUE;
    }

    return FALSE;
}


INT_PTR CALLBACK WinnersDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam){
    switch (uMsg){
        case WM_HELP:
		{
			HELPINFO* pHelpInfo = (HELPINFO*)lParam;
			WinHelpW((HWND)pHelpInfo->hItemHandle, L"winmine.hlp", HELP_WM_HELP, (ULONG_PTR)winnersHelpData);
			return FALSE;
		}
        case WM_CONTEXTMENU:
		{
			HELPINFO* pHelpInfo = (HELPINFO*)lParam;
			WinHelpW((HWND)wParam, L"winmine.hlp", HELP_CONTEXTMENU, (ULONG_PTR)winnersHelpData);
			return FALSE;
		}
        case WM_INITDIALOG:
            break;
        case WM_COMMAND:
		{
			DWORD msg = LOWORD(wParam);

			if (msg <= 0) {
				return FALSE;
			}

			if (msg <= 2 || msg == 100 || msg == 109) {
				EndDialog(hDlg, 1);
				return FALSE;
			}

			if (msg == ID_DIALOG_WINNERS_BTN_RESET_SCORES) {
				Time_InitFile[0] = 999;
				Time_InitFile[1] = 999;
				Time_InitFile[2] = 999;

				lstrcpyW(Name1_InitFile, AnonymousStr);
				lstrcpyW(Name2_InitFile, AnonymousStr);
				lstrcpyW(Name3_InitFile, AnonymousStr);

				NeedToSaveConfigToRegistry = TRUE;
			}
		}
        default:
            return FALSE;
    }


	ShowWinnerNameAndTime(hDlg, ID_DIALOG_WINNERS_TIME_BEGINNER, Time_InitFile[0] , Name1_InitFile);
    ShowWinnerNameAndTime(hDlg, ID_DIALOG_WINNERS_TIME_INTERMIDIATE, Time_InitFile[1], Name2_InitFile);
	ShowWinnerNameAndTime(hDlg, ID_DIALOG_WINNERS_TIME_EXPERT, Time_InitFile[2] , Name3_InitFile);
    return TRUE;
}

void ShowWinnerNameAndTime(HWND hDlg, int nIDDlgItem, int secondsLeft, LPCWSTR lpName){
    WCHAR lpSecondsLeftFormatted[30];
    wsprintfW(SecondsLeftBuffer, lpSecondsLeftFormatted, secondsLeft);
    SetDlgItemTextW(hDlg, nIDDlgItem, lpSecondsLeftFormatted);
    SetDlgItemTextW(hDlg, nIDDlgItem+1, lpName);
}

void WinnersDialogBox(){
    DialogBoxParamW(hModule, (LPCWSTR)ID_DIALOG_WINNERS,
        hWnd, WinnersDialogProc, (LPARAM)NULL);
}


DWORD GetFlagBlocksCount(BoardPoint point){
    DWORD flagsCount = 0;

    // Search in the sorrunding blocks
    for (DWORD loop_row=(point.Row-1); loop_row<=(point.Row+1); ++loop_row){
        for (DWORD loop_column=(point.Column-1); loop_column<=(point.Column+1); ++loop_column){
            
            BYTE blockValue = BlockArray[loop_row][loop_column] & BLOCK_STATE_MASK;
            
            if (blockValue == BLOCK_STATE_FLAG){
                flagsCount++;
            }
        }

    }

    return flagsCount;
}


void DisplayTimerSeconds(){
    HDC hDC = GetDC(hWnd);
    DisplayTimerSecondsOnDC(hDC);
    ReleaseDC(hWnd, hDC);
}


void DisplayTimerSecondsOnDC(HDC hDC){

    DWORD layout = GetLayout(hDC);

    if (layout & 1){
        SetLayout(hDC, 0);
    }

    // Largest Digit
    DisplayNumber(hDC, xRight - WindowWidthInPixels - 56, TimerSeconds / 100);

    // Second Digit
    DisplayNumber(hDC, xRight - WindowWidthInPixels - 43, TimerSeconds / 10);

    // Last Digit
    DisplayNumber(hDC, xRight - WindowWidthInPixels - 30, TimerSeconds % 10);

    if (layout & 1){
        SetLayout(hDC, layout);
    }
}

void PlayGameSound(DWORD soundType) {
    if (Sound_InitFile != SOUND_ON) {
        return;
    }
    
    DWORD soundResourceId;

    switch (soundType) {
    case SOUNDTYPE_TICK:
        soundResourceId = ID_SOUND_TICK;
        break;
    case SOUNDTYPE_WIN:
        soundResourceId = ID_SOUND_WIN;
        break;
    case SOUNDTYPE_LOSE:
        soundResourceId = ID_SOUND_LOSE;
        break;
    default:
        return;
    }

    PlaySoundW((LPCWSTR)soundResourceId, hModule, SND_ASYNC | SND_RESOURCE);
}

__inline BOOL IsInBoardRange(BoardPoint point) {
	return point.Column > 0 && point.Row > 0 && point.Column <= Width_InitFile2 && point.Row <= Height_InitFile2;
}

__inline VOID UpdateClickedBlocksStateNormal(BoardPoint newClick, BoardPoint oldClick) {
	if (IsInBoardRange(oldClick) && (ACCESS_BLOCK(oldClick) & BLOCK_IS_REVEALED) == 0) {
		UpdateBlockStateToUnclicked(oldClick);
		DrawBlock(oldClick);
	}

	if (IsInBoardRange(newClick)) {
		const BYTE block = ACCESS_BLOCK(newClick);

		if ((block & BLOCK_IS_REVEALED) == 0 && !BLOCK_IS_STATE(block, BLOCK_STATE_FLAG)) {
			UpdateBlockStateToClicked(ClickedBlock);
			DrawBlock(ClickedBlock);
		}
	}
}

__inline VOID UpdateClickedBlocksState3x3(BoardPoint newClick, BoardPoint oldClick) {
	BOOL isNewLocationInBounds = IsInBoardRange(newClick);
	BOOL isOldLocationInBounds = IsInBoardRange(oldClick);

	// Get 3x3 bounds for the old and new clicks
	DWORD oldTopRow = max(1, oldClick.Row - 1);
	DWORD oldBottomRow = min(Height_InitFile2, oldClick.Row + 1);

	DWORD topRow = max(1, newClick.Row - 1);
	DWORD bottomRow = min(Height_InitFile2, newClick.Row + 1);

	DWORD oldLeftColumn = max(1, oldClick.Column - 1);
	DWORD oldRightColumn = min(Width_InitFile2, oldClick.Column + 1);

	DWORD leftColumn = max(1, newClick.Column - 1);
	DWORD rightColumn = min(Width_InitFile2, newClick.Column + 1);

	// Change old to unclicked. WIERD: Missing bounds check
	for (DWORD loop_row = oldTopRow; loop_row <= oldBottomRow; loop_row++) {
		for (DWORD loop_column = oldLeftColumn; loop_column <= oldRightColumn; ++loop_column) {
			if ((BlockArray[loop_row][loop_column] & BLOCK_IS_REVEALED) == 0) {
				UpdateBlockStateToUnclicked((BoardPoint) { loop_column, loop_row });
			}
		}
	}

	// Change new to clicked
	if (isNewLocationInBounds) {
		for (DWORD loop_row = topRow; loop_row <= bottomRow; ++loop_row) {
			for (DWORD loop_column = leftColumn; loop_column < rightColumn; loop_column++) {
				if ((BlockArray[loop_row][loop_column] & BLOCK_IS_REVEALED) == 0) {
					UpdateBlockStateToClicked((BoardPoint) { loop_column, loop_row });
				}
			}
		}
	}

	// Draw old blocks
	if (isOldLocationInBounds) {
		for (DWORD loop_row = oldTopRow; loop_row <= oldBottomRow; loop_row++) {
			for (DWORD loop_column = oldLeftColumn; loop_column <= oldRightColumn; ++loop_column) {
				DrawBlock((BoardPoint) { loop_column, loop_row });
			}
		}
	}

	// Draw new blocks
	if (isNewLocationInBounds) {
		for (DWORD loop_row = topRow; loop_row <= bottomRow; ++loop_row) {
			for (DWORD loop_column = leftColumn; loop_column <= rightColumn; ++loop_column) {
				DrawBlock((BoardPoint) { loop_column, loop_row });
			}
		}
	}
}

__inline void ReleaseBlocksClick() {
	UpdateClickedBlocksState((BoardPoint) { -2, -2 });
}

void UpdateClickedBlocksState(BoardPoint point) {
    if (point.Column == ClickedBlock.Column && point.Row == ClickedBlock.Row) {
        return;
    }

	// Save old click point
	const BoardPoint oldClickPoint = ClickedBlock;
	
	// Update new click point
	ClickedBlock = point;

    if (Is3x3Click) {
		UpdateClickedBlocksState3x3(point, oldClickPoint);
    }
    else {
		UpdateClickedBlocksStateNormal(point, oldClickPoint);
    }

}

void UpdateBlockStateToClicked(BoardPoint point) {
	PBYTE pBlock = &ACCESS_BLOCK(point);
    BYTE BlockFlags = *pBlock & BLOCK_STATE_MASK;

    if (*pBlock == BLOCK_STATE_QUESTION_MARK) {
        BlockFlags = BLOCK_STATE_CLICKED_QUESTION_MARK;
    }
    else if (*pBlock == BLOCK_STATE_EMPTY_UNCLICKED) {
        BlockFlags = BLOCK_STATE_READ_EMPTY;
    } 

    // Clean the block state    
    // set the block state
    *pBlock = (*pBlock & ~BLOCK_STATE_MASK) | BlockFlags;
}

void UpdateBlockStateToUnclicked(BoardPoint point) {
	PBYTE pBlock = &ACCESS_BLOCK(point);
    BYTE Block = *pBlock & BLOCK_STATE_MASK;
	BYTE res;

    if (Block == BLOCK_STATE_CLICKED_QUESTION_MARK || Block == BLOCK_STATE_READ_EMPTY) {
        res = BLOCK_STATE_QUESTION_MARK;
	}
	else {
		res = BLOCK_STATE_EMPTY_UNCLICKED;
	}

    *pBlock = (Block & BLOCK_STATE_FLAG) | res;
}

void DrawBlock(BoardPoint point) {
    HDC hDC = GetDC(hWnd);
    BYTE BlockValue = ACCESS_BLOCK(point) & BLOCK_STATE_MASK;
    BitBlt(hDC, point.Column * 16 - 4, point.Row * 16 + 39, BLOCK_WIDTH, BLOCK_HEIGHT, BlockStates[BlockValue], 0, 0, SRCCOPY);
    ReleaseDC(hWnd, hDC);
}

BOOL HandleLeftClick(DWORD dwLocation) {
    MSG msg;
    RECT rect;

    // Copy point into struct
	msg.pt.y = HIWORD(dwLocation);
	msg.pt.x = LOWORD(dwLocation);

    // The rect represents the game board
    rect.left = (xRight - 24) / 2;
    rect.right = rect.left + 24;
    rect.top = 16;
    rect.bottom = 40;

	// Check if the click is in the range of the board
    if (!PtInRect(&rect, msg.pt)) {
        return FALSE;
    }
    
    // Capture mouse events outside the window 
	//   - to capture MOUSEMOVE and BOTTONUP events 
    SetCapture(hWnd);
    DisplaySmile(SMILE_CLICKED);


	// Convert window relative points to screen relative points
    MapWindowPoints(
		hWnd, // hWndFrom: Game window
		NULL, // hWndTo: Desktop screen
		(LPPOINT)&rect, // lpPoints - 2 points: (left, top), (right, bottom)
		2 // cPoints
	);

    int ebx = 0;
	
    while (TRUE) {
        // Wait for a message of this kind
		// Handle WM_MOUSEMOVE, WM_BUTTONUP events
        while (!PeekMessageW(&msg, hWnd, WM_MOUSEMOVE, WM_XBUTTONDBLCLK, TRUE)) {
        }
        
        switch (msg.message) {
        case WM_MOUSEMOVE:
            if (PtInRect(&rect, msg.pt) && ebx != 0) {
                DisplaySmile(GlobalSmileId);
                ebx = 0;
            }
            else if (ebx == 0) {
                ebx++;
                DisplaySmile(SMILE_NORMAL);
            }

            break;

        case WM_LBUTTONUP:
            if (ebx != 0 && PtInRect(&rect, msg.pt)) {
                GlobalSmileId = SMILE_NORMAL;
                DisplaySmile(SMILE_NORMAL);
                InitializeNewGame();
            }

            ReleaseCapture();
            return TRUE;
        default:
            if (ebx == 0) {
                ebx++;
                DisplaySmile(SMILE_CLICKED);
            }
            break;
        }
    }
}



void DisplaySmile(DWORD smileId) {
    HDC hDC = GetDC(hWnd);
    DisplaySmileOnDC(hDC, smileId);
    ReleaseDC(hWnd, hDC);
}


void DisplaySmileOnDC(HDC hDC, DWORD smileId) {
    SetDIBitsToDevice(
        hDC, // hdc
        (xRight - 24) / 2, // x
        16, // y
        24, // w
        24, // h
        0, // xSrc
        0, // ySrc 
        0, // ScanStart
        24, // cLines
        SmileBitmapIndex[smileId] + lpSmilesBitmapInfo, // lpvBits
        lpSmilesBitmapInfo, // lpbmi
        FALSE // ColorUse
    );
}

void InitializeMenu(DWORD menuFlags) {
    Menu_InitFile = menuFlags;
    InitializeCheckedMenuItems();
    SetMenu(hWnd, (Menu_InitFile & 1) ? NULL : hMenu);
	InitializeWindowBorder(MOVE_WINDOW);
}


void InitializeCheckedMenuItems() {
    // Set Difficulty Level Checkbox
    SetMenuItemState(ID_MENUITEM_BEGINNER, Difficulty_InitFile == DIFFICULTY_BEGINNER);
    SetMenuItemState(ID_MENUITEM_INTERMEDIATE, Difficulty_InitFile == DIFFICULTY_INTERMEDIATE);
    SetMenuItemState(ID_MENUITEM_EXPERT, Difficulty_InitFile == DIFFICULTY_EXPERT);
    SetMenuItemState(ID_MENUITEM_CUSTOM, Difficulty_InitFile == DIFFICULTY_CUSTOM);

    // Set Another Flags
    SetMenuItemState(ID_MENUITEM_COLOR, Color_InitFile);
    SetMenuItemState(ID_MENUITEM_MARKS, Mark_InitFile);
    SetMenuItemState(ID_MENUITEM_SOUND, Sound_InitFile);
}

void SetMenuItemState(DWORD uID, BOOL isChecked) {
    CheckMenuItem(hMenu, uID, isChecked ? MF_CHECKED : MF_BYCOMMAND);

}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hModule = hInstance;
    InitializeA();

    if (nCmdShow == SW_SHOWMINNOACTIVE || nCmdShow == SW_SHOWMINIMIZED) {
        Minimized = TRUE;
    }
    else {
        Minimized = FALSE;
    }

    INITCOMMONCONTROLSEX picce;
    picce.dwSize = sizeof(INITCOMMONCONTROLSEX);
    picce.dwICC =
        ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS |
        ICC_PROGRESS_CLASS | ICC_HOTKEY_CLASS | ICC_ANIMATE_CLASS | ICC_USEREX_CLASSES |
        ICC_COOL_CLASSES | ICC_PAGESCROLLER_CLASS;

    InitCommonControlsEx(&picce);
    hIcon = LoadIconW(hModule, (LPCWSTR)ID_ICON_GROUP);

    WNDCLASSW cls;
    
    cls.style = 0;
    cls.lpfnWndProc = WindowProc;
    cls.cbClsExtra = 0;
    cls.cbWndExtra = 0;
    cls.hInstance = hInstance;
    cls.hIcon = hIcon;
    cls.hCursor = LoadCursorW(NULL, (LPWSTR)IDC_ARROW);
    cls.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    cls.lpszMenuName = NULL;
    cls.lpszClassName = ClassName;

    if (!RegisterClassW(&cls)) {
        return 0;
    }

    hMenu = LoadMenuW(hModule, (LPCWSTR)ID_MENU);
    HACCEL hAccTable = LoadAcceleratorsW(hModule, (LPCWSTR)ID_ACCELERATORS);
    
    InitializeConfigFromRegistry();
    
    hWnd = CreateWindowExW(0,
        ClassName,
        ClassName,
        WS_MINIMIZEBOX | WS_SYSMENU | WS_DLGFRAME | WS_BORDER,
        Xpos_InitFile - WindowWidthInPixels,
        Ypos_InitFile - WindowHeightIncludingMenu,
        xRight + WindowWidthInPixels,
        yBottom + WindowHeightIncludingMenu,
        NULL,
        NULL,
        hModule,
        NULL
    );

    if (hWnd == NULL) {
        // (Error: %d, 1000)
        DisplayErrorMessage(1000);
        return 0;
    }

    // 1 does not do anything...
    InitializeWindowBorder(1);
    
    if (!InitializeBitmapsAndBlockArray()) {
        DisplayErrorMessage(ID_OUT_OF_MEM);
        return 0;
    }

    InitializeMenu(Menu_InitFile);
    InitializeNewGame();
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    Minimized = 0;
    MSG msg;
    
    while (GetMessageW(&msg, hWnd, 0, 0)) {
        if (TranslateAcceleratorW(hWnd, hAccTable, &msg) == 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    FreePenAndBlocksAndSound();
    
    if (NeedToSaveConfigToRegistry) {
        SaveConfigToRegistry();
    }

	return msg.wParam;
}


void SaveConfigToRegistry() {
    DWORD dwDisposition;
    RegCreateKeyExW(HKEY_CURRENT_USER, lpSubKey, 0, 0, 0, KEY_WRITE, 0, &hRegistryKey, &dwDisposition);

    SetIntegerInRegistry(Difficulty, Difficulty_InitFile);
    SetIntegerInRegistry(Height, Height_InitFile);
    SetIntegerInRegistry(Width, Width_InitFile);
    SetIntegerInRegistry(Mines, Mines_InitFile);
    SetIntegerInRegistry(Mark, Mark_InitFile);
    SetIntegerInRegistry(AlreadyPlayed, TRUE);
    SetIntegerInRegistry(Color, Color_InitFile);
    SetIntegerInRegistry(Sound, Sound_InitFile);
    SetIntegerInRegistry(Xpos, Xpos_InitFile);
    SetIntegerInRegistry(Ypos, Ypos_InitFile);
    SetIntegerInRegistry(Time1, Time_InitFile[0]);
    SetIntegerInRegistry(Time2, Time_InitFile[1]);
    SetIntegerInRegistry(Time3, Time_InitFile[2]);
    
    SetStringInRegistry(Name1, Name1_InitFile);
    SetStringInRegistry(Name2, Name2_InitFile);
    SetStringInRegistry(BestExpertName, Name3_InitFile);

    RegCloseKey(hRegistryKey);
}

void SetStringInRegistry(RegistryValue regValue, LPCWSTR lpStringValue) {
    RegSetValueExW(
        HKEY_CURRENT_USER, 
        RegistryValuesNames[(DWORD)regValue], 
        0, 
        REG_SZ, 
        (PBYTE)lpStringValue, 
        lstrlenW(lpStringValue)
    );
}

void SetIntegerInRegistry(RegistryValue regValue, DWORD value) {
    RegSetValueExW(
		HKEY_CURRENT_USER,
        RegistryValuesNames[(DWORD)regValue],
		0, 
		REG_DWORD, 
		(PBYTE)&value, 
		sizeof(DWORD)
	);
}

void FreePenAndBlocksAndSound() {
    FreePenAndBlocks();
    FreeSound();
}

void FreePenAndBlocks() {
    if (hPen != NULL) {
        DeleteObject(hPen);
    }

    for (int i = 0; i < 16; ++i) {
        DeleteDC(BlockStates[i]);
        DeleteObject(BlockBitmaps[i]);
    }
}

void FreeSound() {
    if (Sound_InitFile == 3) {
        // Stop all music
        PlaySoundW(0, 0, SND_PURGE);
    }
}

void InitializeNewGame() {
    DWORD flags = MOVE_WINDOW | REPAINT_WINDOW;

    if (Width_InitFile == Width_InitFile2 && Height_InitFile == Height_InitFile2) {
        flags = REPAINT_WINDOW;
    }

    Width_InitFile2 = Width_InitFile;
    Height_InitFile2 = Height_InitFile;

    InitializeBlockArrayBorders();
    
    GlobalSmileId = 0;
    
    // Setup all the mines
    for (Mines_Copy = 0; Mines_Copy < Mines_InitFile; ++Mines_Copy) {
		BoardPoint randomPoint;
        
        // Find a location for the mine
        do {
			randomPoint = (BoardPoint) {
				.Column = GetRandom(Width_InitFile2) + 1,
				.Row = GetRandom(Height_InitFile2) + 1
			};

        } while (ACCESS_BLOCK(randomPoint) & BLOCK_IS_BOMB);
		
		// SET A MINE
		ACCESS_BLOCK(randomPoint) |= BLOCK_IS_BOMB;
    }
    
    TimerSeconds = 0;
    LeftFlags = Mines_Copy;
    NumberOfRevealedBlocks = 0;
    NumberOfEmptyBlocks = (Height_InitFile2 * Width_InitFile2) - Mines_InitFile;
    StateFlags = STATE_GAME_IS_ON;
    AddAndDisplayLeftFlags(0); // Should have called DisplayLeftFlags()!
    InitializeWindowBorder(flags);
}

void AddAndDisplayLeftFlags(DWORD leftFlagsToAdd) {
    LeftFlags += leftFlagsToAdd;
    DisplayLeftFlags();
}

void DisplayLeftFlags() {
    HDC hDC = GetDC(hWnd);
    DisplayLeftFlagsOnDC(hDC);
    ReleaseDC(hWnd, hDC);
}

void DisplayLeftFlagsOnDC(HDC hDC) {
    DWORD layout = GetLayout(hDC);

    if (layout & 1) {
        // Set layout to Left To Right
        SetLayout(hDC, 0);
    }

    int lowDigit;
    int highNum;

    if (LeftFlags >= 0) {
        lowDigit = LeftFlags / 100;
        highNum = LeftFlags % 100;
    }
    else {
        lowDigit = NUM_MINUS;
        highNum = LeftFlags % 100;
    }
   
    DisplayNumber(hDC, POINT_BIG_NUM_X, lowDigit);
    DisplayNumber(hDC, POINT_MID_NUM_X, highNum / 10);
    DisplayNumber(hDC, POINT_LOW_NUM_X, highNum % 10);

    if (layout & 1) {
        SetLayout(hDC, layout);
    }
}

int GetRandom(DWORD maxValue) {
    return rand() % maxValue;
}


void InitializeA() {

    //
    // Initialize The Random Seed
    //
    srand(GetTickCount());

    //
    // Load Strings From Resource Table
    //
    LoadResourceString(ID_MINESWEEPER_CLASSNAME, ClassName,  sizeof(ClassName)/sizeof(WCHAR));
    LoadResourceString(ID_SECONDS_LEFT, SecondsLeftBuffer, sizeof(SecondsLeftBuffer) / sizeof(WCHAR));
    LoadResourceString(ID_ANONYMOUS, AnonymousStr, sizeof(AnonymousStr) / sizeof(WCHAR));

    ScreenHeightInPixels = GetSystemMetrics(SM_CYCAPTION) + 1;
    MenuBarHeightInPixels = GetSystemMetrics(SM_CYMENU) + 1;
    WindowHeightInPixels = GetSystemMetrics(SM_CYBORDER) + 1;
    WindowWidthInPixels = GetSystemMetrics(SM_CXBORDER) + 1;

    DWORD dwDisposition;
    
    if (RegCreateKeyExW(HKEY_CURRENT_USER, lpSubKey , 0, NULL,
        0, KEY_READ, NULL, &hRegistryKey, &dwDisposition) != ERROR_SUCCESS) {
        return;
    }
    
    //
    // Check If The Player Already Played
    //
    int alreadyPlayed = GetIntegerFromRegistry(AlreadyPlayed, 0, 0, 1);
        
    RegCloseKey(hRegistryKey);

    if (alreadyPlayed) {
        return;
    }

	// Load Items From .ini File
    Height_InitFile = GetIntegerFromInitFile(Height, 9, 9, 25);
    Width_InitFile = GetIntegerFromInitFile(Width, 9, 9, 30);
    Difficulty_InitFile = GetIntegerFromInitFile(Difficulty, 0, 0, 3);
    Mines_InitFile = GetIntegerFromInitFile(Mines, 10, 10, 999);
    Xpos_InitFile = GetIntegerFromInitFile(Xpos, 80, 0, 1024);
    Ypos_InitFile = GetIntegerFromInitFile(Ypos, 80, 0, 1024);
    Sound_InitFile = GetIntegerFromInitFile(Sound, 0, 0, 3);
    Mark_InitFile = GetIntegerFromInitFile(Mark, 1, 0, 1);
    Tick_InitFile = GetIntegerFromInitFile(Tick, 0, 0, 1);
    Menu_InitFile = GetIntegerFromInitFile(Menu, 0, 0, 2);
    Time_InitFile[TIME_BEGINNER] = GetIntegerFromInitFile(Time1, 999, 0, 999);
    Time_InitFile[TIME_INTERMIDIATE] = GetIntegerFromInitFile(Time2, 999, 0, 999);
    Time_InitFile[TIME_EXPERT] = GetIntegerFromInitFile(Time3, 999, 0, 999);
        
    GetStringFromInitFile(Name1, Name1_InitFile);
    GetStringFromInitFile(Name2, Name2_InitFile);
    GetStringFromInitFile(BestExpertName, Name3_InitFile);
        
    HDC hDC = GetDC(GetDesktopWindow());
    int desktopColors = GetDeviceCaps(hDC, NUMCOLORS);
    Color_InitFile = GetIntegerFromInitFile(Color, (desktopColors == 2) ? FALSE : TRUE, 0, 1);
    ReleaseDC(GetDesktopWindow(), hDC);

    if (Sound_InitFile == 3) {
        Sound_InitFile = StopAllSound();
    }
}

VOID GetStringFromRegistry(RegistryValue id, LPWSTR lpData) {
	BYTE cbData = 64;

	if (RegQueryValueExW(
		hRegistryKey,
		RegistryValuesNames[(int)id],
		NULL,
		NULL,
		&cbData,
		(LPDWORD)lpData
	)) {
		lstrcpyW(lpData, AnonymousStr);
	}
}

void InitializeConfigFromRegistry() {
    DWORD dwDisposition;

    RegCreateKeyExW(HKEY_CURRENT_USER, lpSubKey, 0, NULL, 0, KEY_READ, NULL, &hRegistryKey, &dwDisposition);

    Height_InitFile = GetIntegerFromRegistry(Height, 9, 9, 25);
    Height_InitFile2 = Height_InitFile;
    Width_InitFile = GetIntegerFromRegistry(Width, 9, 9, 25);
    Width_InitFile2 = Width_InitFile;
    Difficulty_InitFile = GetIntegerFromRegistry(Difficulty, 0, 0, 3);
    Mines_InitFile = GetIntegerFromRegistry(Mines, 10, 10, 999);
    Xpos_InitFile = GetIntegerFromRegistry(Xpos, 80, 0, 1024);
    Ypos_InitFile = GetIntegerFromRegistry(Ypos, 80, 0, 1024);
    Sound_InitFile = GetIntegerFromRegistry(Sound, 0, 0, 3);
    Mark_InitFile = GetIntegerFromRegistry(Mark, 1, 0, 1);
    Tick_InitFile = GetIntegerFromRegistry(Tick, 0, 0, 1);
    Menu_InitFile = GetIntegerFromRegistry(Menu, 0, 0, 2);
    Time_InitFile[TIME_BEGINNER] = GetIntegerFromRegistry(Time1, 999, 0, 999);
	Time_InitFile[TIME_INTERMIDIATE] = GetIntegerFromRegistry(Time2, 999, 0, 999);
	Time_InitFile[TIME_EXPERT] = GetIntegerFromRegistry(Time3, 999, 0, 999);

    GetStringFromRegistry(Name1, Name1_InitFile);
    GetStringFromRegistry(Name2, Name2_InitFile);
    GetStringFromRegistry(BestExpertName, Name3_InitFile);
    
    HDC hDC = GetDC(GetDesktopWindow());

    int desktopColors = GetDeviceCaps(hDC, NUMCOLORS);
    Color_InitFile = GetIntegerFromInitFile(Color, (desktopColors == 2) ? 0 : 1, 0, 1);

    ReleaseDC(GetDesktopWindow(), hDC);

    if (Sound_InitFile == 3) {
        Sound_InitFile = StopAllSound();
    }

    RegCloseKey(hRegistryKey);

}


// In the assembly code there are many calls to GetPrivateProfileIntW(). 
// It is because it is called inside a macro argument.
// Moreover, the "min" logic is calculated twice, again because it's called inside a macro argument.
int GetIntegerFromInitFile(RegistryValue regValue, int nDefault, DWORD minValue, DWORD maxValue) {

	LPCWSTR lpKeyName = RegistryValuesNames[(DWORD)regValue];
	
	return max(min(maxValue, GetPrivateProfileIntW(ClassName, lpKeyName, nDefault, lpInitFileName)), minValue);
}

int GetStringFromInitFile(RegistryValue regValue, LPWSTR lpReturnedString) {
	return GetPrivateProfileStringW(ClassName, RegistryValuesNames[(DWORD)regValue],
		AnonymousStr, lpReturnedString, 32, lpInitFileName);
}

// 3 if SND_PURGE succeeded else 2
DWORD StopAllSound() {
    if (PlaySoundW(NULL, NULL, SND_PURGE)) {
        3;
    }

    return 2;
}

void InitializeWindowBorder(DWORD flags) {
    BOOL differentCordsForMenus = FALSE;
    RECT rcGameMenu;
    RECT rcHelpMenu;

    if (hWnd == NULL) {
        return;
    }
    
    WindowHeightIncludingMenu = ScreenHeightInPixels;

    if (Menu_InitFile & 1) {
        WindowHeightIncludingMenu += MenuBarHeightInPixels;
        

        if (hMenu != NULL && 
            GetMenuItemRect(hWnd, hMenu, GAME_MENU_INDEX, &rcGameMenu) &&
            GetMenuItemRect(hWnd, hMenu, HELP_MENU_INDEX, &rcHelpMenu) &&
            rcGameMenu.top != rcHelpMenu.top) {
            differentCordsForMenus = TRUE;
            // Add it twice
            WindowHeightIncludingMenu += MenuBarHeightInPixels;
        }
    }

    xRight = (Width_InitFile2 * 16) + 24;   // 24 is the size of pixels in the side of the window, 16 is the size of block
    yBottom = (Height_InitFile2 * 16) + 67; // 16 is the size of the pixels in a block, 67 is the size of pixels in the sides

    // Check If The Place On The Screen Overflows The End Of The Screen
    // If it is, Move The Window To A New Place
    int diffFromEnd;
    
    // If diffFromEnd is negative, it means the window does not overflow the screen
    // If diffFromEnd is positive, it means the window overflows the screen and needs to be moved
    diffFromEnd = (xRight + Xpos_InitFile) - SimpleGetSystemMetrics(GET_SCREEN_WIDTH);

    if (diffFromEnd > 0) {
        flags |= MOVE_WINDOW;
        Xpos_InitFile -= diffFromEnd;
    }

    diffFromEnd = (yBottom + Ypos_InitFile) - SimpleGetSystemMetrics(GET_SCREEN_HEIGHT);

    if (diffFromEnd > 0) {
        flags |= MOVE_WINDOW;
        Ypos_InitFile -= diffFromEnd;
    }

    if (Minimized) {
        return;
    }

    if (flags & MOVE_WINDOW) {
        MoveWindow(hWnd,
            Xpos_InitFile, Ypos_InitFile,
            WindowWidthInPixels + xRight,
            yBottom + WindowHeightIncludingMenu,
            TRUE);
    }

    if (differentCordsForMenus &&
        hMenu != NULL && 
        GetMenuItemRect(hWnd, hMenu, 0, &rcGameMenu) &&
        GetMenuItemRect(hWnd, hMenu, 1, &rcHelpMenu) &&
        rcGameMenu.top == rcHelpMenu.top) {
        WindowHeightIncludingMenu -= MenuBarHeightInPixels;
    
        MoveWindow(hWnd, Xpos_InitFile, Ypos_InitFile, WindowWidthInPixels + xRight, yBottom + WindowHeightIncludingMenu, TRUE);
    }

    if (flags & REPAINT_WINDOW) {
        RECT rc;
        SetRect(&rc, 0, 0, xRight, yBottom);
        // Cause a Repaint of the whole window
        InvalidateRect(hWnd, &rc, TRUE);
    }

        
}

BOOL InitializeBitmapsAndBlockArray() {
    if (LoadBitmaps()) {
        InitializeBlockArrayBorders();
        return TRUE;
    }

    return FALSE;
}

void InitializeBlockArrayBorders() {
	
    memset(BlockArray, BLOCK_STATE_EMPTY_UNCLICKED, sizeof(BlockArray));

    for (int column = Width_InitFile2 + 1; column >= 0; column--) {
		// Fill upper border
		BlockArray[0][column] = BLOCK_STATE_BORDER_VALUE;

		// Fill lower border
		BlockArray[Height_InitFile2 + 1][column] = BLOCK_STATE_BORDER_VALUE;
    }

	for (int row = Height_InitFile2 + 1; row >= 0; row++) {
		// Fill left border
		BlockArray[row][0] = BLOCK_STATE_BORDER_VALUE;

		// Fill right border
		BlockArray[row][Width_InitFile2 + 1] = BLOCK_STATE_BORDER_VALUE;
	}
}

BOOL LoadBitmaps() {
	if (!LoadBitmapResources()) {
		return FALSE;
	}
	
	InitializePen();

	InitializeBitmapsIndexes();

	ProcessBlockBitmaps();

    return TRUE;
}

__inline void InitializePen() {
	if (Color_InitFile) {
		hPen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
	}
	else {
		hPen = (HPEN)GetStockObject(BLACK_PEN);
	}
}

__inline void ProcessBlockBitmaps() {
	HDC hWndDC = GetDC(hWnd);

	for (int i = 0; i < _countof(BlockStates); ++i) {
		BlockStates[i] = CreateCompatibleDC(hWndDC);

		if (BlockStates[i] == NULL) {
			// Maybe that means the original name of the function was "FLoad"
			// This is a bad name dude. 
			OutputDebugStringA("FLoad failed to create compatible dc\n");
		}

		BlockBitmaps[i] = CreateCompatibleBitmap(hWndDC, 16, 16);

		if (BlockBitmaps[i] == NULL) {
			OutputDebugStringA("Failed to create Bitmap\n");
		}

		// Mmm. BlockState[i] might be an invalid value, Also BlockBitmaps[i] 
		// Why would he perform this operation anyway.. :(
		SelectObject(BlockStates[i], BlockBitmaps[i]);
		SetDIBitsToDevice(BlockStates[i], 0, 0, BLOCK_WIDTH, BLOCK_HEIGHT, 
			0, 0, 0, 16, BlockBitmapIndex[i] + lpBlocksBitmapInfo, lpBlocksBitmapInfo, 0);
	}

	ReleaseDC(hWnd, hWndDC);
}

__inline BOOL LoadBitmapResources() {
	hBlocksBitmapResource = TryLoadBitmapResource(ID_BITMAP_BLOCK);
	hNumbersBitmapResource = TryLoadBitmapResource(ID_BITMAP_NUMBERS);
	hSmilesBitmapResource = TryLoadBitmapResource(ID_BITMAP_SMILES);

	// Yea I know, it's wierd that the check is performed after all the resources are loaded..
	if (hSmilesBitmapResource == NULL || hBlocksBitmapResource == NULL || hNumbersBitmapResource == NULL) {
		return FALSE;
	}

	lpBlocksBitmapInfo = (PBITMAPINFO)LockResource(hBlocksBitmapResource);
	lpNumbersBitmapInfo = (PBITMAPINFO)LockResource(hNumbersBitmapResource);
	lpSmilesBitmapInfo = (PBITMAPINFO)LockResource(hSmilesBitmapResource);

	return TRUE;
}

__inline void InitializeBitmapsIndexes() {
#define COLORED_BMP_START 104
#define NONCOLORED_BMP_START 48

	const DWORD bitmapStartIndex = (Color_InitFile) ? COLORED_BMP_START : NONCOLORED_BMP_START;

	InitializeBitmapIndexes(
		BlockBitmapIndex,           // indexesArray
		_countof(BlockBitmapIndex), // numberOfBitmaps
		bitmapStartIndex,           // firstBitmapIndex
		GetBitmapByteLength(BLOCK_WIDTH, BLOCK_HEIGHT) // bytesPerBitmap
	);

	InitializeBitmapIndexes(
		NumberBitmapIndex,         // indexesArray
		_countof(NumberBitmapIndex), // numberOfBitmaps
		bitmapStartIndex, // firstBitmapIndex
		GetBitmapByteLength(NUMBER_WIDTH, NUMBER_HEIGHT) // bytesPerBitmap
	);

	InitializeBitmapIndexes(
		SmileBitmapIndex, // indexesArray
		_countof(SmileBitmapIndex), // numberOfBitmaps
		bitmapStartIndex, // firstBitmapIndex
		GetBitmapByteLength(SMILE_BITMAP_WIDTH, SMILE_BITMAP_HEIGHT) // bytesPerBitmap
	);
}

__inline void InitializeBitmapIndexes(PDWORD indexesArray, int numberOfBitmaps, DWORD firstBitmapIndex, DWORD bytesPerBitmap) {
    for (int i = 0; i < numberOfBitmaps ; ++i) {
        indexesArray[i] = firstBitmapIndex;
        firstBitmapIndex += bytesPerBitmap;
    }
}

__inline HGLOBAL TryLoadBitmapResource(USHORT resourceId) {
    HRSRC hRsrc = FindBitmapResource(resourceId);

    if (hRsrc != NULL) {
        return LoadResource(hModule, hRsrc);
    }

    return NULL;
}

void DisplayNumber(HDC hDC, int xPosition, int numberToDisplay) {
    SetDIBitsToDevice(hDC,
        xPosition, NUMBER_Y, NUMBER_WIDTH, NUMBER_HEIGHT, 0, 0, 0, 23,
        NumberBitmapIndex[numberToDisplay] + lpNumbersBitmapInfo, lpNumbersBitmapInfo, 0);
    
}

int GetBitmapByteLength(int a, int b) {
    int colorDepth = (Color_InitFile) ? 4 : 1;
    colorDepth = (colorDepth * a + 31) / 8;
    // Clear those bits: 0b11
    colorDepth &= ~0b11;
    return colorDepth * b;
}

HRSRC FindBitmapResource(USHORT resourceId) {
    // If there is no color, 
    // Get the next resource which is color-less
    if (!Color_InitFile) {
		resourceId++;
    }

    return FindResourceW(hModule, (LPCWSTR)(resourceId), (LPWSTR)RT_BITMAP);
}