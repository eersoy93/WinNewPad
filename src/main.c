/*
 * WinNewPad - An old WordPad-like word processor written with C and Windows API.
 * main.c - Application entry point, main window and editor logic.
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#endif

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

#define APP_CLASSNAME       L"WinNewPadMainWindow"
#define MAX_TEXT_LIMIT      0x7FFFFFFE
#define TWIPS_PER_INCH      1440
#define GLYPH_SIZE          16

/* Document formats */
typedef enum DocFormat {
    FORMAT_RTF = 1,     /* Rich Text Format */
    FORMAT_UTF8 = 2,    /* Plain text, UTF-8 */
    FORMAT_UNICODE = 3  /* Plain text, UTF-16 LE with BOM */
} DocFormat;

/* Format bar glyph indices */
enum {
    GLYPH_BOLD, GLYPH_ITALIC, GLYPH_UNDERLINE, GLYPH_COLOR,
    GLYPH_LEFT, GLYPH_CENTER, GLYPH_RIGHT, GLYPH_BULLET,
    GLYPH_COUNT
};

/* Application state */
static HINSTANCE  g_hInst;
static HWND       g_hwndMain;
static HWND       g_hwndEdit;
static HWND       g_hwndToolbar;
static HWND       g_hwndFormatBar;
static HWND       g_hwndStatus;
static HWND       g_hwndFontCombo;
static HWND       g_hwndSizeCombo;
static HWND       g_hwndFindDlg;
static HFONT      g_hUiFont;
static HIMAGELIST g_hFormatImages;
static HMODULE    g_hRichEditLib;
static UINT       g_uFindMsg;

static WCHAR      g_szFileName[MAX_PATH];
static DocFormat  g_docFormat = FORMAT_RTF;
static BOOL       g_bShowToolbar = TRUE;
static BOOL       g_bShowFormatBar = TRUE;
static BOOL       g_bShowStatus = TRUE;
static BOOL       g_bWordWrap = TRUE;
static BOOL       g_bUpdatingFormatBar = FALSE;

static FINDREPLACEW g_findReplace;
static WCHAR      g_szFindText[256];
static WCHAR      g_szReplaceText[256];
static DWORD      g_findFlags = FR_DOWN;

static HGLOBAL    g_hDevMode;
static HGLOBAL    g_hDevNames;
static RECT       g_rcMargins = { 1250, 1000, 1250, 1000 }; /* thousandths of an inch */
static COLORREF   g_customColors[16];

static const WCHAR *const g_fontSizes[] = {
    L"8", L"9", L"10", L"11", L"12", L"14", L"16", L"18", L"20",
    L"22", L"24", L"26", L"28", L"36", L"48", L"72"
};

/* Forward declarations */
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static void UpdateFormatBar(void);
static void UpdateStatusBar(void);
static void UpdateToolbarState(void);

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static int LoadStr(UINT id, WCHAR *buf, int cch)
{
    int len = LoadStringW(g_hInst, id, buf, cch);
    if (len <= 0 && cch > 0) {
        buf[0] = L'\0';
        len = 0;
    }
    return len;
}

/* Strings in the string table are "long help\ntooltip". */
static void LoadCommandString(UINT id, BOOL wantTooltip, WCHAR *buf, int cch)
{
    WCHAR tmp[256];
    WCHAR *nl;

    buf[0] = L'\0';
    if (!LoadStr(id, tmp, ARRAYSIZE(tmp)))
        return;
    nl = wcschr(tmp, L'\n');
    if (wantTooltip) {
        lstrcpynW(buf, nl ? nl + 1 : tmp, cch);
    } else {
        if (nl)
            *nl = L'\0';
        lstrcpynW(buf, tmp, cch);
    }
}

static int MsgBox(UINT type, const WCHAR *fmt, ...)
{
    WCHAR text[1024];
    WCHAR title[64];
    va_list args;

    va_start(args, fmt);
    _vsnwprintf(text, ARRAYSIZE(text) - 1, fmt, args);
    va_end(args);
    text[ARRAYSIZE(text) - 1] = L'\0';
    LoadStr(IDS_APPNAME, title, ARRAYSIZE(title));
    return MessageBoxW(g_hwndMain, text, title, type);
}

static const WCHAR *FileTitle(const WCHAR *path)
{
    const WCHAR *p = wcsrchr(path, L'\\');
    const WCHAR *q = wcsrchr(path, L'/');
    if (q > p)
        p = q;
    return p ? p + 1 : path;
}

static BOOL HasExtension(const WCHAR *path, const WCHAR *ext)
{
    const WCHAR *dot = wcsrchr(FileTitle(path), L'.');
    return dot && _wcsicmp(dot, ext) == 0;
}

static void UpdateTitle(void)
{
    WCHAR app[64], untitled[64], title[MAX_PATH + 80];

    LoadStr(IDS_APPNAME, app, ARRAYSIZE(app));
    LoadStr(IDS_UNTITLED, untitled, ARRAYSIZE(untitled));
    _snwprintf(title, ARRAYSIZE(title) - 1, L"%s - %s",
               g_szFileName[0] ? FileTitle(g_szFileName) : untitled, app);
    title[ARRAYSIZE(title) - 1] = L'\0';
    SetWindowTextW(g_hwndMain, title);
}

static void GetSelRange(CHARRANGE *cr)
{
    SendMessageW(g_hwndEdit, EM_EXGETSEL, 0, (LPARAM)cr);
}

static BOOL HasSelection(void)
{
    CHARRANGE cr;
    GetSelRange(&cr);
    return cr.cpMin != cr.cpMax;
}

/* ------------------------------------------------------------------------- */
/* Character and paragraph formatting                                        */
/* ------------------------------------------------------------------------- */

static void GetSelCharFormat(CHARFORMAT2W *cf)
{
    ZeroMemory(cf, sizeof(*cf));
    cf->cbSize = sizeof(*cf);
    SendMessageW(g_hwndEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)cf);
}

static void SetSelCharFormat(CHARFORMAT2W *cf)
{
    cf->cbSize = sizeof(*cf);
    SendMessageW(g_hwndEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)cf);
}

static void GetSelParaFormat(PARAFORMAT2 *pf)
{
    ZeroMemory(pf, sizeof(*pf));
    pf->cbSize = sizeof(*pf);
    SendMessageW(g_hwndEdit, EM_GETPARAFORMAT, 0, (LPARAM)pf);
}

static void SetSelParaFormat(PARAFORMAT2 *pf)
{
    pf->cbSize = sizeof(*pf);
    SendMessageW(g_hwndEdit, EM_SETPARAFORMAT, 0, (LPARAM)pf);
}

static void ToggleEffect(DWORD mask, DWORD effect)
{
    CHARFORMAT2W cf;
    BOOL on;

    GetSelCharFormat(&cf);
    on = (cf.dwMask & mask) && (cf.dwEffects & effect);
    ZeroMemory(&cf, sizeof(cf));
    cf.dwMask = mask;
    cf.dwEffects = on ? 0 : effect;
    SetSelCharFormat(&cf);
    UpdateFormatBar();
}

static void SetFontFace(const WCHAR *face)
{
    CHARFORMAT2W cf;

    if (!face[0])
        return;
    ZeroMemory(&cf, sizeof(cf));
    cf.dwMask = CFM_FACE | CFM_CHARSET;
    cf.bCharSet = DEFAULT_CHARSET;
    lstrcpynW(cf.szFaceName, face, LF_FACESIZE);
    SetSelCharFormat(&cf);
}

static BOOL SetFontSize(const WCHAR *text)
{
    CHARFORMAT2W cf;
    double pts = wcstod(text, NULL);

    if (pts < 1.0 || pts > 1638.0)
        return FALSE;
    ZeroMemory(&cf, sizeof(cf));
    cf.dwMask = CFM_SIZE;
    cf.yHeight = (LONG)(pts * 20.0 + 0.5);
    SetSelCharFormat(&cf);
    return TRUE;
}

static void SetAlignment(WORD align)
{
    PARAFORMAT2 pf;

    ZeroMemory(&pf, sizeof(pf));
    pf.dwMask = PFM_ALIGNMENT;
    pf.wAlignment = align;
    SetSelParaFormat(&pf);
    UpdateFormatBar();
}

static void ToggleBullets(void)
{
    PARAFORMAT2 pf;
    BOOL on;

    GetSelParaFormat(&pf);
    on = (pf.dwMask & PFM_NUMBERING) && pf.wNumbering == PFN_BULLET;
    ZeroMemory(&pf, sizeof(pf));
    pf.dwMask = PFM_NUMBERING | PFM_OFFSET;
    pf.wNumbering = on ? 0 : PFN_BULLET;
    pf.dxOffset = on ? 0 : TWIPS_PER_INCH / 4;
    SetSelParaFormat(&pf);
    UpdateFormatBar();
}

