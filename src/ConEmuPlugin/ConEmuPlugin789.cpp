#include <windows.h>
#include "..\common\common.hpp"
#include "..\common\pluginW789.hpp"
#include "PluginHeader.h"

#ifndef FORWARD_WM_COPYDATA
#define FORWARD_WM_COPYDATA(hwnd, hwndFrom, pcds, fn) \
    (BOOL)(UINT)(DWORD)(fn)((hwnd), WM_COPYDATA, (WPARAM)(hwndFrom), (LPARAM)(pcds))
#endif


struct PluginStartupInfo *InfoW789=NULL;
struct FarStandardFunctions *FSFW789=NULL;




void ProcessDragFrom789()
{
	WindowInfo WInfo;				
    WInfo.Pos=0;
	InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_GETSHORTWINDOWINFO, (void*)&WInfo);
	if (!WInfo.Current)
	{
		int ItemsCount=0;
		//WriteFile(hPipe, &ItemsCount, sizeof(int), &cout, NULL);				
		OutDataAlloc(sizeof(ItemsCount));
		OutDataWrite(&ItemsCount,sizeof(ItemsCount));
		return;
	}

	PanelInfo PInfo;
	WCHAR *szCurDir=gszDir1; szCurDir[0]=0; //(WCHAR*)calloc(0x400,sizeof(WCHAR));
	InfoW789->Control(PANEL_ACTIVE, FCTL_GETPANELINFO, NULL, (LONG_PTR)&PInfo);
	if ((PInfo.PanelType == PTYPE_FILEPANEL || PInfo.PanelType == PTYPE_TREEPANEL) && PInfo.Visible)
	{
		InfoW789->Control(PANEL_ACTIVE, FCTL_GETCURRENTDIRECTORY, 0x400, (LONG_PTR)szCurDir);
		int nDirLen=0, nDirNoSlash=0;
		if (szCurDir[0])
		{
			nDirLen=lstrlen(szCurDir);
			if (nDirLen>0)
				if (szCurDir[nDirLen-1]!=L'\\')
					nDirNoSlash=1;
		}

		OutDataAlloc(sizeof(int)+PInfo.SelectedItemsNumber*((MAX_PATH+2)+sizeof(int)));

		//Maximus5 - ����� ������ ��������
		int nNull=0; // ItemsCount
		//WriteFile(hPipe, &nNull, sizeof(int), &cout, NULL);
		OutDataWrite(&nNull/*ItemsCount*/, sizeof(int));
		
		if (PInfo.SelectedItemsNumber>0)
		{
			PluginPanelItem **pi = (PluginPanelItem**)calloc(PInfo.SelectedItemsNumber, sizeof(PluginPanelItem*));
			int ItemsCount=PInfo.SelectedItemsNumber, i;

			int nMaxLen=MAX_PATH+1, nWholeLen=1;

			// ������� ��������� ������������ ����� ������
			for (i=0;i<ItemsCount;i++)
			{
				size_t sz = InfoW789->Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, i, NULL);
				if (!sz)
					continue;
				pi[i] = (PluginPanelItem*)calloc(sz, 1); // ������ ������������ � ������
				if (!InfoW789->Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, i, (LONG_PTR)(pi[i])))
					continue;

				int nLen=nDirLen+nDirNoSlash;
				nLen += lstrlen(pi[i]->FindData.lpwszFileName);
				if (nLen>nMaxLen)
					nMaxLen = nLen;
				nWholeLen += (nLen+1);
			}
			//WriteFile(hPipe, &nWholeLen, sizeof(int), &cout, NULL);
			OutDataWrite(&nWholeLen, sizeof(int));

			WCHAR* Path=new WCHAR[nMaxLen+1];

			for (i=0;i<ItemsCount;i++)
			{
				//WCHAR Path[MAX_PATH+1];
				//ZeroMemory(Path, MAX_PATH+1);
				//Maximus5 - ������ � ������ ����� � ������������ overflow
				//wsprintf(Path, L"%s\\%s", szCurDir, PInfo.SelectedItems[i]->FindData.lpwszFileName);
				Path[0]=0;

				if (!pi[i] || !pi[i]->FindData.lpwszFileName) continue; //���� ������� �������� �� �������

				int nLen=nDirLen+nDirNoSlash;
				nLen += lstrlen(pi[i]->FindData.lpwszFileName);

				if (nDirLen>0) {
					lstrcpy(Path, szCurDir);
					if (nDirNoSlash) {
						Path[nDirLen]=L'\\';
						Path[nDirLen+1]=0;
					}
				}
				lstrcpy(Path+nDirLen+nDirNoSlash, pi[i]->FindData.lpwszFileName);

				nLen++;
				//WriteFile(hPipe, &nLen, sizeof(int), &cout, NULL);
				OutDataWrite(&nLen, sizeof(int));
				//WriteFile(hPipe, Path, sizeof(WCHAR)*nLen, &cout, NULL);
				OutDataWrite(Path, sizeof(WCHAR)*nLen);
			}

			for (i=0;i<ItemsCount;i++)
			{
				if (pi[i]) free(pi[i]);
			}
			free ( pi ); pi = NULL;

			delete Path; Path=NULL;

			// ����� ������
			//WriteFile(hPipe, &nNull, sizeof(int), &cout, NULL);
			OutDataWrite(&nNull, sizeof(int));
		}
	}
	else
	{
		int ItemsCount=0;
		OutDataWrite(&ItemsCount, sizeof(int));
		OutDataWrite(&ItemsCount, sizeof(int)); // ����� �������
	}
	//free(szCurDir);
}

