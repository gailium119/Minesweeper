/**********/
/* util.h */
/**********/


VOID InitConst(VOID);
VOID LoadSz(WORD, TCHAR *, DWORD);
VOID ReportErr(WORD);
INT  Rnd(INT);

INT  GetDlgInt(HWND, INT, INT, INT);

VOID DoHelp(WORD, UINT);
VOID DoAbout(VOID);

VOID CheckEm(WORD, BOOL);
VOID SetMenuBar(INT);