static void ChooseTextFont(void)
{
    CHARFORMAT2W cf;
    LOGFONTW lf;
    CHOOSEFONTW cfd;
    HDC hdc;
    int dpiY;

    GetSelCharFormat(&cf);
    hdc = GetDC(g_hwndMain);
    dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(g_hwndMain, hdc);

    ZeroMemory(&lf, sizeof(lf));
    if (cf.dwMask & CFM_SIZE)
        lf.lfHeight = -MulDiv(cf.yHeight, dpiY, TWIPS_PER_INCH);
    lf.lfWeight = ((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD)) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)((cf.dwMask & CFM_ITALIC) && (cf.dwEffects & CFE_ITALIC));
    lf.lfUnderline = (BYTE)((cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE));
    lf.lfStrikeOut = (BYTE)((cf.dwMask & CFM_STRIKEOUT) && (cf.dwEffects & CFE_STRIKEOUT));
    lf.lfCharSet = (cf.dwMask & CFM_CHARSET) ? cf.bCharSet : DEFAULT_CHARSET;
    lf.lfPitchAndFamily = cf.bPitchAndFamily;
    if (cf.dwMask & CFM_FACE)
        lstrcpynW(lf.lfFaceName, cf.szFaceName, LF_FACESIZE);

    ZeroMemory(&cfd, sizeof(cfd));
    cfd.lStructSize = sizeof(cfd);
    cfd.hwndOwner = g_hwndMain;
    cfd.lpLogFont = &lf;
    cfd.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
    cfd.rgbColors = ((cf.dwMask & CFM_COLOR) && !(cf.dwEffects & CFE_AUTOCOLOR))
                    ? cf.crTextColor : GetSysColor(COLOR_WINDOWTEXT);
    if (!ChooseFontW(&cfd))
        return;

    ZeroMemory(&cf, sizeof(cf));
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE |
                CFM_STRIKEOUT | CFM_COLOR | CFM_CHARSET;
    lstrcpynW(cf.szFaceName, lf.lfFaceName, LF_FACESIZE);
    cf.yHeight = cfd.iPointSize * 2; /* tenths of a point -> twips */
    cf.bCharSet = lf.lfCharSet;
    cf.bPitchAndFamily = lf.lfPitchAndFamily;
    cf.crTextColor = cfd.rgbColors;
    if (lf.lfWeight >= FW_BOLD)
        cf.dwEffects |= CFE_BOLD;
    if (lf.lfItalic)
        cf.dwEffects |= CFE_ITALIC;
    if (lf.lfUnderline)
        cf.dwEffects |= CFE_UNDERLINE;
    if (lf.lfStrikeOut)
        cf.dwEffects |= CFE_STRIKEOUT;
    SetSelCharFormat(&cf);
    UpdateFormatBar();
}

static void ChooseTextColor(void)
{
    CHARFORMAT2W cf;
    CHOOSECOLORW cc;

    GetSelCharFormat(&cf);
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g_hwndMain;
    cc.lpCustColors = g_customColors;
    cc.rgbResult = ((cf.dwMask & CFM_COLOR) && !(cf.dwEffects & CFE_AUTOCOLOR))
                   ? cf.crTextColor : GetSysColor(COLOR_WINDOWTEXT);
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc))
        return;

    ZeroMemory(&cf, sizeof(cf));
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = cc.rgbResult;
    SetSelCharFormat(&cf);
}

static void SetDefaultFormat(void)
{
    CHARFORMAT2W cf;

    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR | CFM_BOLD |
                CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    cf.dwEffects = CFE_AUTOCOLOR;
    cf.yHeight = 10 * 20; /* 10 pt */
    cf.bCharSet = DEFAULT_CHARSET;
    lstrcpyW(cf.szFaceName, L"Arial");
    SendMessageW(g_hwndEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    SendMessageW(g_hwndEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static void ApplyWordWrap(void)
{
    /* 0 = wrap to window, 1 = no wrapping */
    SendMessageW(g_hwndEdit, EM_SETTARGETDEVICE, 0, g_bWordWrap ? 0 : 1);
}

/* ------------------------------------------------------------------------- */
/* File input / output                                                       */
/* ------------------------------------------------------------------------- */

typedef struct StreamBuffer {
    const BYTE *data;
    size_t      size;
    size_t      pos;
} StreamBuffer;

static DWORD CALLBACK StreamInCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    StreamBuffer *sb = (StreamBuffer *)cookie;
    size_t remain = sb->size - sb->pos;
    size_t n = (size_t)cb < remain ? (size_t)cb : remain;

    CopyMemory(buf, sb->data + sb->pos, n);
    sb->pos += n;
    *pcb = (LONG)n;
    return 0;
}

static DWORD CALLBACK StreamOutCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    HANDLE hFile = (HANDLE)cookie;
    DWORD written = 0;

    if (!WriteFile(hFile, buf, (DWORD)cb, &written, NULL))
        return 1;
    *pcb = (LONG)written;
    return 0;
}

static BOOL IsValidUtf8(const BYTE *data, size_t size)
{
    if (size == 0)
        return TRUE;
    if (size > 0x7FFFFFFF)
        return FALSE;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                               (LPCSTR)data, (int)size, NULL, 0) > 0;
}

static BOOL LoadDocument(const WCHAR *path)
{
    HANDLE hFile;
    LARGE_INTEGER fileSize;
    BYTE *data;
    DWORD read = 0;
    StreamBuffer sb;
    EDITSTREAM es;
    WPARAM sf;
    size_t size;

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MsgBox(MB_ICONERROR, L"Cannot open \"%s\".", path);
        return FALSE;
    }
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart > 0x7FFFFFF0) {
        CloseHandle(hFile);
        MsgBox(MB_ICONERROR, L"\"%s\" is too large to open.", path);
        return FALSE;
    }
    size = (size_t)fileSize.QuadPart;
    data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!data) {
        CloseHandle(hFile);
        MsgBox(MB_ICONERROR, L"Not enough memory to open \"%s\".", path);
        return FALSE;
    }
    if (size && (!ReadFile(hFile, data, (DWORD)size, &read, NULL) || read != size)) {
        HeapFree(GetProcessHeap(), 0, data);
        CloseHandle(hFile);
        MsgBox(MB_ICONERROR, L"Cannot read \"%s\".", path);
        return FALSE;
    }
    CloseHandle(hFile);

    sb.data = data;
    sb.size = size;
    sb.pos = 0;

    if (size >= 5 && memcmp(data, "{\\rtf", 5) == 0) {
        sf = SF_RTF;
        g_docFormat = FORMAT_RTF;
    } else if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        sf = SF_TEXT | SF_UNICODE;
        sb.pos = 2;
        g_docFormat = FORMAT_UNICODE;
    } else if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        sf = SF_TEXT | SF_USECODEPAGE | ((WPARAM)CP_UTF8 << 16);
        sb.pos = 3;
        g_docFormat = FORMAT_UTF8;
    } else if (IsValidUtf8(data, size)) {
        sf = SF_TEXT | SF_USECODEPAGE | ((WPARAM)CP_UTF8 << 16);
        g_docFormat = FORMAT_UTF8;
    } else {
        sf = SF_TEXT;
        g_docFormat = FORMAT_UTF8; /* Saved back as UTF-8 */
    }

    SendMessageW(g_hwndEdit, WM_SETREDRAW, FALSE, 0);
    SetDefaultFormat();
    ZeroMemory(&es, sizeof(es));
    es.dwCookie = (DWORD_PTR)&sb;
    es.pfnCallback = StreamInCallback;
    SendMessageW(g_hwndEdit, EM_STREAMIN, sf, (LPARAM)&es);
    SendMessageW(g_hwndEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hwndEdit, NULL, TRUE);
    HeapFree(GetProcessHeap(), 0, data);

    if (es.dwError) {
        MsgBox(MB_ICONERROR, L"An error occurred while loading \"%s\".", path);
    }

    SendMessageW(g_hwndEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g_hwndEdit, EM_SETMODIFY, FALSE, 0);
    SendMessageW(g_hwndEdit, EM_SETSEL, 0, 0);
    lstrcpynW(g_szFileName, path, MAX_PATH);
    UpdateTitle();
    UpdateFormatBar();
    UpdateStatusBar();
    UpdateToolbarState();
    return TRUE;
}

static BOOL WriteDocument(const WCHAR *path, DocFormat format)
{
    HANDLE hFile;
    EDITSTREAM es;
    WPARAM sf;
    DWORD written;

    hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MsgBox(MB_ICONERROR, L"Cannot save \"%s\".", path);
        return FALSE;
    }

    switch (format) {
    case FORMAT_UNICODE: {
        static const BYTE bom[2] = { 0xFF, 0xFE };
        WriteFile(hFile, bom, sizeof(bom), &written, NULL);
        sf = SF_TEXT | SF_UNICODE;
        break;
    }
    case FORMAT_UTF8:
        sf = SF_TEXT | SF_USECODEPAGE | ((WPARAM)CP_UTF8 << 16);
        break;
    case FORMAT_RTF:
    default:
        sf = SF_RTF;
        break;
    }

    ZeroMemory(&es, sizeof(es));
    es.dwCookie = (DWORD_PTR)hFile;
    es.pfnCallback = StreamOutCallback;
    SendMessageW(g_hwndEdit, EM_STREAMOUT, sf, (LPARAM)&es);
    CloseHandle(hFile);

    if (es.dwError) {
        MsgBox(MB_ICONERROR, L"An error occurred while saving \"%s\".", path);
        return FALSE;
    }

    lstrcpynW(g_szFileName, path, MAX_PATH);
    g_docFormat = format;
    SendMessageW(g_hwndEdit, EM_SETMODIFY, FALSE, 0);
    UpdateTitle();
    return TRUE;
}

static const WCHAR g_szFileFilter[] =
    L"Rich Text Format (*.rtf)\0*.rtf\0"
    L"Text Document - UTF-8 (*.txt)\0*.txt\0"
    L"Unicode Text Document (*.txt)\0*.txt\0"
    L"All Documents (*.*)\0*.*\0";

