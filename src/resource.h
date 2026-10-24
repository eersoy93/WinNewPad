/*
 * WinNewPad - An old WordPad-like word processor written with C and Windows API.
 * resource.h - Resource and command identifiers.
 */

#ifndef WINNEWPAD_RESOURCE_H
#define WINNEWPAD_RESOURCE_H

/* Menus and accelerators */
#define IDR_MAINMENU            100
#define IDR_ACCEL               101
#define IDR_CONTEXTMENU         102

/* Dialogs */
#define IDD_DATETIME            200
#define IDD_PARAGRAPH           201

/* Dialog controls */
#define IDC_DATETIME_LIST       300
#define IDC_PARA_LEFT           310
#define IDC_PARA_RIGHT          311
#define IDC_PARA_FIRST          312
#define IDC_PARA_ALIGN          313

/* Child window identifiers */
#define IDC_EDITOR              400
#define IDC_TOOLBAR             401
#define IDC_FORMATBAR           402
#define IDC_STATUSBAR           403
#define IDC_FONTNAME            404
#define IDC_FONTSIZE            405

/* File menu */
#define IDM_FILE_NEW            1000
#define IDM_FILE_OPEN           1001
#define IDM_FILE_SAVE           1002
#define IDM_FILE_SAVEAS         1003
#define IDM_FILE_PRINT          1004
#define IDM_FILE_PAGESETUP      1005
#define IDM_FILE_EXIT           1006

/* Edit menu */
#define IDM_EDIT_UNDO           1100
#define IDM_EDIT_REDO           1101
#define IDM_EDIT_CUT            1102
#define IDM_EDIT_COPY           1103
#define IDM_EDIT_PASTE          1104
#define IDM_EDIT_DELETE         1105
#define IDM_EDIT_SELECTALL      1106
#define IDM_EDIT_FIND           1107
#define IDM_EDIT_FINDNEXT       1108
#define IDM_EDIT_REPLACE        1109

/* View menu */
#define IDM_VIEW_TOOLBAR        1200
#define IDM_VIEW_FORMATBAR      1201
#define IDM_VIEW_STATUSBAR      1202
#define IDM_VIEW_WORDWRAP       1203

/* Insert menu */
#define IDM_INSERT_DATETIME     1300

/* Format menu */
#define IDM_FORMAT_FONT         1400
#define IDM_FORMAT_BOLD         1401
#define IDM_FORMAT_ITALIC       1402
#define IDM_FORMAT_UNDERLINE    1403
#define IDM_FORMAT_COLOR        1404
#define IDM_FORMAT_BULLET       1405
#define IDM_FORMAT_PARAGRAPH    1406
#define IDM_FORMAT_ALIGNLEFT    1407
#define IDM_FORMAT_ALIGNCENTER  1408
#define IDM_FORMAT_ALIGNRIGHT   1409

/* Help menu */
#define IDM_HELP_ABOUT          1500

/* Strings */
#define IDS_APPNAME             2000
#define IDS_UNTITLED            2001
#define IDS_READY               2002

#endif /* WINNEWPAD_RESOURCE_H */
