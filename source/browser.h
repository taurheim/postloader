/*
browser.h contain common code as macro

*/

#ifndef _BROWSERCOMMON_
#define _BROWSERCOMMON_

#define REDRAW()\
	{\
	static int ds1,ds2, bbcmd;\
	\
	grlib_PopScreen ();\
	FindSpot ();\
	Overlay ();\
	\
	if (!config.lockCurrentMode)\
		{\
		DrawTopBar (&ds1, &browserRet, &btn, NULL);\
		bbcmd = DrawBottomBar (&ds2, &btn, NULL);\
		}\
	\
	if (ds1 || ds2) disableSpots = 1; else disableSpots = 0;\
	\
	grlib_DrawIRCursor ();\
	grlib_Render();\
	\
	if (!config.lockCurrentMode)\
		{\
		if (bbcmd != -1) redraw = 1;\
		if (bbcmd == 0)	ShowAboutPLMenu ();\
		if (bbcmd == 1)	ShowAboutMenu ();\
		if (bbcmd == 2 && CheckDisk(NULL) == 1) browserRet = INTERACTIVE_RET_DISC;\
		if (bbcmd == 3)	browserRet = ShowExitMenu();\
		if (bbcmd == 4)	browserRet = ShowBootmiiMenu();\
		if (bbcmd == 5)	browserRet = INTERACTIVE_RET_SE;\
		if (bbcmd == 6)	browserRet = INTERACTIVE_RET_WM;\
		}\
	}

#define CLOSETOPBAR()\
	{\
	int closed;\
	do\
		{\
		grlib_irPos.x = 320;\
		grlib_irPos.y = 240;\
		\
		grlib_PopScreen ();\
		Overlay ();\
		DrawTopBar (&disableSpots, NULL, NULL, &closed);\
        grlib_Render();\
		}\
	while (!closed);\
	}

#define CLOSEBOTTOMBAR()\
	{\
	int closed;\
	do\
		{\
		grlib_irPos.x = 320;\
		grlib_irPos.y = 240;\
		\
		grlib_PopScreen ();\
		Overlay ();\
		DrawBottomBar (&disableSpots, NULL, &closed);\
        grlib_Render();\
		}\
	while (!closed);\
	}
#endif