static BOOL SaveDocumentAs(void)
{
    OPENFILENAMEW ofn;
    WCHAR path[MAX_PATH];
    DocFormat format;

    lstrcpynW(path, g_szFileName, MAX_PATH);
    if (!path[0])
        LoadStr(IDS_UNTITLED, path, MAX_PATH);

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFilter = g_szFileFilter;
    ofn.nFilterIndex = (DWORD)g_docFormat;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = (g_docFormat == FORMAT_RTF) ? L"rtf" : L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn))
        return FALSE;

    switch (ofn.nFilterIndex) {
    case 2:  format = FORMAT_UTF8; break;
    case 3:  format = FORMAT_UNICODE; break;
    case 1:  format = FORMAT_RTF; break;
    default: format = HasExtension(path, L".rtf") ? FORMAT_RTF : FORMAT_UTF8; break;
    }

    if (format != FORMAT_RTF && g_docFormat == FORMAT_RTF) {
        if (MsgBox(MB_ICONWARNING | MB_OKCANCEL,
                   L"You are about to save the document in a Text-Only format, "
                   L"which will remove all formatting.\n\nDo you want to continue?") != IDOK)
            return FALSE;
    }
    return WriteDocument(path, format);
}

static BOOL SaveDocument(void)
{
    if (!g_szFileName[0])
        return SaveDocumentAs();
    return WriteDocument(g_szFileName, g_docFormat);
}

/* Returns TRUE if it is OK to discard the current document. */
static BOOL QuerySaveChanges(void)
{
    WCHAR untitled[64];
    int ret;

    if (!SendMessageW(g_hwndEdit, EM_GETMODIFY, 0, 0))
        return TRUE;
    LoadStr(IDS_UNTITLED, untitled, ARRAYSIZE(untitled));
    ret = MsgBox(MB_ICONEXCLAMATION | MB_YESNOCANCEL, L"Save changes to %s?",
                 g_szFileName[0] ? FileTitle(g_szFileName) : untitled);
    if (ret == IDCANCEL)
        return FALSE;
    if (ret == IDYES)
        return SaveDocument();
    return TRUE;
}

static void NewDocument(void)
{
    if (!QuerySaveChanges())
        return;
    SetWindowTextW(g_hwndEdit, L"");
    SetDefaultFormat();
    {
        PARAFORMAT2 pf;
        ZeroMemory(&pf, sizeof(pf));
        pf.dwMask = PFM_ALIGNMENT | PFM_NUMBERING | PFM_STARTINDENT |
                    PFM_RIGHTINDENT | PFM_OFFSET;
        pf.wAlignment = PFA_LEFT;
        SetSelParaFormat(&pf);
    }
    SendMessageW(g_hwndEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g_hwndEdit, EM_SETMODIFY, FALSE, 0);
    g_szFileName[0] = L'\0';
    g_docFormat = FORMAT_RTF;
    UpdateTitle();
    UpdateFormatBar();
    UpdateStatusBar();
    UpdateToolbarState();
}

static void OpenDocument(void)
{
    OPENFILENAMEW ofn;
    WCHAR path[MAX_PATH] = L"";

    if (!QuerySaveChanges())
        return;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFilter = g_szFileFilter;
    ofn.nFilterIndex = 4;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        LoadDocument(path);
}

/* ------------------------------------------------------------------------- */
/* Printing                                                                  */
/* ------------------------------------------------------------------------- */

static void PageSetup(void)
{
    PAGESETUPDLGW psd;

    ZeroMemory(&psd, sizeof(psd));
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = g_hwndMain;
    psd.hDevMode = g_hDevMode;
    psd.hDevNames = g_hDevNames;
    psd.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES;
    psd.rtMargin = g_rcMargins;
    if (PageSetupDlgW(&psd)) {
        g_hDevMode = psd.hDevMode;
        g_hDevNames = psd.hDevNames;
        g_rcMargins = psd.rtMargin;
    }
}