void ProcessDragTo789()
{
	WindowInfo WInfo;				
    WInfo.Pos=0;
	InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_GETSHORTWINDOWINFO, (void*)&WInfo);
	if (!WInfo.Current)
	{
		//InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_FREEWINDOWINFO, (void*)&WInfo);
		int ItemsCount=0;
		//WriteFile(hPipe, &ItemsCount, sizeof(int), &cout, NULL);				
		OutDataAlloc(sizeof(ItemsCount));
		OutDataWrite(&ItemsCount,sizeof(ItemsCount));
		return;
	}
	//InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_FREEWINDOWINFO, (void*)&WInfo);
	
	PanelInfo PAInfo, PPInfo;
	ForwardedPanelInfo *pfpi=NULL;
	int nStructSize = sizeof(ForwardedPanelInfo)+4; // ����� �������� �� ����� �����
	//ZeroMemory(&fpi, sizeof(fpi));
	BOOL lbAOK=FALSE, lbPOK=FALSE;
	WCHAR *szPDir=gszDir1; szPDir[0]=0; //(WCHAR*)calloc(0x400,sizeof(WCHAR));
	WCHAR *szADir=gszDir2; szADir[0]=0; //(WCHAR*)calloc(0x400,sizeof(WCHAR));
	
	//if (!(lbAOK=InfoW789->Control(PANEL_ACTIVE, FCTL_GETPANELSHORTINFO, &PAInfo)))
	lbAOK=InfoW789->Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&PAInfo) &&
		  InfoW789->Control(PANEL_ACTIVE, FCTL_GETCURRENTDIRECTORY, 0x400, (LONG_PTR)szADir);
	if (lbAOK && szADir)
		nStructSize += (lstrlen(szADir))*sizeof(WCHAR);

	lbPOK=InfoW789->Control(PANEL_PASSIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&PPInfo) &&
		  InfoW789->Control(PANEL_PASSIVE, FCTL_GETCURRENTDIRECTORY, 0x400, (LONG_PTR)szPDir);
	if (lbPOK && szPDir)
		nStructSize += (lstrlen(szPDir))*sizeof(WCHAR); // ������ WCHAR! �� TCHAR

	pfpi = (ForwardedPanelInfo*)calloc(nStructSize,1);


	pfpi->ActivePathShift = sizeof(ForwardedPanelInfo);
	pfpi->pszActivePath = (WCHAR*)(((char*)pfpi)+pfpi->ActivePathShift);

	pfpi->PassivePathShift = pfpi->ActivePathShift+2; // ���� ActivePath ���������� - ��������

	if (lbAOK)
	{
		pfpi->ActiveRect=PAInfo.PanelRect;
		if ((PAInfo.PanelType == PTYPE_FILEPANEL || PAInfo.PanelType == PTYPE_TREEPANEL) && PAInfo.Visible)
		{
			if (szADir[0]) {
				lstrcpyW(pfpi->pszActivePath, szADir);
				pfpi->PassivePathShift += lstrlenW(pfpi->pszActivePath)*2;
			}
		}
	}

	pfpi->pszPassivePath = (WCHAR*)(((char*)pfpi)+pfpi->PassivePathShift);
	if (lbPOK)
	{
		pfpi->PassiveRect=PPInfo.PanelRect;
		if ((PPInfo.PanelType == PTYPE_FILEPANEL || PPInfo.PanelType == PTYPE_TREEPANEL) && PPInfo.Visible)
		{
			if (szPDir[0])
				lstrcpyW(pfpi->pszPassivePath, szPDir);
		}
	}

	// ����������, ��������� ����������
	//WriteFile(hPipe, &nStructSize, sizeof(nStructSize), &cout, NULL);
	//WriteFile(hPipe, pfpi, nStructSize, &cout, NULL);
	OutDataAlloc(nStructSize+4);
	OutDataWrite(&nStructSize, sizeof(nStructSize));
	OutDataWrite(pfpi, nStructSize);

	free(pfpi); pfpi=NULL;
}