static void PrintDocument(void)
{
    PRINTDLGW pd;
    DOCINFOW di;
    FORMATRANGE fr;
    WCHAR docName[MAX_PATH];
    LONG textLength, lastChar;
    int physW, physH, offX, offY, dpiX, dpiY;
    BOOL ok = TRUE;

    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = g_hwndMain;
    pd.hDevMode = g_hDevMode;
    pd.hDevNames = g_hDevNames;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOPAGENUMS;
    if (!HasSelection())
        pd.Flags |= PD_NOSELECTION;
    pd.nCopies = 1;
    if (!PrintDlgW(&pd))
        return;
    g_hDevMode = pd.hDevMode;
    g_hDevNames = pd.hDevNames;
    if (!pd.hDC)
        return;

    dpiX = GetDeviceCaps(pd.hDC, LOGPIXELSX);
    dpiY = GetDeviceCaps(pd.hDC, LOGPIXELSY);
    physW = GetDeviceCaps(pd.hDC, PHYSICALWIDTH);
    physH = GetDeviceCaps(pd.hDC, PHYSICALHEIGHT);
    offX = GetDeviceCaps(pd.hDC, PHYSICALOFFSETX);
    offY = GetDeviceCaps(pd.hDC, PHYSICALOFFSETY);

    ZeroMemory(&fr, sizeof(fr));
    fr.hdc = pd.hDC;
    fr.hdcTarget = pd.hDC;
    /* Margins: thousandths of an inch -> twips, relative to the printable area */
    fr.rc.left   = MulDiv(g_rcMargins.left, TWIPS_PER_INCH, 1000) - MulDiv(offX, TWIPS_PER_INCH, dpiX);
    fr.rc.top    = MulDiv(g_rcMargins.top, TWIPS_PER_INCH, 1000) - MulDiv(offY, TWIPS_PER_INCH, dpiY);
    fr.rc.right  = MulDiv(physW, TWIPS_PER_INCH, dpiX) - MulDiv(g_rcMargins.right, TWIPS_PER_INCH, 1000)
                   - MulDiv(offX, TWIPS_PER_INCH, dpiX);
    fr.rc.bottom = MulDiv(physH, TWIPS_PER_INCH, dpiY) - MulDiv(g_rcMargins.bottom, TWIPS_PER_INCH, 1000)
                   - MulDiv(offY, TWIPS_PER_INCH, dpiY);
    if (fr.rc.left < 0) fr.rc.left = 0;
    if (fr.rc.top < 0) fr.rc.top = 0;
    fr.rcPage.left = 0;
    fr.rcPage.top = 0;
    fr.rcPage.right = MulDiv(physW, TWIPS_PER_INCH, dpiX);
    fr.rcPage.bottom = MulDiv(physH, TWIPS_PER_INCH, dpiY);

    if (pd.Flags & PD_SELECTION) {
        GetSelRange(&fr.chrg);
        lastChar = fr.chrg.cpMax;
    } else {
        GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
        textLength = (LONG)SendMessageW(g_hwndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
        fr.chrg.cpMin = 0;
        fr.chrg.cpMax = -1;
        lastChar = textLength;
    }

    if (g_szFileName[0])
        lstrcpynW(docName, FileTitle(g_szFileName), MAX_PATH);
    else
        LoadStr(IDS_UNTITLED, docName, MAX_PATH);

    ZeroMemory(&di, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = docName;

    SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (StartDocW(pd.hDC, &di) > 0) {
        do {
            RECT rcSave = fr.rc;
            LONG next;

            if (StartPage(pd.hDC) <= 0) {
                ok = FALSE;
                break;
            }
            next = (LONG)SendMessageW(g_hwndEdit, EM_FORMATRANGE, TRUE, (LPARAM)&fr);
            if (EndPage(pd.hDC) <= 0) {
                ok = FALSE;
                break;
            }
            fr.rc = rcSave;
            if (next <= fr.chrg.cpMin)
                break; /* No progress - avoid an endless loop */
            fr.chrg.cpMin = next;
        } while (fr.chrg.cpMin < lastChar);

        SendMessageW(g_hwndEdit, EM_FORMATRANGE, FALSE, 0);
        if (ok)
            EndDoc(pd.hDC);
        else
            AbortDoc(pd.hDC);
    } else {
        ok = FALSE;
    }
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    DeleteDC(pd.hDC);

    if (!ok)
        MsgBox(MB_ICONERROR, L"An error occurred while printing.");
}

/* ------------------------------------------------------------------------- */
/* Find and replace                                                          */
/* ------------------------------------------------------------------------- */

static void ShowFindDialog(BOOL replace)
{
    WCHAR sel[256];
    CHARRANGE cr;

    if (g_hwndFindDlg) {
        DestroyWindow(g_hwndFindDlg);
        g_hwndFindDlg = NULL;
    }

    /* Use a short single-line selection as the initial search text */
    GetSelRange(&cr);
    if (cr.cpMax > cr.cpMin && cr.cpMax - cr.cpMin < (LONG)ARRAYSIZE(sel)) {
        sel[0] = L'\0';
        SendMessageW(g_hwndEdit, EM_GETSELTEXT, 0, (LPARAM)sel);
        if (!wcschr(sel, L'\r') && !wcschr(sel, L'\n'))
            lstrcpynW(g_szFindText, sel, ARRAYSIZE(g_szFindText));
    }

    ZeroMemory(&g_findReplace, sizeof(g_findReplace));
    g_findReplace.lStructSize = sizeof(g_findReplace);
    g_findReplace.hwndOwner = g_hwndMain;
    g_findReplace.lpstrFindWhat = g_szFindText;
    g_findReplace.wFindWhatLen = ARRAYSIZE(g_szFindText);
    g_findReplace.lpstrReplaceWith = g_szReplaceText;
    g_findReplace.wReplaceWithLen = ARRAYSIZE(g_szReplaceText);
    g_findReplace.Flags = g_findFlags & (FR_DOWN | FR_MATCHCASE | FR_WHOLEWORD);
    if (replace) {
        g_findReplace.Flags &= ~FR_DOWN;
        g_hwndFindDlg = ReplaceTextW(&g_findReplace);
    } else {
        g_hwndFindDlg = FindTextW(&g_findReplace);
    }
}

static BOOL FindNext(const WCHAR *text, DWORD flags, BOOL showNotFound)
{
    FINDTEXTEXW ft;
    CHARRANGE cr;
    WPARAM searchFlags = 0;
    LRESULT pos;

    if (!text[0])
        return FALSE;
    GetSelRange(&cr);
    if (flags & FR_DOWN) {
        ft.chrg.cpMin = cr.cpMax;
        ft.chrg.cpMax = -1;
        searchFlags |= FR_DOWN;
    } else {
        ft.chrg.cpMin = cr.cpMin;
        ft.chrg.cpMax = 0;
    }
    if (flags & FR_MATCHCASE)
        searchFlags |= FR_MATCHCASE;
    if (flags & FR_WHOLEWORD)
        searchFlags |= FR_WHOLEWORD;
    ft.lpstrText = text;

    pos = SendMessageW(g_hwndEdit, EM_FINDTEXTEXW, searchFlags, (LPARAM)&ft);
    if (pos < 0) {
        if (showNotFound) {
            HWND owner = g_hwndFindDlg ? g_hwndFindDlg : g_hwndMain;
            WCHAR title[64], msg[400];
            LoadStr(IDS_APPNAME, title, ARRAYSIZE(title));
            _snwprintf(msg, ARRAYSIZE(msg) - 1, L"Finished searching the document.\n\nCannot find \"%s\".", text);
            msg[ARRAYSIZE(msg) - 1] = L'\0';
            MessageBoxW(owner, msg, title, MB_ICONINFORMATION);
        }
        return FALSE;
    }
    SendMessageW(g_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
    SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
    return TRUE;
}

static BOOL SelectionMatches(const WCHAR *text, DWORD flags)
{
    WCHAR sel[256];
    CHARRANGE cr;
    LONG len = (LONG)wcslen(text);

    GetSelRange(&cr);
    if (cr.cpMax - cr.cpMin != len || len >= (LONG)ARRAYSIZE(sel))
        return FALSE;
    sel[0] = L'\0';
    SendMessageW(g_hwndEdit, EM_GETSELTEXT, 0, (LPARAM)sel);
    return (flags & FR_MATCHCASE) ? wcscmp(sel, text) == 0 : _wcsicmp(sel, text) == 0;
}

static void ReplaceAll(const WCHAR *findText, const WCHAR *replText, DWORD flags)
{
    FINDTEXTEXW ft;
    WPARAM searchFlags = FR_DOWN;
    LONG replLen = (LONG)wcslen(replText);
    int count = 0;

    if (!findText[0])
        return;
    if (flags & FR_MATCHCASE)
        searchFlags |= FR_MATCHCASE;
    if (flags & FR_WHOLEWORD)
        searchFlags |= FR_WHOLEWORD;

    SendMessageW(g_hwndEdit, WM_SETREDRAW, FALSE, 0);
    ft.chrg.cpMin = 0;
    ft.chrg.cpMax = -1;
    ft.lpstrText = findText;
    while (SendMessageW(g_hwndEdit, EM_FINDTEXTEXW, searchFlags, (LPARAM)&ft) >= 0) {
        SendMessageW(g_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
        SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)replText);
        ft.chrg.cpMin = ft.chrgText.cpMin + replLen;
        ft.chrg.cpMax = -1;
        count++;
    }
    SendMessageW(g_hwndEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hwndEdit, NULL, TRUE);

    {
        HWND owner = g_hwndFindDlg ? g_hwndFindDlg : g_hwndMain;
        WCHAR title[64], msg[128];
        LoadStr(IDS_APPNAME, title, ARRAYSIZE(title));
        _snwprintf(msg, ARRAYSIZE(msg) - 1, L"%d occurrence(s) replaced.", count);
        msg[ARRAYSIZE(msg) - 1] = L'\0';
        MessageBoxW(owner, msg, title, MB_ICONINFORMATION);
    }
}

static void OnFindMessage(FINDREPLACEW *fr)
{
    if (fr->Flags & FR_DIALOGTERM) {
        g_hwndFindDlg = NULL;
        return;
    }
    g_findFlags = fr->Flags & (FR_DOWN | FR_MATCHCASE | FR_WHOLEWORD);

    if (fr->Flags & FR_FINDNEXT) {
        FindNext(fr->lpstrFindWhat, g_findFlags, TRUE);
    } else if (fr->Flags & FR_REPLACE) {
        DWORD flags = g_findFlags | FR_DOWN;
        if (SelectionMatches(fr->lpstrFindWhat, flags))
            SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)fr->lpstrReplaceWith);
        FindNext(fr->lpstrFindWhat, flags, TRUE);
    } else if (fr->Flags & FR_REPLACEALL) {
        ReplaceAll(fr->lpstrFindWhat, fr->lpstrReplaceWith, g_findFlags);
    }
}

/* ------------------------------------------------------------------------- */
/* Dialogs                                                                   */
/* ------------------------------------------------------------------------- */

static INT_PTR CALLBACK DateTimeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static WCHAR *s_result;

    switch (msg) {
    case WM_INITDIALOG: {
        static const DWORD dateFlags[] = { DATE_SHORTDATE, DATE_LONGDATE };
        static const WCHAR *const datePics[] = {
            L"dd/MM/yyyy", L"MM/dd/yyyy", L"yyyy-MM-dd", L"d MMMM yyyy", L"MMMM d, yyyy", L"dddd, MMMM d, yyyy"
        };
        static const WCHAR *const timePics[] = { L"HH:mm:ss", L"h:mm:ss tt" };
        HWND hList = GetDlgItem(hDlg, IDC_DATETIME_LIST);
        SYSTEMTIME st;
        WCHAR buf[128];
        int i;

        s_result = (WCHAR *)lParam;
        GetLocalTime(&st);
        for (i = 0; i < (int)ARRAYSIZE(dateFlags); i++) {
            if (GetDateFormatW(LOCALE_USER_DEFAULT, dateFlags[i], &st, NULL, buf, ARRAYSIZE(buf)))
                if (SendMessageW(hList, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)buf) == LB_ERR)
                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
        }
        for (i = 0; i < (int)ARRAYSIZE(datePics); i++) {
            if (GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, datePics[i], buf, ARRAYSIZE(buf)))
                if (SendMessageW(hList, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)buf) == LB_ERR)
                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
        }
        if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, buf, ARRAYSIZE(buf)))
            if (SendMessageW(hList, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)buf) == LB_ERR)
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
        for (i = 0; i < (int)ARRAYSIZE(timePics); i++) {
            if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, timePics[i], buf, ARRAYSIZE(buf)))
                if (SendMessageW(hList, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)buf) == LB_ERR)
                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessageW(hList, LB_SETCURSEL, 0, 0);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_DATETIME_LIST:
            if (HIWORD(wParam) != LBN_DBLCLK)
                break;
            /* fall through */
        case IDOK: {
            HWND hList = GetDlgItem(hDlg, IDC_DATETIME_LIST);
            LRESULT sel = SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && SendMessageW(hList, LB_GETTEXTLEN, sel, 0) < 128) {
                SendMessageW(hList, LB_GETTEXT, sel, (LPARAM)s_result);
                EndDialog(hDlg, IDOK);
            } else {
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void InsertDateTime(void)
{
    WCHAR text[128] = L"";

    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_DATETIME), g_hwndMain,
                        DateTimeDlgProc, (LPARAM)text) == IDOK && text[0])
        SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)text);
}

static void SetDlgItemInches(HWND hDlg, int id, LONG twips)
{
    WCHAR buf[32];
    _snwprintf(buf, ARRAYSIZE(buf) - 1, L"%.2f", (double)twips / TWIPS_PER_INCH);
    buf[ARRAYSIZE(buf) - 1] = L'\0';
    SetDlgItemTextW(hDlg, id, buf);
}

static BOOL GetDlgItemInches(HWND hDlg, int id, LONG *twips)
{
    WCHAR buf[32];
    WCHAR *end;
    double v;

    GetDlgItemTextW(hDlg, id, buf, ARRAYSIZE(buf));
    v = wcstod(buf, &end);
    while (*end == L' ')
        end++;
    if (end == buf || (*end && *end != L'"') || v < -22.0 || v > 22.0)
        return FALSE;
    *twips = (LONG)(v * TWIPS_PER_INCH + (v < 0 ? -0.5 : 0.5));
    return TRUE;
}

static INT_PTR CALLBACK ParagraphDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static PARAFORMAT2 *s_pf;

    switch (msg) {
    case WM_INITDIALOG: {
        HWND hAlign = GetDlgItem(hDlg, IDC_PARA_ALIGN);
        int sel;

        s_pf = (PARAFORMAT2 *)lParam;
        SetDlgItemInches(hDlg, IDC_PARA_LEFT, s_pf->dxStartIndent + s_pf->dxOffset);
        SetDlgItemInches(hDlg, IDC_PARA_RIGHT, s_pf->dxRightIndent);
        SetDlgItemInches(hDlg, IDC_PARA_FIRST, -s_pf->dxOffset);
        SendMessageW(hAlign, CB_ADDSTRING, 0, (LPARAM)L"Left");
        SendMessageW(hAlign, CB_ADDSTRING, 0, (LPARAM)L"Right");
        SendMessageW(hAlign, CB_ADDSTRING, 0, (LPARAM)L"Center");
        switch (s_pf->wAlignment) {
        case PFA_RIGHT:  sel = 1; break;
        case PFA_CENTER: sel = 2; break;
        default:         sel = 0; break;
        }
        SendMessageW(hAlign, CB_SETCURSEL, sel, 0);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            LONG left, right, first;
            static const WORD aligns[] = { PFA_LEFT, PFA_RIGHT, PFA_CENTER };
            LRESULT sel;

            if (!GetDlgItemInches(hDlg, IDC_PARA_LEFT, &left) ||
                !GetDlgItemInches(hDlg, IDC_PARA_RIGHT, &right) ||
                !GetDlgItemInches(hDlg, IDC_PARA_FIRST, &first)) {
                MessageBoxW(hDlg, L"Please enter a valid measurement in inches.",
                            L"Paragraph", MB_ICONEXCLAMATION);
                return TRUE;
            }
            sel = SendDlgItemMessageW(hDlg, IDC_PARA_ALIGN, CB_GETCURSEL, 0, 0);
            if (sel < 0 || sel > 2)
                sel = 0;
            s_pf->dxOffset = -first;
            s_pf->dxStartIndent = left + first;
            s_pf->dxRightIndent = right;
            s_pf->wAlignment = aligns[sel];
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void FormatParagraph(void)
{
    PARAFORMAT2 pf;

    GetSelParaFormat(&pf);
    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_PARAGRAPH), g_hwndMain,
                        ParagraphDlgProc, (LPARAM)&pf) == IDOK) {
        PARAFORMAT2 out;
        ZeroMemory(&out, sizeof(out));
        out.dwMask = PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_OFFSET | PFM_ALIGNMENT;
        out.dxStartIndent = pf.dxStartIndent;
        out.dxRightIndent = pf.dxRightIndent;
        out.dxOffset = pf.dxOffset;
        out.wAlignment = pf.wAlignment;
        SetSelParaFormat(&out);
        UpdateFormatBar();
    }
}

static void ShowAbout(void)
{
    WCHAR app[64];
    LoadStr(IDS_APPNAME, app, ARRAYSIZE(app));
    ShellAboutW(g_hwndMain, app,
                L"An old WordPad-like word processor written with C and Windows API.",
                LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION));
}

/* ------------------------------------------------------------------------- */
/* Toolbars and status bar                                                   */
/* ------------------------------------------------------------------------- */

static HIMAGELIST CreateFormatImages(void)
{
    const COLORREF mask = RGB(255, 0, 255);
    HIMAGELIST himl;
    HDC hdcScreen, hdc;
    HBITMAP hbm, hbmOld;
    HFONT hfBold, hfItalic, hfUnder, hfOld;
    HBRUSH hbrMask;
    RECT rc;
    int i;

    himl = ImageList_Create(GLYPH_SIZE, GLYPH_SIZE, ILC_COLOR24 | ILC_MASK, GLYPH_COUNT, 0);
    if (!himl)
        return NULL;

    hdcScreen = GetDC(NULL);
    hdc = CreateCompatibleDC(hdcScreen);
    hbm = CreateCompatibleBitmap(hdcScreen, GLYPH_SIZE * GLYPH_COUNT, GLYPH_SIZE);
    ReleaseDC(NULL, hdcScreen);
    hbmOld = (HBITMAP)SelectObject(hdc, hbm);

    rc.left = 0;
    rc.top = 0;
    rc.right = GLYPH_SIZE * GLYPH_COUNT;
    rc.bottom = GLYPH_SIZE;
    hbrMask = CreateSolidBrush(mask);
    FillRect(hdc, &rc, hbrMask);
    DeleteObject(hbrMask);

    hfBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                         VARIABLE_PITCH | FF_ROMAN, L"Times New Roman");
    hfItalic = CreateFontW(-14, 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                           VARIABLE_PITCH | FF_ROMAN, L"Times New Roman");
    hfUnder = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, TRUE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                          VARIABLE_PITCH | FF_ROMAN, L"Times New Roman");

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    /* Bold, italic, underline, color glyphs */
    {
        static const WCHAR letters[] = { L'B', L'I', L'U', L'A' };
        HFONT fonts[4];
        fonts[0] = hfBold;
        fonts[1] = hfItalic;
        fonts[2] = hfUnder;
        fonts[3] = hfBold;
        for (i = 0; i < 4; i++) {
            RECT r;
            r.left = (GLYPH_BOLD + i) * GLYPH_SIZE;
            r.top = (i == 3) ? -2 : 0;
            r.right = r.left + GLYPH_SIZE;
            r.bottom = (i == 3) ? GLYPH_SIZE - 2 : GLYPH_SIZE;
            hfOld = (HFONT)SelectObject(hdc, fonts[i]);
            DrawTextW(hdc, &letters[i], 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, hfOld);
        }
        /* Red bar under the "A" of the color button */
        rc.left = GLYPH_COLOR * GLYPH_SIZE + 2;
        rc.right = rc.left + GLYPH_SIZE - 4;
        rc.top = GLYPH_SIZE - 4;
        rc.bottom = GLYPH_SIZE - 1;
        {
            HBRUSH hbrRed = CreateSolidBrush(RGB(220, 0, 0));
            FillRect(hdc, &rc, hbrRed);
            DeleteObject(hbrRed);
        }
    }

    /* Alignment glyphs: horizontal lines of varying lengths */
    {
        static const int widths[] = { 12, 8, 12, 8, 12 };
        HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
        int g, row;
        for (g = 0; g < 3; g++) {
            int base = (GLYPH_LEFT + g) * GLYPH_SIZE;
            for (row = 0; row < 5; row++) {
                int w = widths[row];
                int x;
                if (g == 0)
                    x = 2;
                else if (g == 1)
                    x = (GLYPH_SIZE - w) / 2;
                else
                    x = GLYPH_SIZE - 2 - w;
                rc.left = base + x;
                rc.right = rc.left + w;
                rc.top = 3 + row * 2;
                rc.bottom = rc.top + 1;
                FillRect(hdc, &rc, hbrBlack);
            }
        }

        /* Bullets glyph: three dots with lines */
        {
            int base = GLYPH_BULLET * GLYPH_SIZE;
            for (row = 0; row < 3; row++) {
                rc.left = base + 2;
                rc.right = rc.left + 2;
                rc.top = 3 + row * 4;
                rc.bottom = rc.top + 2;
                FillRect(hdc, &rc, hbrBlack);
                rc.left = base + 6;
                rc.right = base + 14;
                rc.top = 4 + row * 4;
                rc.bottom = rc.top + 1;
                FillRect(hdc, &rc, hbrBlack);
            }
        }
    }

    SelectObject(hdc, hbmOld);
    DeleteDC(hdc);
    DeleteObject(hfBold);
    DeleteObject(hfItalic);
    DeleteObject(hfUnder);

    ImageList_AddMasked(himl, hbm, mask);
    DeleteObject(hbm);
    return himl;
}