void SetStartupInfoW789(void *aInfo)
{
	::InfoW789 = (PluginStartupInfo*)calloc(sizeof(PluginStartupInfo),1);
	::FSFW789 = (FarStandardFunctions*)calloc(sizeof(FarStandardFunctions),1);
	*::InfoW789 = *((struct PluginStartupInfo*)aInfo);
	*::FSFW789 = *((struct PluginStartupInfo*)aInfo)->FSF;
	::InfoW789->FSF = ::FSFW789;
	
	/*if (!FarHwnd)
		InitHWND((HWND)InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_GETFARHWND, 0));*/
}

extern int lastModifiedStateW;
// watch non-modified -> modified editor status change
int ProcessEditorInputW789(LPCVOID aRec)
{
	const INPUT_RECORD *Rec = (const INPUT_RECORD*)aRec;
	// only key events with virtual codes > 0 are likely to cause status change (?)
	if (Rec->EventType == KEY_EVENT && Rec->Event.KeyEvent.wVirtualKeyCode > 0  && Rec->Event.KeyEvent.bKeyDown)
	{
		EditorInfo ei;
		InfoW789->EditorControl(ECTL_GETINFO, &ei);
		int currentModifiedState = ei.CurState == ECSTATE_MODIFIED ? 1 : 0;
		InfoW789->EditorControl(ECTL_FREEINFO, &ei);
		if (lastModifiedStateW != currentModifiedState)
		{
			// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
			UpdateConEmuTabsW(0, false, false);
			lastModifiedStateW = currentModifiedState;
		}
	}
	return 0;
}

int ProcessEditorEventW789(int Event, void *Param)
{
	switch (Event)
	{
	case EE_CLOSE:
	case EE_GOTFOCUS:
	case EE_KILLFOCUS:
	case EE_SAVE:
	//case EE_READ: -- � ���� ������ ���������� ���� ��� �� ����������
		{
			// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
			UpdateConEmuTabsW(Event, Event == EE_KILLFOCUS, Event == EE_SAVE);
			break;
		}
	}
	return 0;
}

int ProcessViewerEventW789(int Event, void *Param)
{
	switch (Event)
	{
	case VE_CLOSE:
	case VE_READ:
	case VE_KILLFOCUS:
	case VE_GOTFOCUS:
		{
			// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
			UpdateConEmuTabsW(Event, Event == VE_KILLFOCUS, false);
		}
	}
	return 0;
}


void UpdateConEmuTabsW789(int event, bool losingFocus, bool editorSave)
{
    BOOL lbCh = FALSE;
	WindowInfo WInfo;

	int windowCount = (int)InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_GETWINDOWCOUNT, NULL);
	lbCh = (lastWindowCount != windowCount);
	
	if (!CreateTabs ( windowCount ))
		return;

	EditorInfo ei;
	if (editorSave)
	{
		InfoW789->EditorControl(ECTL_GETINFO, &ei);
	}

	int tabCount = 1;
	for (int i = 0; i < windowCount; i++)
	{
		WInfo.Pos = i;
		InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_GETWINDOWINFO, (void*)&WInfo);
		if (WInfo.Type == WTYPE_EDITOR || WInfo.Type == WTYPE_VIEWER || WInfo.Type == WTYPE_PANELS)
			lbCh |= AddTab(tabCount, losingFocus, editorSave, 
				WInfo.Type, WInfo.Name, editorSave ? ei.FileName : NULL, 
				WInfo.Current, WInfo.Modified);
		InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_FREEWINDOWINFO, (void*)&WInfo);
	}
	
	if (editorSave) 
		InfoW789->EditorControl(ECTL_FREEINFO, &ei);

#ifdef _DEBUG
	WCHAR szDbg[128]; wsprintfW(szDbg, L"Event: %i, count %i\n", event, tabCount);
	OutputDebugStringW(szDbg);
#endif

	SendTabs(tabCount);
}

void ExitFARW789(void)
{
	if (InfoW789) {
		free(InfoW789);
		InfoW789=NULL;
	}
	if (FSFW789) {
		free(FSFW789);
		FSFW789=NULL;
	}
}

int ShowMessage789(int aiMsg, int aiButtons)
{
	if (!InfoW789 || !InfoW789->Message)
		return -1;
	return InfoW789->Message(InfoW789->ModuleNumber, FMSG_ALLINONE, NULL, 
		(const wchar_t * const *)InfoW789->GetMsg(InfoW789->ModuleNumber,aiMsg), 0, aiButtons);
}

void ReloadMacro789()
{
	if (!InfoW789 || !InfoW789->AdvControl)
		return;

	ActlKeyMacro command;
	command.Command=MCMD_LOADALL;
	InfoW789->AdvControl(InfoW789->ModuleNumber,ACTL_KEYMACRO,&command);
}

void SetWindow789(int nTab)
{
	if (!InfoW789 || !InfoW789->AdvControl)
		return;

	if (InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_SETCURRENTWINDOW, (void*)nTab))
		InfoW789->AdvControl(InfoW789->ModuleNumber, ACTL_COMMIT, 0);
}