static HWND CreateMainToolbar(HWND hwndParent)
{
    static const TBBUTTON buttons[] = {
        { STD_FILENEW,  IDM_FILE_NEW,   TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { STD_FILEOPEN, IDM_FILE_OPEN,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { STD_FILESAVE, IDM_FILE_SAVE,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { 0,            0,              0,               BTNS_SEP,    {0}, 0, 0 },
        { STD_PRINT,    IDM_FILE_PRINT, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { 0,            0,              0,               BTNS_SEP,    {0}, 0, 0 },
        { STD_FIND,     IDM_EDIT_FIND,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { 0,            0,              0,               BTNS_SEP,    {0}, 0, 0 },
        { STD_CUT,      IDM_EDIT_CUT,   TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { STD_COPY,     IDM_EDIT_COPY,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { STD_PASTE,    IDM_EDIT_PASTE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { STD_UNDO,     IDM_EDIT_UNDO,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { STD_REDOW,    IDM_EDIT_REDO,  TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { 0,            0,              0,               BTNS_SEP,    {0}, 0, 0 },
        { STD_PROPERTIES, IDM_INSERT_DATETIME, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
    };
    HWND hwnd;

    hwnd = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT |
                           TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE,
                           0, 0, 0, 0, hwndParent, (HMENU)(INT_PTR)IDC_TOOLBAR, g_hInst, NULL);
    if (!hwnd)
        return NULL;
    SendMessageW(hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(hwnd, TB_LOADIMAGES, IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    SendMessageW(hwnd, TB_ADDBUTTONSW, ARRAYSIZE(buttons), (LPARAM)buttons);
    SendMessageW(hwnd, TB_AUTOSIZE, 0, 0);
    return hwnd;
}

static int CALLBACK EnumFontProc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lParam)
{
    HWND hCombo = (HWND)lParam;
    (void)tm;
    (void)type;

    if (lf->lfFaceName[0] == L'@')
        return 1; /* Skip vertical fonts */
    if (SendMessageW(hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)lf->lfFaceName) == CB_ERR)
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
    return 1;
}

static void FillFontCombos(void)
{
    LOGFONTW lf;
    HDC hdc;
    int i;

    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    hdc = GetDC(NULL);
    EnumFontFamiliesExW(hdc, &lf, (FONTENUMPROCW)EnumFontProc, (LPARAM)g_hwndFontCombo, 0);
    ReleaseDC(NULL, hdc);

    for (i = 0; i < (int)ARRAYSIZE(g_fontSizes); i++)
        SendMessageW(g_hwndSizeCombo, CB_ADDSTRING, 0, (LPARAM)g_fontSizes[i]);
}

static LRESULT CALLBACK FormatBarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR idSubclass, DWORD_PTR refData)
{
    (void)idSubclass;
    (void)refData;
    if (msg == WM_COMMAND) {
        /* Forward notifications from the font combo boxes to the main window */
        HWND ctl = (HWND)lParam;
        if (ctl == g_hwndFontCombo || ctl == g_hwndSizeCombo)
            return SendMessageW(g_hwndMain, WM_COMMAND, wParam, lParam);
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, FormatBarSubclassProc, 1);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void PlaceCombo(HWND hCombo, int index)
{
    RECT rc, rcCombo;
    int h;

    SendMessageW(g_hwndFormatBar, TB_GETITEMRECT, index, (LPARAM)&rc);
    GetWindowRect(hCombo, &rcCombo);
    h = rcCombo.bottom - rcCombo.top;
    SetWindowPos(hCombo, NULL, rc.left + 2, rc.top + (rc.bottom - rc.top - h) / 2,
                 rc.right - rc.left - 4, 300, SWP_NOZORDER | SWP_NOACTIVATE);
}

static HWND CreateFormatBar(HWND hwndParent)
{
    const int fontWidth = 170, sizeWidth = 54;
    TBBUTTON buttons[] = {
        { 0,                0,                      0,               BTNS_SEP,    {0}, 0, 0 },
        { 0,                0,                      0,               BTNS_SEP,    {0}, 0, 0 },
        { 0,                0,                      0,               BTNS_SEP,    {0}, 0, 0 },
        { GLYPH_BOLD,       IDM_FORMAT_BOLD,        TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
        { GLYPH_ITALIC,     IDM_FORMAT_ITALIC,      TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
        { GLYPH_UNDERLINE,  IDM_FORMAT_UNDERLINE,   TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
        { GLYPH_COLOR,      IDM_FORMAT_COLOR,       TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0 },
        { 0,                0,                      0,               BTNS_SEP,    {0}, 0, 0 },
        { GLYPH_LEFT,       IDM_FORMAT_ALIGNLEFT,   TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
        { GLYPH_CENTER,     IDM_FORMAT_ALIGNCENTER, TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
        { GLYPH_RIGHT,      IDM_FORMAT_ALIGNRIGHT,  TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
        { 0,                0,                      0,               BTNS_SEP,    {0}, 0, 0 },
        { GLYPH_BULLET,     IDM_FORMAT_BULLET,      TBSTATE_ENABLED, BTNS_CHECK,  {0}, 0, 0 },
    };
    HWND hwnd;
    RECT rcCombo;
    int comboHeight;

    /* Separator widths reserve room for the combo boxes */
    buttons[0].iBitmap = fontWidth;
    buttons[1].iBitmap = 6;
    buttons[2].iBitmap = sizeWidth;

    hwnd = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
                           TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN |
                           CCS_NORESIZE | CCS_NODIVIDER,
                           0, 0, 0, 0, hwndParent, (HMENU)(INT_PTR)IDC_FORMATBAR, g_hInst, NULL);
    if (!hwnd)
        return NULL;
    g_hwndFormatBar = hwnd;

    SendMessageW(hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    g_hFormatImages = CreateFormatImages();
    SendMessageW(hwnd, TB_SETIMAGELIST, 0, (LPARAM)g_hFormatImages);
    SendMessageW(hwnd, TB_ADDBUTTONSW, ARRAYSIZE(buttons), (LPARAM)buttons);

    g_hwndFontCombo = CreateWindowExW(0, WC_COMBOBOXW, NULL,
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                                      CBS_DROPDOWN | CBS_SORT | CBS_AUTOHSCROLL,
                                      0, 0, fontWidth, 300, hwnd,
                                      (HMENU)(INT_PTR)IDC_FONTNAME, g_hInst, NULL);
    g_hwndSizeCombo = CreateWindowExW(0, WC_COMBOBOXW, NULL,
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                                      CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                      0, 0, sizeWidth, 300, hwnd,
                                      (HMENU)(INT_PTR)IDC_FONTSIZE, g_hInst, NULL);
    SendMessageW(g_hwndFontCombo, WM_SETFONT, (WPARAM)g_hUiFont, FALSE);
    SendMessageW(g_hwndSizeCombo, WM_SETFONT, (WPARAM)g_hUiFont, FALSE);
    SendMessageW(g_hwndFontCombo, CB_LIMITTEXT, LF_FACESIZE - 1, 0);
    SendMessageW(g_hwndSizeCombo, CB_LIMITTEXT, 6, 0);

    /* Make buttons at least as tall as the combo boxes */
    GetWindowRect(g_hwndFontCombo, &rcCombo);
    comboHeight = rcCombo.bottom - rcCombo.top;
    {
        DWORD size = (DWORD)SendMessageW(hwnd, TB_GETBUTTONSIZE, 0, 0);
        int bh = HIWORD(size);
        if (bh < comboHeight + 2)
            SendMessageW(hwnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(LOWORD(size), comboHeight + 2));
    }
    SendMessageW(hwnd, TB_AUTOSIZE, 0, 0);
    PlaceCombo(g_hwndFontCombo, 0);
    PlaceCombo(g_hwndSizeCombo, 2);

    SetWindowSubclass(hwnd, FormatBarSubclassProc, 1, 0);
    FillFontCombos();
    return hwnd;
}

static void UpdateFormatBar(void)
{
    CHARFORMAT2W cf;
    PARAFORMAT2 pf;
    WCHAR buf[32];

    if (!g_hwndFormatBar || !g_hwndEdit)
        return;

    g_bUpdatingFormatBar = TRUE;
    GetSelCharFormat(&cf);
    GetSelParaFormat(&pf);

    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_BOLD,
                 MAKELONG((cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD), 0));
    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_ITALIC,
                 MAKELONG((cf.dwMask & CFM_ITALIC) && (cf.dwEffects & CFE_ITALIC), 0));
    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_UNDERLINE,
                 MAKELONG((cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE), 0));
    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_ALIGNLEFT,
                 MAKELONG((pf.dwMask & PFM_ALIGNMENT) && (pf.wAlignment == PFA_LEFT || pf.wAlignment == PFA_JUSTIFY), 0));
    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_ALIGNCENTER,
                 MAKELONG((pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_CENTER, 0));
    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_ALIGNRIGHT,
                 MAKELONG((pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_RIGHT, 0));
    SendMessageW(g_hwndFormatBar, TB_CHECKBUTTON, IDM_FORMAT_BULLET,
                 MAKELONG((pf.dwMask & PFM_NUMBERING) && pf.wNumbering == PFN_BULLET, 0));

    if (GetFocus() != g_hwndFontCombo && GetParent(GetFocus()) != g_hwndFontCombo)
        SetWindowTextW(g_hwndFontCombo, (cf.dwMask & CFM_FACE) ? cf.szFaceName : L"");
    if (GetFocus() != g_hwndSizeCombo && GetParent(GetFocus()) != g_hwndSizeCombo) {
        if (cf.dwMask & CFM_SIZE) {
            if (cf.yHeight % 20 == 0)
                _snwprintf(buf, ARRAYSIZE(buf) - 1, L"%ld", (long)(cf.yHeight / 20));
            else
                _snwprintf(buf, ARRAYSIZE(buf) - 1, L"%.1f", cf.yHeight / 20.0);
            buf[ARRAYSIZE(buf) - 1] = L'\0';
        } else {
            buf[0] = L'\0';
        }
        SetWindowTextW(g_hwndSizeCombo, buf);
    }
    g_bUpdatingFormatBar = FALSE;
}

static void UpdateStatusBar(void)
{
    CHARRANGE cr;
    LONG line, col;
    WCHAR buf[64];

    if (!g_hwndStatus || !g_hwndEdit)
        return;
    GetSelRange(&cr);
    line = (LONG)SendMessageW(g_hwndEdit, EM_EXLINEFROMCHAR, 0, cr.cpMax);
    col = cr.cpMax - (LONG)SendMessageW(g_hwndEdit, EM_LINEINDEX, line, 0);
    _snwprintf(buf, ARRAYSIZE(buf) - 1, L"Ln %ld, Col %ld", (long)line + 1, (long)col + 1);
    buf[ARRAYSIZE(buf) - 1] = L'\0';
    SendMessageW(g_hwndStatus, SB_SETTEXTW, 1, (LPARAM)buf);
}

static void SetStatusHelp(const WCHAR *text)
{
    if (g_hwndStatus)
        SendMessageW(g_hwndStatus, SB_SETTEXTW, 0, (LPARAM)text);
}

static void SetStatusReady(void)
{
    WCHAR buf[128];
    LoadStr(IDS_READY, buf, ARRAYSIZE(buf));
    SetStatusHelp(buf);
}

static void UpdateToolbarState(void)
{
    BOOL sel;

    if (!g_hwndToolbar || !g_hwndEdit)
        return;
    sel = HasSelection();
    SendMessageW(g_hwndToolbar, TB_ENABLEBUTTON, IDM_EDIT_CUT, MAKELONG(sel, 0));
    SendMessageW(g_hwndToolbar, TB_ENABLEBUTTON, IDM_EDIT_COPY, MAKELONG(sel, 0));
    SendMessageW(g_hwndToolbar, TB_ENABLEBUTTON, IDM_EDIT_PASTE,
                 MAKELONG(SendMessageW(g_hwndEdit, EM_CANPASTE, 0, 0) != 0, 0));
    SendMessageW(g_hwndToolbar, TB_ENABLEBUTTON, IDM_EDIT_UNDO,
                 MAKELONG(SendMessageW(g_hwndEdit, EM_CANUNDO, 0, 0) != 0, 0));
    SendMessageW(g_hwndToolbar, TB_ENABLEBUTTON, IDM_EDIT_REDO,
                 MAKELONG(SendMessageW(g_hwndEdit, EM_CANREDO, 0, 0) != 0, 0));
}

static void LayoutWindow(HWND hwnd)
{
    RECT rc;
    int y = 0, bottom;

    GetClientRect(hwnd, &rc);
    bottom = rc.bottom;

    if (g_bShowToolbar) {
        RECT r;
        int h;
        SendMessageW(g_hwndToolbar, TB_AUTOSIZE, 0, 0);
        SendMessageW(g_hwndToolbar, TB_GETITEMRECT, 0, (LPARAM)&r);
        h = r.bottom + 2;
        MoveWindow(g_hwndToolbar, 0, y, rc.right, h, TRUE);
        ShowWindow(g_hwndToolbar, SW_SHOW);
        y += h;
    } else {
        ShowWindow(g_hwndToolbar, SW_HIDE);
    }

    if (g_bShowFormatBar) {
        RECT r;
        int h;
        SendMessageW(g_hwndFormatBar, TB_AUTOSIZE, 0, 0);
        SendMessageW(g_hwndFormatBar, TB_GETITEMRECT, 3, (LPARAM)&r);
        h = r.bottom + 2;
        MoveWindow(g_hwndFormatBar, 0, y, rc.right, h, TRUE);
        ShowWindow(g_hwndFormatBar, SW_SHOW);
        y += h;
    } else {
        ShowWindow(g_hwndFormatBar, SW_HIDE);
    }

    if (g_bShowStatus) {
        RECT r;
        int parts[2];
        SendMessageW(g_hwndStatus, WM_SIZE, 0, 0);
        GetWindowRect(g_hwndStatus, &r);
        bottom -= r.bottom - r.top;
        parts[0] = rc.right > 180 ? rc.right - 160 : rc.right / 2;
        parts[1] = -1;
        SendMessageW(g_hwndStatus, SB_SETPARTS, 2, (LPARAM)parts);
        ShowWindow(g_hwndStatus, SW_SHOW);
    } else {
        ShowWindow(g_hwndStatus, SW_HIDE);
    }

    MoveWindow(g_hwndEdit, 0, y, rc.right, bottom > y ? bottom - y : 0, TRUE);
}

/* ------------------------------------------------------------------------- */
/* Main window                                                               */
/* ------------------------------------------------------------------------- */

static BOOL CreateEditor(HWND hwnd)
{
    const WCHAR *cls = MSFTEDIT_CLASS;

    g_hRichEditLib = LoadLibraryW(L"Msftedit.dll");
    if (!g_hRichEditLib) {
        g_hRichEditLib = LoadLibraryW(L"Riched20.dll");
        cls = RICHEDIT_CLASSW;
    }
    if (!g_hRichEditLib)
        return FALSE;

    g_hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, cls, NULL,
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                 ES_NOHIDESEL | ES_WANTRETURN | ES_SAVESEL,
                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_EDITOR, g_hInst, NULL);
    if (!g_hwndEdit)
        return FALSE;

    SendMessageW(g_hwndEdit, EM_EXLIMITTEXT, 0, MAX_TEXT_LIMIT);
    SendMessageW(g_hwndEdit, EM_SETEVENTMASK, 0, ENM_SELCHANGE | ENM_CHANGE);
    SendMessageW(g_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(8, 8));
    SetDefaultFormat();
    ApplyWordWrap();
    SendMessageW(g_hwndEdit, EM_SETMODIFY, FALSE, 0);
    return TRUE;
}

static void ApplyComboFont(void)
{
    WCHAR buf[LF_FACESIZE];
    GetWindowTextW(g_hwndFontCombo, buf, ARRAYSIZE(buf));
    SetFontFace(buf);
}

static void ApplyComboSize(void)
{
    WCHAR buf[16];
    GetWindowTextW(g_hwndSizeCombo, buf, ARRAYSIZE(buf));
    if (!SetFontSize(buf)) {
        MsgBox(MB_ICONEXCLAMATION, L"The number must be between 1 and 1638.");
        UpdateFormatBar();
    }
}

static void OnCommand(HWND hwnd, int id, int code, HWND hwndCtl)
{
    /* Font combo notifications (forwarded by the format bar) */
    if (hwndCtl == g_hwndFontCombo || hwndCtl == g_hwndSizeCombo) {
        if (g_bUpdatingFormatBar)
            return;
        if (code == CBN_SELENDOK) {
            HWND hCombo = hwndCtl;
            LRESULT sel = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            WCHAR buf[LF_FACESIZE];
            if (sel != CB_ERR && SendMessageW(hCombo, CB_GETLBTEXTLEN, sel, 0) < LF_FACESIZE) {
                SendMessageW(hCombo, CB_GETLBTEXT, sel, (LPARAM)buf);
                if (hCombo == g_hwndFontCombo)
                    SetFontFace(buf);
                else
                    SetFontSize(buf);
                SetFocus(g_hwndEdit);
            }
        }
        return;
    }

    if (hwndCtl == g_hwndEdit) {
        if (code == EN_CHANGE)
            UpdateToolbarState();
        return;
    }

    switch (id) {
    case IDM_FILE_NEW:       NewDocument(); break;
    case IDM_FILE_OPEN:      OpenDocument(); break;
    case IDM_FILE_SAVE:      SaveDocument(); break;
    case IDM_FILE_SAVEAS:    SaveDocumentAs(); break;
    case IDM_FILE_PRINT:     PrintDocument(); break;
    case IDM_FILE_PAGESETUP: PageSetup(); break;
    case IDM_FILE_EXIT:      SendMessageW(hwnd, WM_CLOSE, 0, 0); break;

    case IDM_EDIT_UNDO:      SendMessageW(g_hwndEdit, EM_UNDO, 0, 0); break;
    case IDM_EDIT_REDO:      SendMessageW(g_hwndEdit, EM_REDO, 0, 0); break;
    case IDM_EDIT_CUT:       SendMessageW(g_hwndEdit, WM_CUT, 0, 0); break;
    case IDM_EDIT_COPY:      SendMessageW(g_hwndEdit, WM_COPY, 0, 0); break;
    case IDM_EDIT_PASTE:     SendMessageW(g_hwndEdit, WM_PASTE, 0, 0); break;
    case IDM_EDIT_DELETE:    SendMessageW(g_hwndEdit, WM_CLEAR, 0, 0); break;
    case IDM_EDIT_SELECTALL: SendMessageW(g_hwndEdit, EM_SETSEL, 0, -1); break;
    case IDM_EDIT_FIND:      ShowFindDialog(FALSE); break;
    case IDM_EDIT_REPLACE:   ShowFindDialog(TRUE); break;
    case IDM_EDIT_FINDNEXT:
        if (g_szFindText[0])
            FindNext(g_szFindText, g_findFlags, TRUE);
        else
            ShowFindDialog(FALSE);
        break;

    case IDM_VIEW_TOOLBAR:   g_bShowToolbar = !g_bShowToolbar; LayoutWindow(hwnd); break;
    case IDM_VIEW_FORMATBAR: g_bShowFormatBar = !g_bShowFormatBar; LayoutWindow(hwnd); break;
    case IDM_VIEW_STATUSBAR: g_bShowStatus = !g_bShowStatus; LayoutWindow(hwnd); break;
    case IDM_VIEW_WORDWRAP:  g_bWordWrap = !g_bWordWrap; ApplyWordWrap(); break;

    case IDM_INSERT_DATETIME: InsertDateTime(); break;

    case IDM_FORMAT_FONT:        ChooseTextFont(); break;
    case IDM_FORMAT_BOLD:        ToggleEffect(CFM_BOLD, CFE_BOLD); break;
    case IDM_FORMAT_ITALIC:      ToggleEffect(CFM_ITALIC, CFE_ITALIC); break;
    case IDM_FORMAT_UNDERLINE:   ToggleEffect(CFM_UNDERLINE, CFE_UNDERLINE); break;
    case IDM_FORMAT_COLOR:       ChooseTextColor(); break;
    case IDM_FORMAT_BULLET:      ToggleBullets(); break;
    case IDM_FORMAT_PARAGRAPH:   FormatParagraph(); break;
    case IDM_FORMAT_ALIGNLEFT:   SetAlignment(PFA_LEFT); break;
    case IDM_FORMAT_ALIGNCENTER: SetAlignment(PFA_CENTER); break;
    case IDM_FORMAT_ALIGNRIGHT:  SetAlignment(PFA_RIGHT); break;

    case IDM_HELP_ABOUT:     ShowAbout(); break;
    default:
        return;
    }

    if (id != IDM_FILE_EXIT && IsWindow(g_hwndEdit) && GetFocus() != g_hwndFindDlg &&
        !IsChild(g_hwndFindDlg, GetFocus()))
        SetFocus(g_hwndEdit);
}

static void OnInitMenuPopup(HMENU hMenu)
{
    BOOL sel = HasSelection();
    BOOL canUndo = SendMessageW(g_hwndEdit, EM_CANUNDO, 0, 0) != 0;
    BOOL canRedo = SendMessageW(g_hwndEdit, EM_CANREDO, 0, 0) != 0;
    BOOL canPaste = SendMessageW(g_hwndEdit, EM_CANPASTE, 0, 0) != 0;
    CHARFORMAT2W cf;
    PARAFORMAT2 pf;

    GetSelCharFormat(&cf);
    GetSelParaFormat(&pf);

#define ENABLE(id, cond) EnableMenuItem(hMenu, (id), MF_BYCOMMAND | ((cond) ? MF_ENABLED : MF_GRAYED))
#define CHECK(id, cond)  CheckMenuItem(hMenu, (id), MF_BYCOMMAND | ((cond) ? MF_CHECKED : MF_UNCHECKED))
    ENABLE(IDM_EDIT_UNDO, canUndo);
    ENABLE(IDM_EDIT_REDO, canRedo);
    ENABLE(IDM_EDIT_CUT, sel);
    ENABLE(IDM_EDIT_COPY, sel);
    ENABLE(IDM_EDIT_DELETE, sel);
    ENABLE(IDM_EDIT_PASTE, canPaste);
    CHECK(IDM_VIEW_TOOLBAR, g_bShowToolbar);
    CHECK(IDM_VIEW_FORMATBAR, g_bShowFormatBar);
    CHECK(IDM_VIEW_STATUSBAR, g_bShowStatus);
    CHECK(IDM_VIEW_WORDWRAP, g_bWordWrap);
    CHECK(IDM_FORMAT_BOLD, (cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD));
    CHECK(IDM_FORMAT_ITALIC, (cf.dwMask & CFM_ITALIC) && (cf.dwEffects & CFE_ITALIC));
    CHECK(IDM_FORMAT_UNDERLINE, (cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE));
    CHECK(IDM_FORMAT_BULLET, (pf.dwMask & PFM_NUMBERING) && pf.wNumbering == PFN_BULLET);
    CHECK(IDM_FORMAT_ALIGNLEFT, (pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_LEFT);
    CHECK(IDM_FORMAT_ALIGNCENTER, (pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_CENTER);
    CHECK(IDM_FORMAT_ALIGNRIGHT, (pf.dwMask & PFM_ALIGNMENT) && pf.wAlignment == PFA_RIGHT);
#undef ENABLE
#undef CHECK
}

static void ShowContextMenu(HWND hwnd, LPARAM lParam)
{
    HMENU hMenu, hPopup;
    POINT pt;

    pt.x = (short)LOWORD(lParam);
    pt.y = (short)HIWORD(lParam);
    if (lParam == -1) {
        /* Keyboard-invoked: show at the caret */
        CHARRANGE cr;
        POINTL ptl;
        GetSelRange(&cr);
        SendMessageW(g_hwndEdit, EM_POSFROMCHAR, (WPARAM)&ptl, cr.cpMax);
        pt.x = ptl.x;
        pt.y = ptl.y + 16;
        ClientToScreen(g_hwndEdit, &pt);
    }

    hMenu = LoadMenuW(g_hInst, MAKEINTRESOURCEW(IDR_CONTEXTMENU));
    if (!hMenu)
        return;
    hPopup = GetSubMenu(hMenu, 0);
    OnInitMenuPopup(hPopup);
    TrackPopupMenu(hPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_uFindMsg && g_uFindMsg) {
        OnFindMessage((FINDREPLACEW *)lParam);
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm;

        g_hwndMain = hwnd;
        ZeroMemory(&ncm, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);
        if (!g_hUiFont)
            g_hUiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        if (!CreateEditor(hwnd)) {
            MessageBoxW(hwnd, L"Unable to create the Rich Edit control.", L"WinNewPad", MB_ICONERROR);
            return -1;
        }
        g_hwndToolbar = CreateMainToolbar(hwnd);
        CreateFormatBar(hwnd);
        g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
                                       WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                       0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_STATUSBAR, g_hInst, NULL);
        SetStatusReady();
        UpdateTitle();
        UpdateFormatBar();
        UpdateStatusBar();
        UpdateToolbarState();
        return 0;
    }

    case WM_SIZE:
        LayoutWindow(hwnd);
        return 0;

    case WM_SETFOCUS:
        if (g_hwndEdit)
            SetFocus(g_hwndEdit);
        return 0;

    case WM_COMMAND:
        OnCommand(hwnd, LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
        return 0;

    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lParam;
        if (hdr->code == TTN_GETDISPINFOW) {
            NMTTDISPINFOW *di = (NMTTDISPINFOW *)lParam;
            static WCHAR tip[128];
            LoadCommandString((UINT)hdr->idFrom, TRUE, tip, ARRAYSIZE(tip));
            di->lpszText = tip;
            di->hinst = NULL;
            return 0;
        }
        if (hdr->hwndFrom == g_hwndEdit && hdr->code == EN_SELCHANGE) {
            UpdateFormatBar();
            UpdateStatusBar();
            UpdateToolbarState();
            return 0;
        }
        break;
    }

    case WM_INITMENUPOPUP:
        if (!HIWORD(lParam))
            OnInitMenuPopup((HMENU)wParam);
        return 0;

    case WM_MENUSELECT: {
        UINT item = LOWORD(wParam);
        UINT flags = HIWORD(wParam);
        if (flags == 0xFFFF && lParam == 0) {
            SetStatusReady();
        } else if (!(flags & (MF_POPUP | MF_SEPARATOR)) && item) {
            WCHAR help[256];
            LoadCommandString(item, FALSE, help, ARRAYSIZE(help));
            SetStatusHelp(help);
        } else {
            SetStatusHelp(L"");
        }
        return 0;
    }

    case WM_CONTEXTMENU:
        if ((HWND)wParam == g_hwndEdit) {
            ShowContextMenu(hwnd, lParam);
            return 0;
        }
        break;

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        WCHAR path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH) && QuerySaveChanges())
            LoadDocument(path);
        DragFinish(hDrop);
        return 0;
    }

    case WM_QUERYENDSESSION:
        return QuerySaveChanges();

    case WM_CLOSE:
        if (QuerySaveChanges())
            DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_hwndFindDlg)
            DestroyWindow(g_hwndFindDlg);
        if (g_hFormatImages)
            ImageList_Destroy(g_hFormatImages);
        if (g_hUiFont && g_hUiFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
            DeleteObject(g_hUiFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Handles Enter / Escape in the font name and size combo boxes. */
static BOOL HandleComboKeys(const MSG *msg)
{
    HWND parent;

    if (msg->message != WM_KEYDOWN || (msg->wParam != VK_RETURN && msg->wParam != VK_ESCAPE))
        return FALSE;
    parent = GetParent(msg->hwnd);
    if (msg->hwnd != g_hwndFontCombo && msg->hwnd != g_hwndSizeCombo &&
        parent != g_hwndFontCombo && parent != g_hwndSizeCombo)
        return FALSE;

    if (msg->wParam == VK_RETURN) {
        if (msg->hwnd == g_hwndFontCombo || parent == g_hwndFontCombo)
            ApplyComboFont();
        else
            ApplyComboSize();
    } else {
        UpdateFormatBar();
    }
    SetFocus(g_hwndEdit);
    UpdateFormatBar();
    return TRUE;
}

static BOOL IsComboFocused(void)
{
    HWND focus = GetFocus();
    HWND parent = focus ? GetParent(focus) : NULL;
    return focus && (focus == g_hwndFontCombo || focus == g_hwndSizeCombo ||
                     parent == g_hwndFontCombo || parent == g_hwndSizeCombo);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    HACCEL hAccel;
    HWND hwnd;
    MSG msg;
    WCHAR app[64];
    int argc = 0;
    LPWSTR *argv;

    (void)hPrevInstance;
    (void)lpCmdLine;
    g_hInst = hInstance;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    g_uFindMsg = RegisterWindowMessageW(FINDMSGSTRINGW);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hIconSm = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = APP_CLASSNAME;
    if (!RegisterClassExW(&wc))
        return 1;

    LoadStr(IDS_APPNAME, app, ARRAYSIZE(app));
    hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, APP_CLASSNAME, app, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                           NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* Open a document passed on the command line */
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1 && argv[1][0])
            LoadDocument(argv[1]);
        LocalFree(argv);
    }

    hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_hwndFindDlg && IsDialogMessageW(g_hwndFindDlg, &msg))
            continue;
        if (HandleComboKeys(&msg))
            continue;
        if (!IsComboFocused() && TranslateAcceleratorW(hwnd, hAccel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
