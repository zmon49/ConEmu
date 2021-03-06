﻿
/*
Copyright (c) 2009-2014 Maximus5
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define HIDE_USE_EXCEPTION_INFO

#define AssertCantActivate(x) //MBoxAssert(x)

#define SHOWDEBUGSTR
//#define ALLOWUSEFARSYNCHRO

#include "Header.h"
#include <Tlhelp32.h>
#include <ShlObj.h>

#include "../common/ConEmuCheck.h"
#include "../common/ConEmuPipeMode.h"
#include "../common/Execute.h"
#include "../common/RgnDetect.h"
#include "../common/SetEnvVar.h"
#include "ConEmu.h"
#include "ConEmuApp.h"
#include "ConEmuPipe.h"
#include "ConfirmDlg.h"
#include "Inside.h"
#include "Macro.h"
#include "Menu.h"
#include "RealBuffer.h"
#include "RealConsole.h"
#include "RunQueue.h"
#include "Status.h"
#include "TabBar.h"
#include "VConChild.h"
#include "VConGroup.h"
#include "VirtualConsole.h"

#define DEBUGSTRCMD(s) //DEBUGSTR(s)
#define DEBUGSTRDRAW(s) //DEBUGSTR(s)
#define DEBUGSTRINPUT(s) //DEBUGSTR(s)
#define DEBUGSTRWHEEL(s) //DEBUGSTR(s)
#define DEBUGSTRINPUTPIPE(s) //DEBUGSTR(s)
#define DEBUGSTRSIZE(s) //DEBUGSTR(s)
#define DEBUGSTRPROC(s) //DEBUGSTR(s)
#define DEBUGSTRPKT(s) //DEBUGSTR(s)
#define DEBUGSTRCON(s) //DEBUGSTR(s)
#define DEBUGSTRLANG(s) //DEBUGSTR(s)// ; Sleep(2000)
#define DEBUGSTRSENDMSG(s) //DEBUGSTR(s)
#define DEBUGSTRLOG(s) //OutputDebugStringA(s)
#define DEBUGSTRALIVE(s) //DEBUGSTR(s)
#define DEBUGSTRTABS(s) //DEBUGSTR(s)
#define DEBUGSTRMACRO(s) //DEBUGSTR(s)
#define DEBUGSTRALTSRV(s) //DEBUGSTR(s)
#define DEBUGSTRSTOP(s) //DEBUGSTR(s)
#define DEBUGSTRFOCUS(s) //LogFocusInfo(s)
#define DEBUGSTRGUICHILDPOS(s) //DEBUGSTR(s)
#define DEBUGSTRPROGRESS(s) DEBUGSTR(s)

// Иногда не отрисовывается диалог поиска полностью - только бежит текущая сканируемая директория.
// Иногда диалог отрисовался, но часть до текста "..." отсутствует

WARNING("В каждой VCon создать буфер BYTE[256] для хранения распознанных клавиш (Ctrl,...,Up,PgDn,Add,и пр.");
WARNING("Нераспознанные можно помещать в буфер {VKEY,wchar_t=0}, в котором заполнять последний wchar_t по WM_CHAR/WM_SYSCHAR");
WARNING("При WM_(SYS)CHAR помещать wchar_t в начало, в первый незанятый VKEY");
WARNING("При нераспознном WM_KEYUP - брать(и убрать) wchar_t из этого буфера, послав в консоль UP");
TODO("А периодически - проводить процерку isKeyDown, и чистить буфер");
WARNING("при переключении на другую консоль (да наверное и в процессе просто нажатий - модификатор может быть изменен в самой программе) требуется проверка caps, scroll, num");
WARNING("а перед пересылкой символа/клавиши проверять нажат ли на клавиатуре Ctrl/Shift/Alt");

WARNING("Часто после разблокирования компьютера размер консоли изменяется (OK), но обновленное содержимое консоли не приходит в GUI - там остется обрезанная верхняя и нижняя строка");


//Курсор, его положение, размер консоли, измененный текст, и пр...

#define VCURSORWIDTH 2
#define HCURSORHEIGHT 2

#define Free SafeFree
#define Alloc calloc

//#define Assert(V) if ((V)==FALSE) { wchar_t szAMsg[MAX_PATH*2]; _wsprintf(szAMsg, SKIPLEN(countof(szAMsg)) L"Assertion (%s) at\n%s:%i", _T(#V), _T(__FILE__), __LINE__); Box(szAMsg); }

#ifdef _DEBUG
#define HEAPVAL MCHKHEAP
#else
#define HEAPVAL
#endif

#ifdef _DEBUG
#define FORCE_INVALIDATE_TIMEOUT 999999999
#else
#define FORCE_INVALIDATE_TIMEOUT 300
#endif

#define CHECK_CONHWND_TIMEOUT 500

static BOOL gbInSendConEvent = FALSE;


wchar_t CRealConsole::ms_LastRConStatus[80] = {};

const wchar_t gsCloseGui[] = L"Confirm closing active child window?";
const wchar_t gsCloseCon[] = L"Confirm closing console?";
//const wchar_t gsCloseAny[] = L"Confirm closing console?";
const wchar_t gsCloseEditor[] = L"Confirm closing Far editor?";
const wchar_t gsCloseViewer[] = L"Confirm closing Far viewer?";

#define GUI_MACRO_PREFIX L'#'

#define ALL_MODIFIERS (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED|LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED|SHIFT_PRESSED)
#define CTRL_MODIFIERS (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED)

//static bool gbInTransparentAssert = false;

CRealConsole::CRealConsole()
{
}

bool CRealConsole::Construct(CVirtualConsole* apVCon, RConStartArgs *args)
{
	Assert(apVCon && args);
	MCHKHEAP;

	mp_VCon = apVCon;
	mp_Log = NULL;

	MCHKHEAP;
	SetConStatus(L"Initializing ConEmu (2)", true, true);
	//mp_VCon->mp_RCon = this;
	HWND hView = apVCon->GetView();
	if (!hView)
	{
		_ASSERTE(hView!=NULL);
	}
	else
	{
		PostMessage(apVCon->GetView(), WM_SETCURSOR, -1, -1);
	}

	mb_MonitorAssertTrap = false;

	//mp_Rgn = new CRgnDetect();
	//mn_LastRgnFlags = -1;
	m_ConsoleKeyShortcuts = 0;
	memset(Title,0,sizeof(Title)); memset(TitleCmp,0,sizeof(TitleCmp));
	mn_tabsCount = 0;
	ms_PanelTitle[0] = 0;
	mn_ActiveTab = 0;
	mn_MaxTabs = 20;
	mb_TabsWasChanged = false;
	mp_tabs = (ConEmuTab*)Alloc(mn_MaxTabs, sizeof(ConEmuTab));
	_ASSERTE(mp_tabs!=NULL);
	
	lstrcpyn(ms_RenameFirstTab, args->pszRenameTab ? args->pszRenameTab : L"", countof(ms_RenameFirstTab));

	//memset(&m_PacketQueue, 0, sizeof(m_PacketQueue));
	mn_FlushIn = mn_FlushOut = 0;
	mb_MouseButtonDown = FALSE;
	mb_BtnClicked = FALSE; mrc_BtnClickPos = MakeCoord(-1,-1);
	mcr_LastMouseEventPos = MakeCoord(-1,-1);
	mb_MouseTapChanged = FALSE;
	mcr_MouseTapReal = mcr_MouseTapChanged = MakeCoord(-1,-1);
	//m_DetectedDialogs.Count = 0;
	//mn_DetectCallCount = 0;
	wcscpy_c(Title, gpConEmu->GetDefaultTitle());
	wcscpy_c(TitleFull, Title);
	TitleAdmin[0] = 0;
	wcscpy_c(ms_PanelTitle, Title);
	mb_ForceTitleChanged = FALSE;
	ZeroStruct(m_Progress);
	m_Progress.Progress = m_Progress.PreWarningProgress = m_Progress.LastShownProgress = -1; // Процентов нет
	m_Progress.ConsoleProgress = m_Progress.LastConsoleProgress = -1;
	hPictureView = NULL; mb_PicViewWasHidden = FALSE;
	mh_MonitorThread = NULL; mn_MonitorThreadID = 0; mb_WasForceTerminated = FALSE;
	mh_PostMacroThread = NULL; mn_PostMacroThreadID = 0;
	//mh_InputThread = NULL; mn_InputThreadID = 0;
	mp_sei = NULL;
	mn_MainSrv_PID = mn_ConHost_PID = 0; mh_MainSrv = NULL; mb_MainSrv_Ready = false;
	mn_CheckFreqLock = 0;
	mp_ConHostSearch = NULL;
	mn_ActiveLayout = 0;
	mn_AltSrv_PID = 0;  //mh_AltSrv = NULL;
	mb_SwitchActiveServer = false;
	mh_SwitchActiveServer = CreateEvent(NULL,FALSE,FALSE,NULL);
	mh_ActiveServerSwitched = CreateEvent(NULL,FALSE,FALSE,NULL);
	mh_ConInputPipe = NULL;
	mb_UseOnlyPipeInput = FALSE;
	//mh_CreateRootEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	mb_InCreateRoot = FALSE;
	mb_NeedStartProcess = FALSE; mb_IgnoreCmdStop = FALSE;
	ms_MainSrv_Pipe[0] = 0; ms_ConEmuC_Pipe[0] = 0; ms_ConEmuCInput_Pipe[0] = 0; ms_VConServer_Pipe[0] = 0;
	mn_TermEventTick = 0;
	mh_TermEvent = CreateEvent(NULL,TRUE/*MANUAL - используется в нескольких нитях!*/,FALSE,NULL); ResetEvent(mh_TermEvent);
	mh_StartExecuted = CreateEvent(NULL,FALSE,FALSE,NULL); ResetEvent(mh_StartExecuted);
	mb_StartResult = mb_WaitingRootStartup = FALSE;
	mh_MonitorThreadEvent = CreateEvent(NULL,TRUE,FALSE,NULL); //2009-09-09 Поставил Manual. Нужно для определения, что можно убирать флаг Detached
	mn_ServerActiveTick1 = mn_ServerActiveTick2 = 0;
	mh_UpdateServerActiveEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
	//mb_UpdateServerActive = FALSE;
	mh_ApplyFinished = CreateEvent(NULL,TRUE,FALSE,NULL);
	//mh_EndUpdateEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
	//WARNING("mh_Sync2WindowEvent убрать");
	//mh_Sync2WindowEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
	//mh_ConChanged = CreateEvent(NULL,FALSE,FALSE,NULL);
	//mh_PacketArrived = CreateEvent(NULL,FALSE,FALSE,NULL);
	//mh_CursorChanged = NULL;
	//mb_Detached = FALSE;
	//m_Args.pszSpecialCmd = NULL; -- не требуется
	//mpsz_CmdBuffer = NULL;
	mb_FullRetrieveNeeded = FALSE;
	//mb_AdminShieldChecked = FALSE;
	ZeroStruct(m_LastMouse);
	ZeroStruct(m_LastMouseGuiPos);
	mb_DataChanged = FALSE;
	mb_RConStartedSuccess = FALSE;
	ms_LogShellActivity[0] = 0; mb_ShellActivityLogged = false;
	mn_ProgramStatus = 0; mn_FarStatus = 0; mn_Comspec4Ntvdm = 0;
	isShowConsole = gpSet->isConVisible;
	//mb_ConsoleSelectMode = false;
	mn_SelectModeSkipVk = 0;
	mn_ProcessCount = mn_ProcessClientCount = 0;
	mn_FarPID = mn_ActivePID = 0; //mn_FarInputTID = 0;
	mn_LastProcessNamePID = 0; ms_LastProcessName[0] = 0; mn_LastAppSettingsId = -1;
	memset(m_FarPlugPIDs, 0, sizeof(m_FarPlugPIDs)); mn_FarPlugPIDsCount = 0;
	memset(m_TerminatedPIDs, 0, sizeof(m_TerminatedPIDs)); mn_TerminatedIdx = 0;
	mb_SkipFarPidChange = FALSE;
	mn_InRecreate = 0; mb_ProcessRestarted = FALSE; mb_InCloseConsole = FALSE;
	CloseConfirmReset();
	mb_WasSendClickToReadCon = false;
	mn_LastSetForegroundPID = 0;
	mb_InPostCloseMacro = false;

	mn_TextColorIdx = 7; mn_BackColorIdx = 0;
	mn_PopTextColorIdx = 5; mn_PopBackColorIdx = 15;
	
	m_RConServer.Init(this);

	//mb_ThawRefreshThread = FALSE;
	mn_LastUpdateServerActive = 0;
	
	//mb_BuferModeChangeLocked = FALSE;
	
	mn_DefaultBufferHeight = gpSetCls->bForceBufferHeight ? gpSetCls->nForceBufferHeight : gpSet->DefaultBufferHeight;
	
	mp_RBuf = new CRealBuffer(this);
	_ASSERTE(mp_RBuf!=NULL);
	mp_EBuf = NULL;
	mp_SBuf = NULL;
	SetActiveBuffer(mp_RBuf, false);
	mb_ABufChaged = false;
	
	mn_LastInactiveRgnCheck = 0;
	#ifdef _DEBUG
	mb_DebugLocked = FALSE;
	#endif
	
	ZeroStruct(m_ServerClosing);
	ZeroStruct(m_Args);
	ms_RootProcessName[0] = 0;
	mn_RootProcessIcon = -1;
	mn_LastInvalidateTick = 0;

	hConWnd = NULL;
	hGuiWnd = mh_GuiWndFocusStore = NULL;
	mb_GuiExternMode = FALSE;
	mb_GuiForceConView = false;
	mn_GuiAttachInputTID = 0;
	mn_GuiWndStyle = mn_GuiWndStylEx = 0; mn_GuiAttachFlags = 0;
	ZeroStruct(mrc_LastGuiWnd);
	setGuiWndPID(0, NULL); // set mn_GuiWndPID to 0
	mb_InGuiAttaching = FALSE;
	mn_InPostDeadChar = 0;

	mb_InSetFocus = FALSE;
	rcPreGuiWndRect = MakeRect(0,0);
	//hFileMapping = NULL; pConsoleData = NULL;
	mn_Focused = -1;
	mn_LastVKeyPressed = 0;
	//mh_LogInput = NULL; mpsz_LogInputFile = NULL; //mpsz_LogPackets = NULL; mn_LogPackets = 0;
	//mh_FileMapping = mh_FileMappingData = mh_FarFileMapping =
	//mh_FarAliveEvent = NULL;
	//mp_ConsoleInfo = NULL;
	//mp_ConsoleData = NULL;
	//mp_FarInfo = NULL;
	mn_LastConsoleDataIdx = mn_LastConsolePacketIdx = /*mn_LastFarReadIdx =*/ -1;
	mn_LastFarReadTick = 0;
	//ms_HeaderMapName[0] = ms_DataMapName[0] = 0;
	//mh_ColorMapping = NULL;
	//mp_ColorHdr = NULL;
	//mp_ColorData = NULL;
	mn_LastColorFarID = 0;
	//ms_ConEmuC_DataReady[0] = 0; mh_ConEmuC_DataReady = NULL;
	m_UseLogs = gpSetCls->isAdvLogging;

	mp_TrueColorerData = NULL;
	memset(&m_TrueColorerHeader, 0, sizeof(m_TrueColorerHeader));

	//mb_PluginDetected = FALSE;
	mn_FarPID_PluginDetected = 0;
	memset(&m_FarInfo, 0, sizeof(m_FarInfo));
	lstrcpy(ms_Editor, L"edit ");
	lstrcpy(ms_EditorRus, L"редактирование ");
	lstrcpy(ms_Viewer, L"view ");
	lstrcpy(ms_ViewerRus, L"просмотр ");
	lstrcpy(ms_TempPanel, L"{Temporary panel");
	lstrcpy(ms_TempPanelRus, L"{Временная панель");
	//lstrcpy(ms_NameTitle, L"Name");

	PreInit(); // просто инициализировать переменные размеров...

	if (!PreCreate(args))
		return false;

	mb_WasStartDetached = m_Args.bDetached;
	_ASSERTE(mb_WasStartDetached == args->bDetached);

	// -- т.к. автопоказ табов может вызвать ресайз - то табы в самом конце инициализации!
	_ASSERTE(gpConEmu->isMainThread()); // Иначе табы сразу не перетряхнутся
	SetTabs(NULL,1); // Для начала - показывать вкладку Console, а там ФАР разберется
	MCHKHEAP;

	if (mb_NeedStartProcess)
	{
		// Push request to "startup queue"
		mb_WaitingRootStartup = TRUE;
		gpConEmu->mp_RunQueue->RequestRConStartup(this);
	}

	return true;
}

CRealConsole::~CRealConsole()
{
	MCHKHEAP;
	DEBUGSTRCON(L"CRealConsole::~CRealConsole()\n");

	if (!gpConEmu->isMainThread())
	{
		//_ASSERTE(gpConEmu->isMainThread());
		MBoxA(L"~CRealConsole() called from background thread");
	}

	if (gbInSendConEvent)
	{
#ifdef _DEBUG
		_ASSERTE(gbInSendConEvent==FALSE);
#endif
		Sleep(100);
	}

	StopThread();
	MCHKHEAP

	if (mp_RBuf)
		{ delete mp_RBuf; mp_RBuf = NULL; }
	if (mp_EBuf)
		{ delete mp_EBuf; mp_EBuf = NULL; }
	if (mp_SBuf)
		{ delete mp_SBuf; mp_SBuf = NULL; }
	mp_ABuf = NULL;

	SafeCloseHandle(mh_StartExecuted);

	SafeCloseHandle(mh_MainSrv); mn_MainSrv_PID = mn_ConHost_PID = 0; mb_MainSrv_Ready = false;
	/*SafeCloseHandle(mh_AltSrv);*/  mn_AltSrv_PID = 0;
	SafeCloseHandle(mh_SwitchActiveServer); mb_SwitchActiveServer = false;
	SafeCloseHandle(mh_ActiveServerSwitched);
	SafeCloseHandle(mh_ConInputPipe);
	m_ConDataChanged.Close();
	if (mp_ConHostSearch)
	{
		mp_ConHostSearch->Release();
		SafeFree(mp_ConHostSearch);
	}


	if (mp_tabs) Free(mp_tabs);
	mp_tabs = NULL;
	mn_MaxTabs = 0;
	
	mn_ActiveTab = 0;
	mn_tabsCount = 0;
	
	//
	CloseLogFiles();

	if (mp_sei)
	{
		SafeCloseHandle(mp_sei->hProcess);
		SafeFree(mp_sei);
	}

	m_RConServer.Stop(true);

	//CloseMapping();
	CloseMapHeader(); // CloseMapData() & CloseFarMapData() зовет сам CloseMapHeader
	CloseColorMapping(); // Colorer data

	//SafeFree(mpsz_CmdBuffer);

	//if (mp_Rgn)
	//{
	//	delete mp_Rgn;
	//	mp_Rgn = NULL;
	//}
	MCHKHEAP;
}

CVirtualConsole* CRealConsole::VCon()
{
	return this ? mp_VCon : NULL;
}

bool CRealConsole::PreCreate(RConStartArgs *args)
{
	_ASSERTE(args!=NULL);

	// В этот момент логи данной консоли еще не созданы, пишем в GUI-шный
	if (gpSetCls->isAdvLogging)
	{
		wchar_t szPrefix[128];
		_wsprintf(szPrefix, SKIPLEN(countof(szPrefix)) L"CRealConsole::PreCreate, hView=x%08X, hBack=x%08X, Detached=%u, AsAdmin=%u, Cmd=",
			(DWORD)(DWORD_PTR)mp_VCon->GetView(), (DWORD)(DWORD_PTR)mp_VCon->GetBack(),
			args->bDetached, args->bRunAsAdministrator);
		wchar_t* pszInfo = lstrmerge(szPrefix, args->pszSpecialCmd ? args->pszSpecialCmd : L"<NULL>");
		gpConEmu->LogString(pszInfo ? pszInfo : szPrefix);
		SafeFree(pszInfo);
	}

	bool bCopied = m_Args.AssignFrom(args);

	// Don't leave security information (passwords) in memory
	if (bCopied && args->pszUserName)
	{
		_ASSERTE(*args->pszUserName);

		SecureZeroMemory(args->szUserPassword, sizeof(args->szUserPassword));

		// When User name was set, but password - Not...
		if (!*m_Args.szUserPassword && !m_Args.bUseEmptyPassword)
		{
			int nRc = gpConEmu->RecreateDlg(&m_Args);

			if (nRc != IDC_START)
				bCopied = false;
		}
	}

	if (!bCopied)
		return false;

	// 111211 - здесь может быть передан "-new_console:..."
	if (m_Args.pszSpecialCmd)
	{
		// Должен быть обработан в вызывающей функции (CVirtualConsole::CreateVCon?)
		_ASSERTE(wcsstr(args->pszSpecialCmd, L"-new_console")==NULL);
		m_Args.ProcessNewConArg();
	}
	else
	{
		_ASSERTE((args->bDetached || (args->pszSpecialCmd && *args->pszSpecialCmd)) && "Command line must be specified already!");
	}

	mb_NeedStartProcess = FALSE;
	
	// Если надо - подготовить портабельный реестр
	if (gpConEmu->mb_PortableRegExist)
	{
		// Если реестр обломался, или юзер сказал "не продолжать"
		if (!gpConEmu->PreparePortableReg())
		{
			return false;
		}
	}


	// Go
	if (m_Args.bBufHeight)
	{
		mn_DefaultBufferHeight = m_Args.nBufHeight;
		mp_RBuf->SetBufferHeightMode(mn_DefaultBufferHeight>0);
	}


	BYTE nTextColorIdx /*= 7*/, nBackColorIdx /*= 0*/, nPopTextColorIdx /*= 5*/, nPopBackColorIdx /*= 15*/;
	PrepareDefaultColors(nTextColorIdx, nBackColorIdx, nPopTextColorIdx, nPopBackColorIdx);


	if (args->bDetached)
	{
		// Пока ничего не делаем - просто создается серверная нить
		if (!PreInit())  //TODO: вообще-то PreInit() уже наверное вызван...
		{
			return false;
		}

		m_Args.bDetached = TRUE;
	}
	else
	{
		mb_NeedStartProcess = TRUE;
	}

	if (mp_RBuf)
	{
		mp_RBuf->PreFillBuffers();
	}

	if (!StartMonitorThread())
	{
		return false;
	}
	
	// В фоновой вкладке?
	args->bBackgroundTab = m_Args.bBackgroundTab;

	return true;
}

RealBufferType CRealConsole::GetActiveBufferType()
{
	if (!this || !mp_ABuf)
		return rbt_Undefined;
	return mp_ABuf->m_Type;
}

void CRealConsole::DumpConsole(HANDLE ahFile)
{
	_ASSERTE(mp_ABuf!=NULL);
	
	return mp_ABuf->DumpConsole(ahFile);
}

bool CRealConsole::LoadDumpConsole(LPCWSTR asDumpFile)
{
	if (!this)
		return false;
	
	if (!mp_SBuf)
	{
		mp_SBuf = new CRealBuffer(this, rbt_DumpScreen);
		if (!mp_SBuf)
		{
			_ASSERTE(mp_SBuf!=NULL);
			return false;
		}
	}
	
	if (!mp_SBuf->LoadDumpConsole(asDumpFile))
	{
		SetActiveBuffer(mp_RBuf);
		return false;
	}
	
	SetActiveBuffer(mp_SBuf);
	
	return true;
}

bool CRealConsole::LoadAlternativeConsole(LoadAltMode iMode /*= lam_Default*/)
{
	if (!this)
		return false;
	
	if (!mp_SBuf)
	{
		mp_SBuf = new CRealBuffer(this, rbt_Alternative);
		if (!mp_SBuf)
		{
			_ASSERTE(mp_SBuf!=NULL);
			return false;
		}
	}
	
	if (!mp_SBuf->LoadAlternativeConsole(iMode))
	{
		SetActiveBuffer(mp_RBuf);
		return false;
	}
	
	SetActiveBuffer(mp_SBuf);
	
	return true;
}

bool CRealConsole::SetActiveBuffer(RealBufferType aBufferType)
{
	if (!this)
		return false;

	bool lbRc;
	switch (aBufferType)
	{
	case rbt_Primary:
		lbRc = SetActiveBuffer(mp_RBuf);
		break;

	case rbt_Alternative:
		if (GetActiveBufferType() != rbt_Primary)
		{
			lbRc = true; // уже НЕ основной буфер. Не меняем
			break;
		}

		lbRc = LoadAlternativeConsole();
		break;

	default:
		// Другие тут пока не поддерживаются
		_ASSERTE(aBufferType==rbt_Primary);
		lbRc = false;
	}

	//mp_VCon->Invalidate();

	return lbRc;
}

bool CRealConsole::SetActiveBuffer(CRealBuffer* aBuffer, bool abTouchMonitorEvent /*= true*/)
{
	if (!this)
		return false;
	
	if (!aBuffer || (aBuffer != mp_RBuf && aBuffer != mp_EBuf && aBuffer != mp_SBuf))
	{
		_ASSERTE(aBuffer && (aBuffer == mp_RBuf || aBuffer == mp_EBuf || aBuffer == mp_SBuf));
		return false;
	}

	CRealBuffer* pOldBuffer = mp_ABuf;
	
	mp_ABuf = aBuffer;
	mb_ABufChaged = true;
	
	if (isActive())
	{
		// Обновить на тулбаре статусы Scrolling(BufferHeight) & Alternative
		OnBufferHeight();
	}

	if (mh_MonitorThreadEvent && abTouchMonitorEvent)
	{
		// Передернуть нить MonitorThread
		SetMonitorThreadEvent();
	}

	if (pOldBuffer && (pOldBuffer == mp_SBuf))
	{
		mp_SBuf->ReleaseMem();
	}

	return true;
}

BOOL CRealConsole::SetConsoleSize(USHORT sizeX, USHORT sizeY, USHORT sizeBuffer, DWORD anCmdID/*=CECMD_SETSIZESYNC*/)
{
	if (!this) return FALSE;
	
	// Всегда меняем _реальный_ буфер консоли.
	return mp_RBuf->SetConsoleSize(sizeX, sizeY, sizeBuffer, anCmdID);
}

void CRealConsole::SyncGui2Window(const RECT rcVConBack)
{
	if (!this)
		return;

	if (hGuiWnd && !mb_GuiExternMode)
	{
		if (!IsWindow(hGuiWnd))
		{
			// Странно это, по идее, приложение при закрытии окна должно было сообщить,
			// что оно закрылось, hGuiWnd больше не валиден...
			_ASSERTE(IsWindow(hGuiWnd));
			hGuiWnd = mh_GuiWndFocusStore = NULL;
			return;
		}

		#ifdef _DEBUG
		RECT rcTemp = gpConEmu->CalcRect(CER_BACK, mp_VCon);
		#endif
		// Окошко гуевого приложения нужно разместить поверх области, отведенной под наш VCon.
		// Но! тут нужна вся область, без отрезания прокруток и округлений размеров под знакоместо
		RECT rcGui = rcVConBack;
		OffsetRect(&rcGui, -rcGui.left, -rcGui.top);

		DWORD dwExStyle = GetWindowLong(hGuiWnd, GWL_EXSTYLE);
		DWORD dwStyle = GetWindowLong(hGuiWnd, GWL_STYLE);
		CorrectGuiChildRect(dwStyle, dwExStyle, rcGui);
		RECT rcCur = {};
		GetWindowRect(hGuiWnd, &rcCur);
		HWND hBack = mp_VCon->GetBack();
		MapWindowPoints(NULL, hBack, (LPPOINT)&rcCur, 2);
		if (memcmp(&rcCur, &rcGui, sizeof(RECT)) != 0)
		{
			// Через команду пайпа, а то если он "под админом" будет Access denied
			SetOtherWindowPos(hGuiWnd, HWND_TOP, rcGui.left,rcGui.top, rcGui.right-rcGui.left, rcGui.bottom-rcGui.top,
				SWP_ASYNCWINDOWPOS|SWP_NOACTIVATE);
		}
		// Запомнить ЭКРАННЫЕ координаты, куда поставили окошко
		MapWindowPoints(hBack, NULL, (LPPOINT)&rcGui, 2);
		StoreGuiChildRect(&rcGui);
	}
}

// Изменить размер консоли по размеру окна (главного)
// prcNewWnd передается из CConEmuMain::OnSizing(WPARAM wParam, LPARAM lParam)
// для опережающего ресайза консоли (во избежание мелькания отрисовки панелей)
void CRealConsole::SyncConsole2Window(BOOL abNtvdmOff/*=FALSE*/, LPRECT prcNewWnd/*=NULL*/)
{
	if (!this)
		return;

	//2009-06-17 Попробуем так. Вроде быстрее и наверное ничего блокироваться не должно
	/*
	if (GetCurrentThreadId() != mn_MonitorThreadID) {
	    RECT rcClient; Get ClientRect(ghWnd, &rcClient);
	    _ASSERTE(rcClient.right>250 && rcClient.bottom>200);

	    // Посчитать нужный размер консоли
	    RECT newCon = gpConEmu->CalcRect(CER_CONSOLE, rcClient, CER_MAINCLIENT);

	    if (newCon.right==TextWidth() && newCon.bottom==TextHeight())
	        return; // размеры не менялись

	    SetEvent(mh_Sync2WindowEvent);
	    return;
	}
	*/
	DEBUGLOGFILE("CRealConsole::SyncConsole2Window\n");
	RECT rcClient;

	if (prcNewWnd == NULL)
	{
		WARNING("DoubleView: переделать для...");
		rcClient = gpConEmu->GetGuiClientRect();
	}
	else
	{
		rcClient = gpConEmu->CalcRect(CER_MAINCLIENT, *prcNewWnd, CER_MAIN);
	}

	//_ASSERTE(rcClient.right>140 && rcClient.bottom>100);
	// Посчитать нужный размер консоли
	gpConEmu->AutoSizeFont(rcClient, CER_MAINCLIENT);
	RECT newCon = gpConEmu->CalcRect(abNtvdmOff ? CER_CONSOLE_NTVDMOFF : CER_CONSOLE_CUR, rcClient, CER_MAINCLIENT, mp_VCon);
	_ASSERTE(newCon.right>=MIN_CON_WIDTH && newCon.bottom>=MIN_CON_HEIGHT);
	
	#if 0
	if (hGuiWnd && !mb_GuiExternMode)
	{
		_ASSERTE(FALSE && "Fails on DoubleView?");
		SyncGui2Window(rcClient);
	}
	#endif

	// Меняем активный буфер (пусть даже и виртуальный...)
	mp_ABuf->SyncConsole2Window(newCon.right, newCon.bottom);
}

// Вызывается при аттаче (после детача), или после RunAs?
// sbi передавать не ссылкой, а копией, ибо та же память
BOOL CRealConsole::AttachConemuC(HWND ahConWnd, DWORD anConemuC_PID, const CESERVER_REQ_STARTSTOP* rStartStop, CESERVER_REQ_STARTSTOPRET* pRet)
{
	DWORD dwErr = 0;
	HANDLE hProcess = NULL;
	_ASSERTE(pRet!=NULL);
	DEBUGTEST(bool bAdmStateChanged = (rStartStop->bUserIsAdmin != m_Args.bRunAsAdministrator));
	m_Args.bRunAsAdministrator = rStartStop->bUserIsAdmin;

	// Процесс запущен через ShellExecuteEx под другим пользователем (Administrator)
	if (mp_sei)
	{
		// Issue 791: Console server fails to duplicate self Process handle to GUI
		// _ASSERTE(mp_sei->hProcess==NULL);

		if (!rStartStop->hServerProcessHandle)
		{
			if (mp_sei->hProcess)
			{
				hProcess = mp_sei->hProcess;
			}
			else
			{
				_ASSERTE(rStartStop->hServerProcessHandle!=0);
				_ASSERTE(mp_sei->hProcess!=NULL);
				DisplayLastError(L"Server process handle was not recieved!", -1);
			}
		}
		else
		{
			//_ASSERTE(FALSE && "Continue to AttachConEmuC");

			// Пока не все хорошо.
			// --Переделали. Запуск идет через GUI, чтобы окошко не мелькало.
			// --mp_sei->hProcess не заполняется
			HANDLE hRecv = (HANDLE)(DWORD_PTR)rStartStop->hServerProcessHandle;
			DWORD nWait = WaitForSingleObject(hRecv, 0);

			#ifdef _DEBUG
			DWORD nSrvPID = 0;
			typedef DWORD (WINAPI* GetProcessId_t)(HANDLE);
			GetProcessId_t getProcessId = (GetProcessId_t)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetProcessId");
			if (getProcessId)
				nSrvPID = getProcessId(hRecv);
			#endif

			if (nWait != WAIT_TIMEOUT)
			{
				DisplayLastError(L"Invalid server process handle recieved!");
			}
		}
	}

	if (rStartStop->sCmdLine && *rStartStop->sCmdLine)
	{
		SafeFree(m_Args.pszSpecialCmd);
		_ASSERTE(m_Args.bDetached == TRUE);
		m_Args.pszSpecialCmd = lstrdup(rStartStop->sCmdLine);
	}

	_ASSERTE(hProcess==NULL || (mp_sei && mp_sei->hProcess));
	_ASSERTE(rStartStop->hServerProcessHandle!=NULL || mh_MainSrv!=NULL);

	if (rStartStop->hServerProcessHandle && (hProcess != (HANDLE)(DWORD_PTR)rStartStop->hServerProcessHandle))
	{
		if (!hProcess)
		{
			hProcess = (HANDLE)(DWORD_PTR)rStartStop->hServerProcessHandle;
		}
		else
		{
			CloseHandle((HANDLE)(DWORD_PTR)rStartStop->hServerProcessHandle);
		}
	}

	if (mp_sei && mp_sei->hProcess)
	{
		_ASSERTE(mp_sei->hProcess == hProcess || hProcess == (HANDLE)(DWORD_PTR)rStartStop->hServerProcessHandle);
		mp_sei->hProcess = NULL;
	}

	// Иначе - отркрываем как обычно
	if (!hProcess)
	{
		hProcess = OpenProcess(MY_PROCESS_ALL_ACCESS, FALSE, anConemuC_PID);
		if (!hProcess || hProcess == INVALID_HANDLE_VALUE)
		{
			hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|SYNCHRONIZE, FALSE, anConemuC_PID);
		}
	}

	if (!hProcess)
	{
		DisplayLastError(L"Can't open ConEmuC process! Attach is impossible!", dwErr = GetLastError());
		return FALSE;
	}

	WARNING("TODO: Support horizontal scroll");

	//2010-03-03 переделано для аттача через пайп
	CONSOLE_SCREEN_BUFFER_INFO lsbi = rStartStop->sbi;
	// Remove bRootIsCmdExe&isScroll() from expression. Use REAL scrolls from REAL console
	BOOL bCurBufHeight = /*rStartStop->bRootIsCmdExe || mp_RBuf->isScroll() ||*/ mp_RBuf->BufferHeightTurnedOn(&lsbi);

	// Смотрим реальный буфер - изменилось ли наличие прокрутки?
	if (mp_RBuf->isScroll() != bCurBufHeight)
	{
		_ASSERTE(mp_RBuf->isBuferModeChangeLocked()==FALSE);
		mp_RBuf->SetBufferHeightMode(bCurBufHeight, FALSE);
	}

	RECT rcWnd = gpConEmu->GetGuiClientRect();
	TODO("DoubleView: ?");
	gpConEmu->AutoSizeFont(rcWnd, CER_MAINCLIENT);
	RECT rcCon = gpConEmu->CalcRect(CER_CONSOLE_CUR, rcWnd, CER_MAINCLIENT);
	// Скорректировать sbi на новый, который БУДЕТ установлен после отработки сервером аттача
	lsbi.dwSize.X = rcCon.right;
	lsbi.srWindow.Left = 0; lsbi.srWindow.Right = rcCon.right-1;

	if (bCurBufHeight)
	{
		// sbi.dwSize.Y не трогаем
		lsbi.srWindow.Bottom = lsbi.srWindow.Top + rcCon.bottom - 1;
	}
	else
	{
		lsbi.dwSize.Y = rcCon.bottom;
		lsbi.srWindow.Top = 0; lsbi.srWindow.Bottom = rcCon.bottom - 1;
	}

	mp_RBuf->InitSBI(&lsbi);
	
	//// Событие "изменения" консоли //2009-05-14 Теперь события обрабатываются в GUI, но прийти из консоли может изменение размера курсора
	//swprintf_c(ms_ConEmuC_Pipe, CE_CURSORUPDATE, mn_MainSrv_PID);
	//mh_CursorChanged = CreateEvent ( NULL, FALSE, FALSE, ms_ConEmuC_Pipe );
	//if (!mh_CursorChanged) {
	//    ms_ConEmuC_Pipe[0] = 0;
	//    DisplayLastError(L"Can't create event!");
	//    return FALSE;
	//}

	//mh_MainSrv = hProcess;
	//mn_MainSrv_PID = anConemuC_PID;
	SetMainSrvPID(anConemuC_PID, hProcess);

	SetHwnd(ahConWnd);
	ProcessUpdate(&anConemuC_PID, 1);

	CreateLogFiles();
	// Инициализировать имена пайпов, событий, мэппингов и т.п.
	InitNames();
	//// Имя пайпа для управления ConEmuC
	//swprintf_c(ms_ConEmuC_Pipe, CESERVERPIPENAME, L".", mn_MainSrv_PID);
	//swprintf_c(ms_ConEmuCInput_Pipe, CESERVERINPUTNAME, L".", mn_MainSrv_PID);
	//MCHKHEAP
	// Открыть map с данными, он уже должен быть создан
	OpenMapHeader(TRUE);
	//SetConsoleSize(MakeCoord(TextWidth,TextHeight));
	// Командой - низя, т.к. сервер сейчас только что запустился и ждет GUI
	//SetConsoleSize(rcCon.right,rcCon.bottom);
	pRet->bWasBufferHeight = bCurBufHeight;
	pRet->hWnd = ghWnd;
	pRet->hWndDc = mp_VCon->GetView();
	pRet->hWndBack = mp_VCon->GetBack();
	pRet->dwPID = GetCurrentProcessId();
	pRet->nBufferHeight = bCurBufHeight ? lsbi.dwSize.Y : 0;
	pRet->nWidth = rcCon.right;
	pRet->nHeight = rcCon.bottom;
	pRet->dwMainSrvPID = anConemuC_PID;
	pRet->dwAltSrvPID = 0;
	pRet->bNeedLangChange = TRUE;
	TODO("Проверить на x64, не будет ли проблем с 0xFFFFFFFFFFFFFFFFFFFFF");
	pRet->NewConsoleLang = gpConEmu->GetActiveKeyboardLayout();
	// Установить шрифт для консоли
	pRet->Font.cbSize = sizeof(pRet->Font);
	pRet->Font.inSizeY = gpSet->ConsoleFont.lfHeight;
	pRet->Font.inSizeX = gpSet->ConsoleFont.lfWidth;
	lstrcpy(pRet->Font.sFontName, gpSet->ConsoleFont.lfFaceName);
	// Передернуть нить MonitorThread
	SetMonitorThreadEvent();

	_ASSERTE((pRet->nBufferHeight == 0) || ((int)pRet->nBufferHeight > (rStartStop->sbi.srWindow.Bottom-rStartStop->sbi.srWindow.Top)));

	return TRUE;
}

#if 0
//120714 - аналогичные параметры работают в ConEmuC.exe, а в GUI они и не работали. убрал пока
BOOL CRealConsole::AttachPID(DWORD dwPID)
{
	TODO("AttachPID пока работать не будет");
	return FALSE;
#ifdef ALLOW_ATTACHPID
	#ifdef MSGLOGGER
	TCHAR szMsg[100]; _wsprintf(szMsg, countof(szMsg), _T("Attach to process %i"), (int)dwPID);
	DEBUGSTRPROC(szMsg);
	#endif

	BOOL lbRc = AttachConsole(dwPID);

	if (!lbRc)
	{
		DEBUGSTRPROC(_T(" - failed\n"));
		BOOL lbFailed = TRUE;
		DWORD dwErr = GetLastError();

		if (/*dwErr==0x1F || dwErr==6 &&*/ dwPID == -1)
		{
			// Если ConEmu запускается из FAR'а батником - то родительский процесс - CMD.EXE/TCC.EXE, а он уже скорее всего закрыт. то есть подцепиться не удастся
			HWND hConsole = FindWindowEx(NULL,NULL,RealConsoleClass,NULL);
			if (!hConsole)
				hConsole = FindWindowEx(NULL,NULL,WineConsoleClass,NULL);

			if (hConsole && IsWindowVisible(hConsole))
			{
				DWORD dwCurPID = 0;

				if (GetWindowThreadProcessId(hConsole,  &dwCurPID))
				{
					// PROCESS_ALL_ACCESS may fails on WinXP!
					HANDLE hProcess = OpenProcess((STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0xFFF),FALSE,dwCurPID);
					dwErr = GetLastError();

					if (AttachConsole(dwCurPID))
						lbFailed = FALSE;
					else
						dwErr = GetLastError();
				}
			}
		}

		if (lbFailed)
		{
			TCHAR szErr[255];
			_wsprintf(szErr, countof(szErr), _T("AttachConsole failed (PID=%i)!"), dwPID);
			DisplayLastError(szErr, dwErr);
			return FALSE;
		}
	}

	DEBUGSTRPROC(_T(" - OK"));
	TODO("InitHandler в GUI наверное уже и не нужен...");
	//InitHandlers(FALSE);
	// Попытаться дернуть плагин для установки шрифта.
	CConEmuPipe pipe;

	//DEBUGSTRPROC(_T("CheckProcesses\n"));
	//gpConEmu->CheckProcesses(0,TRUE);

	if (pipe.Init(_T("DefFont.in.attach"), TRUE))
		pipe.Execute(CMD_DEFFONT);

	return TRUE;
#endif
}
#endif

//BOOL CRealConsole::FlushInputQueue(DWORD nTimeout /*= 500*/)
//{
//	if (!this) return FALSE;
//
//	if (nTimeout > 1000) nTimeout = 1000;
//	DWORD dwStartTick = GetTickCount();
//
//	mn_FlushOut = mn_FlushIn;
//	mn_FlushIn++;
//
//	_ASSERTE(mn_ConEmuC_Input_TID!=0);
//
//	TODO("Активной может быть нить ввода плагина фара а не сервера!");
//
//	//TODO("Преверка зависания нити и ее перезапуск");
//	PostThreadMessage(mn_ConEmuC_Input_TID, INPUT_THREAD_ALIVE_MSG, mn_FlushIn, 0);
//
//	while (mn_FlushOut != mn_FlushIn) {
//		if (WaitForSingleObject(mh_MainSrv, 100) == WAIT_OBJECT_0)
//			break; // Процесс сервера завершился
//
//		DWORD dwCurTick = GetTickCount();
//		DWORD dwDelta = dwCurTick - dwStartTick;
//		if (dwDelta > nTimeout) break;
//	}
//
//	return (mn_FlushOut == mn_FlushIn);
//}

void CRealConsole::ShowKeyBarHint(WORD nID)
{
	if (!this)
		return;

	if (mp_RBuf)
		mp_RBuf->ShowKeyBarHint(nID);
}

bool CRealConsole::PostPromptCmd(bool CD, LPCWSTR asCmd)
{
	if (!this || !asCmd || !*asCmd)
		return false;

	bool lbRc = false;
	// "\x27 cd /d \"%s\" \x0A"
	DWORD nActivePID = GetActivePID();
	if (nActivePID && (mp_ABuf->m_Type == rbt_Primary))
	{
		size_t cchMax = _tcslen(asCmd);

		if (CD && isFar(true))
		{
			// Передать макросом!
			cchMax = cchMax*2 + 128;
			wchar_t* pszMacro = (wchar_t*)malloc(cchMax*sizeof(*pszMacro));
			if (pszMacro)
			{
				_wcscpy_c(pszMacro, cchMax, L"@panel.setpath(0,\"");
				wchar_t* pszDst = pszMacro+_tcslen(pszMacro);
				LPCWSTR pszSrc = asCmd;
				while (*pszSrc)
				{
					if (*pszSrc == L'\\')
						*(pszDst++) = L'\\';
					*(pszDst++) = *(pszSrc++);
				}
				*(pszDst++) = L'"';
				*(pszDst++) = L')';
				*(pszDst++) = 0;
				
				PostMacro(pszMacro, TRUE/*async*/);
			}
		}
		else
		{
			LPCWSTR pszFormat = NULL;
			
			// \e cd /d "%s" \n
			cchMax += 32;

			if (CD)
			{
				pszFormat = gpConEmu->mp_Inside ? gpConEmu->mp_Inside->ms_InsideSynchronizeCurDir : NULL; // \ecd /d \1\n - \e - ESC, \b - BS, \n - ENTER, \1 - "dir", \2 - "bash dir"
				if (!pszFormat || !*pszFormat)
				{
					LPCWSTR pszExe = GetActiveProcessName();
					if (pszExe && (lstrcmpi(pszExe, L"powershell.exe") == 0))
					{
						//_wsprintf(psz, SKIPLEN(cchMax) L"%ccd \"%s\"%c", 27, asCmd, L'\n');
						pszFormat = L"\\ecd \\1\\n";
					}
					else if (pszExe && (lstrcmpi(pszExe, L"bash.exe") == 0 || lstrcmpi(pszExe, L"sh.exe") == 0))
					{
						pszFormat = L"\\e\\bcd \\2\\n";
					}
					else
					{
						//_wsprintf(psz, SKIPLEN(cchMax) L"%ccd /d \"%s\"%c", 27, asCmd, L'\n');
						pszFormat = L"\\ecd /d \\1\\n";
					}
				}

				cchMax += _tcslen(pszFormat);
			}

			CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_PROMPTCMD, sizeof(CESERVER_REQ_HDR)+sizeof(wchar_t)*cchMax);
			if (pIn)
			{
				wchar_t* psz = (wchar_t*)pIn->wData;
				if (CD)
				{
					_ASSERTE(pszFormat!=NULL); // уже должен был быть подготовлен выше
					// \ecd /d %1 - \e - ESC, \b - BS, \n - ENTER, \1 - "dir", \2 - "bash dir"

					wchar_t* pszDst = psz;
					wchar_t* pszEnd = pszDst + cchMax - 1;

					while (*pszFormat && (pszDst < pszEnd))
					{
						switch (*pszFormat)
						{
						case L'\\':
							pszFormat++;
							switch (*pszFormat)
							{
							case L'e': case L'E':
								*(pszDst++) = 27;
								break;
							case L'b': case L'B':
								*(pszDst++) = 8;
								break;
							case L'n': case L'N':
								*(pszDst++) = L'\n';
								break;
							case L'1':
								if ((pszDst+3) < pszEnd)
								{
									_ASSERTE(asCmd && (*asCmd != L'"'));
									LPCWSTR pszText = asCmd;

									*(pszDst++) = L'"';

									while (*pszText && (pszDst < pszEnd))
									{
										*(pszDst++) = *(pszText++);
									}

									// Done, quote
									if (pszDst < pszEnd)
									{
										*(pszDst++) = L'"';
									}
								}
								break;
							case L'2':
								// bash style - "/c/user/dir/..."
								if ((pszDst+4) < pszEnd)
								{
									_ASSERTE(asCmd && (*asCmd != L'"') && (*asCmd != L'/'));
									LPCWSTR pszText = asCmd;

									*(pszDst++) = L'"';

									if (pszText[0] && (pszText[1] == L':'))
									{
										*(pszDst++) = L'/';
										*(pszDst++) = pszText[0];
										pszText += 2;
									}
									else
									{
										// А bash понимает сетевые пути?
										_ASSERTE(pszText[0] == L'\\' && pszText[1] == L'\\');
									}

									while (*pszText && (pszDst < pszEnd))
									{
										if (*pszText == L'\\')
										{
											*(pszDst++) = L'/';
											pszText++;
										}
										else
										{
											*(pszDst++) = *(pszText++);
										}
									}

									// Done, quote
									if (pszDst < pszEnd)
									{
										*(pszDst++) = L'"';
									}
								}
								break;
							default:
								*(pszDst++) = *pszFormat;
							}
							pszFormat++;
							break;
						default:
							*(pszDst++) = *(pszFormat++);
						}
					}
					*pszDst = 0;
				}
				else
				{
					_wsprintf(psz, SKIPLEN(cchMax) L"%c%s%c", 27, asCmd, L'\n');
				}

				CESERVER_REQ* pOut = ExecuteHkCmd(nActivePID, pIn, ghWnd);
				if (pOut && (pOut->DataSize() >= sizeof(DWORD)))
				{
					lbRc = (pOut->dwData[0] != 0);
				}
				ExecuteFreeResult(pOut);
				ExecuteFreeResult(pIn);
			}
		}
	}

	return lbRc;
}

// !!! Функция может менять буфер pszChars! !!! Private !!!
bool CRealConsole::PostString(wchar_t* pszChars, size_t cchCount)
{
	if (!pszChars || !cchCount)
	{
		_ASSERTE(pszChars && cchCount);
		return false;
	}

	wchar_t* pszEnd = pszChars + cchCount;
	INPUT_RECORD r[2];
	MSG64* pirChars = (MSG64*)malloc(sizeof(MSG64)+cchCount*2*sizeof(MSG64::MsgStr));
	if (!pirChars)
	{
		AssertMsg(L"Can't allocate (INPUT_RECORD* pirChars)!");
		return false;
	}

	bool lbRc = false;
	bool lbIsFar = isFar();
	//wchar_t szMsg[128];

	size_t cchSucceeded = 0;
	MSG64::MsgStr* pir = pirChars->msg;
	for (wchar_t* pch = pszChars; pch < pszEnd; pch++, pir+=2)
	{
		_ASSERTE(*pch); // оно ASCIIZ

		if (pch[0] == L'\r' && pch[1] == L'\n')
		{
			pch++; // "\r\n" - слать "\n"
			if (pch[1] == L'\n')
				pch++; // buggy line returns "\r\n\n"
		}

		// "слать" в консоль '\r' а не '\n' чтобы "Enter" нажался.
		if (pch[0] == L'\n')
		{
			*pch = L'\r'; // буфер наш, что хотим - то и делаем
		}

		TranslateKeyPress(0, 0, *pch, -1, r, r+1);
		
		// 130822 - Japanese+iPython - (wVirtualKeyCode!=0) fails with pyreadline
		if (lbIsFar && (r->EventType == KEY_EVENT) && !r->Event.KeyEvent.wVirtualKeyCode)
		{
			// Если в RealConsole валится VK=0, но его фар игнорит
			r[0].Event.KeyEvent.wVirtualKeyCode = VK_PROCESSKEY;
			r[1].Event.KeyEvent.wVirtualKeyCode = VK_PROCESSKEY;
		}

		PackInputRecord(r, pir);
		PackInputRecord(r+1, pir+1);
		cchSucceeded += 2;

		//// функция сама разберется что посылать
		//while (!PostKeyPress(0, 0, *pch))
		//{
		//	wcscpy_c(szMsg, L"Key press sending failed!\nTry again?");

		//	if (MessageBox(ghWnd, szMsg, GetTitle(), MB_RETRYCANCEL) != IDRETRY)
		//	{
		//		goto wrap;
		//	}

		//	// try again
		//}
	}

	if (cchSucceeded)
		lbRc = PostConsoleEventPipe(pirChars, cchSucceeded);

	if (!lbRc)
		MBox(L"Key press sending failed!");

	//lbRc = true;
	//wrap:
	free(pirChars);
	return lbRc;
}

bool CRealConsole::PostKeyPress(WORD vkKey, DWORD dwControlState, wchar_t wch, int ScanCode /*= -1*/)
{
	if (!this)
		return false;

	INPUT_RECORD r[2] = {{KEY_EVENT},{KEY_EVENT}};
	TranslateKeyPress(vkKey, dwControlState, wch, ScanCode, r, r+1);

	bool lbPress = PostConsoleEvent(r);
	bool lbDepress = lbPress && PostConsoleEvent(r+1);
	return (lbPress && lbDepress);
}

//void CRealConsole::TranslateKeyPress(WORD vkKey, DWORD dwControlState, wchar_t wch, int ScanCode, INPUT_RECORD& rDown, INPUT_RECORD& rUp)
//{
//	// Может приходить запрос на отсылку даже если текущий буфер НЕ rbt_Primary,
//	// например, при начале выделения и автоматическом переключении на альтернативный буфер
//
//	if (!vkKey && !dwControlState && wch)
//	{
//		USHORT vk = VkKeyScan(wch);
//		if (vk && (vk != 0xFFFF))
//		{
//			vkKey = (vk & 0xFF);
//			vk = vk >> 8;
//			if ((vk & 7) == 6)
//			{
//				// For keyboard layouts that use the right-hand ALT key as a shift
//				// key (for example, the French keyboard layout), the shift state is
//				// represented by the value 6, because the right-hand ALT key is
//				// converted internally into CTRL+ALT.
//				dwControlState |= SHIFT_PRESSED;
//			}
//			else
//			{
//				if (vk & 1)
//					dwControlState |= SHIFT_PRESSED;
//				if (vk & 2)
//					dwControlState |= LEFT_CTRL_PRESSED;
//				if (vk & 4)
//					dwControlState |= LEFT_ALT_PRESSED;
//			}
//		}
//	}
//
//	if (ScanCode == -1)
//		ScanCode = MapVirtualKey(vkKey, 0/*MAPVK_VK_TO_VSC*/);
//
//	INPUT_RECORD r = {KEY_EVENT};
//	r.Event.KeyEvent.bKeyDown = TRUE;
//	r.Event.KeyEvent.wRepeatCount = 1;
//	r.Event.KeyEvent.wVirtualKeyCode = vkKey;
//	r.Event.KeyEvent.wVirtualScanCode = ScanCode;
//	r.Event.KeyEvent.uChar.UnicodeChar = wch;
//	r.Event.KeyEvent.dwControlKeyState = dwControlState;
//	rDown = r;
//
//	TODO("Может нужно в dwControlKeyState применять модификатор, если он и есть vkKey?");
//
//	r.Event.KeyEvent.bKeyDown = FALSE;
//	r.Event.KeyEvent.dwControlKeyState = dwControlState;
//	rUp = r;
//}

bool CRealConsole::PostKeyUp(WORD vkKey, DWORD dwControlState, wchar_t wch, int ScanCode /*= -1*/)
{
	if (!this)
		return false;

	if (ScanCode == -1)
		ScanCode = MapVirtualKey(vkKey, 0/*MAPVK_VK_TO_VSC*/);

	INPUT_RECORD r = {KEY_EVENT};
	r.Event.KeyEvent.bKeyDown = FALSE;
	r.Event.KeyEvent.wRepeatCount = 1;
	r.Event.KeyEvent.wVirtualKeyCode = vkKey;
	r.Event.KeyEvent.wVirtualScanCode = ScanCode;
	r.Event.KeyEvent.uChar.UnicodeChar = wch;
	r.Event.KeyEvent.dwControlKeyState = dwControlState;
	bool lbOk = PostConsoleEvent(&r);
	return lbOk;
}

bool CRealConsole::DeleteWordKeyPress(bool bTestOnly /*= false*/)
{
	DWORD nActivePID = GetActivePID();
	if (!nActivePID || (mp_ABuf->m_Type != rbt_Primary) || isFar() || isNtvdm())
		return false;

	const Settings::AppSettings* pApp = gpSet->GetAppSettings(GetActiveAppSettingsId());
	if (!pApp || !pApp->CTSDeleteLeftWord())
		return false;

	if (!bTestOnly)
	{
		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_BSDELETEWORD, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_PROMPTACTION));
		if (pIn)
		{
			pIn->Prompt.Force = (pApp->CTSDeleteLeftWord() == 1);
			pIn->Prompt.BashMargin = pApp->CTSBashMargin();

			CESERVER_REQ* pOut = ExecuteHkCmd(nActivePID, pIn, ghWnd);
			ExecuteFreeResult(pOut);
			ExecuteFreeResult(pIn);
		}
	}

	return true;
}

bool CRealConsole::PostLeftClickSync(COORD crDC)
{
	if (!this)
	{
		_ASSERTE(this!=NULL);
		return false;
	}

	DWORD nFarPID = GetFarPID();
	if (!nFarPID)
	{
		_ASSERTE(nFarPID!=NULL);
		return false;
	}

	bool lbOk = false;
	COORD crMouse = ScreenToBuffer(mp_VCon->ClientToConsole(crDC.X, crDC.Y));
	CConEmuPipe pipe(nFarPID, CONEMUREADYTIMEOUT);

	if (pipe.Init(_T("CConEmuMain::EMenu"), TRUE))
	{
		gpConEmu->DebugStep(_T("PostLeftClickSync: Waiting for result (10 sec)"));

		DWORD nClickData[2] = {TRUE, (DWORD)MAKELONG(crMouse.X,crMouse.Y)};

		if (!pipe.Execute(CMD_LEFTCLKSYNC, nClickData, sizeof(nClickData)))
		{
			LogString("pipe.Execute(CMD_LEFTCLKSYNC) failed");
		}
		else
		{
			lbOk = true;
		}

		gpConEmu->DebugStep(NULL);
	}

	return lbOk;
}

bool CRealConsole::PostConsoleEvent(INPUT_RECORD* piRec, bool bFromIME /*= false*/)
{
	if (!this)
		return false;

	if (mn_MainSrv_PID == 0 || !m_ConsoleMap.IsValid())
		return false; // Сервер еще не стартовал. События будут пропущены...

	bool lbRc = false;

	// Если GUI-режим - все посылаем в окно, в консоль ничего не нужно
	if (hGuiWnd)
	{
		if (piRec->EventType == KEY_EVENT)
		{
			UINT msg = bFromIME ? WM_IME_CHAR : WM_CHAR;
			WPARAM wParam = 0;
			LPARAM lParam = 0;
			
			if (piRec->Event.KeyEvent.bKeyDown && piRec->Event.KeyEvent.uChar.UnicodeChar)
				wParam = piRec->Event.KeyEvent.uChar.UnicodeChar;

			if (wParam || lParam)
			{
				PostConsoleMessage(hGuiWnd, msg, wParam, lParam);
			}
		}

		return lbRc;
	}

	//DWORD dwTID = 0;
	//#ifdef ALLOWUSEFARSYNCHRO
	//	if (isFar() && mn_FarInputTID) {
	//		dwTID = mn_FarInputTID;
	//	} else {
	//#endif
	//if (mn_ConEmuC_Input_TID == 0) // значит еще TID ввода не получили
	//	return;
	//dwTID = mn_ConEmuC_Input_TID;
	//#ifdef ALLOWUSEFARSYNCHRO
	//	}
	//#endif
	//if (dwTID == 0) {
	//	//_ASSERTE(dwTID!=0);
	//	gpConEmu->DebugStep(L"ConEmu: Input thread id is NULL");
	//	return;
	//}

	if (piRec->EventType == MOUSE_EVENT)
	{
#ifdef _DEBUG
		static DWORD nLastBtnState;
#endif
		//WARNING!!! Тут проблема следующая.
		// Фаровский AltIns требует получения MOUSE_MOVE в той же координате, где прошел клик.
		//  Иначе граббинг начинается не с "кликнутой" а со следующей позиции.
		// В других случаях наблюдаются проблемы с кликами. Например, в диалоге
		//  UCharMap. При его минимизации, если кликнуть по кнопке минимизации
		//  и фар получит MOUSE_MOVE - то диалог закроется при отпускании кнопки мышки.
		//2010-07-12 обработка вынесена в CRealConsole::OnMouse с учетом GUI курсора по пикселам
		//if (piRec->Event.MouseEvent.dwEventFlags == MOUSE_MOVED)
		//{
		//    if (m_LastMouse.dwButtonState     == piRec->Event.MouseEvent.dwButtonState
		//     && m_LastMouse.dwControlKeyState == piRec->Event.MouseEvent.dwControlKeyState
		//     && m_LastMouse.dwMousePosition.X == piRec->Event.MouseEvent.dwMousePosition.X
		//     && m_LastMouse.dwMousePosition.Y == piRec->Event.MouseEvent.dwMousePosition.Y)
		//    {
		//        //#ifdef _DEBUG
		//        //wchar_t szDbg[60];
		//        //swprintf_c(szDbg, L"!!! Skipping ConEmu.Mouse event at: {%ix%i}\n", m_LastMouse.dwMousePosition.X, m_LastMouse.dwMousePosition.Y);
		//        //DEBUGSTRINPUT(szDbg);
		//        //#endif
		//        return; // Это событие лишнее. Движения мышки реально не было, кнопки не менялись
		//    }
		//    #ifdef _DEBUG
		//    if ((nLastBtnState&FROM_LEFT_1ST_BUTTON_PRESSED)) {
		//    	nLastBtnState = nLastBtnState;
		//    }
		//    #endif
		//}
		#ifdef _DEBUG
		if (piRec->Event.MouseEvent.dwButtonState == RIGHTMOST_BUTTON_PRESSED
			&& ((piRec->Event.MouseEvent.dwControlKeyState & 9) != 9))
		{
			nLastBtnState = piRec->Event.MouseEvent.dwButtonState;
		}
		#endif
		
		// Запомним
		m_LastMouse.dwMousePosition   = piRec->Event.MouseEvent.dwMousePosition;
		m_LastMouse.dwEventFlags      = piRec->Event.MouseEvent.dwEventFlags;
		m_LastMouse.dwButtonState     = piRec->Event.MouseEvent.dwButtonState;
		m_LastMouse.dwControlKeyState = piRec->Event.MouseEvent.dwControlKeyState;
#ifdef _DEBUG
		nLastBtnState = piRec->Event.MouseEvent.dwButtonState;
#endif
		//#ifdef _DEBUG
		//wchar_t szDbg[60];
		//swprintf_c(szDbg, L"ConEmu.Mouse event at: {%ix%i}\n", m_LastMouse.dwMousePosition.X, m_LastMouse.dwMousePosition.Y);
		//DEBUGSTRINPUT(szDbg);
		//#endif
	}
	else if (piRec->EventType == KEY_EVENT)
	{
#if 0
		if (piRec->Event.KeyEvent.uChar.UnicodeChar == 3/*'^C'*/
			&& (piRec->Event.KeyEvent.dwControlKeyState & CTRL_MODIFIERS)
			&& ((piRec->Event.KeyEvent.dwControlKeyState & ALL_MODIFIERS)
				== (piRec->Event.KeyEvent.dwControlKeyState & CTRL_MODIFIERS))
			&& !isFar()
			)
		{
			if (piRec->Event.KeyEvent.bKeyDown)
			{
				lbRc = PostConsoleMessage(hConWnd, piRec->Event.KeyEvent.bKeyDown ? WM_KEYDOWN : WM_KEYUP,
					piRec->Event.KeyEvent.wVirtualKeyCode, 0);
			}
			else
			{
				lbRc = true;
			}

			goto wrap;
#if 0
			if (piRec->Event.KeyEvent.bKeyDown)
			{
				bool bGenerated = false;
				DWORD nActivePID = GetActivePID();
				CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_CTRLBREAK, sizeof(CESERVER_REQ_HDR)+2*sizeof(DWORD));
				if (pIn)
				{
					pIn->dwData[0] = CTRL_C_EVENT;
					pIn->dwData[1] = 0;

					CESERVER_REQ* pOut = ExecuteHkCmd(nActivePID, pIn, ghWnd);
					if (pOut)
					{
						if (pOut->DataSize() >= sizeof(DWORD))
							bGenerated = (pOut->dwData[0] != 0);

						ExecuteFreeResult(pOut);
					}
					ExecuteFreeResult(pIn);
				}

				if (bGenerated)
				{
					lbRc = true;
					goto wrap;
				}
			}
			else
			{
				TODO("Только если было обработано?");
				lbRc = true;
				goto wrap;
			}
#endif
		}
#endif

		if (!piRec->Event.KeyEvent.wRepeatCount)
		{
			_ASSERTE(piRec->Event.KeyEvent.wRepeatCount!=0);
			piRec->Event.KeyEvent.wRepeatCount = 0;
		}
	}
	
	if (ghOpWnd && gpSetCls->mh_Tabs[gpSetCls->thi_Debug] && gpSetCls->m_ActivityLoggingType == glt_Input)
	{
		//INPUT_RECORD *prCopy = (INPUT_RECORD*)calloc(sizeof(INPUT_RECORD),1);
		CESERVER_REQ_PEEKREADINFO* pCopy = (CESERVER_REQ_PEEKREADINFO*)malloc(sizeof(CESERVER_REQ_PEEKREADINFO));
		if (pCopy)
		{
			pCopy->nCount = 1;
			pCopy->bMainThread = TRUE;
			pCopy->cPeekRead = 'S';
			pCopy->cUnicode = 'W';
			pCopy->Buffer[0] = *piRec;
			PostMessage(gpSetCls->mh_Tabs[gpSetCls->thi_Debug], DBGMSG_LOG_ID, DBGMSG_LOG_INPUT_MAGIC, (LPARAM)pCopy);
		}
	}

	// ***
	{
		MSG64 msg = {sizeof(msg), 1};

		if (PackInputRecord(piRec, msg.msg))
		{
			if (m_UseLogs)
				LogInput(piRec);

			_ASSERTE(msg.msg[0].message!=0);
			//if (mb_UseOnlyPipeInput) {
			lbRc = PostConsoleEventPipe(&msg);

			#ifdef _DEBUG
			if (gbInSendConEvent)
			{
				_ASSERTE(!gbInSendConEvent);
			}
			#endif
		}
		else
		{
			gpConEmu->DebugStep(L"ConEmu: PackInputRecord failed!");
		}
	}

//wrap:
	return lbRc;
}

//DWORD CRealConsole::InputThread(LPVOID lpParameter)
//{
//    CRealConsole* pRCon = (CRealConsole*)lpParameter;
//
//    MSG msg;
//    while (GetMessage(&msg,0,0,0)) {
//		ConEmuMsgLogger::Log(msg);
//    	if (msg.message == WM_QUIT) break;
//    	if (WaitForSingleObject(pRCon->mh_TermEvent, 0) == WAIT_OBJECT_0) break;
//
//    	if (msg.message == INPUT_THREAD_ALIVE_MSG) {
//    		pRCon->mn_FlushOut = msg.wParam;
//    		continue;
//
//    	} else {
//
//    		INPUT_RECORD r = {0};
//
//    		if (UnpackInputRecord(&msg, &r)) {
//    			pRCon->SendConsoleEvent(&r);
//    		}
//
//    	}
//    }
//
//    return 0;
//}

void CRealConsole::OnTimerCheck()
{
	if (!this)
		return;
	if (InCreateRoot() || InRecreate())
		return;

	//TODO: На проверку пайпов а не хэндла процесса
	if (!mh_MainSrv)
		return;

	DWORD nWait = WaitForSingleObject(mh_MainSrv, 0);
	if (nWait == WAIT_OBJECT_0)
	{
		_ASSERTE((mn_TermEventTick!=0 && mn_TermEventTick!=(DWORD)-1) && "Server was terminated, StopSignal was not called");
		StopSignal();
		return;
	}

	// А это наверное вообще излишне, проверим под дебагом, как жить будет
	#ifdef _DEBUG
	if (hConWnd && !IsWindow(hConWnd))
	{
		_ASSERTE((mn_TermEventTick!=0 && mn_TermEventTick!=(DWORD)-1) && "Console window was destroyed, StopSignal was not called");
		return;
	}
	#endif

	// Кроме того, здесь проверяется "нужно ли скроллить консоль во время выделения мышкой"
	if (mp_ABuf->isSelectionPresent())
	{
		mp_ABuf->OnTimerCheckSelection();
	}

	return;
}

void CRealConsole::MonitorAssertTrap()
{
	mb_MonitorAssertTrap = true;
	SetMonitorThreadEvent();
}

enum
{
	IDEVENT_TERM = 0,           // Завершение нити/консоли/conemu
	IDEVENT_MONITORTHREADEVENT, // Используется, чтобы вызвать Update & Invalidate
	IDEVENT_UPDATESERVERACTIVE, // Дернуть pRCon->UpdateServerActive()
	IDEVENT_SWITCHSRV,          // пришел запрос от альт.сервера переключиться на него
	IDEVENT_SERVERPH,           // ConEmuC.exe process handle (server)
	EVENTS_COUNT
};

DWORD CRealConsole::MonitorThread(LPVOID lpParameter)
{
	DWORD nWait = IDEVENT_TERM;
	CRealConsole* pRCon = (CRealConsole*)lpParameter;
	BOOL bDetached = pRCon->m_Args.bDetached && !pRCon->mb_ProcessRestarted && !pRCon->mn_InRecreate;
	BOOL lbChildProcessCreated = FALSE;

	pRCon->SetConStatus(bDetached ? L"Detached" : L"Initializing RealConsole...", true);

	//pRCon->mb_WaitingRootStartup = TRUE;

	if (pRCon->mb_NeedStartProcess)
	{
		_ASSERTE(pRCon->mh_MainSrv==NULL);

		// -- pushed to queue from ::Constructor!
		//// Move all "CreateProcess-es" to Main Thread
		//gpConEmu->mp_RunQueue->RequestRConStartup(pRCon);

		HANDLE hWait[] = {pRCon->mh_TermEvent, pRCon->mh_StartExecuted};
		DWORD nWait = WaitForMultipleObjects(countof(hWait), hWait, FALSE, INFINITE);
		if ((nWait == WAIT_OBJECT_0) || !pRCon->mb_StartResult)
		{
			//_ASSERTE(FALSE && "Failed to start console?"); -- no need in debug
			goto wrap;
		}

		#ifdef _DEBUG
		int nNumber = gpConEmu->isVConValid(pRCon->mp_VCon);
		UNREFERENCED_PARAMETER(nNumber);
		#endif

	}

	pRCon->mb_WaitingRootStartup = FALSE;

	//_ASSERTE(pRCon->mh_ConChanged!=NULL);
	// Пока нить запускалась - произошел "аттач" так что все нормально...
	//_ASSERTE(pRCon->mb_Detached || pRCon->mh_MainSrv!=NULL);

	// а тут мы будем читать консоль...
	nWait = pRCon->MonitorThreadWorker(bDetached, lbChildProcessCreated);

wrap:

	if (nWait == IDEVENT_SERVERPH)
	{
		//ShutdownGuiStep(L"### Server was terminated\n");
		DWORD nExitCode = 999;
		GetExitCodeProcess(pRCon->mh_MainSrv, &nExitCode);
		wchar_t szErrInfo[255];
		_wsprintf(szErrInfo, SKIPLEN(countof(szErrInfo))
			(nExitCode > 0 && nExitCode <= 2048) ?
				L"Server process was terminated, ExitCode=%i" :
				L"Server process was terminated, ExitCode=0x%08X",
			nExitCode);
		if (nExitCode == 0xC000013A)
			wcscat_c(szErrInfo, L" (by Ctrl+C)");

		ShutdownGuiStep(szErrInfo);

		if (nExitCode == 0)
		{
			pRCon->SetConStatus(NULL);
		}
		else
		{
			pRCon->SetConStatus(szErrInfo);
		}
	}

	// А это чтобы не осталось висеть окно ConEmu, раз всё уже закрыто
	gpConEmu->OnRConStartedSuccess(NULL);

	ShutdownGuiStep(L"StopSignal");

	pRCon->StopSignal();

	ShutdownGuiStep(L"Leaving MonitorThread\n");
	return 0;
}

int CRealConsole::WorkerExFilter(unsigned int code, struct _EXCEPTION_POINTERS *ep, LPCTSTR szFile, UINT nLine)
{
	wchar_t szInfo[100];
	_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"Exception 0x%08X triggered in CRealConsole::MonitorThreadWorker", code);

	AssertBox(szInfo, szFile, nLine, ep);

	return EXCEPTION_EXECUTE_HANDLER;
}

DWORD CRealConsole::MonitorThreadWorker(BOOL bDetached, BOOL& rbChildProcessCreated)
{
	rbChildProcessCreated = FALSE;

	mb_MonitorAssertTrap = false;

	_ASSERTE(IDEVENT_SERVERPH==(EVENTS_COUNT-1)); // Должен быть последним хэндлом!
	HANDLE hEvents[EVENTS_COUNT];
	_ASSERTE(EVENTS_COUNT==countof(hEvents)); // проверить размерность

	hEvents[IDEVENT_TERM] = mh_TermEvent;
	hEvents[IDEVENT_MONITORTHREADEVENT] = mh_MonitorThreadEvent; // Использовать, чтобы вызвать Update & Invalidate
	hEvents[IDEVENT_UPDATESERVERACTIVE] = mh_UpdateServerActiveEvent; // Дернуть UpdateServerActive()
	hEvents[IDEVENT_SWITCHSRV] = mh_SwitchActiveServer;
	hEvents[IDEVENT_SERVERPH] = mh_MainSrv;
	//HANDLE hAltServerHandle = NULL;

	DWORD  nEvents = countof(hEvents);

	// Скорее всего будет NULL, если запуск идет через ShellExecuteEx(runas)
	if (hEvents[IDEVENT_SERVERPH] == NULL)
		nEvents --;

	DWORD  nWait = 0, nSrvWait = -1, nAcvWait = -1;
	BOOL   bException = FALSE, bIconic = FALSE, /*bFirst = TRUE,*/ bActive = TRUE, bGuiVisible = FALSE;
	DWORD nElapse = max(10,gpSet->nMainTimerElapse);
	DWORD nInactiveElapse = max(10,gpSet->nMainTimerInactiveElapse);
	DWORD nLastFarPID = 0;
	bool bLastAlive = false, bLastAliveActive = false;
	bool lbForceUpdate = false;
	WARNING("Переделать ожидание на hDataReadyEvent, который выставляется в сервере?");
	TODO("Нить не завершается при F10 в фаре - процессы пока не инициализированы...");
	DWORD nConsoleStartTick = GetTickCount();
	DWORD nTimeout = 0;
	CRealBuffer* pLastBuf = NULL;
	bool lbActiveBufferChanged = false;
	DWORD nConWndCheckTick = GetTickCount();

	while (TRUE)
	{
		bActive = isActive();
		bIconic = gpConEmu->isIconic();

		// в минимизированном/неактивном режиме - сократить расходы
		nTimeout = bIconic ? max(1000,nInactiveElapse) : !bActive ? nInactiveElapse : nElapse;


		if (bActive)
			gpSetCls->Performance(tPerfInterval, TRUE); // считается по своему

		#ifdef _DEBUG
		// 1-based console index
		int nVConNo = gpConEmu->isVConValid(mp_VCon);
		UNREFERENCED_PARAMETER(nVConNo);
		#endif

		// Проверка, вдруг осталась висеть "мертвая" консоль?
		if (hEvents[IDEVENT_SERVERPH] == NULL && mh_MainSrv)
		{
			nSrvWait = WaitForSingleObject(mh_MainSrv,0);
			if (nSrvWait == WAIT_OBJECT_0)
			{
				// ConEmuC was terminated!
				_ASSERTE(bDetached == FALSE);
				nWait = IDEVENT_SERVERPH;
				break;
			}
		}
		else if (hConWnd)
		{
			DWORD nDelta = (GetTickCount() - nConWndCheckTick);
			if (nDelta >= CHECK_CONHWND_TIMEOUT)
			{
				if (!IsWindow(hConWnd))
				{
					_ASSERTE(FALSE && "Console window was abnormally terminated?");
					nWait = IDEVENT_SERVERPH;
					break;
				}
				nConWndCheckTick = GetTickCount();
			}
		}

		


		nWait = WaitForMultipleObjects(nEvents, hEvents, FALSE, nTimeout);
		if (nWait == (DWORD)-1)
		{
			DWORD nWaitItems[EVENTS_COUNT] = {99,99,99,99,99};

			for (size_t i = 0; i < nEvents; i++)
			{
				nWaitItems[i] = WaitForSingleObject(hEvents[i], 0);
				if (nWaitItems[i] == 0)
				{
					nWait = (DWORD)i;
				}
			}
			_ASSERTE(nWait!=(DWORD)-1);

			#ifdef _DEBUG
			int nDbg = nWaitItems[EVENTS_COUNT-1];
			UNREFERENCED_PARAMETER(nDbg);
			#endif
		}

		//if ((nWait == IDEVENT_SERVERPH) && (hEvents[IDEVENT_SERVERPH] != mh_MainSrv))
		//{
		//	// Закрылся альт.сервер, переключиться на основной
		//	_ASSERTE(mh_MainSrv!=NULL);
		//	if (mh_AltSrv == hAltServerHandle)
		//	{
		//		mh_AltSrv = NULL;
		//		SetAltSrvPID(0, NULL);
		//	}
		//	SafeCloseHandle(hAltServerHandle);
		//	hEvents[IDEVENT_SERVERPH] = mh_MainSrv;

		//	if (mb_InCloseConsole && mh_MainSrv && (WaitForSingleObject(mh_MainSrv, 0) == WAIT_OBJECT_0))
		//	{
		//		ShutdownGuiStep(L"AltServer and MainServer are closed");
		//		// Подтверждаем nWait, основной сервер закрылся
		//		nWait = IDEVENT_SERVERPH;
		//	}
		//	else
		//	{
		//		ShutdownGuiStep(L"AltServer closed, executing ReopenServerPipes");
		//		if (ReopenServerPipes())
		//		{
		//			// Меняем nWait, т.к. основной сервер еще не закрылся (консоль жива)
		//			nWait = IDEVENT_MONITORTHREADEVENT;
		//		}
		//		else
		//		{
		//			ShutdownGuiStep(L"ReopenServerPipes failed, exiting from MonitorThread");
		//		}
		//	}
		//}

		if (nWait == IDEVENT_TERM || nWait == IDEVENT_SERVERPH)
		{
			//if (nWait == IDEVENT_SERVERPH) -- внизу
			//{
			//	DEBUGSTRPROC(L"### ConEmuC.exe was terminated\n");
			//}
			#ifdef _DEBUG
			DWORD nSrvExitPID = 0, nSrvPID = 0, nFErr;
			BOOL bFRc = GetExitCodeProcess(hEvents[IDEVENT_SERVERPH], &nSrvExitPID);
			nFErr = GetLastError();
			typedef DWORD (WINAPI* GetProcessId_t)(HANDLE);
			GetProcessId_t getProcessId = (GetProcessId_t)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetProcessId");
			if (getProcessId)
				nSrvPID = getProcessId(hEvents[IDEVENT_SERVERPH]);
			#endif

			// Чтобы однозначность кода в MonitorThread была
			nWait = IDEVENT_SERVERPH;
			// требование завершения нити
			break;
		}

		if ((nWait == IDEVENT_SWITCHSRV) || mb_SwitchActiveServer)
		{
			//hAltServerHandle = mh_AltSrv;
			//_ASSERTE(hAltServerHandle!=NULL);
			//hEvents[IDEVENT_SERVERPH] = hAltServerHandle ? hAltServerHandle : mh_MainSrv;

			if (!ReopenServerPipes())
			{
				// Try again?
				mb_SwitchActiveServer = true;
			}
			else
			{
				// Done
				mb_SwitchActiveServer = false;
				SetEvent(mh_ActiveServerSwitched);
			}
		}

		bool bNeedUpdateServerActive = (nWait == IDEVENT_UPDATESERVERACTIVE);
		if (!bNeedUpdateServerActive && mn_ServerActiveTick1)
		{
			nAcvWait = WaitForSingleObject(mh_UpdateServerActiveEvent, 0);
			bNeedUpdateServerActive = (nAcvWait == WAIT_OBJECT_0);
		}
		if (bNeedUpdateServerActive)
		{
			if (isServerCreated(true))
			{
				_ASSERTE(hConWnd!=NULL && "Console window must be already detected!");
				mn_ServerActiveTick2 = GetTickCount();
				DEBUGTEST(DWORD nDelay = mn_ServerActiveTick2 - mn_ServerActiveTick1);

				UpdateServerActive(TRUE);
			}
			ResetEvent(mh_UpdateServerActiveEvent);
			mn_ServerActiveTick1 = 0;
		}

		// Это событие теперь ManualReset
		if (nWait == IDEVENT_MONITORTHREADEVENT
		        || WaitForSingleObject(hEvents[IDEVENT_MONITORTHREADEVENT],0) == WAIT_OBJECT_0)
		{
			ResetEvent(hEvents[IDEVENT_MONITORTHREADEVENT]);

			// Сюда мы попадаем, например, при запуске новой консоли "Под админом", когда GUI НЕ "Под админом"
			// В начале нити (mh_MainSrv == NULL), если запуск идет через ShellExecuteEx(runas)
			if (hEvents[IDEVENT_SERVERPH] == NULL)
			{
				if (mh_MainSrv)
				{
					if (bDetached || m_Args.bRunAsAdministrator)
					{
						bDetached = FALSE;
						hEvents[IDEVENT_SERVERPH] = mh_MainSrv;
						nEvents = countof(hEvents);
					}
					else
					{
						_ASSERTE(bDetached==TRUE);
					}
				}
				else
				{
					_ASSERTE(mh_MainSrv!=NULL);
				}
			}
		}

		if (!rbChildProcessCreated
			&& (mn_ProcessClientCount > 0)
			&& ((GetTickCount() - nConsoleStartTick) > PROCESS_WAIT_START_TIME))
		{
			rbChildProcessCreated = TRUE;
			OnStartedSuccess();
		}

		// IDEVENT_SERVERPH уже проверен, а код возврата обработается при выходе из цикла
		//// Проверим, что ConEmuC жив
		//if (mh_MainSrv)
		//{
		//	DWORD dwExitCode = 0;
		//	#ifdef _DEBUG
		//	BOOL fSuccess =
		//	#endif
		//	    GetExitCodeProcess(mh_MainSrv, &dwExitCode);
		//	if (dwExitCode!=STILL_ACTIVE)
		//	{
		//		StopSignal();
		//		return 0;
		//	}
		//}

		// Если консоль не должна быть показана - но ее кто-то отобразил
		if (!isShowConsole && !gpSet->isConVisible)
		{
			/*if (foreWnd == hConWnd)
			    apiSetForegroundWindow(ghWnd);*/
			bool bMonitorVisibility = true;

			#ifdef _DEBUG
			if ((GetKeyState(VK_SCROLL) & 1))
				bMonitorVisibility = false;

			WARNING("bMonitorVisibility = false - для отлова сброса буфера");
			bMonitorVisibility = false;
			#endif

			if (bMonitorVisibility && IsWindowVisible(hConWnd))
				ShowOtherWindow(hConWnd, SW_HIDE);
		}

		// Размер консоли меняем в том треде, в котором это требуется. Иначе можем заблокироваться при Update (InitDC)
		// Требуется изменение размеров консоли
		/*if (nWait == (IDEVENT_SYNC2WINDOW)) {
		    SetConsoleSize(m_ReqSetSize);
		    //SetEvent(mh_ReqSetSizeEnd);
		    //continue; -- и сразу обновим информацию о ней
		}*/
		DWORD dwT1 = GetTickCount();
		SAFETRY
		{
			if (mb_MonitorAssertTrap)
			{
				mb_MonitorAssertTrap = false;
				//#ifdef _DEBUG
				//MyAssertTrap();
				//#else
				//DebugBreak();
				//#endif
				RaiseTestException();
			}

			//ResetEvent(mh_EndUpdateEvent);

			// Тут и ApplyConsole вызывается
			//if (mp_ConsoleInfo)

			lbActiveBufferChanged = (mp_ABuf != pLastBuf);
			if (lbActiveBufferChanged)
			{
				mb_ABufChaged = lbForceUpdate = true;
			}

			if (mp_RBuf != mp_ABuf)
			{
				mn_LastFarReadTick = GetTickCount();
				if (lbActiveBufferChanged || mp_ABuf->isConsoleDataChanged())
					lbForceUpdate = true;
			}
			// Если сервер жив - можно проверить наличие фара и его отклик
			else if ((!mb_SkipFarPidChange) && m_ConsoleMap.IsValid())
			{
				bool lbFarChanged = false;
				// Alive?
				DWORD nCurFarPID = GetFarPID(TRUE);

				if (!nCurFarPID)
				{
					// Возможно, сменился FAR (возврат из cmd.exe/tcc.exe, или вложенного фара)
					DWORD nPID = GetFarPID(FALSE);

					if (nPID)
					{
						for (UINT i = 0; i < mn_FarPlugPIDsCount; i++)
						{
							if (m_FarPlugPIDs[i] == nPID)
							{
								nCurFarPID = nPID;
								SetFarPluginPID(nCurFarPID);
								break;
							}
						}
					}
				}

				// Если фара (с плагином) нет, а раньше был
				if (!nCurFarPID && nLastFarPID)
				{
					// Закрыть и сбросить PID
					CloseFarMapData();
					nLastFarPID = 0;
					lbFarChanged = true;
				}

				// Если PID фара (с плагином) сменился
				if (nCurFarPID && nLastFarPID != nCurFarPID)
				{
					//mn_LastFarReadIdx = -1;
					mn_LastFarReadTick = 0;
					nLastFarPID = nCurFarPID;

					// Переоткрывать мэппинг при смене PID фара
					// (из одного фара запустили другой, который закрыли и вернулись в первый)
					if (!OpenFarMapData())
					{
						// Значит его таки еще (или уже) нет?
						if (mn_FarPID_PluginDetected == nCurFarPID)
						{
							for (UINT i = 0; i < mn_FarPlugPIDsCount; i++)  // сбросить ИД списка плагинов
							{
								if (m_FarPlugPIDs[i] == nCurFarPID)
									m_FarPlugPIDs[i] = 0;
							}

							SetFarPluginPID(0);
						}
					}

					lbFarChanged = true;
				}

				bool bAlive = false;

				//if (nCurFarPID && mn_LastFarReadIdx != mp_ConsoleInfo->nFarReadIdx) {
				if (nCurFarPID && m_FarInfo.cbSize && m_FarAliveEvent.Open())
				{
					DWORD nCurTick = GetTickCount();

					// Чтобы опрос события не шел слишком часто.
					if (mn_LastFarReadTick == 0 ||
					        (nCurTick - mn_LastFarReadTick) >= (FAR_ALIVE_TIMEOUT/2))
					{
						//if (WaitForSingleObject(mh_FarAliveEvent, 0) == WAIT_OBJECT_0)
						if (m_FarAliveEvent.Wait(0) == WAIT_OBJECT_0)
						{
							mn_LastFarReadTick = nCurTick ? nCurTick : 1;
							bAlive = true; // живой
						}

#ifdef _DEBUG
						else
						{
							mn_LastFarReadTick = nCurTick - FAR_ALIVE_TIMEOUT - 1;
							bAlive = false; // занят
						}

#endif
					}
					else
					{
						bAlive = true; // еще не успело протухнуть
					}

					//if (mn_LastFarReadIdx != mp_FarInfo->nFarReadIdx) {
					//	mn_LastFarReadIdx = mp_FarInfo->nFarReadIdx;
					//	mn_LastFarReadTick = GetTickCount();
					//	DEBUGSTRALIVE(L"*** FAR ReadTick updated\n");
					//	bAlive = true;
					//}
				}
				else
				{
					bAlive = true; // если нет фаровского плагина, или это не фар
				}

				//if (!bAlive) {
				//	bAlive = isAlive();
				//}
				if (isActive())
				{
					WARNING("Тут нужно бы сравнивать с переменной, хранящейся в gpConEmu, а не в этом instance RCon!");
#ifdef _DEBUG

					if (!IsDebuggerPresent())
					{
						bool lbIsAliveDbg = isAlive();

						if (lbIsAliveDbg != bAlive)
						{
							_ASSERTE(lbIsAliveDbg == bAlive);
						}
					}

#endif

					if (bLastAlive != bAlive || !bLastAliveActive)
					{
						DEBUGSTRALIVE(bAlive ? L"MonitorThread: Alive changed to TRUE\n" : L"MonitorThread: Alive changed to FALSE\n");
						PostMessage(GetView(), WM_SETCURSOR, -1, -1);
					}

					bLastAliveActive = true;

					if (lbFarChanged)
						gpConEmu->UpdateProcessDisplay(FALSE); // обновить PID в окне настройки
				}
				else
				{
					bLastAliveActive = false;
				}

				bLastAlive = bAlive;
				//вроде не надо
				//UpdateFarSettings(mn_FarPID_PluginDetected);
				// Загрузить изменения из консоли
				//if ((HWND)mp_ConsoleInfo->hConWnd && mp_ConsoleInfo->nCurDataMapIdx
				//	&& mp_ConsoleInfo->nPacketId
				//	&& mn_LastConsolePacketIdx != mp_ConsoleInfo->nPacketId)
				WARNING("!!! Если ожидание m_ConDataChanged будет перенесно выше - то тут нужно пользовать полученное выше значение !!!");

				if (!m_ConDataChanged.Wait(0,TRUE))
				{
					// если сменился статус (Far/не Far) - перерисовать на всякий случай, 
					// чтобы после возврата из батника, гарантированно отрисовалось в режиме фара
					_ASSERTE(mp_RBuf==mp_ABuf);
					if (mb_InCloseConsole && mh_MainSrv && (WaitForSingleObject(mh_MainSrv, 0) == WAIT_OBJECT_0))
					{
						// Чтобы однозначность кода в MonitorThread была
						nWait = IDEVENT_SERVERPH;
						// Основной сервер закрылся (консоль закрыта), идем на выход
						break;
					}
					else if (mp_RBuf->ApplyConsoleInfo())
					{
						lbForceUpdate = true;
					}
				}
			}

			bool bCheckStatesFindPanels = false, lbForceUpdateProgress = false;

			// Если консоль неактивна - CVirtualConsole::Update не вызывается и диалоги не детектятся. А это требуется.
			// Смотрим именно mp_ABuf, т.к. здесь нас интересует то, что нужно показать на экране!
			if ((!bActive || bIconic) && (lbActiveBufferChanged || mp_ABuf->isConsoleDataChanged()))
			{
				DWORD nCurTick = GetTickCount();
				DWORD nDelta = nCurTick - mn_LastInactiveRgnCheck;

				if (nDelta > CONSOLEINACTIVERGNTIMEOUT)
				{
					mn_LastInactiveRgnCheck = nCurTick;

					// Если при старте ConEmu создано несколько консолей через '@'
					// то все кроме активной - не инициализированы (InitDC не вызывался),
					// что нужно делать в главной нити, LoadConsoleData об этом позаботится
					if (mp_VCon->LoadConsoleData())
						bCheckStatesFindPanels = true;
				}
			}


			// обновить статусы, найти панели, и т.п.
			if (mb_DataChanged || mb_TabsWasChanged)
			{
				lbForceUpdate = true; // чтобы если консоль неактивна - не забыть при ее активации передернуть что нужно...
				mb_TabsWasChanged = false;
				mb_DataChanged = false;
				// Функция загружает ТОЛЬКО ms_PanelTitle, чтобы показать
				// корректный текст в закладке, соответсвующей панелям
				CheckPanelTitle();
				// выполнит ниже CheckFarStates & FindPanels
				bCheckStatesFindPanels = true;
			}

			if (!bCheckStatesFindPanels)
			{
				// Если была обнаружена "ошибка" прогресса - проверить заново.
				// Это может также произойти при извлечении файла из архива через MA.
				// Проценты бегут (панелей нет), проценты исчезают, панели появляются, но
				// пока не произойдет хоть каких-нибудь изменений в консоли - статус не обновлялся.
				if (m_Progress.LastWarnCheckTick || mn_FarStatus & (CES_WASPROGRESS|CES_OPER_ERROR))
					bCheckStatesFindPanels = true;
			}

			if (bCheckStatesFindPanels)
			{
				// Статусы mn_FarStatus & mn_PreWarningProgress
				// Результат зависит от распознанных регионов!
				// В функции есть вложенные вызовы, например,
				// mp_ABuf->GetDetector()->GetFlags()
				CheckFarStates();
				// Если возможны панели - найти их в консоли,
				// заодно оттуда позовется CheckProgressInConsole
				mp_RBuf->FindPanels();
			}

			if (m_Progress.ConsoleProgress >= 0
				&& m_Progress.LastConsoleProgress == -1
				&& m_Progress.LastConProgrTick != 0)
			{
				// Пока бежит запаковка 7z - иногда попадаем в момент, когда на новой строке процентов еще нет
				DWORD nDelta = GetTickCount() - m_Progress.LastConProgrTick;

				if (nDelta >= CONSOLEPROGRESSTIMEOUT)
				{
					logProgress(L"Clearing console progress due timeout", -1);
					setConsoleProgress(-1);
					setLastConsoleProgress(-1, true);
					lbForceUpdateProgress = true;
				}
			}


			// Видимость гуй-клиента - это кнопка "буферного режима"
			if (hGuiWnd && bActive)
			{
				BOOL lbVisible = ::IsWindowVisible(hGuiWnd);
				if (lbVisible != bGuiVisible)
				{
					// Обновить на тулбаре статусы Scrolling(BufferHeight) & Alternative
					OnBufferHeight();
					bGuiVisible = lbVisible;
				}
			}

			if (hConWnd || hGuiWnd)  // Если знаем хэндл окна -
				GetWindowText(hGuiWnd ? hGuiWnd : hConWnd, TitleCmp, countof(TitleCmp)-2);

			if (mb_ForceTitleChanged
				|| lbForceUpdateProgress
				|| wcscmp(Title, TitleCmp))
			{
				mb_ForceTitleChanged = FALSE;
				OnTitleChanged();
				lbForceUpdateProgress = false; // прогресс обновлен
			}
			else if (bActive)
			{
				// Если в консоли заголовок не менялся, но он отличается от заголовка в ConEmu
				gpConEmu->CheckNeedUpdateTitle(GetTitle());
			}

			bool lbIsActive = isActive();
			bool lbIsVisible = lbIsActive || isVisible();

			#ifdef _DEBUG
			if (mb_DebugLocked)
				lbIsActive = false;
			#endif

			TODO("После DoubleView нужно будет заменить на IsVisible");
			if (!lbIsVisible)
			{
				if (lbForceUpdate)
					mp_VCon->UpdateThumbnail();
			}
			else
			{
				//2009-01-21 сомнительно, что здесь действительно нужно подресайзивать дочерние окна
				//if (lbForceUpdate) // размер текущего консольного окна был изменен
				//	gpConEmu->OnSize(false); // послать в главную нить запрос на обновление размера
				bool lbNeedRedraw = false;

				if ((nWait == (WAIT_OBJECT_0+1)) || lbForceUpdate)
				{
					//2010-05-18 lbForceUpdate вызывал CVirtualConsole::Update(abForce=true), что приводило к тормозам
					bool bForce = false; //lbForceUpdate;
					lbForceUpdate = false;
					//mp_VCon->Validate(); // сбросить флажок

					if (m_UseLogs>2) LogString("mp_VCon->Update from CRealConsole::MonitorThread");

					if (mp_VCon->Update(bForce))
					{
						// Invalidate уже вызван!
						lbNeedRedraw = false;
					}
				}
				else if (lbIsVisible // где мигать курсором
					&& gpSet->GetAppSettings(GetActiveAppSettingsId())->CursorBlink(lbIsActive)
					&& mb_RConStartedSuccess)
				{
					// Возможно, настало время мигнуть курсором?
					bool lbNeedBlink = false;
					mp_VCon->UpdateCursor(lbNeedBlink);

					// UpdateCursor Invalidate не зовет
					if (lbNeedBlink)
					{
						if (m_UseLogs>2) LogString("Invalidating from CRealConsole::MonitorThread.1");

						lbNeedRedraw = true;
					}
				}
				else if (((GetTickCount() - mn_LastInvalidateTick) > FORCE_INVALIDATE_TIMEOUT))
				{
					DEBUGSTRDRAW(L"+++ Force invalidate by timeout\n");

					if (m_UseLogs>2) LogString("Invalidating from CRealConsole::MonitorThread.2");

					lbNeedRedraw = true;
				}

				// Если нужна отрисовка - дернем основную нить
				if (lbNeedRedraw)
				{
					//#ifndef _DEBUG
					//WARNING("******************");
					//TODO("После этого двойная отрисовка не вызывается?");
					//mp_VCon->Redraw();
					//#endif

					mp_VCon->Invalidate();
					mn_LastInvalidateTick = GetTickCount();
				}
			}

			pLastBuf = mp_ABuf;

		} SAFECATCHFILTER(WorkerExFilter(GetExceptionCode(), GetExceptionInformation(), _T(__FILE__), __LINE__))
		{
			bException = TRUE;
			// Assertion is shown in WorkerExFilter
			#if 0
			AssertBox(L"Exception triggered in CRealConsole::MonitorThread", _T(__FILE__), __LINE__, pExc);
			#endif
		}
		// Чтобы не было слишком быстрой отрисовки (тогда процессор загружается на 100%)
		// делаем такой расчет задержки
		DWORD dwT2 = GetTickCount();
		DWORD dwD = max(10,(dwT2 - dwT1));
		nElapse = (DWORD)(nElapse*0.7 + dwD*0.3);

		if (nElapse > 1000) nElapse = 1000;  // больше секунды - не ждать! иначе курсор мигать не будет

		if (bException)
		{
			bException = FALSE;

			//#ifdef _DEBUG
			//_ASSERTE(FALSE);
			//#endif

			//AssertMsg(L"Exception triggered in CRealConsole::MonitorThread");
		}

		//if (bActive)
		//	gpSetCls->Performance(tPerfInterval, FALSE);

		if (m_ServerClosing.nServerPID
		        && m_ServerClosing.nServerPID == mn_MainSrv_PID
		        && (GetTickCount() - m_ServerClosing.nRecieveTick) >= SERVERCLOSETIMEOUT)
		{
			// Видимо, сервер повис во время выхода?
			isConsoleClosing(); // функция позовет TerminateProcess(mh_MainSrv)
			nWait = IDEVENT_SERVERPH;
			break;
		}

		#ifdef _DEBUG
		UNREFERENCED_PARAMETER(nVConNo);
		#endif
	}

	return nWait;
}

BOOL CRealConsole::PreInit()
{
	TODO("Инициализация остальных буферов?");
	
	_ASSERTE(mp_RBuf==mp_ABuf);
	MCHKHEAP;
	
	return mp_RBuf->PreInit();
}

void CRealConsole::SetMonitorThreadEvent()
{
	if (!this)
	{
		_ASSERTE(this);
		return;
	}

	SetEvent(mh_MonitorThreadEvent);
}

BOOL CRealConsole::StartMonitorThread()
{
	BOOL lbRc = FALSE;
	_ASSERTE(mh_MonitorThread==NULL);
	//_ASSERTE(mh_InputThread==NULL);
	//_ASSERTE(mb_Detached || mh_MainSrv!=NULL); -- процесс теперь запускаем в MonitorThread
	DWORD nCreateBegin = GetTickCount();
	SetConStatus(L"Initializing ConEmu (4)", true);
	mh_MonitorThread = CreateThread(NULL, 0, MonitorThread, (LPVOID)this, 0, &mn_MonitorThreadID);
	SetConStatus(L"Initializing ConEmu (5)", true);
	DWORD nCreateEnd = GetTickCount();
	DWORD nThreadCreationTime = nCreateEnd - nCreateBegin;
	if (nThreadCreationTime > 2500)
	{
		wchar_t szInfo[80];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo))
			L"[DBG] Very high CPU load? CreateThread takes %u ms", nThreadCreationTime);
		#ifdef _DEBUG
		AssertMsg(szInfo);
		#endif
		LogString(szInfo);
	}

	//mh_InputThread = CreateThread(NULL, 0, InputThread, (LPVOID)this, 0, &mn_InputThreadID);

	if (mh_MonitorThread == NULL /*|| mh_InputThread == NULL*/)
	{
		DisplayLastError(_T("Can't create console thread!"));
	}
	else
	{
		//lbRc = SetThreadPriority(mh_MonitorThread, THREAD_PRIORITY_ABOVE_NORMAL);
		lbRc = TRUE;
	}

	return lbRc;
}

void CRealConsole::PrepareDefaultColors(BYTE& nTextColorIdx, BYTE& nBackColorIdx, BYTE& nPopTextColorIdx, BYTE& nPopBackColorIdx, bool bUpdateRegistry /*= false*/, HKEY hkConsole /*= NULL*/)
{
	//nTextColorIdx = 7; nBackColorIdx = 0; nPopTextColorIdx = 5; nPopBackColorIdx = 15;

	// Тут берем именно "GetDefaultAppSettingsId", а не "GetActiveAppSettingsId"
	// т.к. довольно стремно менять АТРИБУТЫ консоли при выполнении пакетников и пр.
	const Settings::AppSettings* pApp = gpSet->GetAppSettings(GetDefaultAppSettingsId());
	_ASSERTE(pApp!=NULL);

	// User choose special palette for this console?
	const Settings::ColorPalette* pPal = NULL;
	_ASSERTE(countof(pApp->szPaletteName)>0); // must be array, not pointer
	LPCWSTR pszPalette = (m_Args.pszPalette && *m_Args.pszPalette) ? m_Args.pszPalette
		: (pApp->OverridePalette && *pApp->szPaletteName) ? pApp->szPaletteName
		: NULL;
	if (pszPalette && *pszPalette)
	{
		int iPalIdx = gpSet->PaletteGetIndex(pszPalette);
		if (iPalIdx >= 0)
		{
			pPal = gpSet->PaletteGet(iPalIdx);
		}
	}

	nTextColorIdx = pPal ? pPal->nTextColorIdx : pApp->TextColorIdx(); // 0..15,16
	nBackColorIdx = pPal ? pPal->nBackColorIdx : pApp->BackColorIdx(); // 0..15,16
	if (nTextColorIdx <= 15 || nBackColorIdx <= 15)
	{
		if (nTextColorIdx >= 16) nTextColorIdx = 7;
		if (nBackColorIdx >= 16) nBackColorIdx = 0;
		if ((nTextColorIdx == nBackColorIdx)
			&& (!gpSetCls->IsBackgroundEnabled(mp_VCon)
				|| !(gpSet->nBgImageColors & (1 << nBackColorIdx))))  // bg color is an image
		{
			nTextColorIdx = nBackColorIdx ? 0 : 7;
		}
		//si.dwFlags |= STARTF_USEFILLATTRIBUTE;
		//si.dwFillAttribute = (nBackColorIdx << 4) | nTextColorIdx;
		mn_TextColorIdx = nTextColorIdx;
		mn_BackColorIdx = nBackColorIdx;
	}
	else
	{
		nTextColorIdx = nBackColorIdx = 16;
		//si.dwFlags &= ~STARTF_USEFILLATTRIBUTE;
		mn_TextColorIdx = 7;
		mn_BackColorIdx = 0;
	}

	nPopTextColorIdx = pPal ? pPal->nPopTextColorIdx : pApp->PopTextColorIdx(); // 0..15,16
	nPopBackColorIdx = pPal ? pPal->nPopBackColorIdx : pApp->PopBackColorIdx(); // 0..15,16
	if (nPopTextColorIdx <= 15 || nPopBackColorIdx <= 15)
	{
		if (nPopTextColorIdx >= 16) nPopTextColorIdx = 5;
		if (nPopBackColorIdx >= 16) nPopBackColorIdx = 15;
		if (nPopTextColorIdx == nPopBackColorIdx)
			nPopBackColorIdx = nPopTextColorIdx ? 0 : 15;
		mn_PopTextColorIdx = nPopTextColorIdx;
		mn_PopBackColorIdx = nPopBackColorIdx;
	}
	else
	{
		nPopTextColorIdx = nPopBackColorIdx = 16;
		mn_PopTextColorIdx = 5;
		mn_PopBackColorIdx = 15;
	}


	if (bUpdateRegistry)
	{
		bool bNeedClose = false;
		if (hkConsole == NULL)
		{
			LONG lRegRc;
			if (0 != (lRegRc = RegCreateKeyEx(HKEY_CURRENT_USER, L"Console\\ConEmu", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkConsole, NULL)))
			{
				DisplayLastError(L"Failed to create/open registry key 'HKCU\\Console\\ConEmu'", lRegRc);
				hkConsole = NULL;
			}
			else
			{
				bNeedClose = true;
			}
		}

		if (nTextColorIdx > 15)
			nTextColorIdx = GetDefaultTextColorIdx();
		if (nBackColorIdx > 15)
			nBackColorIdx = GetDefaultBackColorIdx();
		DWORD nColors = (nBackColorIdx << 4) | nTextColorIdx;
		if (hkConsole)
		{
			RegSetValueEx(hkConsole, L"ScreenColors", 0, REG_DWORD, (LPBYTE)&nColors, sizeof(nColors));
		}

		if (nPopTextColorIdx <= 15 || nPopBackColorIdx <= 15)
		{
			if (hkConsole)
			{
				DWORD nColors = ((mn_PopBackColorIdx & 0xF) << 4) | (mn_PopTextColorIdx & 0xF);
				RegSetValueEx(hkConsole, L"PopupColors", 0, REG_DWORD, (LPBYTE)&nColors, sizeof(nColors));
			}
		}

		if (bNeedClose)
		{
			RegCloseKey(hkConsole);
		}
	}
}

//// Заменить подстановки вида: !ConEmuHWND!, !ConEmuDrawHWND!, !ConEmuBackHWND!, !ConEmuWorkDir!
//wchar_t* CRealConsole::ParseConEmuSubst(LPCWSTR asCmd)
//{
//	if (!this || !mp_VCon || !asCmd || !*asCmd)
//		return NULL;
//
//	size_t cchMax = _tcslen(asCmd) + 1;
//
//	// Прикинуть, сколько путей нужно будет заменять
//	size_t nPathCount = 0;
//	wchar_t* pszCount = StrStrI(asCmd, ENV_CONEMUWORKDIR_VAR_W);
//	while (pszCount)
//	{
//		nPathCount++;
//		pszCount = StrStrI(pszCount+_tcslen(ENV_CONEMUWORKDIR_VAR_W), ENV_CONEMUWORKDIR_VAR_W);
//	}
//	LPCWSTR pszStartupDir = NULL;
//	if (nPathCount > 0)
//	{
//		pszStartupDir = GetStartupDir();
//		cchMax += _tcslen(pszStartupDir)*nPathCount;
//	}
//
//
//	wchar_t* pszChange = (wchar_t*)calloc(cchMax, sizeof(*pszChange)); //lstrdup(asCmd);
//	_wcscpy_c(pszChange, cchMax, asCmd);
//	LPCWSTR szNames[] = {ENV_CONEMUHWND_VAR_W, ENV_CONEMUDRAW_VAR_W, ENV_CONEMUBACK_VAR_W, ENV_CONEMUWORKDIR_VAR_W};
//	struct { HWND hWnd; LPCWSTR pszStr; } Repl[] = {{ghWnd}, {mp_VCon->GetView()}, {mp_VCon->GetBack()}, {NULL, pszStartupDir}};
//	bool bChanged = false;
//
//	for (size_t i = 0; i < countof(szNames); ++i)
//	{
//		wchar_t szTemp[16];
//		LPCWSTR pszReplace = NULL;
//		if (Repl[i].hWnd)
//		{
//			_wsprintf(szTemp, SKIPLEN(countof(szTemp)) L"0x%08X", (DWORD)(DWORD_PTR)(Repl[i].hWnd));
//			pszReplace = szTemp;
//		}
//		else if (Repl[i].pszStr)
//		{
//			pszReplace = Repl[i].pszStr;
//		}
//		else
//		{
//			continue;
//		}
//
//		size_t rLen = _tcslen(pszReplace); _ASSERTE(rLen==10 || Repl[i].pszStr!=NULL);
//
//		size_t iLen = _tcslen(szNames[i]); _ASSERTE(iLen>=rLen || Repl[i].pszStr!=NULL);
//
//		wchar_t* pszStart = StrStrI(pszChange, szNames[i]);
//		if (!pszStart /*|| pszStart == pszChange*/)
//			continue;
//		while (pszStart)
//		{
//			if ((pszStart > pszChange)
//				&& ((*(pszStart-1) == L'!' && *(pszStart+iLen) == L'!')
//					|| (*(pszStart-1) == L'%' && *(pszStart+iLen) == L'%')))
//			{
//				bChanged = true;
//				pszStart--;
//				wchar_t* pszEnd = pszStart + iLen+2;
//				_ASSERTE(*(pszEnd-1)==L'!' || *(pszEnd-1)==L'%');
//
//				size_t cchLeft = _tcslen(pszEnd)+1;
//				// Сначала - двигаем хвост, т.к. тело может быть перетерто, если rLen>iLen
//				memmove(pszStart+rLen, pszEnd, cchLeft*sizeof(*pszEnd));
//				memmove(pszStart, pszReplace, rLen*sizeof(*pszReplace));
//			}
//			pszStart = StrStrI(pszStart+2, szNames[i]);
//		}
//	}
//
//	if (!bChanged)
//		SafeFree(pszChange);
//	return pszChange;
//}

void CRealConsole::OnStartProcessAllowed()
{
	if (!this || !mb_NeedStartProcess)
	{
		_ASSERTE(this && mb_NeedStartProcess);

		if (this)
		{
			mb_StartResult = TRUE;
			SetEvent(mh_StartExecuted);
		}

		return;
	}

	_ASSERTE(mh_MainSrv==NULL);
	
	if (!PreInit())
	{
		DEBUGSTRPROC(L"### RCon:PreInit failed\n");
		SetConStatus(L"RCon:PreInit failed");

		mb_StartResult = FALSE;
		mb_NeedStartProcess = FALSE;
		SetEvent(mh_StartExecuted);

		return;
	}

	BOOL bStartRc = StartProcess();

	if (!bStartRc)
	{
		wchar_t szErrInfo[128];
		_wsprintf(szErrInfo, SKIPLEN(countof(szErrInfo)) L"Can't start root process, ErrCode=0x%08X...", GetLastError());
		DEBUGSTRPROC(L"### Can't start process\n");
		
		SetConStatus(szErrInfo);

		WARNING("Need to be checked, what happens on 'Run errors'");
		return;
	}

	//// Если ConEmu был запущен с ключом "/single /cmd xxx" то после окончания
	//// загрузки - сбросить команду, которая пришла из "/cmd" - загрузить настройку
	//if (gpSetCls->SingleInstanceArg == sgl_Enabled)
	//{
	//	gpSetCls->ResetCmdArg();
	//}
}

void CRealConsole::ConHostSearchPrepare()
{
	if (!this || !mp_ConHostSearch)
	{
		_ASSERTE(this && mp_ConHostSearch);
		return;
	}

	LogString(L"Prepare searching for conhost", TRUE);

	mp_ConHostSearch->Reset();

	HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (h && (h != INVALID_HANDLE_VALUE))
	{
		PROCESSENTRY32 PI = {sizeof(PI)};
		if (Process32First(h, &PI))
		{
			BOOL bFlag = TRUE;
			do {
				if (lstrcmpi(PI.szExeFile, L"conhost.exe") == 0)
					mp_ConHostSearch->Set(PI.th32ProcessID, bFlag);
			} while (Process32Next(h, &PI));
		}
		CloseHandle(h);
	}

	LogString(L"Prepare searching for conhost - Done", TRUE);
}

DWORD CRealConsole::ConHostSearch(bool bFinal)
{
	if (!this || !mp_ConHostSearch)
	{
		_ASSERTE(this && mp_ConHostSearch);
		return 0;
	}

	if (!mn_ConHost_PID)
	{
		_ASSERTE(mn_ConHost_PID==0);
		mn_ConHost_PID = 0;
	}

	LogString(L"Searching for conhost", TRUE);

	for (int s = 0; s <= 1; s++)
	{
		// Ищем новые процессы "conhost.exe"
		MArray<PROCESSENTRY32> CreatedHost;
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (h && (h != INVALID_HANDLE_VALUE))
		{
			PROCESSENTRY32 PI = {sizeof(PI)};
			if (Process32First(h, &PI))
			{
				do {
					if (lstrcmpi(PI.szExeFile, L"conhost.exe") == 0)
					{
						if (!mp_ConHostSearch->Get(PI.th32ProcessID, NULL))
						{
							CreatedHost.push_back(PI);
						}
					}
				} while (Process32Next(h, &PI));
			}
			CloseHandle(h);
		}

		if (CreatedHost.size() <= 0)
		{
			_ASSERTE(!bFinal && "Created conhost.exe was not found!");
			goto wrap;
		}
		else if (CreatedHost.size() > 1)
		{
			_ASSERTE(FALSE && "More than one created conhost.exe was found!");
			Sleep(250); // Попробовать еще раз? Может кто-то левый проехал...
			continue;
		}
		else
		{
			// Установить mn_ConHost_PID
			ConHostSetPID(CreatedHost[0].th32ProcessID);
			break;
		}
	}

wrap:

	if (gpSetCls->isAdvLogging)
	{
		wchar_t szInfo[100];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"ConHostPID=%u", mn_ConHost_PID);
		LogString(szInfo, TRUE);
	}

	if (mn_ConHost_PID)
		gpConEmu->ReleaseConhostDelay();
	return mn_ConHost_PID;
}

void CRealConsole::ConHostSetPID(DWORD nConHostPID)
{
	mn_ConHost_PID = nConHostPID;

	// Вывести информацию в отладчик.
	// Надо! Даже в релизе. (хотя, при желании, можно только при mb_BlockChildrenDebuggers)
	if (nConHostPID)
	{
		wchar_t szInfo[100];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo)) CONEMU_CONHOST_CREATED_MSG L"%u\n", nConHostPID);
		OutputDebugString(szInfo);
	}
}

LPCWSTR CRealConsole::GetStartupDir()
{
	LPCWSTR pszStartupDir = m_Args.pszStartupDir;

	if (m_Args.pszUserName != NULL)
	{
		// When starting under another credentials - try to use %USERPROFILE% instead of "system32"
		if (!pszStartupDir || !*pszStartupDir)
		{
			HANDLE hLogonToken = m_Args.CheckUserToken();
			if (hLogonToken)
			{
				HRESULT hr = E_FAIL;

				// Windows 2000 - hLogonToken - not supported
				if (gOSVer.dwMajorVersion <= 5 && gOSVer.dwMinorVersion == 0)
				{
					if (ImpersonateLoggedOnUser(hLogonToken))
					{
						memset(ms_ProfilePathTemp, 0, sizeof(ms_ProfilePathTemp));
						hr = SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, ms_ProfilePathTemp);
						RevertToSelf();
					}
				}
				else
				{
					memset(ms_ProfilePathTemp, 0, sizeof(ms_ProfilePathTemp));
					hr = SHGetFolderPath(NULL, CSIDL_PROFILE, hLogonToken, SHGFP_TYPE_CURRENT, ms_ProfilePathTemp);
				}

				if (SUCCEEDED(hr) && *ms_ProfilePathTemp)
				{
					pszStartupDir = ms_ProfilePathTemp;
				}

				CloseHandle(hLogonToken);
			}
		}
	}

	LPCWSTR lpszWorkDir = gpConEmu->WorkDir(pszStartupDir);

	return lpszWorkDir;
}

void CRealConsole::ResetVarsOnStart()
{
	mb_InCloseConsole = FALSE;
	mb_SwitchActiveServer = false;
	//Drop flag after Restart console
	mb_InPostCloseMacro = false;
	//mb_WasStartDetached = FALSE; -- не сбрасывать, на него смотрит и isDetached()
	ZeroStruct(m_ServerClosing);

	hConWnd = NULL;
	hGuiWnd = mh_GuiWndFocusStore = NULL;
	mb_GuiExternMode = FALSE;
	mb_GuiForceConView = false;
	//mn_GuiApplicationPID = 0;
	setGuiWndPID(0, NULL); // set mn_GuiWndPID to 0
	mn_GuiWndStyle = mn_GuiWndStylEx = 0;
	mn_GuiAttachFlags = 0;
	mb_WasForceTerminated = FALSE;
}

BOOL CRealConsole::StartProcess()
{
	if (!this)
	{
		_ASSERTE(this);
		return FALSE;
	}

	// Must be executed in Main Thread
	_ASSERTE(gpConEmu->isMainThread());
	// Monitor thread must be started already
	_ASSERTE(mn_MonitorThreadID!=0);

	BOOL lbRc = FALSE;
	SetConStatus(L"Preparing process startup line...", true);

	SetConEmuEnvVarChild(mp_VCon->GetView(), mp_VCon->GetBack());
	SetConEmuEnvVar(ghWnd);

	// Для валидации объектов
	CVirtualConsole* pVCon = mp_VCon;

	//if (!mb_ProcessRestarted)
	//{
	//	if (!PreInit())
	//		goto wrap;
	//}

	HWND hSetForeground = (gpConEmu->isIconic() || !IsWindowVisible(ghWnd)) ? GetForegroundWindow() : ghWnd;

	mb_UseOnlyPipeInput = FALSE;

	if (mp_sei)
	{
		SafeCloseHandle(mp_sei->hProcess);
		SafeFree(mp_sei);
	}

	//ResetEvent(mh_CreateRootEvent);
	CloseConfirmReset();

	//Moved to function
	ResetVarsOnStart();

	//Ready!
	mb_InCreateRoot = TRUE;

	//=====================================
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	wchar_t szInitConTitle[255];
	MCHKHEAP;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW|STARTF_USECOUNTCHARS|STARTF_USESIZE/*|STARTF_USEPOSITION*/;
	si.lpTitle = wcscpy(szInitConTitle, CEC_INITTITLE);
	// К сожалению, можно задать только размер БУФЕРА в символах.
	si.dwXCountChars = mp_RBuf->GetBufferWidth() /*con.m_sbi.dwSize.X*/;
	si.dwYCountChars = mp_RBuf->GetBufferHeight() /*con.m_sbi.dwSize.Y*/;

	// Размер окна нужно задавать в пикселях, а мы заранее не знаем сколько будет нужно...
	// Но можно задать хоть что-то, чтобы окошко сразу не разъехалось (в расчете на шрифт 4*6)...
	if (mp_RBuf->isScroll() /*con.bBufferHeight*/)
	{
		si.dwXSize = 4 * mp_RBuf->GetTextWidth()/*con.m_sbi.dwSize.X*/ + 2*GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXVSCROLL);
		si.dwYSize = 6 * mp_RBuf->GetTextHeight()/*con.nTextHeight*/ + 2*GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION);
	}
	else
	{
		si.dwXSize = 4 * mp_RBuf->GetTextWidth()/*con.m_sbi.dwSize.X*/ + 2*GetSystemMetrics(SM_CXFRAME);
		si.dwYSize = 6 * mp_RBuf->GetTextHeight()/*con.m_sbi.dwSize.X*/ + 2*GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION);
	}

	// Если просят "отладочный" режим - покажем окошко
	si.wShowWindow = gpSet->isConVisible ? SW_SHOWNORMAL : SW_HIDE;
	isShowConsole = gpSet->isConVisible;
	//RECT rcDC; GetWindowRect('ghWnd DC', &rcDC);
	//si.dwX = rcDC.left; si.dwY = rcDC.top;
	ZeroMemory(&pi, sizeof(pi));
	MCHKHEAP;
	wchar_t* psCurCmd = NULL;
	_ASSERTE((m_Args.pszStartupDir == NULL) || (*m_Args.pszStartupDir != 0));

	HKEY hkConsole = NULL;
	LONG lRegRc;
	if (0 == (lRegRc = RegCreateKeyEx(HKEY_CURRENT_USER, L"Console\\ConEmu", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkConsole, NULL)))
	{
		DWORD nSize = sizeof(DWORD), nValue, nType;
		struct {
			LPCWSTR pszName;
			DWORD nMin, nMax, nDef;
		} BufferValues[] = {
			{L"HistoryBufferSize", 16, 999, 50},
			{L"NumberOfHistoryBuffers", 16, 999, 32}
		};
		for (size_t i = 0; i < countof(BufferValues); ++i)
		{
			lRegRc = RegQueryValueEx(hkConsole, BufferValues[i].pszName, NULL, &nType, (LPBYTE)&nValue, &nSize);
			if ((lRegRc != 0) || (nType != REG_DWORD) || (nSize != sizeof(DWORD)) || (nValue < BufferValues[i].nMin) || (nValue > BufferValues[i].nMax))
			{
				if (!lRegRc && (nValue < BufferValues[i].nMin))
					nValue = BufferValues[i].nMin;
				else if (!lRegRc && (nValue > BufferValues[i].nMax))
					nValue = BufferValues[i].nMax;
				else
					nValue = BufferValues[i].nDef;

				// Issue 700: Default history buffers count too small.
				lRegRc = RegSetValueEx(hkConsole, BufferValues[i].pszName, 0, REG_DWORD, (LPBYTE)&nValue, sizeof(nValue));
			}
		}
	}
	else
	{
		DisplayLastError(L"Failed to create/open registry key 'HKCU\\Console\\ConEmu'", lRegRc);
		hkConsole = NULL;
	}

	BYTE nTextColorIdx /*= 7*/, nBackColorIdx /*= 0*/, nPopTextColorIdx /*= 5*/, nPopBackColorIdx /*= 15*/;
	PrepareDefaultColors(nTextColorIdx, nBackColorIdx, nPopTextColorIdx, nPopBackColorIdx, true, hkConsole);
	si.dwFlags |= STARTF_USEFILLATTRIBUTE;
	si.dwFillAttribute = (nBackColorIdx << 4) | nTextColorIdx;
	//if (nTextColorIdx <= 15 || nBackColorIdx <= 15) -- всегда, иначе может снести крышу от старых данных
	//{
	//	if (nTextColorIdx > 15)
	//		nTextColorIdx = GetDefaultTextColorIdx();
	//	if (nBackColorIdx > 15)
	//		nBackColorIdx = GetDefaultBackColorIdx();
	//	si.dwFlags |= STARTF_USEFILLATTRIBUTE;
	//	si.dwFillAttribute = (nBackColorIdx << 4) | nTextColorIdx;
	//	if (hkConsole)
	//	{
	//		DWORD nColors = si.dwFillAttribute;
	//		RegSetValueEx(hkConsole, L"ScreenColors", 0, REG_DWORD, (LPBYTE)&nColors, sizeof(nColors));
	//	}
	//}
	//if (nPopTextColorIdx <= 15 || nPopBackColorIdx <= 15)
	//{
	//	if (hkConsole)
	//	{
	//		DWORD nColors = ((mn_PopBackColorIdx & 0xF) << 4) | (mn_PopTextColorIdx & 0xF);
	//		RegSetValueEx(hkConsole, L"PopupColors", 0, REG_DWORD, (LPBYTE)&nColors, sizeof(nColors));
	//	}
	//}
	if (hkConsole)
	{
		RegCloseKey(hkConsole);
		hkConsole = NULL;
	}

	// Prepare cmd line
	LPCWSTR lpszRawCmd = (m_Args.pszSpecialCmd && *m_Args.pszSpecialCmd) ? m_Args.pszSpecialCmd : gpSetCls->GetCmd(NULL, true);
	_ASSERTE(lpszRawCmd && *lpszRawCmd);
	//SafeFree(mpsz_CmdBuffer);
	//mpsz_CmdBuffer = ParseConEmuSubst(lpszRawCmd);
	//LPCWSTR lpszCmd = mpsz_CmdBuffer ? mpsz_CmdBuffer : lpszRawCmd;
	LPCWSTR lpszCmd = lpszRawCmd;
	LPCWSTR lpszWorkDir = NULL;
	DWORD dwLastError = 0;
	DWORD nCreateBegin, nCreateEnd, nCreateDuration = 0;

	// ConHost.exe появился в Windows 7. Но там он создается "от родительского csrss".
	// А вот в Win8 - уже все хорошо, он создается от корневого консольного процесса.
	bool bNeedConHostSearch = (gnOsVer == 0x0601);
	bool bConHostLocked = false;
	//DEBUGTEST(if (gnOsVer == 0x0602) bNeedConHostSearch = true); // В Win8 искать не надо, но для отладки пока
	if (bNeedConHostSearch)
	{
		if (!mp_ConHostSearch)
		{
			mp_ConHostSearch = (MMap<DWORD,BOOL>*)calloc(1,sizeof(*mp_ConHostSearch));
			bNeedConHostSearch = mp_ConHostSearch && mp_ConHostSearch->Init();
		}
		else
		{
			mp_ConHostSearch->Reset();
		}
	}
	//
	if (!bNeedConHostSearch)
	{
		if (mp_ConHostSearch)
			mp_ConHostSearch->Release();
		SafeFree(mp_ConHostSearch);
	}
	else
	{
		bConHostLocked = gpConEmu->LockConhostStart();
	}


	bool bAllowDefaultCmd = (!m_Args.pszSpecialCmd || !*m_Args.pszSpecialCmd);
	//int nStep = (m_Args.pszSpecialCmd!=NULL) ? 2 : 1;
	int nStep = 1;


	// Go
	while (nStep <= 2)
	{
		_ASSERTE(nStep==1 || nStep==2);
		MCHKHEAP;

		// Do actual process creation
		lbRc = StartProcessInt(lpszCmd, psCurCmd, lpszWorkDir, bNeedConHostSearch, hSetForeground, 
			nCreateBegin, nCreateEnd, nCreateDuration, nTextColorIdx, nBackColorIdx, nPopTextColorIdx, nPopBackColorIdx, si, pi, dwLastError);

		if (lbRc)
		{
			break; // OK, запустили
		}

		/* End of process start-up */

		// ОШИБКА!!
		if (InRecreate())
		{
			m_Args.bDetached = TRUE;
			_ASSERTE(mh_MainSrv==NULL);
			SafeCloseHandle(mh_MainSrv);
			_ASSERTE(isDetached());
			SetConStatus(L"Restart console failed");
		}

		//Box("Cannot execute the command.");
		//DWORD dwLastError = GetLastError();
		DEBUGSTRPROC(L"CreateProcess failed\n");
		size_t nErrLen = _tcslen(psCurCmd)+120+(lpszWorkDir ? _tcslen(lpszWorkDir) : 0);

		LPCWSTR pszDefaultCmd = bAllowDefaultCmd ? gpSetCls->GetDefaultCmd() : NULL;
		nErrLen += pszDefaultCmd ? (100 + _tcslen(pszDefaultCmd)) : 0;

		// Get description of 'dwLastError'
		size_t cchFormatMax = 1024;
		TCHAR* pszErr = (TCHAR*)Alloc(cchFormatMax,sizeof(TCHAR));
		if (0==FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		                    NULL, dwLastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		                    pszErr, cchFormatMax, NULL))
		{
			_wsprintf(pszErr, SKIPLEN(cchFormatMax) L"Unknown system error: 0x%x", dwLastError);
		}

		nErrLen += _tcslen(pszErr);
		TCHAR* psz = (TCHAR*)Alloc(nErrLen+255,sizeof(TCHAR));
		int nButtons = MB_OK|MB_ICONEXCLAMATION|MB_SETFOREGROUND;
		switch (dwLastError)
		{
		case ERROR_ACCESS_DENIED:
			_wsprintf(psz, SKIPLEN(nErrLen) L"Can't create new console, check your antivirus (code %i)!\r\n", (int)dwLastError); break;
		default:
			_wsprintf(psz, SKIPLEN(nErrLen) L"Can't create new console, command execution failed (code %i)\r\n", (int)dwLastError);
		}
		_wcscat_c(psz, nErrLen, pszErr);
		_wcscat_c(psz, nErrLen, L"\r\n");
		_wcscat_c(psz, nErrLen, psCurCmd);
		_wcscat_c(psz, nErrLen, L"\r\n\r\nWorking folder:\r\n\"");
		_wcscat_c(psz, nErrLen, lpszWorkDir ? lpszWorkDir : L"");
		_wcscat_c(psz, nErrLen, L"\"");
		
		

		if (nStep==1)
		{
			if (psz[_tcslen(psz)-1]!=L'\n') _wcscat_c(psz, nErrLen, L"\r\n");
			_wcscat_c(psz, nErrLen, L"\r\n");
			_wcscat_c(psz, nErrLen, L"Choose your shell?");
			nButtons |= MB_YESNO;
		}

		MCHKHEAP
		//Box(psz);
		int nBrc = MessageBox(NULL, psz, gpConEmu->GetDefaultTitle(), nButtons);
		Free(psz); Free(pszErr);

		if (nBrc == IDYES)
		{
			RConStartArgs args;
			args.AssignFrom(&m_Args);
			args.aRecreate = cra_EditTab;

			int nCreateRc = gpConEmu->RecreateDlg(&args);

			if (nCreateRc != IDC_START)
			{
				nBrc = IDCANCEL;
			}
			else
			{
				SafeFree(m_Args.pszSpecialCmd);
				SafeFree(m_Args.pszStartupDir);
				// Rest of fields will be cleared by AssignFrom
				m_Args.AssignFrom(&args);
				lpszCmd = m_Args.pszSpecialCmd;
			}
		}

		if (nBrc != IDYES)
		{
			// ??? Может ведь быть НЕСКОЛЬКО консолей. Нельзя так разрушать основное окно!
			//gpConEmu->Destroy();
			//SetEvent(mh_CreateRootEvent);
			if (gpConEmu->isValid(pVCon))
			{
				mb_InCreateRoot = FALSE;
				if (InRecreate())
				{
					TODO("Поставить флаг Detached?");
				}
				else
				{
					CloseConsole(false, false);
				}
			}
			else
			{
				_ASSERTE(gpConEmu->isValid(pVCon));
			}
			lbRc = FALSE;
			goto wrap;
		}

		nStep ++;
		MCHKHEAP

		SafeFree(psCurCmd);

		// Изменилась команда, пересчитать настройки
		PrepareDefaultColors(nTextColorIdx, nBackColorIdx, nPopTextColorIdx, nPopBackColorIdx, true);
		if (nTextColorIdx <= 15 || nBackColorIdx <= 15)
		{
			si.dwFlags |= STARTF_USEFILLATTRIBUTE;
			si.dwFillAttribute = (nBackColorIdx << 4) | nTextColorIdx;
		}
	}

	MCHKHEAP

	SafeFree(psCurCmd);

	MCHKHEAP
	//TODO: а делать ли это?
	SafeCloseHandle(pi.hThread); pi.hThread = NULL;
	//CloseHandle(pi.hProcess); pi.hProcess = NULL;
	SetMainSrvPID(pi.dwProcessId, pi.hProcess);
	//mn_MainSrv_PID = pi.dwProcessId;
	//mh_MainSrv = pi.hProcess;
	pi.hProcess = NULL;

	if (bNeedConHostSearch)
	{
		_ASSERTE(lbRc);
		// Ищем новые процессы "conhost.exe"
		// Но т.к. он мог еще "не появиться" - не ругаемся
		if (ConHostSearch(false))
		{
			mp_ConHostSearch->Release();
			SafeFree(mp_ConHostSearch);
		}
		// Do Unlock immediately
		if (bConHostLocked)
		{
			gpConEmu->UnlockConhostStart();
			bConHostLocked = false;
		}
	}

	if (!m_Args.bRunAsAdministrator)
	{
		CreateLogFiles();
		//// Событие "изменения" консоль //2009-05-14 Теперь события обрабатываются в GUI, но прийти из консоли может изменение размера курсора
		//swprintf_c(ms_ConEmuC_Pipe, CE_CURSORUPDATE, mn_MainSrv_PID);
		//mh_CursorChanged = CreateEvent ( NULL, FALSE, FALSE, ms_ConEmuC_Pipe );
		// Инициализировать имена пайпов, событий, мэппингов и т.п.
		InitNames();
		//// Имя пайпа для управления ConEmuC
		//swprintf_c(ms_ConEmuC_Pipe, CESERVERPIPENAME, L".", mn_MainSrv_PID);
		//swprintf_c(ms_ConEmuCInput_Pipe, CESERVERINPUTNAME, L".", mn_MainSrv_PID);
		//MCHKHEAP
	}

wrap:
	//SetEvent(mh_CreateRootEvent);

	if (bConHostLocked)
	{
		gpConEmu->UnlockConhostStart();
		bConHostLocked = false;
	}

	// В режиме "администратора" мы будем в "CreateRoot" до тех пор, пока нам не отчитается запущенный сервер
	if (!lbRc || mn_MainSrv_PID)
	{
		mb_InCreateRoot = FALSE;
	}
	else
	{
		_ASSERTE(m_Args.bRunAsAdministrator);
	}

	mb_StartResult = lbRc;
	mb_NeedStartProcess = FALSE;
	SetEvent(mh_StartExecuted);
	#ifdef _DEBUG
	SetEnvironmentVariable(ENV_CONEMU_MONITOR_INTERNAL, NULL);
	#endif
	return lbRc;
}

BOOL CRealConsole::StartProcessInt(LPCWSTR& lpszCmd, wchar_t*& psCurCmd, LPCWSTR& lpszWorkDir,
								   bool bNeedConHostSearch, HWND hSetForeground,
								   DWORD& nCreateBegin, DWORD& nCreateEnd, DWORD& nCreateDuration,
								   BYTE nTextColorIdx /*= 7*/, BYTE nBackColorIdx /*= 0*/, BYTE nPopTextColorIdx /*= 5*/, BYTE nPopBackColorIdx /*= 15*/,
								   STARTUPINFO& si, PROCESS_INFORMATION& pi, DWORD& dwLastError)
{
	BOOL lbRc = FALSE;
	DWORD nColors = (nTextColorIdx) | (nBackColorIdx << 8) | (nPopTextColorIdx << 16) | (nPopBackColorIdx << 24);

	int nCurLen = 0;
	if (lpszCmd == NULL)
	{
		_ASSERTE(lpszCmd!=NULL);
		lpszCmd = L"";
	}
	int nLen = _tcslen(lpszCmd);
	nLen += _tcslen(gpConEmu->ms_ConEmuExe) + 330 + MAX_PATH*2;
	MCHKHEAP;
	psCurCmd = (wchar_t*)malloc(nLen*sizeof(wchar_t));
	_ASSERTE(psCurCmd);


	// Begin generation of execution command line
	*psCurCmd = 0;

	#if 0
	// Issue 791: Server fails, when GUI started under different credentials (login) as server
	if (m_Args.bRunAsAdministrator)
	{
		_wcscat_c(psCurCmd, nLen, L"\"");
		_wcscat_c(psCurCmd, nLen, gpConEmu->ms_ConEmuExe);
		_wcscat_c(psCurCmd, nLen, L"\" /bypass /cmd ");
	}
	#endif

	_wcscat_c(psCurCmd, nLen, L"\"");
	// Copy to psCurCmd full path to ConEmuC.exe or ConEmuC64.exe
	_wcscat_c(psCurCmd, nLen, gpConEmu->ConEmuCExeFull(lpszCmd));
	//lstrcat(psCurCmd, L"\\");
	//lstrcpy(psCurCmd, gpConEmu->ms_ConEmuCExeName);
	_wcscat_c(psCurCmd, nLen, L"\" ");

	if (m_Args.bRunAsAdministrator && !gpConEmu->mb_IsUacAdmin)
	{
		m_Args.bDetached = TRUE;
		_wcscat_c(psCurCmd, nLen, L" /ADMIN ");
	}

	if ((gpSet->nConInMode != (DWORD)-1) || m_Args.bOverwriteMode)
	{
		DWORD nMode = (gpSet->nConInMode != (DWORD)-1) ? gpSet->nConInMode : 0;
		if (m_Args.bOverwriteMode)
		{
			nMode |= (ENABLE_INSERT_MODE << 16); // Mask
			nMode &= ~ENABLE_INSERT_MODE; // Turn bit OFF
		}

		nCurLen = _tcslen(psCurCmd);
		_wsprintf(psCurCmd+nCurLen, SKIPLEN(nLen-nCurLen) L" /CINMODE=%X ", nMode);
	}

	_ASSERTE(mp_RBuf==mp_ABuf);
	int nWndWidth = mp_RBuf->GetTextWidth();
	int nWndHeight = mp_RBuf->GetTextHeight();
	/*было - GetConWindowSize(con.m_sbi, nWndWidth, nWndHeight);*/
	_ASSERTE(nWndWidth>0 && nWndHeight>0);

	const DWORD nAID = GetMonitorThreadID();
	_ASSERTE(gpConEmu->isMainThread());
	_ASSERTE(mn_MonitorThreadID!=0 && mn_MonitorThreadID==nAID && nAID!=GetCurrentThreadId());
		
	nCurLen = _tcslen(psCurCmd);
	_wsprintf(psCurCmd+nCurLen, SKIPLEN(nLen-nCurLen)
		        L"/AID=%u /GID=%u /GHWND=%08X /BW=%i /BH=%i /BZ=%i \"/FN=%s\" /FW=%i /FH=%i /TA=%08X",
		        nAID, GetCurrentProcessId(), (DWORD)ghWnd, nWndWidth, nWndHeight, mn_DefaultBufferHeight,
		        gpSet->ConsoleFont.lfFaceName, gpSet->ConsoleFont.lfWidth, gpSet->ConsoleFont.lfHeight,
		        nColors);

	/*if (gpSet->FontFile[0]) { --  РЕГИСТРАЦИЯ ШРИФТА НА КОНСОЛЬ НЕ РАБОТАЕТ!
		wcscat(psCurCmd, L" \"/FF=");
		wcscat(psCurCmd, gpSet->FontFile);
		wcscat(psCurCmd, L"\"");
	}*/
	if (m_UseLogs) _wcscat_c(psCurCmd, nLen, (m_UseLogs==3) ? L" /LOG3" : (m_UseLogs==2) ? L" /LOG2" : L" /LOG");

	if (!gpSet->isConVisible) _wcscat_c(psCurCmd, nLen, L" /HIDE");

	// /CONFIRM | /NOCONFIRM | /NOINJECT
	m_Args.AppendServerArgs(psCurCmd, nLen);

	_wcscat_c(psCurCmd, nLen, L" /ROOT ");
	_wcscat_c(psCurCmd, nLen, lpszCmd);
	MCHKHEAP;
	dwLastError = 0;
	#ifdef MSGLOGGER
	DEBUGSTRPROC(psCurCmd); DEBUGSTRPROC(_T("\n"));
	#endif

	#ifdef _DEBUG
	wchar_t szMonitorID[20]; _wsprintf(szMonitorID, SKIPLEN(countof(szMonitorID)) L"%u", nAID);
	SetEnvironmentVariable(ENV_CONEMU_MONITOR_INTERNAL, szMonitorID);
	#endif

	if (bNeedConHostSearch)
		ConHostSearchPrepare();

	// Если сам ConEmu запущен под админом - нет смысла звать ShellExecuteEx("RunAs")
	if (!m_Args.bRunAsAdministrator || gpConEmu->mb_IsUacAdmin)
	{
		LockSetForegroundWindow(LSFW_LOCK);
		SetConStatus(L"Starting root process...", true);

		if (m_Args.pszUserName != NULL)
		{
			nCreateBegin = GetTickCount();

			// When starting under another credentials - try to use %USERPROFILE% instead of "system32"
			lpszWorkDir = GetStartupDir();
				
			lbRc = CreateProcessWithLogonW(m_Args.pszUserName, m_Args.pszDomain, m_Args.szUserPassword,
				                        LOGON_WITH_PROFILE, NULL, psCurCmd,
				                        NORMAL_PRIORITY_CLASS|CREATE_DEFAULT_ERROR_MODE|CREATE_NEW_CONSOLE
										, NULL, lpszWorkDir, &si, &pi);
				//if (CreateProcessAsUser(m_Args.hLogonToken, NULL, psCurCmd, NULL, NULL, FALSE,
				//	NORMAL_PRIORITY_CLASS|CREATE_DEFAULT_ERROR_MODE|CREATE_NEW_CONSOLE
				//	, NULL, m_Args.pszStartupDir, &si, &pi))
			nCreateEnd = GetTickCount();

			if (!lbRc)
			{
				dwLastError = GetLastError();
			}
			else
			{
				SetMainSrvPID(pi.dwProcessId, pi.hProcess);
			}

			SecureZeroMemory(m_Args.szUserPassword, sizeof(m_Args.szUserPassword));
		}
		else if (m_Args.bRunAsRestricted)
		{
			nCreateBegin = GetTickCount();
			lpszWorkDir = GetStartupDir();
			lbRc = CreateProcessRestricted(NULL, psCurCmd, NULL, NULL, FALSE,
				                    NORMAL_PRIORITY_CLASS|CREATE_DEFAULT_ERROR_MODE|CREATE_NEW_CONSOLE
				                    , NULL, lpszWorkDir, &si, &pi, &dwLastError);
			nCreateEnd = GetTickCount();

			if (!lbRc)
			{
				dwLastError = GetLastError();
			}
			else
			{
				SetMainSrvPID(pi.dwProcessId, pi.hProcess);
			}

		}
		else
		{
			nCreateBegin = GetTickCount();
			lpszWorkDir = GetStartupDir();
			lbRc = CreateProcess(NULL, psCurCmd, NULL, NULL, FALSE,
				                    NORMAL_PRIORITY_CLASS|CREATE_DEFAULT_ERROR_MODE|CREATE_NEW_CONSOLE
				                    //|CREATE_NEW_PROCESS_GROUP - низя! перестает срабатывать Ctrl-C
				                    , NULL, lpszWorkDir, &si, &pi);
			nCreateEnd = GetTickCount();

			if (!lbRc)
			{
				dwLastError = GetLastError();
			}
			else
			{
				SetMainSrvPID(pi.dwProcessId, pi.hProcess);
			}
		}

		nCreateDuration = nCreateEnd - nCreateBegin;
		UNREFERENCED_PARAMETER(nCreateDuration);

		DEBUGSTRPROC(L"CreateProcess finished\n");
		//if (m_Args.hLogonToken) { CloseHandle(m_Args.hLogonToken); m_Args.hLogonToken = NULL; }
		LockSetForegroundWindow(LSFW_UNLOCK);
	}
	else // m_Args.bRunAsAdministrator
	{
		LPCWSTR pszCmd = psCurCmd;
		CmdArg szExec;

		if (NextArg(&pszCmd, szExec) != 0)
		{
			lbRc = FALSE;
			dwLastError = -1;
		}
		else
		{
			if (mp_sei)
			{
				SafeCloseHandle(mp_sei->hProcess);
				SafeFree(mp_sei);
			}

			wchar_t szCurrentDirectory[MAX_PATH+1];

			lpszWorkDir = GetStartupDir();
			wcscpy(szCurrentDirectory, lpszWorkDir);

			int nWholeSize = sizeof(SHELLEXECUTEINFO)
				                + sizeof(wchar_t) *
				                (10  /* Verb */
				                + _tcslen(szExec)+2
				                + ((pszCmd == NULL) ? 0 : (_tcslen(pszCmd)+2))
				                + _tcslen(szCurrentDirectory) + 2
				                );
			mp_sei = (SHELLEXECUTEINFO*)calloc(nWholeSize, 1);
			mp_sei->cbSize = sizeof(SHELLEXECUTEINFO);
			mp_sei->hwnd = ghWnd;
			//mp_sei->hwnd = /*NULL; */ ghWnd; // почему я тут NULL ставил?

			// 121025 - remove SEE_MASK_NOCLOSEPROCESS
			mp_sei->fMask = SEE_MASK_NO_CONSOLE|SEE_MASK_NOASYNC;
			// Issue 791: Console server fails to duplicate self Process handle to GUI
			mp_sei->fMask |= SEE_MASK_NOCLOSEPROCESS;

			mp_sei->lpVerb = (wchar_t*)(mp_sei+1);
			wcscpy((wchar_t*)mp_sei->lpVerb, L"runas");
			mp_sei->lpFile = mp_sei->lpVerb + _tcslen(mp_sei->lpVerb) + 2;
			wcscpy((wchar_t*)mp_sei->lpFile, szExec);
			mp_sei->lpParameters = mp_sei->lpFile + _tcslen(mp_sei->lpFile) + 2;

			if (pszCmd)
			{
				*(wchar_t*)mp_sei->lpParameters = L' ';
				wcscpy((wchar_t*)(mp_sei->lpParameters+1), pszCmd);
			}

			mp_sei->lpDirectory = mp_sei->lpParameters + _tcslen(mp_sei->lpParameters) + 2;

			if (szCurrentDirectory[0])
				wcscpy((wchar_t*)mp_sei->lpDirectory, szCurrentDirectory);
			else
				mp_sei->lpDirectory = NULL;

			//mp_sei->nShow = gpSet->isConVisible ? SW_SHOWNORMAL : SW_HIDE;
			//mp_sei->nShow = SW_SHOWMINIMIZED;
			mp_sei->nShow = SW_SHOWNORMAL;

			// GuiShellExecuteEx запускается в основном потоке, поэтому nCreateDuration здесь не считаем
			SetConStatus((gOSVer.dwMajorVersion>=6) ? L"Starting root process as Administrator..." : L"Starting root process as user...", true);
			//lbRc = gpConEmu->GuiShellExecuteEx(mp_sei, mp_VCon);

			bool bPrevIgnore = gpConEmu->mb_IgnoreQuakeActivation;
			gpConEmu->mb_IgnoreQuakeActivation = true;

			lbRc = ShellExecuteEx(mp_sei);

			gpConEmu->mb_IgnoreQuakeActivation = bPrevIgnore;

			// ошибку покажем дальше
			dwLastError = GetLastError();
		}
	}

	if (lbRc)
	{
		if (!m_Args.bRunAsAdministrator)
		{
			ProcessUpdate(&pi.dwProcessId, 1);
			AllowSetForegroundWindow(pi.dwProcessId);
		}

		// 131022 If any other process was activated by user during our initialization - don't grab the focus
		HWND hCurFore = NULL;
		bool bMeFore = gpConEmu->isMeForeground(true/*real*/, true/*dialog*/, &hCurFore) || (hCurFore == NULL);
		DEBUGTEST(wchar_t szForeClass[255] = L"");
		if (!bMeFore && !::IsWindowVisible(hCurFore))
		{
			// But current console window handle may not initialized yet, try to check it
			#ifdef _DEBUG
			GetClassName(hCurFore, szForeClass, countof(szForeClass));
			_ASSERTE(!isConsoleClass(szForeClass)); // Some other GUI application was started by user?
			#endif
			bMeFore = true;
		}
		if (bMeFore)
		{
			apiSetForegroundWindow(hSetForeground);
		}

		DEBUGSTRPROC(L"CreateProcess OK\n");
		//lbRc = TRUE;
		/*if (!AttachPID(pi.dwProcessId)) {
			DEBUGSTRPROC(_T("AttachPID failed\n"));
			SetEvent(mh_CreateRootEvent); mb_InCreateRoot = FALSE;
			{ lbRc = FALSE; goto wrap; }
		}
		DEBUGSTRPROC(_T("AttachPID OK\n"));*/
		//break; // OK, запустили
	}

	/* End of process start-up */
	return lbRc;
}

// Инициализировать имена пайпов, событий, мэппингов и т.п.
// Только имена - реальных хэндлов еще может не быть!
void CRealConsole::InitNames()
{
	// Имя пайпа для управления ConEmuC
	_wsprintf(ms_ConEmuC_Pipe, SKIPLEN(countof(ms_ConEmuC_Pipe)) CESERVERPIPENAME, L".", mn_MainSrv_PID);
	_wsprintf(ms_MainSrv_Pipe, SKIPLEN(countof(ms_MainSrv_Pipe)) CESERVERPIPENAME, L".", mn_MainSrv_PID);
	_wsprintf(ms_ConEmuCInput_Pipe, SKIPLEN(countof(ms_ConEmuCInput_Pipe)) CESERVERINPUTNAME, L".", mn_MainSrv_PID);
	// Имя событие измененности данных в консоли
	m_ConDataChanged.InitName(CEDATAREADYEVENT, mn_MainSrv_PID);
	//swprintf_c(ms_ConEmuC_DataReady, CEDATAREADYEVENT, mn_MainSrv_PID);
	MCHKHEAP;
	_ASSERTE(mn_AltSrv_PID==0); // Инициализация должна идти на основной сервер
	m_GetDataPipe.InitName(gpConEmu->GetDefaultTitle(), CESERVERREADNAME, L".", mn_MainSrv_PID);
	// Enable overlapped mode and termination by event
	_ASSERTE(mh_TermEvent!=NULL);
	m_GetDataPipe.SetTermEvent(mh_TermEvent);
}

// Если включена прокрутка - скорректировать индекс ячейки из экранных в буферные
COORD CRealConsole::ScreenToBuffer(COORD crMouse)
{
	if (!this)
		return crMouse;
	return mp_ABuf->ScreenToBuffer(crMouse);
}

COORD CRealConsole::BufferToScreen(COORD crMouse, bool bVertOnly /*= false*/)
{
	if (!this)
		return crMouse;
	return mp_ABuf->BufferToScreen(crMouse, bVertOnly);
}

// x,y - экранные координаты
// Если abForceSend==true - не проверять на "повторность" события, и не проверять "isPressed(VK_?BUTTON)"
void CRealConsole::OnMouse(UINT messg, WPARAM wParam, int x, int y, bool abForceSend /*= false*/, bool abFromTouch /*= false*/)
{
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL                  0x020E
#endif
#ifdef _DEBUG
	wchar_t szDbg[60]; _wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"RCon::MouseEvent at DC {%ix%i}\n", x,y);
	DEBUGSTRINPUT(szDbg);
#endif

	if (!this || !hConWnd)
		return;

	if (messg != WM_MOUSEMOVE)
	{
		mcr_LastMouseEventPos.X = mcr_LastMouseEventPos.Y = -1;
	}

	// Если включен фаровский граббер - то координаты нужны скорректированные, чтобы точно позиции выделять
	TODO("StrictMonospace включать пока нельзя, т.к. сбивается клик в редакторе, например. Да и в диалогах есть текстовые поля!");
	bool bStrictMonospace = false; //!isConSelectMode(); // она реагирует и на фаровский граббер

	// Получить известные координаты символов
	COORD crMouse = ScreenToBuffer(mp_VCon->ClientToConsole(x,y, bStrictMonospace));
	
	if (mp_ABuf->OnMouse(messg, wParam, x, y, crMouse, abFromTouch))
		return; // В консоль не пересылать, событие обработал "сам буфер"


	if (isFar() && gpConEmu->IsGesturesEnabled())
	{
		//120830 - для Far Manager: облегчить "попадание"
		if (messg == WM_LBUTTONDOWN)
		{
			bool bChanged = false;
			mcr_MouseTapReal = crMouse;

			// Клик мышкой в {0x0} гасит-показывает панели, но на планшете - фиг попадешь.
			// Тап в область часов будет делать то же самое
			// Считаем, что если тап пришелся на 2-ю строку - тоже.
			// В редакторе/вьювере не дергаться - там смысла нет.
			if (!(isEditor() || isViewer()) && (crMouse.Y <= 1) && ((crMouse.X + 5) >= (int)TextWidth()))
			{
				bChanged = true;
				mcr_MouseTapChanged = MakeCoord(0,0);
			}

			if (bChanged)
			{
				crMouse = mcr_MouseTapChanged;
				mb_MouseTapChanged = TRUE;
			}
			else
			{
				mb_MouseTapChanged = FALSE;
			}

		}
		else if (mb_MouseTapChanged && (messg == WM_LBUTTONUP || messg == WM_MOUSEMOVE))
		{
			if (mcr_MouseTapReal.X == crMouse.X && mcr_MouseTapReal.Y == crMouse.Y)
			{
				crMouse = mcr_MouseTapChanged;
			}
			else
			{
				mb_MouseTapChanged = FALSE;
			}
		}
	}
	else
	{
		mb_MouseTapChanged = FALSE;
	}


	const Settings::AppSettings* pApp = NULL;
	if ((messg == WM_LBUTTONDOWN) //&& gpSet->isCTSClickPromptPosition
		&& ((pApp = gpSet->GetAppSettings(GetActiveAppSettingsId())) != NULL)
		&& pApp->CTSClickPromptPosition()
		&& gpSet->IsModifierPressed(vkCTSVkPromptClk, true))
	{
		DWORD nActivePID = GetActivePID();
		if (nActivePID && (mp_ABuf->m_Type == rbt_Primary) && !isFar() && !isNtvdm())
		{
			mb_WasSendClickToReadCon = false; // сначала - сброс
			CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_MOUSECLICK, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_PROMPTACTION));
			if (pIn)
			{
				pIn->Prompt.xPos = crMouse.X;
				pIn->Prompt.yPos = crMouse.Y;
				pIn->Prompt.Force = (pApp->CTSClickPromptPosition() == 1);
				pIn->Prompt.BashMargin = pApp->CTSBashMargin();

				CESERVER_REQ* pOut = ExecuteHkCmd(nActivePID, pIn, ghWnd);
				if (pOut && (pOut->DataSize() >= sizeof(DWORD)))
				{
					mb_WasSendClickToReadCon = (pOut->dwData[0] != 0);
				}
				ExecuteFreeResult(pOut);
				ExecuteFreeResult(pIn);
			}

			if (mb_WasSendClickToReadCon)
				return; // уже клик обработали (перемещение текствого курсора в ReadConsoleW)
		}
		else
		{
			mb_WasSendClickToReadCon = false;
		}
	}
	else if (messg == WM_LBUTTONUP && mb_WasSendClickToReadCon)
	{
		mb_WasSendClickToReadCon = false;
		return; // однократно, клик пропускаем
	}

	// Если юзер запретил посылку мышиных событий в консоль
	if (gpSet->isDisableMouse)
		return;

	// Issue 1165: Don't send "Mouse events" into console input buffer if ENABLE_QUICK_EDIT_MODE
	if (!abForceSend && !isSendMouseAllowed())
	{
		return;
	}

	DEBUGTEST(bool lbFarBufferSupported = isFarBufferSupported());

	// Если консоль в режиме с прокруткой - не посылать мышь в консоль
	// Иначе получаются казусы. Если во время выполнения команды (например "dir c: /s")
	// успеть дернуть мышкой - то при возврате в ФАР сразу пойдет фаровский драг
	// -- if (isBufferHeight() && !lbFarBufferSupported)
	// -- заменено на сброс мышиных событий в ConEmuHk при завершении консольного приложения
	if (isFarInStack() && !gpSet->isUseInjects)
		return;

	PostMouseEvent(messg, wParam, crMouse, abForceSend);

	if (messg == WM_MOUSEMOVE)
	{
		m_LastMouseGuiPos.x = x; m_LastMouseGuiPos.y = y;
	}
}

void CRealConsole::PostMouseEvent(UINT messg, WPARAM wParam, COORD crMouse, bool abForceSend /*= false*/)
{
	// По идее, мышь в консоль может пересылаться, только
	// если активный буфер - буфер реальной консоли
	_ASSERTE(mp_ABuf==mp_RBuf);

	INPUT_RECORD r; memset(&r, 0, sizeof(r));
	r.EventType = MOUSE_EVENT;

	// Mouse Buttons
	if (messg != WM_LBUTTONUP && (messg == WM_LBUTTONDOWN || messg == WM_LBUTTONDBLCLK || (!abForceSend && isPressed(VK_LBUTTON))))
		r.Event.MouseEvent.dwButtonState |= FROM_LEFT_1ST_BUTTON_PRESSED;

	if (messg != WM_RBUTTONUP && (messg == WM_RBUTTONDOWN || messg == WM_RBUTTONDBLCLK || (!abForceSend && isPressed(VK_RBUTTON))))
		r.Event.MouseEvent.dwButtonState |= RIGHTMOST_BUTTON_PRESSED;

	if (messg != WM_MBUTTONUP && (messg == WM_MBUTTONDOWN || messg == WM_MBUTTONDBLCLK || (!abForceSend && isPressed(VK_MBUTTON))))
		r.Event.MouseEvent.dwButtonState |= FROM_LEFT_2ND_BUTTON_PRESSED;

	if (messg != WM_XBUTTONUP && (messg == WM_XBUTTONDOWN || messg == WM_XBUTTONDBLCLK))
	{
		if ((HIWORD(wParam) & 0x0001/*XBUTTON1*/))
			r.Event.MouseEvent.dwButtonState |= 0x0008/*FROM_LEFT_3ND_BUTTON_PRESSED*/;
		else if ((HIWORD(wParam) & 0x0002/*XBUTTON2*/))
			r.Event.MouseEvent.dwButtonState |= 0x0010/*FROM_LEFT_4ND_BUTTON_PRESSED*/;
	}

	mb_MouseButtonDown = (r.Event.MouseEvent.dwButtonState
	                      & (FROM_LEFT_1ST_BUTTON_PRESSED|FROM_LEFT_2ND_BUTTON_PRESSED|RIGHTMOST_BUTTON_PRESSED)) != 0;

	// Key modifiers
	if (GetKeyState(VK_CAPITAL) & 1)
		r.Event.MouseEvent.dwControlKeyState |= CAPSLOCK_ON;

	if (GetKeyState(VK_NUMLOCK) & 1)
		r.Event.MouseEvent.dwControlKeyState |= NUMLOCK_ON;

	if (GetKeyState(VK_SCROLL) & 1)
		r.Event.MouseEvent.dwControlKeyState |= SCROLLLOCK_ON;

	if (isPressed(VK_LMENU))
		r.Event.MouseEvent.dwControlKeyState |= LEFT_ALT_PRESSED;

	if (isPressed(VK_RMENU))
		r.Event.MouseEvent.dwControlKeyState |= RIGHT_ALT_PRESSED;

	if (isPressed(VK_LCONTROL))
		r.Event.MouseEvent.dwControlKeyState |= LEFT_CTRL_PRESSED;

	if (isPressed(VK_RCONTROL))
		r.Event.MouseEvent.dwControlKeyState |= RIGHT_CTRL_PRESSED;

	if (isPressed(VK_SHIFT))
		r.Event.MouseEvent.dwControlKeyState |= SHIFT_PRESSED;

	if (messg == WM_LBUTTONDBLCLK || messg == WM_RBUTTONDBLCLK || messg == WM_MBUTTONDBLCLK)
		r.Event.MouseEvent.dwEventFlags = DOUBLE_CLICK;
	else if (messg == WM_MOUSEMOVE)
		r.Event.MouseEvent.dwEventFlags = MOUSE_MOVED;
	else if (messg == WM_MOUSEWHEEL)
	{
		#ifdef SHOWDEBUGSTR
		{
			wchar_t szDbgMsg[128]; _wsprintf(szDbgMsg, SKIPLEN(countof(szDbgMsg)) L"WM_MOUSEWHEEL(%i, Btns=0x%04X, x=%i, y=%i)\n",
				(int)(short)HIWORD(wParam), (DWORD)LOWORD(wParam), crMouse.X, crMouse.Y);
			DEBUGSTRWHEEL(szDbgMsg);			
		}
		#endif
		if (m_UseLogs>=2)
		{
			char szDbgMsg[128]; _wsprintfA(szDbgMsg, SKIPLEN(countof(szDbgMsg)) "WM_MOUSEWHEEL(wParam=0x%08X, x=%i, y=%i)", (DWORD)wParam, crMouse.X, crMouse.Y);
			LogString(szDbgMsg);
		}

		WARNING("Если включен режим прокрутки - посылать команду пайпа, а не мышиное событие");
		// Иначе на 2008 server вообще не крутится
		r.Event.MouseEvent.dwEventFlags = MOUSE_WHEELED;
		SHORT nScroll = (SHORT)(((DWORD)wParam & 0xFFFF0000)>>16);

		if (nScroll<0) { if (nScroll>-120) nScroll=-120; }
		else { if (nScroll<120) nScroll=120; }

		if (nScroll<-120 || nScroll>120)
			nScroll = ((SHORT)(nScroll / 120)) * 120;

		r.Event.MouseEvent.dwButtonState |= ((DWORD)(WORD)nScroll) << 16;
		//r.Event.MouseEvent.dwButtonState |= /*(0xFFFF0000 & wParam)*/ (nScroll > 0) ? 0x00780000 : 0xFF880000;
	}
	else if (messg == WM_MOUSEHWHEEL)
	{
		if (m_UseLogs>=2)
		{
			char szDbgMsg[128]; _wsprintfA(szDbgMsg, SKIPLEN(countof(szDbgMsg)) "WM_MOUSEHWHEEL(wParam=0x%08X, x=%i, y=%i)", (DWORD)wParam, crMouse.X, crMouse.Y);
			LogString(szDbgMsg);
		}

		r.Event.MouseEvent.dwEventFlags = 8; //MOUSE_HWHEELED
		SHORT nScroll = (SHORT)(((DWORD)wParam & 0xFFFF0000)>>16);

		if (nScroll<0) { if (nScroll>-120) nScroll=-120; }
		else { if (nScroll<120) nScroll=120; }

		if (nScroll<-120 || nScroll>120)
			nScroll = ((SHORT)(nScroll / 120)) * 120;

		r.Event.MouseEvent.dwButtonState |= ((DWORD)(WORD)nScroll) << 16;
	}

	if (messg == WM_LBUTTONDOWN || messg == WM_RBUTTONDOWN || messg == WM_MBUTTONDOWN)
	{
		mb_BtnClicked = TRUE; mrc_BtnClickPos = crMouse;
	}

	// В Far3 поменяли действие ПКМ 0_0
	bool lbRBtnDrag = (r.Event.MouseEvent.dwButtonState & RIGHTMOST_BUTTON_PRESSED) == RIGHTMOST_BUTTON_PRESSED;
	bool lbNormalRBtnMode = false;
	// gpSet->isRSelFix добавлен, чтобы этот fix можно было отключить
	if (lbRBtnDrag && isFar(TRUE) && m_FarInfo.cbSize && gpSet->isRSelFix)
	{
		if ((m_FarInfo.FarVer.dwVerMajor > 3) || (m_FarInfo.FarVer.dwVerMajor == 3 && m_FarInfo.FarVer.dwBuild >= 2381))
		{
			if (gpSet->isRClickSendKey == 0)
			{
				// Если не нажаты LCtrl/RCtrl/LAlt/RAlt - считаем что выделение не идет, отдаем в фар "как есть"
				if (0 == (r.Event.MouseEvent.dwControlKeyState & (RIGHT_ALT_PRESSED|LEFT_ALT_PRESSED|RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED)))
					lbRBtnDrag = false;
			}
			else
			{
				COORD crVisible = mp_ABuf->BufferToScreen(crMouse,true);
				// Координата попадает в панель (включая правую/левую рамку)?
				if (CoordInPanel(crVisible, TRUE))
				{
					if (!(r.Event.MouseEvent.dwControlKeyState & (SHIFT_PRESSED|RIGHT_ALT_PRESSED|LEFT_ALT_PRESSED|RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED)))
					{
						lbNormalRBtnMode = true;
						// 18.09.2012 Maks - LEFT_CTRL_PRESSED -> RIGHT_CTRL_PRESSED, cause of AltGr
						r.Event.MouseEvent.dwControlKeyState |= RIGHT_ALT_PRESSED|RIGHT_CTRL_PRESSED;
					}
				}
			}
		}
	}
	UNREFERENCED_PARAMETER(lbNormalRBtnMode);

	if (messg == WM_MOUSEMOVE /*&& mb_MouseButtonDown*/)
	{
		// Issue 172: проблема с правым кликом на PanelTabs
		//if (mcr_LastMouseEventPos.X == crMouse.X && mcr_LastMouseEventPos.Y == crMouse.Y)
		//	return; // не посылать в консоль MouseMove на том же месте
		//mcr_LastMouseEventPos.X = crMouse.X; mcr_LastMouseEventPos.Y = crMouse.Y;
		//// Проверять будем по пикселам, иначе AltIns начинает выделять со следующей позиции
		//int nDeltaX = (m_LastMouseGuiPos.x > x) ? (m_LastMouseGuiPos.x - x) : (x - m_LastMouseGuiPos.x);
		//int nDeltaY = (m_LastMouseGuiPos.y > y) ? (m_LastMouseGuiPos.y - y) : (y - m_LastMouseGuiPos.y);
		// Теперь - проверяем по координатам консоли, а не экрана.
		// Этого достаточно, AltIns не глючит, т.к. смена "типа события" (клик/движение) также отслеживается
		int nDeltaX = m_LastMouse.dwMousePosition.X - crMouse.X;
		int nDeltaY = m_LastMouse.dwMousePosition.Y - crMouse.Y;

		// Последний посланный m_LastMouse запоминается в PostConsoleEvent
		if (m_LastMouse.dwEventFlags == MOUSE_MOVED // только если последним - был послан НЕ клик
				&& m_LastMouse.dwButtonState     == r.Event.MouseEvent.dwButtonState
		        && m_LastMouse.dwControlKeyState == r.Event.MouseEvent.dwControlKeyState
		        //&& (nDeltaX <= 1 && nDeltaY <= 1) // был 1 пиксел
		        && !nDeltaX && !nDeltaY // стал 1 символ
		        && !abForceSend // и если не просили точно послать
		        )
			return; // не посылать в консоль MouseMove на том же месте

		if (mb_BtnClicked)
		{
			// Если после LBtnDown в ЭТУ же позицию не был послан MOUSE_MOVE - дослать в mrc_BtnClickPos
			if (mb_MouseButtonDown && (mrc_BtnClickPos.X != crMouse.X || mrc_BtnClickPos.Y != crMouse.Y))
			{
				r.Event.MouseEvent.dwMousePosition = mrc_BtnClickPos;
				PostConsoleEvent(&r);
			}

			mb_BtnClicked = FALSE;
		}

		//m_LastMouseGuiPos.x = x; m_LastMouseGuiPos.y = y;
		mcr_LastMouseEventPos.X = crMouse.X; mcr_LastMouseEventPos.Y = crMouse.Y;
	}

	// При БЫСТРОМ драге правой кнопкой мышки выделение в панели получается прерывистым. Исправим это.
	if (gpSet->isRSelFix)
	{
		// Имеет смысл только если в GUI сейчас показывается реальный буфер
		if (mp_ABuf != mp_RBuf)
		{
			mp_RBuf->SetRBtnDrag(FALSE);
		}
		else
		{
			//BOOL lbRBtnDrag = (r.Event.MouseEvent.dwButtonState & RIGHTMOST_BUTTON_PRESSED) == RIGHTMOST_BUTTON_PRESSED;
			COORD con_crRBtnDrag = {};
			BOOL con_bRBtnDrag = mp_RBuf->GetRBtnDrag(&con_crRBtnDrag);

			if (con_bRBtnDrag && !lbRBtnDrag)
			{
				con_bRBtnDrag = FALSE;
				mp_RBuf->SetRBtnDrag(FALSE);
			}
			else if (con_bRBtnDrag)
			{
				#ifdef _DEBUG
				SHORT nXDelta = crMouse.X - con_crRBtnDrag.X;
				#endif
				SHORT nYDelta = crMouse.Y - con_crRBtnDrag.Y;

				if (nYDelta < -1 || nYDelta > 1)
				{
					// Если после предыдущего драга прошло более 1 строки
					SHORT nYstep = (nYDelta < -1) ? -1 : 1;
					SHORT nYend = crMouse.Y; // - nYstep;
					crMouse.Y = con_crRBtnDrag.Y + nYstep;

					// досылаем пропущенные строки
					while (crMouse.Y != nYend)
					{
						#ifdef _DEBUG
						wchar_t szDbg[60]; _wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"+++ Add right button drag: {%ix%i}\n", crMouse.X, crMouse.Y);
						DEBUGSTRINPUT(szDbg);
						#endif
						
						r.Event.MouseEvent.dwMousePosition = crMouse;
						PostConsoleEvent(&r);
						crMouse.Y += nYstep;
					}
				}
			}

			if (lbRBtnDrag)
			{
				mp_RBuf->SetRBtnDrag(TRUE, &crMouse);
			}
		}
	}

	r.Event.MouseEvent.dwMousePosition = crMouse;

	if (mn_FarPID && mn_FarPID != mn_LastSetForegroundPID)
	{
		AllowSetForegroundWindow(mn_FarPID);
		mn_LastSetForegroundPID = mn_FarPID;
	}

	// Посылаем событие в консоль через ConEmuC
	PostConsoleEvent(&r);
}

void CRealConsole::StartSelection(BOOL abTextMode, SHORT anX/*=-1*/, SHORT anY/*=-1*/, BOOL abByMouse/*=FALSE*/)
{
	mp_ABuf->StartSelection(abTextMode, anX, anY, abByMouse);
}

void CRealConsole::ExpandSelection(SHORT anX, SHORT anY)
{
	mp_ABuf->ExpandSelection(anX, anY);
}

void CRealConsole::DoSelectionStop()
{
	mp_ABuf->DoSelectionStop();
}

bool CRealConsole::DoSelectionCopy(bool bCopyAll /*= false*/, BYTE nFormat /*= 0xFF*/ /* use gpSet->isCTSHtmlFormat */)
{
	bool bCopyRc = false;
	bool bReturnPrimary = false;
	CRealBuffer* pBuf = mp_ABuf;

	if (bCopyAll)
	{
		if (pBuf->m_Type == rbt_Primary)
		{
			COORD crEnd = {0,0};
			mp_ABuf->GetCursorInfo(&crEnd, NULL);
			crEnd.X = mp_ABuf->GetBufferWidth()-1;
			//crEnd = mp_ABuf->ScreenToBuffer(crEnd);

			if (LoadAlternativeConsole(lam_FullBuffer) && (mp_ABuf->m_Type != rbt_Primary))
			{
				bReturnPrimary = true;
				pBuf = mp_ABuf;
				pBuf->m_Type = rbt_Selection;
				pBuf->StartSelection(TRUE, 0, 0);
				pBuf->ExpandSelection(crEnd.X, crEnd.Y);
			}
		}
		else
		{
			MsgBox(L"Return to Primary buffer first!", MB_ICONEXCLAMATION);
			goto wrap;
		}
	}

	bCopyRc = pBuf->DoSelectionCopy(bCopyAll, nFormat);
wrap:
	if (bReturnPrimary)
	{
		SetActiveBuffer(rbt_Primary);
	}
	return bCopyRc;
}

void CRealConsole::DoFindText(int nDirection)
{
	if (!this)
		return;

	if (gpSet->FindOptions.bFreezeConsole)
	{
		if (mp_ABuf->m_Type == rbt_Primary)
		{
			if (LoadAlternativeConsole(lam_FullBuffer) && (mp_ABuf->m_Type != rbt_Primary))
			{
				mp_ABuf->m_Type = rbt_Find;
			}
		}
	}
	else
	{
		if (mp_ABuf && (mp_ABuf->m_Type == rbt_Find))
		{
			SetActiveBuffer(rbt_Primary);
		}
	}
	mp_ABuf->MarkFindText(nDirection, gpSet->FindOptions.pszText, gpSet->FindOptions.bMatchCase, gpSet->FindOptions.bMatchWholeWords);
}

void CRealConsole::DoEndFindText()
{
	if (!this)
		return;

	if (mp_ABuf && (mp_ABuf->m_Type == rbt_Find))
	{
		SetActiveBuffer(rbt_Primary);
	}
}

BOOL CRealConsole::OpenConsoleEventPipe()
{
	if (mh_ConInputPipe && mh_ConInputPipe!=INVALID_HANDLE_VALUE)
	{
		CloseHandle(mh_ConInputPipe); mh_ConInputPipe = NULL;
	}

	TODO("Если пайп с таким именем не появится в течении 10 секунд (минуты?) - закрыть VirtualConsole показав ошибку");
	// Try to open a named pipe; wait for it, if necessary.
	int nSteps = 10;
	BOOL fSuccess;
	DWORD dwErr = 0, dwWait = 0;

	while ((nSteps--) > 0)
	{
		mh_ConInputPipe = CreateFile(
		                      ms_ConEmuCInput_Pipe,// pipe name
		                      GENERIC_WRITE,
		                      0,              // no sharing
		                      NULL,           // default security attributes
		                      OPEN_EXISTING,  // opens existing pipe
		                      0,              // default attributes
		                      NULL);          // no template file

		// Break if the pipe handle is valid.
		if (mh_ConInputPipe != INVALID_HANDLE_VALUE)
		{
			// The pipe connected; change to message-read mode.
			DWORD dwMode = CE_PIPE_READMODE;
			fSuccess = SetNamedPipeHandleState(
			               mh_ConInputPipe,    // pipe handle
			               &dwMode,  // new pipe mode
			               NULL,     // don't set maximum bytes
			               NULL);    // don't set maximum time

			if (!fSuccess /*&& !gbIsWine*/)
			{
				DEBUGSTRINPUT(L" - FAILED!\n");
				dwErr = GetLastError();
				//SafeCloseHandle(mh_ConInputPipe);

				//if (!IsDebuggerPresent())
				if (!isConsoleClosing() && !gbIsWine)
					DisplayLastError(L"SetNamedPipeHandleState failed", dwErr);

				//return FALSE;
			}

			return TRUE;
		}

		// Exit if an error other than ERROR_PIPE_BUSY occurs.
		dwErr = GetLastError();

		if (dwErr != ERROR_PIPE_BUSY)
		{
			TODO("Подождать, пока появится пайп с таким именем, но только пока жив mh_MainSrv");
			dwWait = WaitForSingleObject(mh_MainSrv, 100);

			if (dwWait == WAIT_OBJECT_0)
			{
				DEBUGSTRINPUT(L"ConEmuC was closed. OpenPipe FAILED!\n");
				return FALSE;
			}

			if (!isConsoleClosing())
				break;

			continue;
			//DisplayLastError(L"Could not open pipe", dwErr);
			//return 0;
		}

		// All pipe instances are busy, so wait for 0.1 second.
		if (!WaitNamedPipe(ms_ConEmuCInput_Pipe, 100))
		{
			dwErr = GetLastError();
			dwWait = WaitForSingleObject(mh_MainSrv, 100);

			if (dwWait == WAIT_OBJECT_0)
			{
				DEBUGSTRINPUT(L"ConEmuC was closed. OpenPipe FAILED!\n");
				return FALSE;
			}

			if (!isConsoleClosing())
				DisplayLastError(L"WaitNamedPipe failed", dwErr);

			return FALSE;
		}
	}

	if (mh_ConInputPipe == NULL || mh_ConInputPipe == INVALID_HANDLE_VALUE)
	{
		// Не дождались появления пайпа. Возможно, ConEmuC еще не запустился
		//DEBUGSTRINPUT(L" - mh_ConInputPipe not found!\n");
#ifdef _DEBUG
		DWORD dwTick1 = GetTickCount();
		struct ServerClosing sc1 = m_ServerClosing;
#endif

		if (!isConsoleClosing())
		{
#ifdef _DEBUG
			DWORD dwTick2 = GetTickCount();
			struct ServerClosing sc2 = m_ServerClosing;

			if (dwErr == WAIT_TIMEOUT)
			{
				TODO("Иногда трапится. Проверить m_ServerClosing.nServerPID. Может его выставлять при щелчке по крестику?");
				MyAssertTrap();
			}

			DWORD dwTick3 = GetTickCount();
			struct ServerClosing sc3 = m_ServerClosing;
#endif
			int nLen = _tcslen(ms_ConEmuCInput_Pipe) + 128;
			wchar_t* pszErrMsg = (wchar_t*)malloc(nLen*sizeof(wchar_t));
			_wsprintf(pszErrMsg, SKIPLEN(nLen) L"ConEmuCInput not found, ErrCode=0x%08X\n%s", dwErr, ms_ConEmuCInput_Pipe);
			//DisplayLastError(L"mh_ConInputPipe not found", dwErr);
			// Выполняем Post-ом, т.к. консоль может уже закрываться (по стеку сообщений)? А сервер еще не прочухал...
			gpConEmu->PostDisplayRConError(this, pszErrMsg);
		}

		return FALSE;
	}

	return FALSE;
}

bool CRealConsole::PostConsoleEventPipe(MSG64 *pMsg, size_t cchCount /*= 1*/)
{
	if (!pMsg || !cchCount)
	{
		_ASSERTE(pMsg && (cchCount>0));
		return false;
	}

	DWORD dwErr = 0; //, dwMode = 0;
	bool lbOk = false;
	BOOL fSuccess = FALSE;

	#ifdef _DEBUG
	if (gbInSendConEvent)
	{
		_ASSERTE(!gbInSendConEvent);
	}
	#endif

	// Пайп есть. Проверим, что ConEmuC жив
	if (!isServerAlive())
	{
		//DisplayLastError(L"ConEmuC was terminated");
		LogString("PostConsoleEventPipe skipped due to server not alive");
		return false;
	}

	TODO("Если пайп с таким именем не появится в течении 10 секунд (минуты?) - закрыть VirtualConsole показав ошибку");

	if (mh_ConInputPipe==NULL || mh_ConInputPipe==INVALID_HANDLE_VALUE)
	{
		// Try to open a named pipe; wait for it, if necessary.
		if (!OpenConsoleEventPipe())
		{
			LogString("PostConsoleEventPipe skipped due to OpenConsoleEventPipe failed");
			return false;
		}

		//int nSteps = 10;
		//while ((nSteps--) > 0)
		//{
		//  mh_ConInputPipe = CreateFile(
		//     ms_ConEmuCInput_Pipe,// pipe name
		//     GENERIC_WRITE,
		//     0,              // no sharing
		//     NULL,           // default security attributes
		//     OPEN_EXISTING,  // opens existing pipe
		//     0,              // default attributes
		//     NULL);          // no template file
		//
		//  // Break if the pipe handle is valid.
		//  if (mh_ConInputPipe != INVALID_HANDLE_VALUE)
		//     break;
		//
		//  // Exit if an error other than ERROR_PIPE_BUSY occurs.
		//  dwErr = GetLastError();
		//  if (dwErr != ERROR_PIPE_BUSY)
		//  {
		//    TODO("Подождать, пока появится пайп с таким именем, но только пока жив mh_MainSrv");
		//    dwErr = WaitForSingleObject(mh_MainSrv, 100);
		//    if (dwErr == WAIT_OBJECT_0) {
		//        return;
		//    }
		//    continue;
		//    //DisplayLastError(L"Could not open pipe", dwErr);
		//    //return 0;
		//  }
		//
		//  // All pipe instances are busy, so wait for 0.1 second.
		//  if (!WaitNamedPipe(ms_ConEmuCInput_Pipe, 100) )
		//  {
		//    dwErr = WaitForSingleObject(mh_MainSrv, 100);
		//    if (dwErr == WAIT_OBJECT_0) {
		//        DEBUGSTRINPUT(L" - FAILED!\n");
		//        return;
		//    }
		//    //DisplayLastError(L"WaitNamedPipe failed");
		//    //return 0;
		//  }
		//}
		//if (mh_ConInputPipe == NULL || mh_ConInputPipe == INVALID_HANDLE_VALUE) {
		//    // Не дождались появления пайпа. Возможно, ConEmuC еще не запустился
		//    DEBUGSTRINPUT(L" - mh_ConInputPipe not found!\n");
		//    return;
		//}
		//
		//// The pipe connected; change to message-read mode.
		//dwMode = CE_PIPE_READMODE;
		//fSuccess = SetNamedPipeHandleState(
		//  mh_ConInputPipe,    // pipe handle
		//  &dwMode,  // new pipe mode
		//  NULL,     // don't set maximum bytes
		//  NULL);    // don't set maximum time
		//if (!fSuccess)
		//{
		//  DEBUGSTRINPUT(L" - FAILED!\n");
		//  DWORD dwErr = GetLastError();
		//  SafeCloseHandle(mh_ConInputPipe);
		//  if (!IsDebuggerPresent())
		//    DisplayLastError(L"SetNamedPipeHandleState failed", dwErr);
		//  return;
		//}
	}

	//// Пайп есть. Проверим, что ConEmuC жив
	//dwExitCode = 0;
	//fSuccess = GetExitCodeProcess(mh_MainSrv, &dwExitCode);
	//if (dwExitCode!=STILL_ACTIVE) {
	//    //DisplayLastError(L"ConEmuC was terminated");
	//    return;
	//}

	#ifdef _DEBUG
	switch (pMsg->msg[0].message)
	{
		case WM_KEYDOWN: case WM_SYSKEYDOWN:
			DEBUGSTRINPUTPIPE(L"ConEmu: Sending key down\n"); break;
		case WM_KEYUP: case WM_SYSKEYUP:
			DEBUGSTRINPUTPIPE(L"ConEmu: Sending key up\n"); break;
		default:
			DEBUGSTRINPUTPIPE(L"ConEmu: Sending input\n");
	}
	#endif

	gbInSendConEvent = TRUE;
	DWORD dwSize = sizeof(MSG64)+(cchCount-1)*sizeof(MSG64::MsgStr), dwWritten;
	//_ASSERTE(pMsg->cbSize==dwSize);
	pMsg->cbSize = dwSize;
	pMsg->nCount = cchCount;

	fSuccess = WriteFile(mh_ConInputPipe, pMsg, dwSize, &dwWritten, NULL);

	if (fSuccess)
	{
		lbOk = true;
	}
	else
	{
		dwErr = GetLastError();

		if (!isConsoleClosing())
		{
			if (dwErr == ERROR_NO_DATA/*0x000000E8*//*The pipe is being closed.*/
				|| dwErr == ERROR_PIPE_NOT_CONNECTED/*No process is on the other end of the pipe.*/)
			{
				if (!isServerAlive())
					goto wrap;

				if (OpenConsoleEventPipe())
				{
					fSuccess = WriteFile(mh_ConInputPipe, pMsg, dwSize, &dwWritten, NULL);

					if (fSuccess)
					{
						lbOk = true;
						goto wrap; // таки ОК
					}
				}
			}

			#ifdef _DEBUG
			//DisplayLastError(L"Can't send console event (pipe)", dwErr);
			wchar_t szDbg[128];
			_wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"Can't send console event (pipe)", dwErr);
			gpConEmu->DebugStep(szDbg);
			#endif
		}

		goto wrap;
	}

wrap:
	gbInSendConEvent = FALSE;

	return lbOk;
}

bool CRealConsole::PostConsoleMessage(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
	BOOL lbOk = FALSE;
	bool bNeedCmd = isAdministrator() || (m_Args.pszUserName != NULL);

	// 120630 - добавил WM_CLOSE, иначе в сервере не успевает выставиться флаг gbInShutdown
	if (nMsg == WM_INPUTLANGCHANGE || nMsg == WM_INPUTLANGCHANGEREQUEST || nMsg == WM_CLOSE)
		bNeedCmd = true;

	#ifdef _DEBUG
	wchar_t szDbg[255];
	if (nMsg == WM_INPUTLANGCHANGE || nMsg == WM_INPUTLANGCHANGEREQUEST)
	{
		const wchar_t* pszMsgID = (nMsg == WM_INPUTLANGCHANGE) ? L"WM_INPUTLANGCHANGE" : L"WM_INPUTLANGCHANGEREQUEST";
		const wchar_t* pszVia = bNeedCmd ? L"CmdExecute" : L"PostThreadMessage";
		_wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"RealConsole: %s, CP:%i, HKL:0x%08I64X via %s\n",
		          pszMsgID, (DWORD)wParam, (unsigned __int64)(DWORD_PTR)lParam, pszVia);
		DEBUGSTRLANG(szDbg);
	}

	_wsprintf(szDbg, SKIPLEN(countof(szDbg)) 
		L"PostMessage(x%08X, x%04X"
		WIN3264TEST(L", x%08X",L", x%08X%08X")
		WIN3264TEST(L", x%08X",L", x%08X%08X")
		L")\n",
	    (DWORD)hWnd, nMsg, WIN3264WSPRINT(wParam), WIN3264WSPRINT(lParam));
	DEBUGSTRSENDMSG(szDbg);
	#endif

	if (!bNeedCmd)
	{
		lbOk = POSTMESSAGE(hWnd/*hConWnd*/, nMsg, wParam, lParam, FALSE);
	}
	else
	{
		CESERVER_REQ in;
		ExecutePrepareCmd(&in, CECMD_POSTCONMSG, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_POSTMSG));
		// Собственно, аргументы
		in.Msg.bPost = TRUE;
		in.Msg.hWnd = hWnd;
		in.Msg.nMsg = nMsg;
		in.Msg.wParam = wParam;
		in.Msg.lParam = lParam;
		
		DWORD dwTickStart = timeGetTime();
		
		// сообщения рассылаем только через главный сервер. альтернативный (приложение) может висеть
		CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(true), &in, ghWnd);

		gpSetCls->debugLogCommand(&in, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);

		if (pOut)
		{
			lbOk = TRUE;
			ExecuteFreeResult(pOut);
		}
	}

	return (lbOk != FALSE);
}

void CRealConsole::StopSignal()
{
	DEBUGSTRCON(L"CRealConsole::StopSignal()\n");

	LogString(L"CRealConsole::StopSignal()", TRUE);

	if (!this)
		return;

	if (mn_ProcessCount)
	{
		MSectionLock SPRC; SPRC.Lock(&csPRC, TRUE);
		m_Processes.clear();
		SPRC.Unlock();
		mn_ProcessCount = 0;
	}

	mn_TermEventTick = GetTickCount();
	SetEvent(mh_TermEvent);

	if (!mn_InRecreate)
	{
		hGuiWnd = mh_GuiWndFocusStore = NULL;
		//mn_GuiApplicationPID = 0;

		// Чтобы при закрытии не было попытка активировать
		// другую вкладку ЭТОЙ консоли
		mn_tabsCount = 0;

		// Очистка массива консолей и обновление вкладок
		CConEmuChild::ProcessVConClosed(mp_VCon);
	}
}

void CRealConsole::StartStopXTerm(DWORD nPID, bool xTerm)
{
	if (gpSetCls->isAdvLogging)
	{
		wchar_t szInfo[100];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"StartStopXTerm(nPID=%u, xTerm=%u)", nPID, (UINT)xTerm);
		LogString(szInfo);
	}

	m_Term.nCallTermPID = nPID;
	m_Term.Term = xTerm ? te_xterm : te_win32;
}

void CRealConsole::StopThread(BOOL abRecreating)
{
#ifdef _DEBUG
	/*
	    HeapValidate(mh_Heap, 0, NULL);
	*/
#endif
	_ASSERTE(abRecreating==mb_ProcessRestarted);
	DEBUGSTRPROC(L"Entering StopThread\n");

	// выставление флагов и завершение потока
	if (mh_MonitorThread)
	{
		// Сначала выставить флаги закрытия
		StopSignal(); //SetEvent(mh_TermEvent);

		// А теперь можно ждать завершения
		if (WaitForSingleObject(mh_MonitorThread, 300) != WAIT_OBJECT_0)
		{
			// А это чтобы не осталось висеть окно ConEmu, раз всё уже закрыто
			gpConEmu->OnRConStartedSuccess(NULL);
			LogString(L"### Main Thread wating timeout, terminating...\n");
			DEBUGSTRPROC(L"### Main Thread wating timeout, terminating...\n");
			mb_WasForceTerminated = TRUE;
			TerminateThread(mh_MonitorThread, 1);
		}
		else
		{
			DEBUGSTRPROC(L"Main Thread closed normally\n");
		}

		SafeCloseHandle(mh_MonitorThread);
	}

	if (mh_PostMacroThread != NULL)
	{
		DWORD nWait = WaitForSingleObject(mh_PostMacroThread, 0);
		if (nWait == WAIT_OBJECT_0)
		{
			CloseHandle(mh_PostMacroThread);
			mh_PostMacroThread = NULL;
		}
		else
		{
			// Должен быть NULL, если нет - значит завис предыдующий макрос
			_ASSERTE(mh_PostMacroThread==NULL);
			TerminateThread(mh_PostMacroThread, 100);
			CloseHandle(mh_PostMacroThread);
		}
	}


	// Завершение серверных нитей этой консоли
	DEBUGSTRPROC(L"About to terminate main server thread (MonitorThread)\n");

	if (ms_VConServer_Pipe[0])  // значит хотя бы одна нить была запущена
	{
		StopSignal(); // уже должен быть выставлен, но на всякий случай


		// Закрыть серверные потоки (пайпы)
		m_RConServer.Stop();


		ms_VConServer_Pipe[0] = 0;
	}

	//if (mh_InputThread) {
	//    if (WaitForSingleObject(mh_InputThread, 300) != WAIT_OBJECT_0) {
	//        DEBUGSTRPROC(L"### Input Thread wating timeout, terminating...\n");
	//        TerminateThread(mh_InputThread, 1);
	//    } else {
	//        DEBUGSTRPROC(L"Input Thread closed normally\n");
	//    }
	//    SafeCloseHandle(mh_InputThread);
	//}

	if (!abRecreating)
	{
		SafeCloseHandle(mh_TermEvent);
		mn_TermEventTick = -1;
		SafeCloseHandle(mh_MonitorThreadEvent);
		//SafeCloseHandle(mh_PacketArrived);
	}

	if (abRecreating)
	{
		hConWnd = NULL;

		// Servers
		_ASSERTE((mn_AltSrv_PID==0) && "AltServer was not terminated?");
		//SafeCloseHandle(mh_AltSrv);
		SetAltSrvPID(0/*, NULL*/);
		//mn_AltSrv_PID = 0;
		SafeCloseHandle(mh_MainSrv);
		SetMainSrvPID(0, NULL);
		//mn_MainSrv_PID = 0;

		SetFarPID(0);
		SetFarPluginPID(0);
		SetActivePID(0);

		//mn_FarPID = mn_ActivePID = mn_FarPID_PluginDetected = 0;
		mn_LastSetForegroundPID = 0;
		//mn_ConEmuC_Input_TID = 0;
		SafeCloseHandle(mh_ConInputPipe);
		m_ConDataChanged.Close();
		m_GetDataPipe.Close();
		// Имя пайпа для управления ConEmuC
		ms_ConEmuC_Pipe[0] = 0;
		ms_MainSrv_Pipe[0] = 0;
		ms_ConEmuCInput_Pipe[0] = 0;
		// Закрыть все мэппинги
		CloseMapHeader();
		CloseColorMapping();
		mp_VCon->Invalidate();
	}

#ifdef _DEBUG
	/*
	    HeapValidate(mh_Heap, 0, NULL);
	*/
#endif
	DEBUGSTRPROC(L"Leaving StopThread\n");
}

bool CRealConsole::InScroll()
{
	if (!this || !mp_VCon || !isBufferHeight())
		return false;

	return mp_VCon->InScroll();
}

BOOL CRealConsole::isGuiVisible()
{
	if (!this)
		return FALSE;

	if (hGuiWnd)
	{
		//return !::IsWindowVisible(hGuiWnd);
		// IsWindowVisible не подходит, т.к. учитывает видимость и mh_WndDC
		DWORD_PTR nStyle = GetWindowLongPtr(hGuiWnd, GWL_STYLE);
		return (nStyle & WS_VISIBLE) != 0;
	}
	return FALSE;
}

BOOL CRealConsole::isGuiOverCon()
{
	if (!this)
		return FALSE;

	if (hGuiWnd && !mb_GuiExternMode)
	{
		return isGuiVisible();
	}

	return FALSE;
}

// Проверить, включен ли буфер (TRUE). Или высота окна равна высоте буфера (FALSE).
BOOL CRealConsole::isBufferHeight()
{
	if (!this)
		return FALSE;

	if (hGuiWnd)
	{
		return !isGuiVisible();
	}

	return mp_ABuf->isScroll();
}

// TRUE - если консоль "заморожена" (альтернативный буфер)
BOOL CRealConsole::isAlternative()
{
	if (!this)
		return FALSE;

	if (hGuiWnd)
		return FALSE;
	
	return (mp_ABuf && (mp_ABuf != mp_RBuf));
}

bool CRealConsole::isConSelectMode()
{
	if (!this || !mp_ABuf)
	{
		return false;
	}

	return mp_ABuf->isConSelectMode();
}

bool CRealConsole::isDetached()
{
	if (this == NULL)
	{
		return FALSE;
	}

	if (!mb_WasStartDetached && !mn_InRecreate)
	{
		return FALSE;
	}

	// Флажок ВООБЩЕ не сбрасываем - ориентируемся на хэндлы
	//_ASSERTE(!mb_Detached || (mb_Detached && (hConWnd==NULL)));
	return (mh_MainSrv == NULL);
}

BOOL CRealConsole::isWindowVisible()
{
	if (!this) return FALSE;

	if (!hConWnd) return FALSE;

	return IsWindowVisible(hConWnd);
}

LPCTSTR CRealConsole::GetTitle(bool abGetRenamed/*=false*/)
{
	// На старте mn_ProcessCount==0, а кнопку в тулбаре показывать уже нужно
	if (!this /*|| !mn_ProcessCount*/)
		return NULL;

	if (abGetRenamed && *ms_RenameFirstTab)
	{
		return ms_RenameFirstTab;
	}
		
	if (isAdministrator() && gpSet->szAdminTitleSuffix[0])
	{
		if (TitleAdmin[0] == 0)
		{
			TitleAdmin[countof(TitleAdmin)-1] = 0;
			wcscpy_c(TitleAdmin, TitleFull);
			StripWords(TitleAdmin, gpSet->pszTabSkipWords);
			wcscat_c(TitleAdmin, gpSet->szAdminTitleSuffix);
		}
		return TitleAdmin;
	}

	return TitleFull;
}

LRESULT CRealConsole::OnScroll(int nDirection)
{
	if (!this) return 0;

	return mp_ABuf->OnScroll(nDirection);
}

LRESULT CRealConsole::OnSetScrollPos(WPARAM wParam)
{
	if (!this) return 0;

	return mp_ABuf->OnSetScrollPos(wParam);
}

const ConEmuHotKey* CRealConsole::ProcessSelectionHotKey(DWORD VkState, bool bKeyDown, const wchar_t *pszChars)
{
	if (!isSelectionPresent())
		return NULL;

	return mp_ABuf->ProcessSelectionHotKey(VkState, bKeyDown, pszChars);
}

void CRealConsole::OnKeyboard(HWND hWnd, UINT messg, WPARAM wParam, LPARAM lParam, const wchar_t *pszChars, const MSG* pDeadCharMsg)
{
	if (!this) return;

	//LRESULT result = 0;
	_ASSERTE(pszChars!=NULL);

	if ((messg == WM_CHAR) || (messg == WM_SYSCHAR) || (messg == WM_IME_CHAR))
	{
		_ASSERTE((messg != WM_CHAR) && (messg != WM_SYSCHAR) && (messg != WM_IME_CHAR));
	}
	else
	{
		if ((wParam == VK_KANJI) || (wParam == VK_PROCESSKEY))
		{
			// Don't send to real console
			return;
		}
		// Dead key? debug
		_ASSERTE(wParam != VK_PACKET);
	}

	#ifdef _DEBUG
	if (wParam != VK_LCONTROL && wParam != VK_RCONTROL && wParam != VK_CONTROL &&
	        wParam != VK_LSHIFT && wParam != VK_RSHIFT && wParam != VK_SHIFT &&
	        wParam != VK_LMENU && wParam != VK_RMENU && wParam != VK_MENU &&
	        wParam != VK_LWIN && wParam != VK_RWIN)
	{
		wParam = wParam;
	}

	if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == 'C')
	{
		if (messg == WM_KEYDOWN || messg == WM_KEYUP /*|| messg == WM_CHAR*/)
		{
			wchar_t szDbg[128];

			if (messg == WM_KEYDOWN)
				_wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"WM_KEYDOWN(%i,0x%08X)\n", (DWORD)wParam, (DWORD)lParam);
			else //if (messg == WM_KEYUP)
				_wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"WM_KEYUP(%i,0x%08X)\n", (DWORD)wParam, (DWORD)lParam);

			DEBUGSTRINPUT(szDbg);
		}
	}
	#endif

	// Проверим, может клавишу обработает сам буфер (выделение текста кнопками, если оно уже начато и т.п.)?
	// Сами HotKeys буфер не обрабатывает
	if (mp_ABuf->OnKeyboard(hWnd, messg, wParam, lParam, pszChars))
	{
		return;
	}

	
	WARNING("Тут кое-что нехорошо. Некоторые кнопки нужно обрабатывать раньше.");
	// Например, AltEnter может посылаться в консоль, а может и "менять FullScreen" (в последнем случае его наверное нужно обработать)

	// Основная обработка
	{
		if (wParam == VK_MENU && (messg == WM_KEYUP || messg == WM_SYSKEYUP) && gpSet->isFixAltOnAltTab)
		{
			// При быстром нажатии Alt-Tab (переключение в другое окно)
			// в консоль проваливается {press Alt/release Alt}
			// В результате, может выполниться макрос, повешенный на Alt.
			if (getForegroundWindow()!=ghWnd && GetFarPID())
			{
				if (/*isPressed(VK_MENU) &&*/ !isPressed(VK_CONTROL) && !isPressed(VK_SHIFT))
				{
					PostKeyPress(VK_CONTROL, LEFT_ALT_PRESSED, 0);
				}
			}
			// Continue!
		}
		
		// Hotkey?
		if (((wParam & 0xFF) >= VK_WHEEL_FIRST) && ((wParam & 0xFF) <= VK_WHEEL_LAST))
		{
			// Такие коды с клавиатуры приходить не должны, а то для "мышки" ничего не останется
			_ASSERTE(!(((wParam & 0xFF) >= VK_WHEEL_FIRST) && ((wParam & 0xFF) <= VK_WHEEL_LAST)));
		}
		else if (gpConEmu->ProcessHotKeyMsg(messg, wParam, lParam, pszChars, this))
		{
			// Yes, Skip
			return;
		}
		// *** Not send to console ***
		else if (hGuiWnd)
		{
			switch (messg)
			{
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
				// Issue 971: Sending dead chars to GUI child applications
				if (pDeadCharMsg)
				{
					if (messg==WM_KEYDOWN || messg==WM_SYSKEYDOWN)
					{
						mn_InPostDeadChar = messg;
						PostConsoleMessage(hGuiWnd, pDeadCharMsg->message, pDeadCharMsg->wParam, pDeadCharMsg->lParam);
					}
					else
					{
						//mn_InPostDeadChar = FALSE;
					}
				}
				else if (mn_InPostDeadChar && (pszChars && *pszChars))
				{
					_ASSERTE(pszChars[0] && (pszChars[1]==0 || (pszChars[1]==pszChars[0] && pszChars[2]==0)));
					for (size_t i = 0; pszChars[i]; i++)
					{
						PostConsoleMessage(hGuiWnd, (mn_InPostDeadChar==WM_KEYDOWN) ? WM_CHAR : WM_SYSCHAR, (WPARAM)pszChars[i], lParam);
					}
					mn_InPostDeadChar = FALSE;
				}
				else
				{
					if (mn_InPostDeadChar)
					{
						if (messg==WM_KEYUP || messg==WM_SYSKEYUP)
						{
							break;
						}
						else
						{
							// Почему-то не сбросился mn_InPostDeadChar
							_ASSERTE(messg==WM_KEYUP || messg==WM_SYSKEYUP);
							mn_InPostDeadChar = FALSE;
						}
					}

					PostConsoleMessage(hGuiWnd, messg, wParam, lParam);
				}
				break;
			case WM_CHAR:
			case WM_SYSCHAR:
			case WM_DEADCHAR:
				// Не должны сюда доходить - обработка через WM_KEYDOWN и т.п.?
				_ASSERTE(messg!=WM_CHAR && messg!=WM_SYSCHAR && messg!=WM_DEADCHAR);
				break;
			default:
				PostConsoleMessage(hGuiWnd, messg, wParam, lParam);
			}
		}
		else
		{
			if (gpConEmu->isInImeComposition())
			{
				// Сейчас ввод работает на окно IME и не должен попадать в консоль!
				return;
			}

			// Завершение выделения по KeyPress?
			if (mp_ABuf->isSelfSelectMode())
			{
				// буквы/цифры/символы/...
				if ((gpSet->isCTSEndOnTyping && (pszChars && *pszChars))
					// +все, что не генерит символы (стрелки, Fn, и т.п.)
					|| (gpSet->isCTSEndOnKeyPress
						&& (messg==WM_KEYDOWN || messg==WM_SYSKEYDOWN)
						&& !(wParam==VK_SHIFT || wParam==VK_CONTROL || wParam==VK_MENU || wParam==VK_LWIN || wParam==VK_RWIN || wParam==VK_APPS)
						&& !gpSet->IsModifierPressed(mp_ABuf->isStreamSelection() ? vkCTSVkText : vkCTSVkBlock, false)
					))
				{
					// 2 - end only, do not copy
					mp_ABuf->DoSelectionFinalize((gpSet->isCTSEndOnTyping == 1));
				}
				else
				{
					return; // В режиме выделения - в консоль ВООБЩЕ клавиатуру не посылать!
				}
			}


			if (GetActiveBufferType() != rbt_Primary)
			{
				// Пропускать кнопки в консоль только если буфер реальный
				return;
			}

			// А теперь собственно отсылка в консоль
			ProcessKeyboard(messg, wParam, lParam, pszChars);
		}
	}
	return;
}

// pszChars may be NULL
void CRealConsole::ProcessKeyboard(UINT messg, WPARAM wParam, LPARAM lParam, const wchar_t *pszChars)
{
	INPUT_RECORD r = {KEY_EVENT};

	//WORD nCaps = 1 & (WORD)GetKeyState(VK_CAPITAL);
	//WORD nNum = 1 & (WORD)GetKeyState(VK_NUMLOCK);
	//WORD nScroll = 1 & (WORD)GetKeyState(VK_SCROLL);
	//WORD nLAlt = 0x8000 & (WORD)GetKeyState(VK_LMENU);
	//WORD nRAlt = 0x8000 & (WORD)GetKeyState(VK_RMENU);
	//WORD nLCtrl = 0x8000 & (WORD)GetKeyState(VK_LCONTROL);
	//WORD nRCtrl = 0x8000 & (WORD)GetKeyState(VK_RCONTROL);
	//WORD nShift = 0x8000 & (WORD)GetKeyState(VK_SHIFT);

	//if (messg == WM_CHAR || messg == WM_SYSCHAR) {
	//    if (((WCHAR)wParam) <= 32 || mn_LastVKeyPressed == 0)
	//        return; // это уже обработано
	//    r.Event.KeyEvent.bKeyDown = TRUE;
	//    r.Event.KeyEvent.uChar.UnicodeChar = (WCHAR)wParam;
	//    r.Event.KeyEvent.wRepeatCount = 1; TODO("0-15 ? Specifies the repeat count for the current message. The value is the number of times the keystroke is autorepeated as a result of the user holding down the key. If the keystroke is held long enough, multiple messages are sent. However, the repeat count is not cumulative.");
	//    r.Event.KeyEvent.wVirtualKeyCode = mn_LastVKeyPressed;
	//} else {

	mn_LastVKeyPressed = wParam & 0xFFFF;

	////PostConsoleMessage(hConWnd, messg, wParam, lParam, FALSE);
	//if ((wParam >= VK_F1 && wParam <= /*VK_F24*/ VK_SCROLL) || wParam <= 32 ||
	//    (wParam >= VK_LSHIFT/*0xA0*/ && wParam <= /*VK_RMENU=0xA5*/ 0xB7 /*=VK_LAUNCH_APP2*/) ||
	//    (wParam >= VK_LWIN/*0x5B*/ && wParam <= VK_APPS/*0x5D*/) ||
	//    /*(wParam >= VK_NUMPAD0 && wParam <= VK_DIVIDE) ||*/ //TODO:
	//    (wParam >= VK_PRIOR/*0x21*/ && wParam <= VK_HELP/*0x2F*/) ||
	//    nLCtrl || nRCtrl ||
	//    ((nLAlt || nRAlt) && !(nLCtrl || nRCtrl || nShift) && (wParam >= VK_NUMPAD0/*0x60*/ && wParam <= VK_NUMPAD9/*0x69*/)) || // Ввод Alt-цифры при включенном NumLock
	//    FALSE)
	//{

	TODO("0-15 ? Specifies the repeat count for the current message. The value is the number of times the keystroke is autorepeated as a result of the user holding down the key. If the keystroke is held long enough, multiple messages are sent. However, the repeat count is not cumulative.");
	r.Event.KeyEvent.wRepeatCount = 1;
	r.Event.KeyEvent.wVirtualKeyCode = mn_LastVKeyPressed;
	r.Event.KeyEvent.uChar.UnicodeChar = pszChars ? pszChars[0] : 0;

	//if (!nLCtrl && !nRCtrl) {
	//    if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_BACK || wParam == VK_TAB || wParam == VK_SPACE
	//        || FALSE)
	//        r.Event.KeyEvent.uChar.UnicodeChar = wParam;
	//}
	//    mn_LastVKeyPressed = 0; // чтобы не обрабатывать WM_(SYS)CHAR
	//} else {
	//    return;
	//}
	r.Event.KeyEvent.bKeyDown = (messg == WM_KEYDOWN || messg == WM_SYSKEYDOWN);
	//}
	r.Event.KeyEvent.wVirtualScanCode = ((DWORD)lParam & 0xFF0000) >> 16; // 16-23 - Specifies the scan code. The value depends on the OEM.
	// 24 - Specifies whether the key is an extended key, such as the right-hand ALT and CTRL keys that appear on an enhanced 101- or 102-key keyboard. The value is 1 if it is an extended key; otherwise, it is 0.
	// 29 - Specifies the context code. The value is 1 if the ALT key is held down while the key is pressed; otherwise, the value is 0.
	// 30 - Specifies the previous key state. The value is 1 if the key is down before the message is sent, or it is 0 if the key is up.
	// 31 - Specifies the transition state. The value is 1 if the key is being released, or it is 0 if the key is being pressed.

	r.Event.KeyEvent.dwControlKeyState = gpConEmu->GetControlKeyState(lParam);

	if (mn_LastVKeyPressed == VK_ESCAPE)
	{
		if ((gpSet->isMapShiftEscToEsc && (gpSet->isMultiMinByEsc == 1))
			&& ((r.Event.KeyEvent.dwControlKeyState & ALL_MODIFIERS) == SHIFT_PRESSED))
		{
			// When enabled feature "Minimize by Esc always"
			// we need easy way to send simple "Esc" to console
			// There is an option for this: Map Shift+Esc to Esc
			r.Event.KeyEvent.dwControlKeyState &= ~ALL_MODIFIERS;
		}
	}

	#ifdef _DEBUG
	if (r.EventType == KEY_EVENT && r.Event.KeyEvent.bKeyDown &&
	        r.Event.KeyEvent.wVirtualKeyCode == VK_F11)
	{
		DEBUGSTRINPUT(L"  ---  F11 sending\n");
	}
	#endif

	// -- заменено на перехват функции ScreenToClient
	//// Сделано (пока) только чтобы текстовое EMenu активировалось по центру консоли,
	//// а не в положении мыши (что смотрится отвратно - оно может сплющиться до 2-3 строк).
	//// Только при скрытой консоли.
	//RemoveFromCursor();

	if (mn_FarPID && mn_FarPID != mn_LastSetForegroundPID)
	{
		//DWORD dwFarPID = GetFarPID();
		//if (dwFarPID)
		AllowSetForegroundWindow(mn_FarPID);
		mn_LastSetForegroundPID = mn_FarPID;
	}

	// Эмуляция терминала?
	static struct xTermKey {
		UINT vk;
		wchar_t szKeys[16];
	} xTermKeys[] = {
		// From vim "term.c"
		{VK_UP,		L"\033O*A"},
		{VK_DOWN,	L"\033O*B"},
		{VK_RIGHT,	L"\033O*C"},
		{VK_LEFT,	L"\033O*D"},
		{VK_F1,		L"\033[11;*~"},
		{VK_F2,		L"\033[12;*~"},
		{VK_F3,		L"\033[13;*~"},
		{VK_F4,		L"\033[14;*~"},
		{VK_F5,		L"\033[15;*~"},
		{VK_F6,		L"\033[17;*~"},
		{VK_F7,		L"\033[18;*~"},
		{VK_F8,		L"\033[19;*~"},
		{VK_F9,		L"\033[20;*~"},
		{VK_F10,	L"\033[21;*~"},
		{VK_F11,	L"\033[23;*~"},
		{VK_F12,	L"\033[24;*~"},
		{VK_INSERT,	L"\033[2;*~"},
		{VK_HOME,	L"\033[1;*H"},
		{VK_END,	L"\033[1;*F"},
		{VK_PRIOR,	L"\033[5;*~"},
		{VK_NEXT,	L"\033[6;*~"},
		{VK_DELETE,	L"\033[3;*~"}, // ???
		{0}
	};
	xTermKey x = {0};
	if (m_Term.Term)
	{
		// Processed keys?
		
		for (int i = 0; xTermKeys[i].vk; i++)
		{
			if (xTermKeys[i].vk == r.Event.KeyEvent.wVirtualKeyCode)
			{
				x = xTermKeys[i];
				break;
			}
		}

		if (x.vk)
		{
			if (!isProcessExist(m_Term.nCallTermPID))
			{
				StartStopXTerm(0,false/*te_win32*/);
			}
			else if (!r.Event.KeyEvent.bKeyDown)
			{
				// only key pressed are sent to terminal
				return;
			}
			else
			{
				PostString(x.szKeys, _tcslen(x.szKeys));
				//pszChars = x.szKeys;
				//r.Event.KeyEvent.wVirtualScanCode = 0;
				//r.Event.KeyEvent.wVirtualKeyCode = x.szKeys[0]; // VK_ESCAPE?
				//r.Event.KeyEvent.uChar.UnicodeChar = x.szKeys[0];
				return;
			}
		}
	}

	PostConsoleEvent(&r);

	// нажатие клавиши может трансформироваться в последовательность нескольких символов...
	if (pszChars && pszChars[0] && pszChars[1])
	{
		/*
		The expected behaviour would be (as it is in a cmd.exe session):
		- hit "^" -> see nothing
		- hit "^" again -> see ^^
		- hit "^" again -> see nothing
		- hit "^" again -> see ^^

		Alternatively:
		- hit "^" -> see nothing
		- hit any other alpha-numeric key, e.g. "k" -> see "^k"
		*/
		for (int i = 1; pszChars[i]; i++)
		{
			r.Event.KeyEvent.uChar.UnicodeChar = pszChars[i];
			PostConsoleEvent(&r);
		}
	}
}

void CRealConsole::OnKeyboardIme(HWND hWnd, UINT messg, WPARAM wParam, LPARAM lParam)
{
	if (messg != WM_IME_CHAR)
		return;

	// Do not run excess ops when routing our msg to GUI application
	if (hGuiWnd)
	{
		PostConsoleMessage(hGuiWnd, messg, wParam, lParam);
		return;
	}


	INPUT_RECORD r = {KEY_EVENT};
	WORD nCaps = 1 & (WORD)GetKeyState(VK_CAPITAL);
	WORD nNum = 1 & (WORD)GetKeyState(VK_NUMLOCK);
	WORD nScroll = 1 & (WORD)GetKeyState(VK_SCROLL);
	WORD nLAlt = 0x8000 & (WORD)GetKeyState(VK_LMENU);
	WORD nRAlt = 0x8000 & (WORD)GetKeyState(VK_RMENU);
	WORD nLCtrl = 0x8000 & (WORD)GetKeyState(VK_LCONTROL);
	WORD nRCtrl = 0x8000 & (WORD)GetKeyState(VK_RCONTROL);
	WORD nShift = 0x8000 & (WORD)GetKeyState(VK_SHIFT);

	r.Event.KeyEvent.wRepeatCount = 1; // Repeat count. Since the first byte and second byte is continuous, this is always 1.
	// 130822 - Japanese+iPython - (wVirtualKeyCode!=0) fails with pyreadline
	r.Event.KeyEvent.wVirtualKeyCode = isFar() ? VK_PROCESSKEY : 0; // В RealConsole валится VK=0, но его фар игнорит
	r.Event.KeyEvent.uChar.UnicodeChar = (wchar_t)wParam;
	r.Event.KeyEvent.bKeyDown = TRUE; //(messg == WM_KEYDOWN || messg == WM_SYSKEYDOWN);
	r.Event.KeyEvent.wVirtualScanCode = ((DWORD)lParam & 0xFF0000) >> 16; // 16-23 - Specifies the scan code. The value depends on the OEM.
	// 24 - Specifies whether the key is an extended key, such as the right-hand ALT and CTRL keys that appear on an enhanced 101- or 102-key keyboard. The value is 1 if it is an extended key; otherwise, it is 0.
	// 29 - Specifies the context code. The value is 1 if the ALT key is held down while the key is pressed; otherwise, the value is 0.
	// 30 - Specifies the previous key state. The value is 1 if the key is down before the message is sent, or it is 0 if the key is up.
	// 31 - Specifies the transition state. The value is 1 if the key is being released, or it is 0 if the key is being pressed.
	r.Event.KeyEvent.dwControlKeyState = 0;

	if (((DWORD)lParam & (DWORD)(1 << 24)) != 0)
		r.Event.KeyEvent.dwControlKeyState |= ENHANCED_KEY;

	if ((nCaps & 1) == 1)
		r.Event.KeyEvent.dwControlKeyState |= CAPSLOCK_ON;

	if ((nNum & 1) == 1)
		r.Event.KeyEvent.dwControlKeyState |= NUMLOCK_ON;

	if ((nScroll & 1) == 1)
		r.Event.KeyEvent.dwControlKeyState |= SCROLLLOCK_ON;

	if (nLAlt & 0x8000)
		r.Event.KeyEvent.dwControlKeyState |= LEFT_ALT_PRESSED;

	if (nRAlt & 0x8000)
		r.Event.KeyEvent.dwControlKeyState |= RIGHT_ALT_PRESSED;

	if (nLCtrl & 0x8000)
		r.Event.KeyEvent.dwControlKeyState |= LEFT_CTRL_PRESSED;

	if (nRCtrl & 0x8000)
		r.Event.KeyEvent.dwControlKeyState |= RIGHT_CTRL_PRESSED;

	if (nShift & 0x8000)
		r.Event.KeyEvent.dwControlKeyState |= SHIFT_PRESSED;

	PostConsoleEvent(&r, true);
}

void CRealConsole::OnDosAppStartStop(enum StartStopType sst, DWORD anPID)
{
	if (sst == sst_App16Start)
	{
		DEBUGSTRPROC(L"16 bit application STARTED\n");

		if (mn_Comspec4Ntvdm == 0)
		{
			// mn_Comspec4Ntvdm может быть еще не заполнен, если 16бит вызван из батника
			WARNING("###: При запуске vc.com из cmd.exe - ntvdm.exe запускается в обход ConEmuC.exe, что нехорошо наверное");
			_ASSERTE(mn_Comspec4Ntvdm != 0 || !isFarInStack());
		}

		if (!(mn_ProgramStatus & CES_NTVDM))
			mn_ProgramStatus |= CES_NTVDM;

		// -- в cmdStartStop - mb_IgnoreCmdStop устанавливался всегда, поэтому условие убрал
		//if (gOSVer.dwMajorVersion>5 || (gOSVer.dwMajorVersion==5 && gOSVer.dwMinorVersion>=1))
		mb_IgnoreCmdStop = TRUE;

		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	}
	else if (sst == sst_App16Stop)
	{
		//gpConEmu->gbPostUpdateWindowSize = true;
		DEBUGSTRPROC(L"16 bit application TERMINATED\n");
		WARNING("Не сбрасывать CES_NTVDM сразу. Еще не отработал возврат размера консоли!");

		if (mn_Comspec4Ntvdm == 0)
		{
			mn_ProgramStatus &= ~CES_NTVDM;
		}

		//2010-02-26 убрал. может прийти с задержкой и только создать проблемы
		//SyncConsole2Window(); // После выхода из 16bit режима хорошо бы отресайзить консоль по размеру GUI
		// хорошо бы проверить, что 16бит не осталось в других консолях
		if (!gpConEmu->isNtvdm(TRUE))
			SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	}
}

// Это приходит из ConEmuC.exe::ServerInitGuiTab (CECMD_SRVSTARTSTOP)
// здесь сервер только "запускается" и еще не готов принимать команды
void CRealConsole::OnServerStarted(const HWND ahConWnd, const DWORD anServerPID, const DWORD dwKeybLayout)
{
	if (!this)
	{
		_ASSERTE(this);
		return;
	}
	if ((ahConWnd == NULL) || (hConWnd && (ahConWnd != hConWnd)) || (anServerPID != mn_MainSrv_PID))
	{
		MBoxAssert(ahConWnd!=NULL);
		MBoxAssert((hConWnd==NULL) || (ahConWnd==hConWnd));
		MBoxAssert(anServerPID==mn_MainSrv_PID);
		return;
	}

	// Окошко консоли скорее всего еще не инициализировано
	if (hConWnd == NULL)
	{
		SetConStatus(L"Waiting for console server...", true);
		SetHwnd(ahConWnd);
	}

	// Само разберется
	OnConsoleKeyboardLayout(dwKeybLayout);
}

//void CRealConsole::OnWinEvent(DWORD anEvent, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
//{
//	_ASSERTE(hwnd!=NULL);
//
//	if (hConWnd == NULL && anEvent == EVENT_CONSOLE_START_APPLICATION && idObject == (LONG)mn_MainSrv_PID)
//	{
//		SetConStatus(L"Waiting for console server...");
//		SetHwnd(hwnd);
//	}
//
//	_ASSERTE(hConWnd!=NULL && hwnd==hConWnd);
//	//TODO("!!! Сделать обработку событий и население m_Processes");
//	//
//	//AddProcess(idobject), и удаление idObject из списка процессов
//	// Не забыть, про обработку флажка Ntvdm
//	TODO("При отцеплении от консоли NTVDM - можно прибить этот процесс");
//
//	switch(anEvent)
//	{
//		case EVENT_CONSOLE_START_APPLICATION:
//			//A new console process has started.
//			//The idObject parameter contains the process identifier of the newly created process.
//			//If the application is a 16-bit application, the idChild parameter is CONSOLE_APPLICATION_16BIT and idObject is the process identifier of the NTVDM session associated with the console.
//		{
//			if (mn_InRecreate>=1)
//				mn_InRecreate = 0; // корневой процесс успешно пересоздался
//
//			//WARNING("Тут можно повиснуть, если нарваться на блокировку: процесс может быть добавлен и через сервер");
//			//Process Add(idObject);
//			// Если запущено 16битное приложение - необходимо повысить приоритет нашего процесса, иначе будут тормоза
//#ifndef WIN64
//			_ASSERTE(CONSOLE_APPLICATION_16BIT==1);
//
//			if (idChild == CONSOLE_APPLICATION_16BIT)
//			{
//				OnDosAppStartStop(sst_App16Start, idObject);
//
//				//if (mn_Comspec4Ntvdm == 0)
//				//{
//				//	// mn_Comspec4Ntvdm может быть еще не заполнен, если 16бит вызван из батника
//				//	_ASSERTE(mn_Comspec4Ntvdm != 0);
//				//}
//
//				//if (!(mn_ProgramStatus & CES_NTVDM))
//				//	mn_ProgramStatus |= CES_NTVDM;
//
//				//if (gOSVer.dwMajorVersion>5 || (gOSVer.dwMajorVersion==5 && gOSVer.dwMinorVersion>=1))
//				//	mb_IgnoreCmdStop = TRUE;
//
//				//SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
//			}
//
//#endif
//		} break;
//		case EVENT_CONSOLE_END_APPLICATION:
//			//A console process has exited.
//			//The idObject parameter contains the process identifier of the terminated process.
//		{
//			//WARNING("Тут можно повиснуть, если нарваться на блокировку: процесс может быть удален и через сервер");
//			//Process Delete(idObject);
//			//
//#ifndef WIN64
//			if (idChild == CONSOLE_APPLICATION_16BIT)
//			{
//				OnDosAppStartStop(sst_App16Stop, idObject);
//			}
//
//#endif
//		} break;
//	}
//}


void CRealConsole::OnServerClosing(DWORD anSrvPID)
{
	if (anSrvPID == mn_MainSrv_PID && mh_MainSrv)
	{
		// 131017 set flags to ON
		StopSignal();
		//int nCurProcessCount = m_Processes.size();
		//_ASSERTE(nCurProcessCount <= 1);
		m_ServerClosing.nRecieveTick = GetTickCount();
		m_ServerClosing.hServerProcess = mh_MainSrv;
		m_ServerClosing.nServerPID = anSrvPID;
		// Поскольку сервер закрывается, пайп более не доступен
		ms_ConEmuC_Pipe[0] = 0;
		ms_MainSrv_Pipe[0] = 0;
	}
	else
	{
		_ASSERTE(anSrvPID == mn_MainSrv_PID);
	}
}

bool CRealConsole::isProcessExist(DWORD anPID)
{
	if (mn_InRecreate || isDetached() || !mn_ProcessCount)
		return false;

	bool bExist = false;
	MSectionLock SPRC; SPRC.Lock(&csPRC);
	int dwProcCount = (int)m_Processes.size();

	for (int i = 0; i < dwProcCount; i++)
	{
		ConProcess prc = m_Processes[i];
		if (prc.ProcessID == anPID)
		{
			bExist = true;
			break;
		}
	}

	SPRC.Unlock();

	return bExist;
}

int CRealConsole::GetProcesses(ConProcess** ppPrc, bool ClientOnly /*= false*/)
{
	if (mn_InRecreate)
	{
		DWORD dwCurTick = GetTickCount();
		DWORD dwDelta = dwCurTick - mn_InRecreate;

		if (dwDelta > CON_RECREATE_TIMEOUT)
		{
			mn_InRecreate = 0;
			m_Args.bDetached = TRUE; // Чтобы GUI не захлопнулся
		}
		else if (ppPrc == NULL)
		{
			if (mn_InRecreate && !mb_ProcessRestarted && mh_MainSrv)
			{
				//DWORD dwExitCode = 0;

				if (!isServerAlive())
				{
					RecreateProcessStart();
				}
			}

			return 1; // Чтобы во время Recreate GUI не захлопнулся
		}
	}
	
	if (isDetached())
	{
		return 1; // Чтобы GUI не захлопнулся
	}

	// Если хотят узнать только количество процессов
	if (ppPrc == NULL || mn_ProcessCount == 0)
	{
		if (ppPrc) *ppPrc = NULL;

		if (hConWnd && !mh_MainSrv)
		{
			if (!IsWindow(hConWnd))
			{
				_ASSERTE(FALSE && "Console window was abnormally terminated?");
				return 0;
			}
		}

		return ClientOnly ? mn_ProcessClientCount : mn_ProcessCount;
	}

	MSectionLock SPRC; SPRC.Lock(&csPRC);
	int dwProcCount = (int)m_Processes.size();
	int nCount = 0;

	if (dwProcCount > 0)
	{
		*ppPrc = (ConProcess*)calloc(dwProcCount, sizeof(ConProcess));
		if (*ppPrc == NULL)
		{
			_ASSERTE((*ppPrc)!=NULL);
			return dwProcCount;
		}
		
		//std::vector<ConProcess>::iterator end = m_Processes.end();
		//int i = 0;
		//for (std::vector<ConProcess>::iterator iter = m_Processes.begin(); iter != end; ++iter, ++i)
		for (int i = 0; i < dwProcCount; i++)
		{
			ConProcess prc = m_Processes[i];
			if (ClientOnly)
			{
				if (prc.IsConHost)
				{
					continue;
				}
				if (prc.ProcessID == mn_MainSrv_PID)
				{
					_ASSERTE(isConsoleService(prc.Name));
					continue;
				}
				_ASSERTE(!isConsoleService(prc.Name));
			}
			//(*ppPrc)[i] = *iter;
			(*ppPrc)[nCount++] = prc;
		}
	}
	else
	{
		*ppPrc = NULL;
	}

	SPRC.Unlock();

	return nCount;
}

DWORD CRealConsole::GetProgramStatus()
{
	if (!this)
		return 0;

	return mn_ProgramStatus;
}

DWORD CRealConsole::GetFarStatus()
{
	if (!this)
		return 0;

	if ((mn_ProgramStatus & CES_FARACTIVE) == 0)
		return 0;

	return mn_FarStatus;
}

bool CRealConsole::isServerAlive()
{
	if (!this || !mh_MainSrv || mh_MainSrv == INVALID_HANDLE_VALUE)
		return false;

#ifdef _DEBUG
	DWORD dwExitCode = 0, nSrvPID = 0, nErrCode = 0;
	LockFrequentExecute(500,mn_CheckFreqLock)
	{
		SetLastError(0);
		BOOL fSuccess = GetExitCodeProcess(mh_MainSrv, &dwExitCode);
		nErrCode = GetLastError();

		typedef DWORD (WINAPI* GetProcessId_t)(HANDLE);
		static GetProcessId_t getProcessId;
		if (!getProcessId)
			getProcessId = (GetProcessId_t)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetProcessId");
		if (getProcessId)
		{
			SetLastError(0);
			nSrvPID = getProcessId(mh_MainSrv);
			nErrCode = GetLastError();
		}
	}
#endif

	DWORD nServerWait = WaitForSingleObject(mh_MainSrv, 0);
	DEBUGTEST(DWORD nWaitErr = GetLastError());

	return (nServerWait == WAIT_TIMEOUT);
}

bool CRealConsole::isServerAvailable()
{
	if (isServerClosing())
		return false;
	TODO("На время переключения на альтернативный сервер - возвращать false");
	return true;
}

bool CRealConsole::isServerClosing()
{
	if (!this)
	{
		_ASSERTE(this);
		return true;
	}

	if (m_ServerClosing.nServerPID && (m_ServerClosing.nServerPID == mn_MainSrv_PID))
		return true;

	return false;
}

DWORD CRealConsole::GetMonitorThreadID()
{
	if (!this)
		return 0;
	return mn_MonitorThreadID;
}

DWORD CRealConsole::GetServerPID(bool bMainOnly /*= false*/)
{
	if (!this)
		return 0;

	if (mb_InCreateRoot && !mn_MainSrv_PID)
	{
		_ASSERTE(!mb_InCreateRoot || mn_MainSrv_PID);
		Sleep(500);
		//_ASSERTE(!mb_InCreateRoot || mn_MainSrv_PID);
		//if (GetCurrentThreadId() != mn_MonitorThreadID) {
		//	WaitForSingleObject(mh_CreateRootEvent, 30000); -- низя - DeadLock
		//}
	}

	return (mn_AltSrv_PID && !bMainOnly) ? mn_AltSrv_PID : mn_MainSrv_PID;
}

void CRealConsole::SetMainSrvPID(DWORD anMainSrvPID, HANDLE ahMainSrv)
{
	_ASSERTE((mh_MainSrv==NULL || mh_MainSrv==ahMainSrv) && "mh_MainSrv must be closed before!");
	_ASSERTE((anMainSrvPID!=0 || mn_AltSrv_PID==0) && "AltServer must be closed before!");

	DEBUGTEST(isServerAlive());

	mh_MainSrv = ahMainSrv;
	mn_MainSrv_PID = anMainSrvPID;

	if (!anMainSrvPID)
	{
		// только сброс! установка в OnServerStarted(DWORD anServerPID, HANDLE ahServerHandle)
		mb_MainSrv_Ready = false;
		// Устанавливается в OnConsoleLangChange
		mn_ActiveLayout = 0;
		// Сброс
		ConHostSetPID(0);
	}

	DEBUGTEST(isServerAlive());

	if (isActive())
		gpConEmu->mp_Status->OnServerChanged(mn_MainSrv_PID, mn_AltSrv_PID);

	DEBUGTEST(isServerAlive());
}

void CRealConsole::SetAltSrvPID(DWORD anAltSrvPID/*, HANDLE ahAltSrv*/)
{
	//_ASSERTE((mh_AltSrv==NULL || mh_AltSrv==ahAltSrv) && "mh_AltSrv must be closed before!");
	//mh_AltSrv = ahAltSrv;
	mn_AltSrv_PID = anAltSrvPID;
}

bool CRealConsole::InitAltServer(DWORD nAltServerPID/*, HANDLE hAltServer*/)
{
	// nAltServerPID may be 0, hAltServer вообще убрать
	bool bOk = false;

	ResetEvent(mh_ActiveServerSwitched);

	//// mh_AltSrv должен закрыться в MonitorThread!
	//HANDLE hOldAltServer = mh_AltSrv;
	//mh_AltSrv = NULL;
	////mn_AltSrv_PID = nAltServerPID;
	////mh_AltSrv = hAltServer;
	SetAltSrvPID(nAltServerPID/*, hAltServer*/);

	if (!nAltServerPID && isServerClosing())
	{
		// Переоткрывать пайпы смысла нет, консоль закрывается
		ResetEvent(mh_SwitchActiveServer);
		mb_SwitchActiveServer = false;
		bOk = true;
	}
	else
	{
		SetEvent(mh_SwitchActiveServer);
		mb_SwitchActiveServer = true;

		HANDLE hWait[] = {mh_ActiveServerSwitched, mh_MonitorThread, mh_MainSrv, mh_TermEvent};
		DWORD nWait = WAIT_TIMEOUT;
		DEBUGTEST(DWORD nStartWait = GetTickCount());

		// mh_TermEvent выставляется после реального закрытия консоли
		// но если инициировано закрытие консоли - ожидание завершения смены
		// сервера может привести к блокировке, поэтому isServerClosing
		while ((nWait == WAIT_TIMEOUT) && !isServerClosing())
		{
			nWait = WaitForMultipleObjects(countof(hWait), hWait, FALSE, 100);

			#ifdef _DEBUG
			if ((nWait == WAIT_TIMEOUT) && nStartWait && ((GetTickCount() - nStartWait) > 2000))
			{
				_ASSERTE((nWait == WAIT_OBJECT_0) && "Switching Monitor thread to altarnative server takes more than 2000ms");
			}
			#endif
		}

		if (nWait == WAIT_OBJECT_0)
		{
			_ASSERTE(mb_SwitchActiveServer==false && "Must be dropped by MonitorThread");
			mb_SwitchActiveServer = false;
		}
		else
		{
			_ASSERTE(isServerClosing());
		}

		bOk = (nWait == WAIT_OBJECT_0);
	}

	if (isActive())
		gpConEmu->mp_Status->OnServerChanged(mn_MainSrv_PID, mn_AltSrv_PID);

	return bOk;
}

bool CRealConsole::ReopenServerPipes()
{
	DWORD nSrvPID = mn_AltSrv_PID ? mn_AltSrv_PID : mn_MainSrv_PID;
	HANDLE hSrvHandle = mh_MainSrv; // (nSrvPID == mn_MainSrv_PID) ? mh_MainSrv : mh_AltSrv;

	if (isServerClosing())
	{
		_ASSERTE(FALSE && "ReopenServerPipes was called in MainServerClosing");
	}

	// переоткрыть event изменений в консоли
	m_ConDataChanged.InitName(CEDATAREADYEVENT, nSrvPID);
	if (m_ConDataChanged.Open() == NULL)
	{
		bool bSrvClosed = (WaitForSingleObject(hSrvHandle, 0) == WAIT_OBJECT_0);
		Assert(mb_InCloseConsole && "m_ConDataChanged.Open() != NULL"); UNREFERENCED_PARAMETER(bSrvClosed);
		return false;
	}

	// переоткрыть m_GetDataPipe
	m_GetDataPipe.InitName(gpConEmu->GetDefaultTitle(), CESERVERREADNAME, L".", nSrvPID);
	bool bOpened = m_GetDataPipe.Open();
	if (!bOpened)
	{
		bool bSrvClosed = (WaitForSingleObject(hSrvHandle, 0) == WAIT_OBJECT_0);
		Assert((bOpened || mb_InCloseConsole) && "m_GetDataPipe.Open() failed"); UNREFERENCED_PARAMETER(bSrvClosed);
		return false;
	}

	// обновить имя "серверного" пайпа
	_wsprintf(ms_ConEmuC_Pipe, SKIPLEN(countof(ms_ConEmuC_Pipe)) CESERVERPIPENAME, L".", nSrvPID);
	_wsprintf(ms_MainSrv_Pipe, SKIPLEN(countof(ms_MainSrv_Pipe)) CESERVERPIPENAME, L".", mn_MainSrv_PID);

	bool bActive = isActive();

	if (bActive)
		gpConEmu->mp_Status->OnServerChanged(mn_MainSrv_PID, mn_AltSrv_PID);

	UpdateServerActive(TRUE);

#ifdef _DEBUG
	wchar_t szDbgInfo[512]; szDbgInfo[0] = 0;

	MSectionLock SC; SC.Lock(&csPRC);
	//std::vector<ConProcess>::iterator i;
	//for (i = m_Processes.begin(); i != m_Processes.end(); ++i)
	for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
	{
		ConProcess* i = &(m_Processes[ii]);
		if (i->ProcessID == nSrvPID)
		{
			_wsprintf(szDbgInfo, SKIPLEN(countof(szDbgInfo)) L"==> Active server changed to '%s' PID=%u\n", i->Name, nSrvPID);
			break;
		}
	}
	SC.Unlock();

	if (!*szDbgInfo)
		_wsprintf(szDbgInfo, SKIPLEN(countof(szDbgInfo)) L"==> Active server changed to PID=%u\n", nSrvPID);

	DEBUGSTRALTSRV(szDbgInfo);
#endif

	return bOpened;
}

// Если bFullRequired - требуется чтобы сервер уже мог принимать команды
bool CRealConsole::isServerCreated(bool bFullRequired /*= false*/)
{
	if (!mn_MainSrv_PID)
		return false;

	if (bFullRequired && !mb_MainSrv_Ready)
		return false;

	return true;
}

DWORD CRealConsole::GetFarPID(bool abPluginRequired/*=false*/)
{
	if (!this)
		return 0;

	if (!mn_FarPID  // Должен быть известен код PID
	        || ((mn_ProgramStatus & CES_FARACTIVE) == 0) // выставлен флаг
	        || ((mn_ProgramStatus & CES_NTVDM) == CES_NTVDM)) // Если активна 16бит программа - значит фар точно не доступен
		return 0;

	if (abPluginRequired)
	{
		if (mn_FarPID_PluginDetected && mn_FarPID_PluginDetected == mn_FarPID)
			return mn_FarPID_PluginDetected;
		else
			return 0;
	}

	return mn_FarPID;
}

void CRealConsole::SetProgramStatus(DWORD nNewProgramStatus)
{
	mn_ProgramStatus = nNewProgramStatus;
}

void CRealConsole::SetFarStatus(DWORD nNewFarStatus)
{
	mn_FarStatus = nNewFarStatus;
}

// Вызывается при запуске в консоли ComSpec
void CRealConsole::SetFarPID(DWORD nFarPID)
{
	bool bNeedUpdate = (mn_FarPID != nFarPID);

	if (nFarPID)
	{
		if ((mn_ProgramStatus & (CES_FARACTIVE|CES_FARINSTACK)) != (CES_FARACTIVE|CES_FARINSTACK))
			SetProgramStatus(mn_ProgramStatus|CES_FARACTIVE|CES_FARINSTACK);
	}
	else
	{
		if (mn_ProgramStatus & CES_FARACTIVE)
			SetProgramStatus(mn_ProgramStatus & ~CES_FARACTIVE);
	}

	mn_FarPID = nFarPID;

	// Для фара могут быть настроены другие параметры фона и прочего...
	if (bNeedUpdate)
	{
		mp_VCon->Update(true);
	}
}

void CRealConsole::SetFarPluginPID(DWORD nFarPluginPID)
{
	bool bNeedUpdate = (mn_FarPID_PluginDetected != nFarPluginPID);
	mn_FarPID_PluginDetected = nFarPluginPID;

	// Для фара могут быть настроены другие параметры фона и прочего...
	if (bNeedUpdate)
	{
		mp_VCon->Update(true);
	}
}

// Вернуть PID "условно активного" процесса в консоли
DWORD CRealConsole::GetActivePID()
{
	if (!this)
		return 0;

	if (hGuiWnd)
	{
		return mn_GuiWndPID;
	}

	DWORD nPID = GetFarPID();
	if (nPID)
	{
		return nPID;
	}

	return mn_ActivePID;
}

LPCWSTR CRealConsole::GetActiveProcessName()
{
	LPCWSTR pszName = NULL;
	GetActiveAppSettingsId(&pszName);
	return pszName;
}

void CRealConsole::ResetActiveAppSettingsId()
{
	mn_LastProcessNamePID = 0;
}

// Вызывается перед запуском процесса
int CRealConsole::GetDefaultAppSettingsId()
{
	if (!this)
		return -1;

	int iAppId = -1;
	LPCWSTR lpszCmd = NULL;
	//wchar_t* pszBuffer = NULL;
	LPCWSTR pszName = NULL;
	CmdArg szExe;
	wchar_t szName[MAX_PATH+1];
	LPCWSTR pszTemp = NULL;
	LPCWSTR pszIconFile = (m_Args.pszIconFile && *m_Args.pszIconFile) ? m_Args.pszIconFile : NULL;
	bool bAsAdmin = false;

	if (m_Args.pszSpecialCmd)
	{
		lpszCmd = m_Args.pszSpecialCmd;
	}
	else if (m_Args.bDetached)
	{
		goto wrap; 
	}
	else
	{
		// Some tricky, but while starting we need to show at least one "empty" tab,
		// otherwise tabbar without tabs looks weird. Therefore on starting stage
		// m_Args.pszSpecialCmd may be NULL. This is not so good, because GetCmd()
		// may return task name instead of real command.
		_ASSERTE(m_Args.pszSpecialCmd && *m_Args.pszSpecialCmd && "Command line must be specified already!");

		lpszCmd = gpSetCls->GetCmd(NULL, true);
		
		//// May be this is batch?
		//pszBuffer = gpConEmu->LoadConsoleBatch(lpszCmd);
		//if (pszBuffer && *pszBuffer)
		//	lpszCmd = pszBuffer;
	}

	if (!lpszCmd || !*lpszCmd)
	{
		ms_RootProcessName[0] = 0;
		goto wrap;
	}

	// Parse command line
	ProcessSetEnvCmd(lpszCmd, false);
	pszTemp = lpszCmd;

	if (0 == NextArg(&pszTemp, szExe))
	{
		pszName = PointToName(szExe);

		pszTemp = (*lpszCmd == L'"') ? NULL : PointToName(lpszCmd);
		if (pszTemp && (wcschr(pszName, L'.') == NULL) && (wcschr(pszTemp, L'.') != NULL))
		{
			// Если в lpszCmd указан полный путь к запускаемому файлу без параметров и без кавычек
			if (FileExists(lpszCmd))
				pszName = pszTemp;
		}

		if (pszName != pszTemp)
			lpszCmd = szExe;
	}

	if (!pszName)
	{
		ms_RootProcessName[0] = 0;
		goto wrap;
	}

	if (wcschr(pszName, L'.') == NULL)
	{
		// Если расширение не указано - предположим, что это .exe
		lstrcpyn(szName, pszName, MAX_PATH-4);
		wcscat_c(szName, L".exe");
		pszName = szName;
	}

	lstrcpyn(ms_RootProcessName, pszName, countof(ms_RootProcessName));

	// In fact, m_Args.bRunAsAdministrator may be not true on startup
	bAsAdmin = m_Args.pszSpecialCmd ? m_Args.bRunAsAdministrator : gpConEmu->mb_IsUacAdmin;

	// Done. Get AppDistinct ID
	iAppId = gpSet->GetAppSettingsId(pszName, bAsAdmin);

	if (!pszIconFile)
		pszIconFile = lpszCmd;

wrap:
	// Load (or create) icon for new tab
	mn_RootProcessIcon = gpConEmu->mp_TabBar->CreateTabIcon(pszIconFile, bAsAdmin);
	// Fin
	if (!*ms_RootProcessName)
		mn_RootProcessIcon = -1;
	return iAppId;
}

// ppProcessName используется в функции GetActiveProcessName()
// возвращать должен реальное имя процесса, вне зависимости от того
// была подмена на корневой ИД или нет.
int CRealConsole::GetActiveAppSettingsId(LPCWSTR* ppProcessName/*=NULL*/)
{
	if (!this)
		return -1;

	DWORD nPID = GetActivePID();
	if (!nPID)
	{
		return GetDefaultAppSettingsId();
	}

	if (nPID == mn_LastProcessNamePID)
	{
		if (ppProcessName)
			*ppProcessName = ms_LastProcessName;
		return mn_LastAppSettingsId;
	}

	LPCWSTR pszName = NULL;
	if (nPID == mn_GuiWndPID)
	{
		pszName = ms_GuiWndProcess;
	}
	else
	{
		MSectionLock SC; SC.Lock(&csPRC);

		//std::vector<ConProcess>::iterator i;
		//for (i = m_Processes.begin(); i != m_Processes.end(); ++i)
		for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
		{
			ConProcess* i = &(m_Processes[ii]);
			if (i->ProcessID == nPID)
			{
				pszName = i->Name;
				break;
			}
		}
	}

	lstrcpyn(ms_LastProcessName, pszName ? pszName : L"", countof(ms_LastProcessName));
	mn_LastProcessNamePID = nPID;
	
	bool isAdmin = isAdministrator();

	int nSetggingsId = gpSet->GetAppSettingsId(pszName, isAdmin);

	_ASSERTE((nSetggingsId != -1) || (*ms_RootProcessName));
	// When explicit AppDistinct not found - take settings for the root process
	// ms_RootProcessName must be processed in prev. GetDefaultAppSettingsId/PrepareDefaultColors
	if ((nSetggingsId == -1) && (*ms_RootProcessName))
		mn_LastAppSettingsId = gpSet->GetAppSettingsId(ms_RootProcessName, isAdmin);
	else
		mn_LastAppSettingsId = nSetggingsId;

	if (ppProcessName)
		*ppProcessName = ms_LastProcessName;

	return mn_LastAppSettingsId;
}

void CRealConsole::SetActivePID(DWORD anNewPID)
{
	if (mn_ActivePID != anNewPID)
	{
		mn_ActivePID = anNewPID;

		if (isActive())
		{
			gpConEmu->mp_Status->UpdateStatusBar(true);
		}
	}
}

// Обновить статус запущенных программ
// Возвращает TRUE если сменился статус (Far/не Far)
BOOL CRealConsole::ProcessUpdateFlags(BOOL abProcessChanged)
{
	BOOL lbChanged = FALSE;
	//Warning: Должен вызываться ТОЛЬКО из ProcessAdd/ProcessDelete, т.к. сам секцию не блокирует
	bool bIsFar = false, bIsTelnet = false, bIsCmd = false;
	DWORD dwFarPID = 0, dwActivePID = 0;
	// Наличие 16bit определяем ТОЛЬКО по WinEvent. Иначе не получится отсечь его завершение,
	// т.к. процесс ntvdm.exe не выгружается, а остается в памяти.
	bool bIsNtvdm = (mn_ProgramStatus & CES_NTVDM) == CES_NTVDM;
	
	if (bIsNtvdm && mn_Comspec4Ntvdm)
		bIsCmd = true;

	int nClientCount = 0;

	//std::vector<ConProcess>::reverse_iterator iter = m_Processes.rbegin();
	//std::vector<ConProcess>::reverse_iterator rend = m_Processes.rend();
	//
	//while (iter != rend)
	for (INT_PTR ii = (m_Processes.size() - 1); ii >= 0; ii--)
	{
		ConProcess* iter = &(m_Processes[ii]);
		//ConProcess cp = *iter;

		// Корневой процесс ConEmuC не учитываем!
		if (iter->ProcessID != mn_MainSrv_PID)
		{
			_ASSERTE(iter->IsMainSrv==false);

			if (!bIsFar)
			{
				if (iter->IsFar)
				{
					bIsFar = true;
				}
				else if (iter->ProcessID == mn_FarPID_PluginDetected)
				{
					bIsFar = true;
					iter->IsFar = iter->IsFarPlugin = true;
				}
			}

			if (!bIsTelnet && iter->IsTelnet)
				bIsTelnet = true;

			//if (!bIsNtvdm && iter->IsNtvdm) bIsNtvdm = true;
			if (!bIsFar && !bIsCmd && iter->IsCmd)
				bIsCmd = true;

			//if (!bIsCmd && mn_Comspec4Ntvdm && iter->ProcessID == mn_Comspec4Ntvdm)
			//	bIsCmd = bIsNtvdm = true;

			//
			if (!dwFarPID && iter->IsFar)
			{
				dwFarPID = iter->ProcessID;
				//dwInputTID = iter->InputTID;
			}
			
			// Считаем "клиентские" процессы
			if (!iter->IsConHost)
			{
				// "условно активный процесс"
				if (!dwActivePID)
					dwActivePID = iter->ProcessID;
				else if (dwActivePID == iter->ParentPID)
					dwActivePID = iter->ProcessID;

				nClientCount++;
			}
		}

		//++iter;
	}

	TODO("Однако, наверное cmd.exe/tcc.exe может быть запущен и в 'фоне'? Например из Update");

	DWORD nNewProgramStatus = 0;

	if (bIsFar) // сначала - ставим флаг "InStack", т.к. ниже флаг фара может быть сброшен из-за bIsCmd
		nNewProgramStatus |= CES_FARINSTACK;

	if (bIsCmd && bIsFar)  // Если в консоли запущен cmd.exe/tcc.exe - значит (скорее всего?) фар выполняет команду
	{
		bIsFar = false; dwFarPID = 0;
	}

	if (bIsFar)
		nNewProgramStatus |= CES_FARACTIVE;

	if (bIsFar && bIsNtvdm)
		// 100627 -- считаем, что фар не запускает 16бит программные без cmd (%comspec%)
		bIsNtvdm = false;

	//#ifdef _DEBUG
	//else
	//	nNewProgramStatus = nNewProgramStatus;
	//#endif
	if (bIsTelnet)
		nNewProgramStatus |= CES_TELNETACTIVE;

	if (bIsNtvdm)  // определяется выше как "(mn_ProgramStatus & CES_NTVDM) == CES_NTVDM"
		nNewProgramStatus |= CES_NTVDM;

	if (mn_ProgramStatus != nNewProgramStatus)
		SetProgramStatus(nNewProgramStatus);

	mn_ProcessCount = (int)m_Processes.size();
	mn_ProcessClientCount = nClientCount;

	if (dwFarPID && mn_FarPID != dwFarPID)
		AllowSetForegroundWindow(dwFarPID);

	if (!mn_FarPID && mn_FarPID != dwFarPID)
	{
		// Если ДО этого был НЕ фар, а сейчас появился фар - перерисовать панели.
		// Это нужно на всякий случай, чтобы гарантированно отрисовались расширения цветов и шрифтов.
		// Если был фар, а стал НЕ фар - перерисовку не делаем, чтобы на экране
		// не мелькали цветные артефакты (пример - вызов ActiveHelp из редактора)
		lbChanged = TRUE;
	}
	//mn_FarPID = dwFarPID;
	SetFarPID(dwFarPID);
	
	if (mn_ActivePID != dwActivePID)
		SetActivePID(dwActivePID);

	//if (!dwFarPID)
	//	mn_FarPID_PluginDetected = 0;

	//TODO("Если сменился FAR - переоткрыть данные для Colorer - CheckColorMapping();");
	//mn_FarInputTID = dwInputTID;

	if (mn_ProcessCount == 0)
	{
		if (mn_InRecreate == 0)
		{
			StopSignal();
		}
		else if (mn_InRecreate == 1)
		{
			mn_InRecreate = 2;
		}
	}

	// Обновить список процессов в окне настроек и закладки
	if (abProcessChanged)
	{
		gpConEmu->UpdateProcessDisplay(abProcessChanged);
		//2009-09-10
		//gpConEmu->mp_TabBar->Refresh(mn_ProgramStatus & CES_FARACTIVE);
		gpConEmu->mp_TabBar->Update();

		//if (!mb_AdminShieldChecked)
		//{
		//	mb_AdminShieldChecked = TRUE;

		//	if ((gOSVer.dwMajorVersion > 6) || ((gOSVer.dwMajorVersion == 6) && (gOSVer.dwMinorVersion >= 1)))
		//		gpConEmu->Taskbar_SetShield(true);
		//}
	}

	return lbChanged;
}

// Возвращает TRUE если сменился статус (Far/не Far)
BOOL CRealConsole::ProcessUpdate(const DWORD *apPID, UINT anCount)
{
	BOOL lbChanged = FALSE;
	TODO("OPTIMIZE: хорошо бы от секции вообще избавиться, да и не всегда обновлять нужно...");
	MSectionLock SPRC; SPRC.Lock(&csPRC);
	BOOL lbRecreateOk = FALSE;
	bool bIsWin64 = IsWindows64();

	if (mn_InRecreate && mn_ProcessCount == 0)
	{
		// Раз сюда что-то пришло, значит мы получили пакет, значит консоль запустилась
		lbRecreateOk = TRUE;
	}

	DWORD PID[40] = {0};
	_ASSERTE(anCount<=countof(PID));
	if (anCount>countof(PID)) anCount = countof(PID);

	if (mp_ConHostSearch)
	{
		if (!mn_ConHost_PID)
		{
			// Ищем новые процессы "conhost.exe"
			// Раз наш сервер "проснулся" - значит "conhost.exe" уже должен быть
			// "ProcessUpdate(SrvPID, 1)" вызывается еще из StartProcess
			ConHostSearch(anCount>1);
		}
	}

	if (mn_ConHost_PID)
	{
		_ASSERTE(*apPID != mn_ConHost_PID);
		UINT nCount = 0;
		PID[nCount++] = *apPID;
		PID[nCount++] = mn_ConHost_PID;
		
		for (UINT i = 1; i < anCount; i++)
		{
			if (apPID[i] && (apPID[i] != mn_ConHost_PID))
			{
				PID[nCount++] = apPID[i];
			}
		}

		_ASSERTE(nCount<=countof(PID));
		anCount = nCount;
	}
	else
	{
		memmove(PID, apPID, anCount*sizeof(DWORD));
	}

	UINT i = 0;
	//std::vector<ConProcess>::iterator iter, end;
	//BOOL bAlive = FALSE;
	BOOL bProcessChanged = FALSE, bProcessNew = FALSE, bProcessDel = FALSE;

	// Проверить, может какие-то процессы уже помечены как закрывающиеся - их не добавлять
	for (UINT j = 0; j < anCount; j++)
	{
		for (UINT k = 0; k < countof(m_TerminatedPIDs); k++)
		{
			if (m_TerminatedPIDs[k] == PID[j])
			{
				PID[j] = 0; break;
			}
		}
	}

	// Проверить, может какие-то из запомненных в m_FarPlugPIDs процессов отвалились от консоли
	for (UINT j = 0; j < mn_FarPlugPIDsCount; j++)
	{
		if (m_FarPlugPIDs[j] == 0)
			continue;

		bool bFound = false;

		for(i = 0; i < anCount; i++)
		{
			if (PID[i] == m_FarPlugPIDs[j])
			{
				bFound = true; break;
			}
		}

		if (!bFound)
			m_FarPlugPIDs[j] = 0;
	}

	// поставить пометочку на все процессы, вдруг кто уже убился
	for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
	{
		m_Processes[ii].inConsole = false;
	}
	//iter = m_Processes.begin();
	//end = m_Processes.end();
	//while (iter != end)
	//{
	//	iter->inConsole = false;
	//	++iter;
	//}

	// Проверяем, какие процессы уже есть в нашем списке
	//iter = m_Processes.begin();
	//while (iter != end)
	for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
	{
		ConProcess* iter = &(m_Processes[ii]);
		for (i = 0; i < anCount; i++)
		{
			if (PID[i] && PID[i] == iter->ProcessID)
			{
				iter->inConsole = true;
				PID[i] = 0; // Его добавлять уже не нужно, мы о нем знаем
				break;
			}
		}

		//++iter;
	}

	// Проверяем, есть ли изменения
	for (i = 0; i < anCount; i++)
	{
		if (PID[i])
		{
			bProcessNew = TRUE; break;
		}
	}

	//iter = m_Processes.begin();
	//while (iter != end)
	for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
	{
		ConProcess* iter = &(m_Processes[ii]);

		if (iter->inConsole == false)
		{
			bProcessDel = TRUE; break;
		}

		//++iter;
	}

	// Теперь нужно добавить новый процесс
	if (bProcessNew || bProcessDel)
	{
		ConProcess cp;
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
		_ASSERTE(h!=INVALID_HANDLE_VALUE);

		if (h==INVALID_HANDLE_VALUE)
		{
			DWORD dwErr = GetLastError();
			wchar_t szError[255];
			_wsprintf(szError, SKIPLEN(countof(szError)) L"Can't create process snapshoot, ErrCode=0x%08X", dwErr);
			gpConEmu->DebugStep(szError);
		}
		else
		{
			//Snapshoot создан, поехали
			// Перед добавлением нового - поставить пометочку на все процессы, вдруг кто уже убился
			for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
			{
				m_Processes[ii].Alive = false;
			}
			//iter = m_Processes.begin();
			//end = m_Processes.end();
			//while (iter != end)
			//{
			//	iter->Alive = false;
			//	++iter;
			//}

			PROCESSENTRY32 p; memset(&p, 0, sizeof(p)); p.dwSize = sizeof(p);
			BOOL TerminatedPIDsExist[128] = {};
			_ASSERTE(countof(TerminatedPIDsExist)==countof(m_TerminatedPIDs));

			if (Process32First(h, &p))
			{
				do
				{
					DWORD th32ProcessID = p.th32ProcessID;
					// Если этот процесс был указан как sst_ComspecStop/sst_AppStop/sst_App16Stop
					// - сбросить лок.переменную в 0 и continue;
					// Иначе в массиве процессов могут оставаться "устаревшие", но еще не отвалившиеся от консоли
					// Что в свою очередь, может приводить к глюкам отрисовки и определения текущего статуса (Far/не Far)
					for (UINT k = 0; k < countof(m_TerminatedPIDs); k++)
					{
						if (m_TerminatedPIDs[k] == th32ProcessID)
						{
							th32ProcessID = 0;
							TerminatedPIDsExist[k] = TRUE;
							break;
						}
					}
					if (!th32ProcessID)
						continue; // следующий процесс

					// Если он есть в PID[] - добавить в m_Processes
					if (bProcessNew)
					{
						for (i = 0; i < anCount; i++)
						{
							if (PID[i] && PID[i] == p.th32ProcessID)
							{
								if (!bProcessChanged) bProcessChanged = TRUE;

								memset(&cp, 0, sizeof(cp));
								cp.ProcessID = PID[i]; cp.ParentPID = p.th32ParentProcessID;
								ProcessCheckName(cp, p.szExeFile); //far, telnet, cmd, tcc, conemuc, и пр.
								cp.Alive = true;
								cp.inConsole = true;

								bool bIsWowProcess = false;
								if (bIsWin64)
								{
									MODULEENTRY32 mi = {sizeof(mi)};
									HANDLE hMod = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, cp.ProcessID);
									DWORD nErrCode = (hMod == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
									if (hMod != INVALID_HANDLE_VALUE)
									{
										if (Module32First(hMod, &mi))
										{
											do {
												if (lstrcmpi(mi.szModule, L"wow64.dll") == 0)
												{
													bIsWowProcess = true; break;
												}
											} while (Module32Next(hMod, &mi));
										}
										SafeCloseHandle(hMod);
									}
									UNREFERENCED_PARAMETER(nErrCode);
								}
								UNREFERENCED_PARAMETER(bIsWowProcess);


								SPRC.RelockExclusive(300); // Заблокировать, если это еще не сделано
								m_Processes.push_back(cp);
							}
						}
					}

					// Перебираем запомненные процессы - поставить флажок Alive
					// сохранить имя для тех процессов, которым ранее это сделать не удалось
					//iter = m_Processes.begin();
					//end = m_Processes.end();
					//while (iter != end)
					//{
					for (INT_PTR ii = 0; ii < m_Processes.size(); ii++)
					{
						ConProcess* iter = &(m_Processes[ii]);
						if (iter->ProcessID == p.th32ProcessID)
						{
							iter->Alive = true;

							if (!iter->NameChecked)
							{
								// пометить, что сменился список (определилось имя процесса)
								if (!bProcessChanged) bProcessChanged = TRUE;

								//far, telnet, cmd, tcc, conemuc, и пр.
								ProcessCheckName(*iter, p.szExeFile);
								// запомнить родителя
								iter->ParentPID = p.th32ParentProcessID;
							}
						}

						//++iter;
					}

					// Следущий процесс
				}
				while(Process32Next(h, &p));

				// Убрать из массива те процессы, которых уже нет
				for (UINT k = 0; k < countof(m_TerminatedPIDs); k++)
				{
					if (m_TerminatedPIDs[k] && !TerminatedPIDsExist[k])
						m_TerminatedPIDs[k] = 0;
				}
			}

			// Закрыть shapshoot
			SafeCloseHandle(h);
		}
	}

	// Убрать процессы, которых уже нет
	//iter = m_Processes.begin();
	//end = m_Processes.end();
	//while (iter != end)
	INT_PTR ii = 0;
	while (ii < m_Processes.size())
	{
		ConProcess* iter = &(m_Processes[ii]);
		if (!iter->Alive || !iter->inConsole)
		{
			if (!bProcessChanged) bProcessChanged = TRUE;

			SPRC.RelockExclusive(300); // Если уже нами заблокирован - просто вернет FALSE
			//iter = m_Processes.erase(iter);
			//end = m_Processes.end();
			m_Processes.erase(ii);
		}
		else
		{
			//if (mn_Far_PluginInputThreadId && mn_FarPID_PluginDetected
			//    && iter->ProcessID == mn_FarPID_PluginDetected
			//    && iter->InputTID == 0)
			//    iter->InputTID = mn_Far_PluginInputThreadId;
			//++iter;
			ii++;
		}
	}

	// Далее могут идти длительные операции, поэтому откатим "Exclusive"
	if (SPRC.isLocked(TRUE))
	{
		SPRC.Unlock();
		SPRC.Lock(&csPRC);
	}

	// Обновить статус запущенных программ, получить PID FAR'а, посчитать количество процессов в консоли
	if (ProcessUpdateFlags(bProcessChanged))
		lbChanged = TRUE;

	//
	if (lbRecreateOk)
		mn_InRecreate = 0;

	return lbChanged;
}

void CRealConsole::ProcessCheckName(struct ConProcess &ConPrc, LPWSTR asFullFileName)
{
	wchar_t* pszSlash = _tcsrchr(asFullFileName, _T('\\'));
	if (pszSlash) pszSlash++; else pszSlash=asFullFileName;

	int nLen = _tcslen(pszSlash);

	if (nLen>=63) pszSlash[63]=0;

	lstrcpyW(ConPrc.Name, pszSlash);

	ConPrc.IsMainSrv = (ConPrc.ProcessID == mn_MainSrv_PID);
	if (ConPrc.IsMainSrv)
	{
		// 
		_ASSERTE(lstrcmpi(ConPrc.Name, _T("conemuc.exe"))==0 || lstrcmpi(ConPrc.Name, _T("conemuc64.exe"))==0);
	}
	else if (lstrcmpi(ConPrc.Name, _T("conemuc.exe"))==0 || lstrcmpi(ConPrc.Name, _T("conemuc64.exe"))==0)
	{
		_ASSERTE(mn_MainSrv_PID!=0 && "Main server PID was not detemined?");
	}

	// Тут главное не промахнуться, и не посчитать корневой conemuc, из которого запущен сам FAR, или который запустил плагин, чтобы GUI прицепился к этой консоли
	ConPrc.IsCmd = !ConPrc.IsMainSrv
			&& (lstrcmpi(ConPrc.Name, _T("cmd.exe"))==0
				|| lstrcmpi(ConPrc.Name, _T("tcc.exe"))==0
				|| lstrcmpi(ConPrc.Name, _T("conemuc.exe"))==0
				|| lstrcmpi(ConPrc.Name, _T("conemuc64.exe"))==0);

	ConPrc.IsConHost = lstrcmpi(ConPrc.Name, _T("conhost.exe"))==0
				|| lstrcmpi(ConPrc.Name, _T("csrss.exe"))==0;

	ConPrc.IsFar = IsFarExe(ConPrc.Name);
	ConPrc.IsNtvdm = lstrcmpi(ConPrc.Name, _T("ntvdm.exe"))==0;
	ConPrc.IsTelnet = lstrcmpi(ConPrc.Name, _T("telnet.exe"))==0;

	ConPrc.NameChecked = true;

	if (!mn_ConHost_PID && ConPrc.IsConHost)
	{
		_ASSERTE(ConPrc.ProcessID!=0);
		ConHostSetPID(ConPrc.ProcessID);
	}
}

BOOL CRealConsole::WaitConsoleSize(int anWaitSize, DWORD nTimeout)
{
	BOOL lbRc = FALSE;
	//CESERVER_REQ *pIn = NULL, *pOut = NULL;
	DWORD nStart = GetTickCount();
	DWORD nDelta = 0;
	//BOOL lbBufferHeight = FALSE;
	//int nNewWidth = 0, nNewHeight = 0;

	if (nTimeout > 10000) nTimeout = 10000;

	if (nTimeout == 0) nTimeout = 100;

	if (GetCurrentThreadId() == mn_MonitorThreadID)
	{
		_ASSERTE(GetCurrentThreadId() != mn_MonitorThreadID);
		return FALSE;
	}

#ifdef _DEBUG
	wchar_t szDbg[128]; _wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"CRealConsole::WaitConsoleSize(H=%i, Timeout=%i)\n", anWaitSize, nTimeout);
	DEBUGSTRTABS(szDbg);
#endif
	WARNING("Вообще, команду в сервер может и не посылать? Сам справится? Просто проверять значения из FileMap");
	
	// Чтобы не обломаться на посылке команды в активный буфер, а ожидания - в реальном
	_ASSERTE(mp_ABuf==mp_RBuf);

	while (nDelta < nTimeout)
	{
		// Было - if (GetConWindowSize(con.m_sbi, nNewWidth, nNewHeight, &lbBufferHeight))
		if (anWaitSize == mp_RBuf->GetWindowHeight())
		{
			lbRc = TRUE;
			break;
		}

		SetMonitorThreadEvent();
		Sleep(10);
		nDelta = GetTickCount() - nStart;
	}

	_ASSERTE(lbRc && "WaitConsoleSize");
	DEBUGSTRTABS(lbRc ? L"CRealConsole::WaitConsoleSize SUCCEEDED\n" : L"CRealConsole::WaitConsoleSize FAILED!!!\n");
	return lbRc;
}

// -- заменено на перехват функции ScreenToClient
//void CRealConsole::RemoveFromCursor()
//{
//	if (!this) return;
//	//
//	if (gpSet->isLockRealConsolePos) return;
//	// Сделано (пока) только чтобы текстовое EMenu активировалось по центру консоли,
//	// а не в положении мыши (что смотрится отвратно - оно может сплющиться до 2-3 строк).
//	// Только при скрытой консоли.
//	if (!isWindowVisible())
//	{  // просто подвинем скрытое окно консоли так, чтобы курсор был ВНЕ него
//		RECT con; POINT ptCur;
//		GetWindowRect(hConWnd, &con);
//		GetCursorPos(&ptCur);
//		short x = ptCur.x + 1;
//		short y = ptCur.y + 1;
//		if (con.left != x || con.top != y)
//			MOVEWINDOW(hConWnd, x, y, con.right - con.left + 1, con.bottom - con.top + 1, TRUE);
//	}
//}

void CRealConsole::ShowConsoleOrGuiClient(int nMode) // -1 Toggle 0 - Hide 1 - Show
{
	if (this == NULL) return;

	// В GUI-режиме (putty, notepad, ...) CtrlWinAltSpace "переключает" привязку (делает detach/attach)
	// Но только в том случае, если НЕ включен "буферный" режим (GUI скрыто, показан текст консоли сервера)
	if (hGuiWnd && isGuiVisible())
	{
		ShowGuiClientExt(nMode);
	}
	else
	{
		ShowConsole(nMode);
	}
}

void CRealConsole::ShowGuiClientInt(bool bShow)
{
	if (bShow && hGuiWnd && IsWindow(hGuiWnd))
	{
		mb_GuiForceConView = false;
		ShowOtherWindow(hGuiWnd, SW_SHOW);
		ShowWindow(GetView(), SW_HIDE);
	}
	else
	{
		mb_GuiForceConView = true;
		ShowWindow(GetView(), SW_SHOW);
		if (hGuiWnd && IsWindow(hGuiWnd))
			ShowOtherWindow(hGuiWnd, SW_HIDE);
		mp_VCon->Invalidate();
	}
}

void CRealConsole::ChildSystemMenu()
{
	if (!this || !hGuiWnd)
		return;

	//Seems like we need to bring focus to ConEmu window before
	SetForegroundWindow(ghWnd);
	gpConEmu->setFocus();

	//PostConsoleMessage(hGuiWnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
	HMENU hSysMenu = GetSystemMenu(hGuiWnd, FALSE);
	if (hSysMenu)
	{
		POINT ptCur = {}; MapWindowPoints(mp_VCon->GetBack(), NULL, &ptCur, 1);
		int nCmd = gpConEmu->mp_Menu->trackPopupMenu(tmp_ChildSysMenu, hSysMenu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|TPM_NONOTIFY,
			ptCur.x, ptCur.y, ghWnd, NULL);
		if (nCmd)
		{
			PostConsoleMessage(hGuiWnd, WM_SYSCOMMAND, nCmd, 0);
		}
	}
}

void CRealConsole::ShowGuiClientExt(int nMode, BOOL bDetach /*= FALSE*/) // -1 Toggle 0 - Hide 1 - Show
{
	if (this == NULL) return;

	// В GUI-режиме (putty, notepad, ...) CtrlWinAltSpace "переключает" привязку (делает detach/attach)
	// Но только в том случае, если НЕ включен "буферный" режим (GUI скрыто, показан текст консоли сервера)
	if (!hGuiWnd)
		return;

	if (nMode == -1)
	{
		nMode = mb_GuiExternMode ? 0 : 1;
	}

	bool bPrev = gpConEmu->SetSkipOnFocus(true);

	// Вынести Gui приложение из вкладки ConEmu (но Detach не делать)	
	CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_SETGUIEXTERN, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_SETGUIEXTERN));
	if (pIn)
	{
		AllowSetForegroundWindow(mn_GuiWndPID);

		//pIn->dwData[0] = (nMode == 0) ? FALSE : TRUE;
		pIn->SetGuiExtern.bExtern = (nMode == 0) ? FALSE : TRUE;
		pIn->SetGuiExtern.bDetach = bDetach;

		CESERVER_REQ *pOut = ExecuteHkCmd(mn_GuiWndPID, pIn, ghWnd);
		if (pOut && (pOut->DataSize() >= sizeof(DWORD)))
		{
			mb_GuiExternMode = pOut->dwData[0];
		}
		ExecuteFreeResult(pOut);
		ExecuteFreeResult(pIn);

		if (gpSetCls->isAdvLogging)
		{
			wchar_t sInfo[200];
			_wsprintf(sInfo, SKIPLEN(countof(sInfo)) L"ShowGuiClientExtern: PID=%u, hGuiWnd=x%08X, bExtern=%i, bDetach=%u",
				mn_GuiWndPID, (DWORD)hGuiWnd, nMode, bDetach);
	        gpConEmu->LogString(sInfo);
		}

		SetOtherWindowPos(hGuiWnd, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
	}

	gpConEmu->SetSkipOnFocus(bPrev);
	
	mp_VCon->Invalidate();
}

void CRealConsole::ShowConsole(int nMode) // -1 Toggle 0 - Hide 1 - Show
{
	if (this == NULL) return;

	if (!hConWnd) return;

	if (nMode == -1)
	{
		//nMode = IsWindowVisible(hConWnd) ? 0 : 1;
		nMode = isShowConsole ? 0 : 1;
	}

	if (nMode == 1)
	{
		isShowConsole = true;
		//apiShowWindow(hConWnd, SW_SHOWNORMAL);
		//if (setParent) SetParent(hConWnd, 0);
		RECT rcCon, rcWnd; GetWindowRect(hConWnd, &rcCon);
		GetClientRect(GetView(), &rcWnd);
		MapWindowPoints(GetView(), NULL, (POINT*)&rcWnd, 2);
		//GetWindowRect(ghWnd, &rcWnd);
		//RECT rcShift = gpConEmu->Calc Margins(CEM_STATUS|CEM_SCROLL|CEM_FRAME,mp_VCon);
		//rcWnd.right -= rcShift.right;
		//rcWnd.bottom -= rcShift.bottom;
		TODO("Скорректировать позицию так, чтобы не вылезло за экран");

		HWND hInsertAfter = HWND_TOPMOST;

		#ifdef _DEBUG
		if (gbIsWine)
			hInsertAfter = HWND_TOP;
		#endif

		if (SetOtherWindowPos(hConWnd, hInsertAfter,
			rcWnd.right-rcCon.right+rcCon.left, rcWnd.bottom-rcCon.bottom+rcCon.top,
			0,0, SWP_NOSIZE|SWP_SHOWWINDOW))
		{
			if (!IsWindowEnabled(hConWnd))
				EnableWindow(hConWnd, true); // Для админской консоли - не сработает.

			DWORD dwExStyle = GetWindowLong(hConWnd, GWL_EXSTYLE);

			#if 0
			DWORD dw1, dwErr;

			if ((dwExStyle & WS_EX_TOPMOST) == 0)
			{
				dw1 = SetWindowLong(hConWnd, GWL_EXSTYLE, dwExStyle|WS_EX_TOPMOST);
				dwErr = GetLastError();
				dwExStyle = GetWindowLong(hConWnd, GWL_EXSTYLE);

				if ((dwExStyle & WS_EX_TOPMOST) == 0)
				{
					HWND hInsertAfter = HWND_TOPMOST;

					#ifdef _DEBUG
					if (gbIsWine)
						hInsertAfter = HWND_TOP;
					#endif

					SetOtherWindowPos(hConWnd, hInsertAfter,
						0,0,0,0, SWP_NOSIZE|SWP_NOMOVE);
				}
			}
			#endif

			// Issue 246. Возвращать фокус в ConEmu можно только если удалось установить
			// "OnTop" для RealConsole, иначе - RealConsole "всплывет" на заднем плане
			if ((dwExStyle & WS_EX_TOPMOST))
				gpConEmu->setFocus();

			//} else { //2010-06-05 Не требуется. SetOtherWindowPos выполнит команду в сервере при необходимости
			//	if (isAdministrator() || (m_Args.pszUserName != NULL)) {
			//		// Если оно запущено в Win7 as admin
			//        CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_SHOWCONSOLE, sizeof(CESERVER_REQ_HDR) + sizeof(DWORD));
			//		if (pIn) {
			//			pIn->dwData[0] = SW_SHOWNORMAL;
			//			CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), pIn, ghWnd);
			//			if (pOut) ExecuteFreeResult(pOut);
			//			ExecuteFreeResult(pIn);
			//		}
			//	}
		}

		//if (setParent) SetParent(hConWnd, 0);
	}
	else
	{
		isShowConsole = false;
		ShowOtherWindow(hConWnd, SW_HIDE);
		////if (!gpSet->isConVisible)
		//if (!apiShowWindow(hConWnd, SW_HIDE)) {
		//	if (isAdministrator() || (m_Args.pszUserName != NULL)) {
		//		// Если оно запущено в Win7 as admin
		//        CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_SHOWCONSOLE, sizeof(CESERVER_REQ_HDR) + sizeof(DWORD));
		//		if (pIn) {
		//			pIn->dwData[0] = SW_HIDE;
		//			CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), pIn, ghWnd);
		//			if (pOut) ExecuteFreeResult(pOut);
		//			ExecuteFreeResult(pIn);
		//		}
		//	}
		//}
		////if (setParent) SetParent(hConWnd, setParent2 ? ghWnd : 'ghWnd DC');
		////if (!gpSet->isConVisible)
		////EnableWindow(hConWnd, false); -- наверное не нужно
		gpConEmu->setFocus();
	}
}

//void CRealConsole::CloseMapping()
//{
//	if (pConsoleData) {
//		UnmapViewOfFile(pConsoleData);
//		pConsoleData = NULL;
//	}
//	if (hFileMapping) {
//		CloseHandle(hFileMapping);
//		hFileMapping = NULL;
//	}
//}

// Вызывается при окончании инициализации сервера из ConEmuC.exe:SendStarted (CECMD_CMDSTARTSTOP)
void CRealConsole::OnServerStarted(DWORD anServerPID, HANDLE ahServerHandle, DWORD dwKeybLayout)
{
	_ASSERTE(anServerPID && (anServerPID == mn_MainSrv_PID));
	if (ahServerHandle != NULL)
	{
		if (mh_MainSrv == NULL)
		{
			// В принципе, это может быть, если сервер запущен "извне"
			SetMainSrvPID(mn_MainSrv_PID, ahServerHandle);
			//mh_MainSrv = ahServerHandle;
		}
		else
		{
			SafeCloseHandle(ahServerHandle); // не нужен, у нас уже есть дескриптор процесса сервера
		}
	}

	//if (!mp_ConsoleInfo)
	if (!m_ConsoleMap.IsValid())
	{
		// Инициализировать имена пайпов, событий, мэппингов и т.п.
		InitNames();
		// Открыть map с данными, теперь он уже должен быть создан
		OpenMapHeader();
	}

	// И атрибуты Colorer
	// создаются по дескриптору окна отрисовки
	CreateColorMapping();

	// Возвращается через CESERVER_REQ_STARTSTOPRET
	//if ((gpSet->isMonitorConsoleLang & 2) == 2) // Один Layout на все консоли
	//	SwitchKeyboardLayout(INPUTLANGCHANGE_SYSCHARSET,gpConEmu->GetActiveKeyboardLayout());

	// Само разберется
	OnConsoleKeyboardLayout(dwKeybLayout);

	// Сервер готов принимать команды
	mb_MainSrv_Ready = true;
}

// Если эта функция вызвана - считаем, что консоль запустилась нормально
// И при ее закрытии не нужно оставлять висящим окно ConEmu
void CRealConsole::OnStartedSuccess()
{
	if (this)
	{
		mb_RConStartedSuccess = TRUE;
		gpConEmu->OnRConStartedSuccess(this);
	}
}

void CRealConsole::SetHwnd(HWND ahConWnd, BOOL abForceApprove /*= FALSE*/)
{
	// Окно разрушено? Пересоздание консоли?
	if (hConWnd && !IsWindow(hConWnd))
	{
		_ASSERTE(IsWindow(hConWnd));
		hConWnd = NULL;
	}

	// Мог быть уже вызван (AttachGui/ConsoleEvent/CMD_START)
	if (hConWnd != NULL)
	{
		if (hConWnd != ahConWnd)
		{
			if (m_ConsoleMap.IsValid())
			{
				_ASSERTE(!m_ConsoleMap.IsValid());
				//CloseMapHeader(); // вдруг был подцеплен к другому окну? хотя не должен
				// OpenMapHeader() пока не зовем, т.к. map мог быть еще не создан
			}

			Assert(hConWnd == ahConWnd);
			if (!abForceApprove)
				return;
		}
	}

	if (ahConWnd && mb_InCreateRoot)
	{
		// При запуске "под администратором" mb_InCreateRoot сразу не сбрасывается
		_ASSERTE(m_Args.bRunAsAdministrator);
		mb_InCreateRoot = FALSE;
	}

	hConWnd = ahConWnd;
	SetWindowLongPtr(mp_VCon->GetView(), 0, (LONG_PTR)ahConWnd);
	SetWindowLong(mp_VCon->GetBack(), 0, (DWORD)ahConWnd);
	SetWindowLong(mp_VCon->GetBack(), 4, (DWORD)mp_VCon->GetView());
	//if (mb_Detached && ahConWnd) // Не сбрасываем, а то нить может не успеть!
	//  mb_Detached = FALSE; // Сброс флажка, мы уже подключились
	//OpenColorMapping();
	mb_ProcessRestarted = FALSE; // Консоль запущена
	mb_InCloseConsole = FALSE;
	m_Args.bDetached = FALSE;
	ZeroStruct(m_ServerClosing);
	if (mn_InRecreate>=1)
		mn_InRecreate = 0; // корневой процесс успешно пересоздался

	if (ms_VConServer_Pipe[0] == 0)
	{
		// Запустить серверный пайп
		_wsprintf(ms_VConServer_Pipe, SKIPLEN(countof(ms_VConServer_Pipe)) CEGUIPIPENAME, L".", (DWORD)hConWnd); //был mn_MainSrv_PID //-V205

		m_RConServer.Start();
	}

#if 0
	ShowConsole(gpSet->isConVisible ? 1 : 0); // установить консольному окну флаг AlwaysOnTop или спрятать его
#endif

	//else if (isAdministrator())
	//	ShowConsole(0); // В Win7 оно таки появляется видимым - проверка вынесена в ConEmuC

	// Перенесено в OnServerStarted
	//if ((gpSet->isMonitorConsoleLang & 2) == 2) // Один Layout на все консоли
	//    SwitchKeyboardLayout(INPUTLANGCHANGE_SYSCHARSET,gpConEmu->GetActiveKeyboardLayout());

	if (isActive())
	{
		#ifdef _DEBUG
		ghConWnd = hConWnd; // на удаление
		#endif
		// Чтобы можно было найти хэндл окна по хэндлу консоли
		gpConEmu->OnActiveConWndStore(hConWnd);
		// StatusBar
		gpConEmu->mp_Status->OnActiveVConChanged(gpConEmu->ActiveConNum(), this);
	}
}

void CRealConsole::OnFocus(BOOL abFocused)
{
	if (!this) return;

	if ((mn_Focused == -1) ||
	        ((mn_Focused == 0) && abFocused) ||
	        ((mn_Focused == 1) && !abFocused))
	{
		#ifdef _DEBUG
		if (abFocused)
		{
			DEBUGSTRINPUT(L"--Get focus\n")
		}
		else
		{
			DEBUGSTRINPUT(L"--Loose focus\n")
		}
		#endif

		// Сразу, иначе по окончании PostConsoleEvent RCon может разрушиться?
		mn_Focused = abFocused ? 1 : 0;

		if (m_ServerClosing.nServerPID
			&& m_ServerClosing.nServerPID == mn_MainSrv_PID)
		{
			return;
		}

		INPUT_RECORD r = {FOCUS_EVENT};
		r.Event.FocusEvent.bSetFocus = abFocused;
		PostConsoleEvent(&r);
	}
}

void CRealConsole::CreateLogFiles()
{
	if (!m_UseLogs) return;

	if (!mp_Log)
	{
		mp_Log = new MFileLog(L"ConEmu-input", gpConEmu->ms_ConEmuExeDir, mn_MainSrv_PID);
	}

	HRESULT hr = mp_Log ? mp_Log->CreateLogFile(L"ConEmu-input", mn_MainSrv_PID, gpSetCls->isAdvLogging) : E_UNEXPECTED;
	if (hr != 0)
	{
		wchar_t szError[MAX_PATH*2];
		_wsprintf(szError, SKIPLEN(countof(szError)) L"Create log file failed! ErrCode=0x%08X\n%s\n", (DWORD)hr, mp_Log->GetLogFileName());
		MBoxA(szError);
		SafeDelete(mp_Log);
		return;
	}

	//mp_Log->LogStartEnv(gpStartEnv);

	//DWORD dwErr = 0;
	//wchar_t szFile[MAX_PATH+64], *pszDot;
	//_ASSERTE(gpConEmu->ms_ConEmuExe[0]);
	//lstrcpyW(szFile, gpConEmu->ms_ConEmuExe);
	//
	//if ((pszDot = wcsrchr(szFile, L'\\')) == NULL)
	//{
	//	DisplayLastError(L"wcsrchr failed!");
	//	return; // ошибка
	//}
	//
	//*pszDot = 0;
	////mpsz_LogPackets = (wchar_t*)calloc(pszDot - szFile + 64, 2);
	////lstrcpyW(mpsz_LogPackets, szFile);
	////swprintf_c(mpsz_LogPackets+(pszDot-szFile), L"\\ConEmu-recv-%i-%%i.con", mn_MainSrv_PID); // ConEmu-recv-<ConEmuC_PID>-<index>.con
	//_wsprintf(pszDot, SKIPLEN(countof(szFile)-(pszDot-szFile)) L"\\ConEmu-input-%i.log", mn_MainSrv_PID);
	//mh_LogInput = CreateFileW(szFile, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	//
	//if (mh_LogInput == INVALID_HANDLE_VALUE)
	//{
	//	mh_LogInput = NULL;
	//	dwErr = GetLastError();
	//	wchar_t szError[MAX_PATH*2];
	//	_wsprintf(szError, SKIPLEN(countof(szError)) L"Create log file failed! ErrCode=0x%08X\n%s\n", dwErr, szFile);
	//	MBoxA(szError);
	//	return;
	//}
	//
	//mpsz_LogInputFile = lstrdup(szFile);
	//// OK, лог создали
	//
	//// Пишем инфу
	//wchar_t szSI[MAX_PATH*4];
	//_wsprintf(szSI, SKIPLEN(countof(szSI)) L"ConEmu startup info\n\tDesktop: %s\n\tTitle: %s\n\tSize: {%u,%u},{%u,%u}\n"
	//	"\tFlags: 0x%08X, ShowWindow: %u\n\tHandles: 0x%08X, 0x%08X, 0x%08X",
	//	gpStartEnv->si.lpDesktop ? gpStartEnv->si.lpDesktop : L"",
	//	gpStartEnv->si.lpTitle ? gpStartEnv->si.lpTitle : L"",
	//	gpStartEnv->si.dwX, gpStartEnv->si.Y, gpStartEnv->si.dwXSize, gpStartEnv->si.dwYSize,
	//	gpStartEnv->si.dwFlags, (DWORD)gpStartEnv->si.wShowWindow,
	//	(DWORD)gpStartEnv->si.hStdInput, (DWORD)gpStartEnv->si.hStdOutput, (DWORD)gpStartEnv->si.hStdError);
	//LogString(szSI, TRUE);
	//
	//LogString("CmdLine:");
	//LogString(gpStartEnv->pszCmdLine ? gpStartEnv->pszCmdLine : L"<NULL>");
	//LogString("ExecMod:");
	//LogString(gpStartEnv->pszExecMod ? gpStartEnv->pszExecMod : L"<NULL>");
	//LogString("WorkDir:");
	//LogString(gpStartEnv->pszWorkDir ? gpStartEnv->pszWorkDir : L"<NULL>");
	//LogString("PathEnv:");
	//LogString(gpStartEnv->pszPathEnv ? gpStartEnv->pszPathEnv : L"<NULL>");
}

void CRealConsole::LogString(LPCWSTR asText, BOOL abShowTime /*= FALSE*/)
{
	if (!this) return;

	if (!asText || !mp_Log) return;

	mp_Log->LogString(asText, abShowTime!=0);

	//char chAnsi[512];
	//WideCharToMultiByte(CP_UTF8, 0, asText, -1, chAnsi, countof(chAnsi)-1, 0,0);
	//chAnsi[countof(chAnsi)-1] = 0;
	//LogString(chAnsi, abShowTime);
}

void CRealConsole::LogString(LPCSTR asText, BOOL abShowTime /*= FALSE*/)
{
	if (!this) return;

	if (!asText) return;

	if (mp_Log)
	{
		mp_Log->LogString(asText, abShowTime!=0);

		//DWORD dwLen;
		//
		//if (abShowTime)
		//{
		//	SYSTEMTIME st; GetLocalTime(&st);
		//	char szTime[32];
		//	_wsprintfA(szTime, SKIPLEN(countof(szTime)) "%i:%02i:%02i.%03i ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		//	dwLen = strlen(szTime);
		//	WriteFile(mh_LogInput, szTime, dwLen, &dwLen, 0);
		//}
		//
		//if ((dwLen = strlen(asText))>0)
		//	WriteFile(mh_LogInput, asText, dwLen, &dwLen, 0);
		//
		//WriteFile(mh_LogInput, "\r\n", 2, &dwLen, 0);
		//FlushFileBuffers(mh_LogInput);
	}
	else
	{
		#ifdef _DEBUG
		DEBUGSTRLOG(asText); DEBUGSTRLOG("\n");
		#endif
	}
}

void CRealConsole::LogInput(UINT uMsg, WPARAM wParam, LPARAM lParam, LPCWSTR pszTranslatedChars /*= NULL*/)
{
	// Есть еще вообще-то и WM_UNICHAR, но ввод UTF-32 у нас пока не поддерживается
	if (!this || !mp_Log || !m_UseLogs)
		return;
	if (!(uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR
		|| uMsg == WM_SYSCHAR || uMsg == WM_DEADCHAR || uMsg == WM_SYSDEADCHAR
		|| uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP
		|| (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)))
		return;
	if ((uMsg == WM_MOUSEMOVE) && (m_UseLogs < 2))
		return;

	char szInfo[192] = {0};
	SYSTEMTIME st; GetLocalTime(&st);
	_wsprintfA(szInfo, SKIPLEN(countof(szInfo)) "%i:%02i:%02i.%03i ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	char *pszAdd = szInfo+strlen(szInfo);

	if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR
		|| uMsg == WM_SYSCHAR || uMsg == WM_SYSDEADCHAR  || uMsg == WM_DEADCHAR
		|| uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP)
	{
		char chUtf8[64] = "";
		if (pszTranslatedChars && (*pszTranslatedChars >= 32))
		{
			chUtf8[0] = L'"';
			WideCharToMultiByte(CP_UTF8, 0, pszTranslatedChars, -1, chUtf8+1, countof(chUtf8)-3, 0,0);
			lstrcatA(chUtf8, "\"");
		}
		else if (uMsg == WM_CHAR || uMsg == WM_SYSCHAR || uMsg == WM_DEADCHAR)
		{
			chUtf8[0] = L'"';
			switch ((WORD)wParam)
			{
			case L'\r':
				chUtf8[1] = L'\\'; chUtf8[2] = L'r';
				break;
			case L'\n':
				chUtf8[1] = L'\\'; chUtf8[2] = L'n';
				break;
			case L'\t':
				chUtf8[1] = L'\\'; chUtf8[2] = L't';
				break;
			default:
				WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)&wParam, 1, chUtf8+1, countof(chUtf8)-3, 0,0);
			}
			lstrcatA(chUtf8, "\"");
		}
		else
		{
			lstrcpynA(chUtf8, "\"\" ", 5);
		}
		/* */ _wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo))
		                 "%s %s wParam=x%08X, lParam=x%08X\r\n",
						 (uMsg == WM_KEYDOWN) ? "WM_KEYDOWN" :
						 (uMsg == WM_KEYUP)   ? "WM_KEYUP  " :
						 (uMsg == WM_CHAR) ? "WM_CHAR" :
						 (uMsg == WM_SYSCHAR) ? "WM_SYSCHAR" :
						 (uMsg == WM_DEADCHAR) ? "WM_DEADCHAR" :
						 (uMsg == WM_SYSDEADCHAR) ? "WM_SYSDEADCHAR" :
						 (uMsg == WM_SYSKEYDOWN) ? "WM_SYSKEYDOWN" :
						 (uMsg == WM_SYSKEYUP) ? "WM_SYSKEYUP" : "???",
						 chUtf8,
						 (DWORD)wParam, (DWORD)lParam);
	}
	else if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)
	{
		/* */ _wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo))
		                 "x%04X (%u) wParam=x%08X, lParam=x%08X\r\n",
						 uMsg, uMsg,
						 (DWORD)wParam, (DWORD)lParam);
	}
	else
	{
		_ASSERTE(FALSE && "Unknown message");
		return;
	}

	if (*pszAdd)
	{
		mp_Log->LogString(szInfo, false, NULL, false);
		//DWORD dwLen = 0;
		//WriteFile(mh_LogInput, szInfo, strlen(szInfo), &dwLen, 0);
		//FlushFileBuffers(mh_LogInput);
	}
}

void CRealConsole::LogInput(INPUT_RECORD* pRec)
{
	if (!this || !mp_Log || !m_UseLogs) return;

	char szInfo[192] = {0};
	SYSTEMTIME st; GetLocalTime(&st);
	_wsprintfA(szInfo, SKIPLEN(countof(szInfo)) "%i:%02i:%02i.%03i ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	char *pszAdd = szInfo+strlen(szInfo);

	switch (pRec->EventType)
	{
			/*case FOCUS_EVENT: _wsprintfA(pszAdd, countof(szInfo)-(pszAdd-szInfo), "FOCUS_EVENT(%i)\r\n", pRec->Event.FocusEvent.bSetFocus);
			    break;*/
		case MOUSE_EVENT: //_wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo)) "MOUSE_EVENT\r\n");
			{
				if ((m_UseLogs < 2) && (pRec->Event.MouseEvent.dwEventFlags == MOUSE_MOVED))
					return; // Движения мышки логировать только при /log2

				_wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo))
				           "Mouse: {%ix%i} Btns:{", pRec->Event.MouseEvent.dwMousePosition.X, pRec->Event.MouseEvent.dwMousePosition.Y);
				pszAdd += strlen(pszAdd);

				if (pRec->Event.MouseEvent.dwButtonState & 1) strcat(pszAdd, "L");

				if (pRec->Event.MouseEvent.dwButtonState & 2) strcat(pszAdd, "R");

				if (pRec->Event.MouseEvent.dwButtonState & 4) strcat(pszAdd, "M1");

				if (pRec->Event.MouseEvent.dwButtonState & 8) strcat(pszAdd, "M2");

				if (pRec->Event.MouseEvent.dwButtonState & 0x10) strcat(pszAdd, "M3");

				pszAdd += strlen(pszAdd);

				if (pRec->Event.MouseEvent.dwButtonState & 0xFFFF0000)
				{
					_wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo))
					           "x%04X", (pRec->Event.MouseEvent.dwButtonState & 0xFFFF0000)>>16);
				}

				strcat(pszAdd, "} "); pszAdd += strlen(pszAdd);
				_wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo))
				           "KeyState: 0x%08X ", pRec->Event.MouseEvent.dwControlKeyState);

				if (pRec->Event.MouseEvent.dwEventFlags & 0x01) strcat(pszAdd, "|MOUSE_MOVED");

				if (pRec->Event.MouseEvent.dwEventFlags & 0x02) strcat(pszAdd, "|DOUBLE_CLICK");

				if (pRec->Event.MouseEvent.dwEventFlags & 0x04) strcat(pszAdd, "|MOUSE_WHEELED"); //-V112

				if (pRec->Event.MouseEvent.dwEventFlags & 0x08) strcat(pszAdd, "|MOUSE_HWHEELED");

				strcat(pszAdd, "\r\n");
			} break;
		case KEY_EVENT:
		{
			char chUtf8[4] = " ";
			if (pRec->Event.KeyEvent.uChar.UnicodeChar >= 32)
				WideCharToMultiByte(CP_UTF8, 0, &pRec->Event.KeyEvent.uChar.UnicodeChar, 1, chUtf8, countof(chUtf8), 0,0);
			/* */ _wsprintfA(pszAdd, SKIPLEN(countof(szInfo)-(pszAdd-szInfo))
			                 "%s (\\x%04X) %s count=%i, VK=%i, SC=%i, CH=%i, State=0x%08x %s\r\n",
			                 chUtf8, pRec->Event.KeyEvent.uChar.UnicodeChar,
			                 pRec->Event.KeyEvent.bKeyDown ? "Down," : "Up,  ",
			                 pRec->Event.KeyEvent.wRepeatCount,
			                 pRec->Event.KeyEvent.wVirtualKeyCode,
			                 pRec->Event.KeyEvent.wVirtualScanCode,
			                 pRec->Event.KeyEvent.uChar.UnicodeChar,
			                 pRec->Event.KeyEvent.dwControlKeyState,
			                 (pRec->Event.KeyEvent.dwControlKeyState & ENHANCED_KEY) ?
			                 "<Enhanced>" : "");
		} break;
	}

	if (*pszAdd)
	{
		mp_Log->LogString(szInfo, false, NULL, false);
		//DWORD dwLen = 0;
		//WriteFile(mh_LogInput, szInfo, strlen(szInfo), &dwLen, 0);
		//FlushFileBuffers(mh_LogInput);
	}
}

void CRealConsole::CloseLogFiles()
{
	SafeDelete(mp_Log);

	//SafeCloseHandle(mh_LogInput);

	//if (mpsz_LogInputFile)
	//{
	//	//DeleteFile(mpsz_LogInputFile);
	//	free(mpsz_LogInputFile); mpsz_LogInputFile = NULL;
	//}

	//if (mpsz_LogPackets) {
	//    //wchar_t szMask[MAX_PATH*2]; wcscpy(szMask, mpsz_LogPackets);
	//    //wchar_t *psz = wcsrchr(szMask, L'%');
	//    //if (psz) {
	//    //    wcscpy(psz, L"*.*");
	//    //    psz = wcsrchr(szMask, L'\\');
	//    //    if (psz) {
	//    //        psz++;
	//    //        WIN32_FIND_DATA fnd;
	//    //        HANDLE hFind = FindFirstFile(szMask, &fnd);
	//    //        if (hFind != INVALID_HANDLE_VALUE) {
	//    //            do {
	//    //                if ((fnd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)==0) {
	//    //                    wcscpy(psz, fnd.cFileName);
	//    //                    DeleteFile(szMask);
	//    //                }
	//    //            } while (FindNextFile(hFind, &fnd));
	//    //            FindClose(hFind);
	//    //        }
	//    //    }
	//    //}
	//    free(mpsz_LogPackets); mpsz_LogPackets = NULL;
	//}
}

//void CRealConsole::LogPacket(CESERVER_REQ* pInfo)
//{
//    if (!mpsz_LogPackets || m_UseLogs<3) return;
//
//    wchar_t szPath[MAX_PATH];
//    swprintf_c(szPath, mpsz_LogPackets, ++mn_LogPackets);
//
//    HANDLE hFile = CreateFile(szPath, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
//    if (hFile != INVALID_HANDLE_VALUE) {
//        DWORD dwSize = 0;
//        WriteFile(hFile, pInfo, pInfo->hdr.cbSize, &dwSize, 0);
//        CloseHandle(hFile);
//    }
//}



// Послать в консоль запрос на закрытие
BOOL CRealConsole::RecreateProcess(RConStartArgs *args)
{
	if (!this)
		return false;

	_ASSERTE((hConWnd && mh_MainSrv) || isDetached());

	if ((!hConWnd || !mh_MainSrv) && !isDetached())
	{
		AssertMsg(L"Console was not created (CRealConsole::SetConsoleSize)");
		return false; // консоль пока не создана?
	}

	if (mn_InRecreate)
	{
		AssertMsg(L"Console already in recreate...");
		return false;
	}

	_ASSERTE(m_Args.pszStartupDir==NULL || (m_Args.pszStartupDir && args->pszStartupDir));
	SafeFree(m_Args.pszStartupDir);

	if (gpSetCls->isAdvLogging)
	{
		wchar_t szPrefix[128];
		_wsprintf(szPrefix, SKIPLEN(countof(szPrefix)) L"CRealConsole::RecreateProcess, hView=x%08X, Detached=%u, AsAdmin=%u, Cmd=",
			(DWORD)(DWORD_PTR)mp_VCon->GetView(), args->bDetached, args->bRunAsAdministrator);
		wchar_t* pszInfo = lstrmerge(szPrefix, args->pszSpecialCmd ? args->pszSpecialCmd : L"<NULL>");
		if (mp_Log)
			mp_Log->LogString(pszInfo ? pszInfo : szPrefix);
		else
			gpConEmu->LogString(pszInfo ? pszInfo : szPrefix);
		SafeFree(pszInfo);
	}

	bool bCopied = m_Args.AssignFrom(args);

	// Don't leave security information (passwords) in memory
	if (args->pszUserName)
	{
		SecureZeroMemory(args->szUserPassword, sizeof(args->szUserPassword));
	}

	if (!bCopied)
	{
		AssertMsg(L"Failed to copy args (CRealConsole::RecreateProcess)");
		return false;
	}

	//if (args->pszSpecialCmd && *args->pszSpecialCmd)
	//{
	//	if (m_Args.pszSpecialCmd) Free(m_Args.pszSpecialCmd);

	//	int nLen = _tcslen(args->pszSpecialCmd);
	//	m_Args.pszSpecialCmd = (wchar_t*)Alloc(nLen+1,2);

	//	if (!m_Args.pszSpecialCmd)
	//	{
	//		Box(_T("Can't allocate memory..."));
	//		return FALSE;
	//	}

	//	lstrcpyW(m_Args.pszSpecialCmd, args->pszSpecialCmd);
	//}
	//if (args->pszStartupDir)
	//{
	//	if (m_Args.pszStartupDir) Free(m_Args.pszStartupDir);

	//	int nLen = _tcslen(args->pszStartupDir);
	//	m_Args.pszStartupDir = (wchar_t*)Alloc(nLen+1,2);

	//	if (!m_Args.pszStartupDir)
	//		return FALSE;

	//	lstrcpyW(m_Args.pszStartupDir, args->pszStartupDir);
	//}
	//m_Args.bRunAsAdministrator = args->bRunAsAdministrator;

	//DWORD nWait = 0;
	mb_ProcessRestarted = FALSE;
	mn_InRecreate = GetTickCount();
	CloseConfirmReset();

	if (!mn_InRecreate)
	{
		DisplayLastError(L"GetTickCount failed");
		return false;
	}

	if (isDetached())
	{
		_ASSERTE(mn_InRecreate && mn_ProcessCount == 0 && !mb_ProcessRestarted);
		RecreateProcessStart();
	}
	else
	{
		CloseConsole(false, false);
	}
	// mb_InCloseConsole сбросим после того, как появится новое окно!
	//mb_InCloseConsole = FALSE;
	//if (con.pConChar && con.pConAttr)
	//{
	//	wmemset((wchar_t*)con.pConAttr, 7, con.nTextWidth * con.nTextHeight);
	//}

	CloseConfirmReset();
	SetConStatus(L"Restarting process...");
	return true;
}

// И запустить ее заново
BOOL CRealConsole::RecreateProcessStart()
{
	bool lbRc = false;


	if ((mn_InRecreate == 0) || (mn_ProcessCount != 0) || mb_ProcessRestarted)
	{
		// Must not be called twice, while Restart is still pending, or was not prepared
		_ASSERTE((mn_InRecreate == 0) || (mn_ProcessCount != 0) || mb_ProcessRestarted);
	}
	else
	{
		mb_ProcessRestarted = TRUE;

		if ((mn_ProgramStatus & CES_NTVDM) == CES_NTVDM)
		{
			SetProgramStatus(0); mb_IgnoreCmdStop = FALSE;

			// При пересоздании сбрасывается 16битный режим, нужно отресайзится
			if (!PreInit())
				return FALSE;
		}
		else
		{
			SetProgramStatus(0); mb_IgnoreCmdStop = FALSE;
		}

		StopThread(TRUE/*abRecreate*/);
		ResetEvent(mh_TermEvent);
		mn_TermEventTick = 0;

		//Moved to function
		ResetVarsOnStart();

		ms_VConServer_Pipe[0] = 0;
		m_RConServer.Stop();

		ResetEvent(mh_TermEvent);
		mn_TermEventTick = 0;
		ResetEvent(mh_StartExecuted);

		mb_NeedStartProcess = TRUE;
		mb_StartResult = FALSE;

		// Взведем флажочек, т.к. консоль как бы отключилась от нашего VCon
		mb_WasStartDetached = TRUE;

		{
			// Push request to "startup queue"
			mb_WaitingRootStartup = TRUE;
			gpConEmu->mp_RunQueue->RequestRConStartup(this);
		}

		lbRc = StartMonitorThread();

		if (!lbRc)
		{
			mb_NeedStartProcess = FALSE;
			mn_InRecreate = 0;
			mb_ProcessRestarted = FALSE;
		}

	}

	return lbRc;
}

BOOL CRealConsole::IsConsoleDataChanged()
{
	if (!this) return FALSE;

	#ifdef _DEBUG
	if (mb_DebugLocked)
		return FALSE;
	#endif
	
	WARNING("После смены буфера - тоже вернуть TRUE!");
	
	return mb_ABufChaged || mp_ABuf->isConsoleDataChanged();
}

bool CRealConsole::IsFarHyperlinkAllowed(bool abFarRequired)
{
	if (!gpSet->isFarGotoEditor)
		return false;
	//if (gpSet->isFarGotoEditorVk && !isPressed(gpSet->isFarGotoEditorVk))
	if (!gpSet->IsModifierPressed(vkFarGotoEditorVk, true))
		return false;

	// Для открытия гиперссылки (http, email) фар не требуется
	if (abFarRequired)
	{
		// А вот чтобы открыть в редакторе файл - нужен фар и плагин
		if (!CVConGroup::isFarExist(fwt_NonModal|fwt_PluginRequired))
		{
			return false;
		}
	}

	// Мышка должна быть в пределах окна, иначе фигня получается
	POINT ptCur = {-1,-1};
	GetCursorPos(&ptCur);
	RECT rcWnd = {};
	GetWindowRect(this->GetView(), &rcWnd);
	if (!PtInRect(&rcWnd, ptCur))
		return false;
	// Можно
	return true;
}

//CRealConsole::ExpandTextRangeType CRealConsole::ExpandTextRange(COORD& crFrom/*[In/Out]*/, COORD& crTo/*[Out]*/, CRealConsole::ExpandTextRangeType etr, wchar_t* pszText /*= NULL*/, size_t cchTextMax /*= 0*/)
//{
//}

BOOL CRealConsole::GetConsoleLine(int nLine, wchar_t** pChar, /*CharAttr** pAttr,*/ int* pLen, MSectionLock* pcsData)
{
	return mp_ABuf->GetConsoleLine(nLine, pChar, pLen, pcsData);
}

// nWidth и nHeight это размеры, которые хочет получить VCon (оно могло еще не среагировать на изменения?
void CRealConsole::GetConsoleData(wchar_t* pChar, CharAttr* pAttr, int nWidth, int nHeight, ConEmuTextRange& etr)
{
	if (!this) return;

	if (mb_ABufChaged)
		mb_ABufChaged = false; // сбросим

	mp_ABuf->GetConsoleData(pChar, pAttr, nWidth, nHeight, etr);
}

bool CRealConsole::SetFullScreen()
{
	DWORD nServerPID;
	if (!this || ((nServerPID = GetServerPID()) == 0))
		return false;

	COORD crNewSize = {};
	bool lbRc = false;
	CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_SETFULLSCREEN, sizeof(CESERVER_REQ_HDR));
	if (pIn)
	{
		CESERVER_REQ* pOut = ExecuteSrvCmd(nServerPID, pIn, ghWnd);
		if (pOut && pOut->DataSize() >= sizeof(CESERVER_REQ_FULLSCREEN))
		{
			lbRc = (pOut->FullScreenRet.bSucceeded != 0);
			if (lbRc)
				crNewSize = pOut->FullScreenRet.crNewSize;
		}
		ExecuteFreeResult(pOut);
		ExecuteFreeResult(pIn);
	}
	TODO("crNewSize");
	return lbRc;
}

//#define PICVIEWMSG_SHOWWINDOW (WM_APP + 6)
//#define PICVIEWMSG_SHOWWINDOW_KEY 0x0101
//#define PICVIEWMSG_SHOWWINDOW_ASC 0x56731469

BOOL CRealConsole::ShowOtherWindow(HWND hWnd, int swShow, BOOL abAsync/*=TRUE*/)
{
	if ((IsWindowVisible(hWnd) == FALSE) == (swShow == SW_HIDE))
		return TRUE; // уже все сделано

	BOOL lbRc = FALSE;

	// Вероятность зависания, поэтому кидаем команду в пайп
	//lbRc = apiShowWindow(hWnd, swShow);
	//
	//lbRc = ((IsWindowVisible(hWnd) == FALSE) == (swShow == SW_HIDE));
	//
	////if (!lbRc)
	//{
	//	DWORD dwErr = GetLastError();
	//
	//	if (dwErr == 0)
	//	{
	//		if ((IsWindowVisible(hWnd) == FALSE) == (swShow == SW_HIDE))
	//			lbRc = TRUE;
	//		else
	//			dwErr = 5; // попробовать через сервер
	//	}
	//
	//	if (dwErr == 5 /*E_access*/)
		{
			//PostConsoleMessage(hWnd, WM_SHOWWINDOW, SW_SHOWNA, 0);
			CESERVER_REQ in;
			ExecutePrepareCmd(&in, CECMD_POSTCONMSG, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_POSTMSG));
			// Собственно, аргументы
			in.Msg.bPost = abAsync;
			in.Msg.hWnd = hWnd;
			in.Msg.nMsg = WM_SHOWWINDOW;
			in.Msg.wParam = swShow; //SW_SHOWNA;
			in.Msg.lParam = 0;
			
			DWORD dwTickStart = timeGetTime();
			
			CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), &in, ghWnd);
			
			gpSetCls->debugLogCommand(&in, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);

			if (pOut) ExecuteFreeResult(pOut);

			lbRc = TRUE;
		}
		//else if (!lbRc)
		//{
		//	wchar_t szClass[64], szMessage[255];
		//
		//	if (!GetClassName(hWnd, szClass, 63))
		//		_wsprintf(szClass, SKIPLEN(countof(szClass)) L"0x%08X", (DWORD)hWnd); else szClass[63] = 0;
		//
		//	_wsprintf(szMessage, SKIPLEN(countof(szMessage)) L"Can't %s %s window!",
		//	          (swShow == SW_HIDE) ? L"hide" : L"show",
		//	          szClass);
		//	DisplayLastError(szMessage, dwErr);
		//}
	//}

	return lbRc;
}

BOOL CRealConsole::SetOtherWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
	#ifdef _DEBUG
	if (!hWnd || !IsWindow(hWnd))
	{
		_ASSERTE(FALSE && "SetOtherWindowPos(<InvalidHWND>)");
	}
	#endif

	if (gpSetCls->isAdvLogging)
	{
		wchar_t sInfo[200];
		_wsprintf(sInfo, SKIPLEN(countof(sInfo)) L"SetOtherWindowPos: hWnd=x%08X, hInsertAfter=x%08X, X=%i, Y=%i, CX=%i, CY=%i, Flags=x%04X",
			(DWORD)hWnd, (DWORD)hWndInsertAfter, X,Y,cx,cy, uFlags);
        gpConEmu->LogString(sInfo);
	}


	BOOL lbRc = SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);

	if (!lbRc)
	{
		DWORD dwErr = GetLastError();

		if (dwErr == 5 /*E_access*/)
		{
			CESERVER_REQ in;
			ExecutePrepareCmd(&in, CECMD_SETWINDOWPOS, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_SETWINDOWPOS));
			// Собственно, аргументы
			in.SetWndPos.hWnd = hWnd;
			in.SetWndPos.hWndInsertAfter = hWndInsertAfter;
			in.SetWndPos.X = X;
			in.SetWndPos.Y = Y;
			in.SetWndPos.cx = cx;
			in.SetWndPos.cy = cy;
			in.SetWndPos.uFlags = uFlags;
			
			DWORD dwTickStart = timeGetTime();
			
			CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(true), &in, ghWnd);
			
			gpSetCls->debugLogCommand(&in, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);

			if (pOut) ExecuteFreeResult(pOut);

			lbRc = TRUE;
		}
		else
		{
			wchar_t szClass[64], szMessage[128];

			if (!GetClassName(hWnd, szClass, 63))
				_wsprintf(szClass, SKIPLEN(countof(szClass)) L"0x%08X", (DWORD)hWnd); else szClass[63] = 0; //-V205

			_wsprintf(szMessage, SKIPLEN(countof(szMessage)) L"SetWindowPos(%s) failed!", szClass);
			DisplayLastError(szMessage, dwErr);
		}
	}

	return lbRc;
}

BOOL CRealConsole::SetOtherWindowFocus(HWND hWnd, BOOL abSetForeground)
{
	BOOL lbRc = FALSE;
	DWORD dwErr = 0;
	HWND hLastFocus = NULL;

	if (!(m_Args.bRunAsAdministrator || m_Args.pszUserName || m_Args.bRunAsRestricted/*?*/))
	{
		if (abSetForeground)
		{
			lbRc = apiSetForegroundWindow(hWnd);
		}
		else
		{
			SetLastError(0);
			hLastFocus = SetFocus(hWnd);
			dwErr = GetLastError();
			lbRc = (dwErr == 0 /* != ERROR_ACCESS_DENIED {5}*/);
		}
	}
	else
	{
		lbRc = apiSetForegroundWindow(hWnd);
	}

	// -- смысла нет, не работает
	//if (!lbRc)
	//{
	//	CESERVER_REQ in;
	//	ExecutePrepareCmd(&in, CECMD_SETFOCUS, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_SETFOCUS));
	//	// Собственно, аргументы
	//	in.setFocus.bSetForeground = abSetForeground;
	//	in.setFocus.hWindow = hWnd;
	//	CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), &in, ghWnd);
	//	if (pOut) ExecuteFreeResult(pOut);
	//	lbRc = TRUE;
	//}

	UNREFERENCED_PARAMETER(hLastFocus);

	return lbRc;
}

HWND CRealConsole::SetOtherWindowParent(HWND hWnd, HWND hParent)
{
	HWND h = NULL;
	DWORD dwErr = 0;

	SetLastError(0);
	h = SetParent(hWnd, hParent);
	if (h == NULL)
		dwErr = GetLastError();

	if (dwErr)
	{
		CESERVER_REQ in;
		ExecutePrepareCmd(&in, CECMD_SETPARENT, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_SETPARENT));
		// Собственно, аргументы
		in.setParent.hWnd = hWnd;
		in.setParent.hParent = hParent;
		
		DWORD dwTickStart = timeGetTime();
		
		CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), &in, ghWnd);
		
		gpSetCls->debugLogCommand(&in, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);
		
		if (pOut)
		{
			h = pOut->setParent.hParent;
			ExecuteFreeResult(pOut);
		}
	}

	return h;
}

BOOL CRealConsole::SetOtherWindowRgn(HWND hWnd, int nRects, LPRECT prcRects, BOOL bRedraw)
{
	BOOL lbRc = FALSE;
	CESERVER_REQ in;
	ExecutePrepareCmd(&in, CECMD_SETWINDOWRGN, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_SETWINDOWRGN));
	// Собственно, аргументы
	in.SetWndRgn.hWnd = hWnd;

	if (nRects <= 0 || !prcRects)
	{
		_ASSERTE(nRects==0 || nRects==-1); // -1 means reset rgn and hide window
		in.SetWndRgn.nRectCount = nRects;
		in.SetWndRgn.bRedraw = bRedraw;
	}
	else
	{
		in.SetWndRgn.nRectCount = nRects;
		in.SetWndRgn.bRedraw = bRedraw;
		memmove(in.SetWndRgn.rcRects, prcRects, nRects*sizeof(RECT));
	}
	
	DWORD dwTickStart = timeGetTime();

	CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), &in, ghWnd);
	
	gpSetCls->debugLogCommand(&in, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);

	if (pOut) ExecuteFreeResult(pOut);

	lbRc = TRUE;
	return lbRc;
}

void CRealConsole::ShowHideViews(BOOL abShow)
{
	// Все окна теперь создаются в дочернем окне, и скрывается именно оно, этого достаточно
#if 0
	// т.к. apiShowWindow обломается, если окно создано от имени другого пользователя (или Run as admin)
	// то скрытие и отображение окна делаем другим способом
	HWND hPic = isPictureView();

	if (hPic)
	{
		if (abShow)
		{
			if (mb_PicViewWasHidden && !IsWindowVisible(hPic))
				ShowOtherWindow(hPic, SW_SHOWNA);

			mb_PicViewWasHidden = FALSE;
		}
		else
		{
			mb_PicViewWasHidden = TRUE;
			ShowOtherWindow(hPic, SW_HIDE);
		}
	}

	for(int p = 0; p <= 1; p++)
	{
		const PanelViewInit* pv = mp_VCon->GetPanelView(p==0);

		if (pv)
		{
			if (abShow)
			{
				if (pv->bVisible && !IsWindowVisible(pv->hWnd))
					ShowOtherWindow(pv->hWnd, SW_SHOWNA);
			}
			else
			{
				if (IsWindowVisible(pv->hWnd))
					ShowOtherWindow(pv->hWnd, SW_HIDE);
			}
		}
	}
#endif
}

void CRealConsole::OnActivate(int nNewNum, int nOldNum)
{
	if (!this)
		return;

	_ASSERTE(isActive());
	// Чтобы можно было найти хэндл окна по хэндлу консоли
	gpConEmu->OnActiveConWndStore(hConWnd);

	// Чтобы корректно таб для группы показывать
	CVConGroup::OnConActivated(mp_VCon);

	#ifdef _DEBUG
	ghConWnd = hConWnd; // на удаление
	#endif
	// Проверить
	mp_VCon->OnAlwaysShowScrollbar();
	// Чтобы все в одном месте было
	OnGuiFocused(TRUE, TRUE);

	gpConEmu->InvalidateGaps();

	gpConEmu->mp_Status->OnActiveVConChanged(nNewNum, this);

	//if ((gOSVer.dwMajorVersion > 6) || ((gOSVer.dwMajorVersion == 6) && (gOSVer.dwMinorVersion >= 1)))
	//	gpConEmu->Taskbar_SetShield(isAdministrator());
	gpConEmu->Taskbar_UpdateOverlay();

	if (hGuiWnd && !mb_GuiExternMode)
	{
		gpConEmu->OnSize();
		//SyncConsole2Window();
		//WARNING("DoubleView: переделать для...");
		//RECT rcClient = gpConEmu->GetGuiClientRect();
		//SyncGui2Window(&rcClient);
	}

	//if (hGuiWnd)
	//{
	//	HWND hFore = GetForegroundWindow();
	//	DWORD nForePID = 0;
	//	if (hFore) GetWindowThreadProcessId(hFore, &nForePID);
	//	if (mn_GuiWndPID != nForePID)
	//	{
	//		//SetOtherWindowFocus(hGuiWnd, FALSE/*use SetFocus*/);
	//		SetFocus(hGuiWnd);
	//		//PostConsoleMessage(hConWnd, WM_SETFOCUS, NULL, NULL);
	//	}
	//}


	//if (mh_MonitorThread) SetThreadPriority(mh_MonitorThread, THREAD_PRIORITY_ABOVE_NORMAL);

	if ((gpSet->isMonitorConsoleLang & 2) == 2)
	{
		// Один Layout на все консоли
		// Но нет смысла дергать сервер, если в нем и так выбран "правильный" layout
		if (gpConEmu->GetActiveKeyboardLayout() != mp_RBuf->GetKeybLayout())
		{
			SwitchKeyboardLayout(INPUTLANGCHANGE_SYSCHARSET,gpConEmu->GetActiveKeyboardLayout());
		}
	}
	else if (mp_RBuf->GetKeybLayout() && (gpSet->isMonitorConsoleLang & 1) == 1)
	{
		// Следить за Layout'ом в консоли
		gpConEmu->SwitchKeyboardLayout(mp_RBuf->GetKeybLayout());
	}

	WARNING("Не работало обновление заголовка");
	gpConEmu->UpdateTitle();
	UpdateScrollInfo();
	gpConEmu->mp_TabBar->OnConsoleActivated(nNewNum/*, isBufferHeight()*/);
	gpConEmu->mp_TabBar->Update();
	// Обновить на тулбаре статусы Scrolling(BufferHeight) & Alternative
	OnBufferHeight();
	gpConEmu->UpdateProcessDisplay(TRUE);
	//gpSet->NeedBackgroundUpdate(); -- 111105 плагиновые подложки теперь в VCon, а файловая - все равно общая, дергать не нужно
	ShowHideViews(TRUE);
	//HWND hPic = isPictureView();
	//if (hPic && mb_PicViewWasHidden) {
	//	if (!IsWindowVisible(hPic)) {
	//		if (!apiShowWindow(hPic, SW_SHOWNA)) {
	//			DisplayLastError(L"Can't show PictireView window!");
	//		}
	//	}
	//}
	//mb_PicViewWasHidden = FALSE;

	if (ghOpWnd && isActive())
		gpSetCls->UpdateConsoleMode(mp_RBuf->GetConMode());

	if (isActive())
	{
		gpConEmu->UpdateActiveGhost(mp_VCon);
		gpConEmu->OnSetCursor(-1,-1);
		gpConEmu->UpdateWindowRgn();
	}
}

void CRealConsole::OnDeactivate(int nNewNum)
{
	if (!this) return;

	HWND hFore = GetForegroundWindow();
	HWND hGui = mp_VCon->GuiWnd();
	if (hGui)
		GuiWndFocusStore();
	
	mp_VCon->SavePaneSnapshoot();

	ShowHideViews(FALSE);
	//HWND hPic = isPictureView();
	//if (hPic && IsWindowVisible(hPic)) {
	//    mb_PicViewWasHidden = TRUE;
	//	if (!apiShowWindow(hPic, SW_HIDE)) {
	//		DisplayLastError(L"Can't hide PictuteView window!");
	//	}
	//}

	// 111125 а зачем выделение сбрасывать при деактивации?
	//if (con.m_sel.dwFlags & CONSOLE_MOUSE_SELECTION)
	//{
	//	con.m_sel.dwFlags &= ~CONSOLE_MOUSE_SELECTION;
	//	//ReleaseCapture();
	//}

	// Чтобы все в одном месте было
	OnGuiFocused(FALSE);

	if ((hFore == ghWnd) // фокус был в ConEmu
		|| (hGuiWnd && (hFore == hGuiWnd))) // или в дочернем приложении
	{
		gpConEmu->setFocus();
	}
}

void CRealConsole::OnGuiFocused(BOOL abFocus, BOOL abForceChild /*= FALSE*/)
{
	if (!this)
		return;

	if (mb_InSetFocus)
	{
		#ifdef _DEBUG
		wchar_t szInfo[128];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo))
			L"CRealConsole::OnGuiFocused(%u) skipped, mb_InSetFocus=1, mb_LastConEmuFocusState=%u)",
			abFocus, gpConEmu->mb_LastConEmuFocusState);
		DEBUGSTRFOCUS(szInfo);
		#endif
		return;
	}

	mb_InSetFocus = TRUE;

	if (abFocus)
	{
		mp_VCon->OnTaskbarFocus();

		if (hGuiWnd)
		{
			if (abForceChild)
			{
				#ifdef _DEBUG
				HWND hFore = getForegroundWindow();
				DWORD nForePID = 0;
				if (hFore) GetWindowThreadProcessId(hFore, &nForePID);
				if (mn_GuiWndPID != nForePID /*|| abForceChild*/)
				{
					//SetOtherWindowFocus(hGuiWnd, FALSE/*use SetFocus*/);
					//--SetFocus(hGuiWnd);
					//PostConsoleMessage(hConWnd, WM_SETFOCUS, NULL, NULL);
					//SetForegroundWindow(hGuiWnd);
				}
				#endif

				GuiNotifyChildWindow();

				GuiWndFocusRestore();
			}
			SendMessage(ghWnd, WM_NCACTIVATE, TRUE, 0);
		}
		else
		{
			// -- От этого кода одни проблемы - например, не активируется диалог Settings щелчком по TaskBar-у
			// gpConEmu->setFocus();
		}
	}

	if (!abFocus)
	{
		if (gpConEmu->isMeForeground(true, true))
		{
			abFocus = TRUE;
			DEBUGSTRFOCUS(L"CRealConsole::OnGuiFocused - checking foreground, ConEmu in front");
		}
		else
		{
			DEBUGSTRFOCUS(L"CRealConsole::OnGuiFocused - checking foreground, ConEmu inactive");
		}
	}

	//// Если FALSE - сервер увеличивает интервал опроса консоли (GUI теряет фокус)
	//mb_ThawRefreshThread = abFocus || !gpSet->isSleepInBackground;

	// 121007 - сервер теперь дергаем только при АКТИВАЦИИ окна
	////BOOL lbNeedChange = FALSE;
	//// Разрешит "заморозку" серверной нити и обновит hdr.bConsoleActive в мэппинге
	//if (m_ConsoleMap.IsValid() && ms_MainSrv_Pipe[0])
	if (abFocus)
	{
		BOOL lbActive = isActive();

		// -- Проверки убираем. Overhead небольшой, а проблем огрести можно (например, мэппинг обновиться не успел)
		//if ((BOOL)m_ConsoleMap.Ptr()->bConsoleActive == lbActive
		//     && (BOOL)m_ConsoleMap.Ptr()->bThawRefreshThread == mb_ThawRefreshThread)
		//{
		//	lbNeedChange = FALSE;
		//}
		//else
		//{
		//	lbNeedChange = TRUE;
		//}
		//if (lbNeedChange)

		if (lbActive)
		{
			UpdateServerActive();
		}
	}

#ifdef _DEBUG
	DEBUGSTRALTSRV(L"--> Updating active was skipped\n");
#endif

	mb_InSetFocus = FALSE;
}

// Обновить в сервере флаги Active & ThawRefreshThread,
// а заодно заставить перечитать содержимое консоли (если abActive == TRUE)
void CRealConsole::UpdateServerActive(BOOL abImmediate /*= FALSE*/)
{
	if (!this) return;

	//mb_UpdateServerActive = abActive;
	bool bActiveNonSleep = false;
	if (isActive())
	{
		bActiveNonSleep = (!gpSet->isSleepInBackground || gpConEmu->isMeForeground(true, true));
	}

	if (!bActiveNonSleep)
		return; // команду в сервер посылаем только при активации

	if (!isServerCreated(true))
		return; // сервер еще не завершил инициализацию

	DEBUGTEST(DWORD nTID = GetCurrentThreadId());

	if (!abImmediate && (mn_MonitorThreadID && (GetCurrentThreadId() != mn_MonitorThreadID)))
	{
		#ifdef _DEBUG
		static bool bDebugIgnoreActiveEvent = false;
		if (bDebugIgnoreActiveEvent)
		{
			return;
		}
		#endif

		DEBUGSTRFOCUS(L"UpdateServerActive - delayed, event");
		_ASSERTE(mh_UpdateServerActiveEvent!=NULL);
		mn_ServerActiveTick1 = GetTickCount();
		SetEvent(mh_UpdateServerActiveEvent);
		return;
	}

	BOOL fSuccess = FALSE;

	// Always and only in the Main server
	if (ms_MainSrv_Pipe[0])
	{
		size_t nInSize = sizeof(CESERVER_REQ_HDR);
		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_ONACTIVATION, nInSize);
		CESERVER_REQ* pOut = NULL;

		if (pIn)
		{
			#if 0
			wchar_t szInfo[255];
			bool bFore = gpConEmu->isMeForeground(true, true);
			HWND hFore = GetForegroundWindow(), hFocus = GetFocus();
			wchar_t szForeWnd[1024]; getWindowInfo(hFore, szForeWnd); szForeWnd[128] = 0;
			_wsprintf(szInfo, SKIPLEN(countof(szInfo))
				L"UpdateServerActive - called(Active=%u, Speed=%s, mb_LastConEmuFocusState=%u, ConEmu=%s, hFore=%s, hFocus=x%08X)",
				abActive, mb_ThawRefreshThread ? L"high" : L"low", gpConEmu->mb_LastConEmuFocusState, bFore ? L"foreground" : L"inactive",
				szForeWnd, (DWORD)hFocus);
			#endif

			DWORD dwTickStart = timeGetTime();
			mn_LastUpdateServerActive = GetTickCount();

			pOut = ExecuteCmd(ms_MainSrv_Pipe, pIn, 500, ghWnd);
			fSuccess = (pOut != NULL);

			gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, ms_MainSrv_Pipe, pOut);

			#if 0
			DEBUGSTRFOCUS(szInfo);
			#endif

			mn_LastUpdateServerActive = GetTickCount();
		}

		ExecuteFreeResult(pIn);
		ExecuteFreeResult(pOut);
	}
	else
	{
		DEBUGSTRFOCUS(L"UpdateServerActive - failed, no Pipe");
	}

#ifdef _DEBUG
	wchar_t szDbgInfo[512];
	_wsprintf(szDbgInfo, SKIPLEN(countof(szDbgInfo)) L"--> Updating active(%i) %s: %s\n",
		bActiveNonSleep, fSuccess ? L"OK" : L"Failed!", *ms_MainSrv_Pipe ? (ms_MainSrv_Pipe+18) : L"<NoPipe>");
	DEBUGSTRALTSRV(szDbgInfo);
#endif
	UNREFERENCED_PARAMETER(fSuccess);
}

void CRealConsole::UpdateScrollInfo()
{
	if (!isActive())
		return;

	if (!gpConEmu->isMainThread())
	{
		gpConEmu->OnUpdateScrollInfo(FALSE/*abPosted*/);
		return;
	}

	CVConGuard guard(mp_VCon);

	UpdateCursorInfo();


	WARNING("DoubleView: заменить static на member");
	static SHORT nLastHeight = 0, nLastWndHeight = 0, nLastTop = 0;

	if (nLastHeight == mp_ABuf->GetBufferHeight()/*con.m_sbi.dwSize.Y*/
	        && nLastWndHeight == mp_ABuf->GetTextHeight()/*(con.m_sbi.srWindow.Bottom - con.m_sbi.srWindow.Top + 1)*/
	        && nLastTop == mp_ABuf->GetBufferPosY()/*con.m_sbi.srWindow.Top*/)
		return; // не менялось

	nLastHeight = mp_ABuf->GetBufferHeight()/*con.m_sbi.dwSize.Y*/;
	nLastWndHeight = mp_ABuf->GetTextHeight()/*(con.m_sbi.srWindow.Bottom - con.m_sbi.srWindow.Top + 1)*/;
	nLastTop = mp_ABuf->GetBufferPosY()/*con.m_sbi.srWindow.Top*/;
	mp_VCon->SetScroll(mp_ABuf->isScroll()/*con.bBufferHeight*/, nLastTop, nLastWndHeight, nLastHeight);
}

void CRealConsole::SetTabs(ConEmuTab* tabs, int tabsCount)
{
#ifdef _DEBUG
	wchar_t szDbg[128];
	_wsprintf(szDbg, SKIPLEN(countof(szDbg)) L"CRealConsole::SetTabs.  ItemCount=%i, PrevItemCount=%i\n", tabsCount, mn_tabsCount);
	DEBUGSTRTABS(szDbg);
#endif
	ConEmuTab* lpTmpTabs = NULL;
	//const size_t nMaxTabName = countof(tabs->Name);
	// Табы нужно проверить и подготовить
	int nActiveTab = 0, i;

	if (tabs && tabsCount)
	{
		_ASSERTE(tabs->Type>1 || !tabs->Modified);

		//int nNewSize = sizeof(ConEmuTab)*tabsCount;
		//lpNewTabs = (ConEmuTab*)Alloc(nNewSize, 1);
		//if (!lpNewTabs)
		//    return;
		//memmove ( lpNewTabs, tabs, nNewSize );

		// иначе вместо "Panels" будет то что в заголовке консоли. Например "edit - doc1.doc"
		// это например, в процессе переключения закладок
		if (tabsCount > 1 && tabs[0].Type == 1/*WTYPE_PANELS*/ && tabs[0].Current)
			tabs[0].Name[0] = 0;

		if (tabsCount == 1)  // принудительно. вдруг флажок не установлен
			tabs[0].Current = 1;

		// найти активную закладку
		for (i = (tabsCount-1); i >= 0; i--)
		{
			if (tabs[i].Current)
			{
				nActiveTab = i; break;
			}
		}

#ifdef _DEBUG

		// отладочно: имена закладок (редакторов/вьюверов) должны быть заданы!
		for(i=1; i<tabsCount; i++)
		{
			if (tabs[i].Name[0] == 0)
			{
				_ASSERTE(tabs[i].Name[0]!=0);
				//wcscpy(tabs[i].Name, gpConEmu->GetDefaultTitle());
			}
		}

#endif
	}
	else if (tabsCount == 1 && tabs == NULL)
	{
		lpTmpTabs = (ConEmuTab*)Alloc(sizeof(ConEmuTab),1);

		if (!lpTmpTabs)
			return;

		tabs = lpTmpTabs;
		tabs->Current = 1;
		tabs->Type = 1;
	}

	// Если запущено под "администратором"
	if (tabsCount && isAdministrator() && (gpSet->isAdminShield() || gpSet->isAdminSuffix()))
	{
		// иконкой на закладке (если пользователь это выбрал) или суффиксом (добавляется в GetTab)
		for (i = 0; i < tabsCount; i++)
		{
			tabs[i].Type |= fwt_Elevated;
		}
	}

	if (tabsCount != mn_tabsCount)
		mb_TabsWasChanged = TRUE;

	MSectionLock SC;

	if (tabsCount > mn_MaxTabs)
	{
		SC.Lock(&msc_Tabs, TRUE);
		mn_tabsCount = 0; Free(mp_tabs); mp_tabs = NULL;
		mn_MaxTabs = tabsCount*2+10;
		mp_tabs = (ConEmuTab*)Alloc(mn_MaxTabs,sizeof(ConEmuTab));
		mb_TabsWasChanged = TRUE;
	}
	else
	{
		SC.Lock(&msc_Tabs, FALSE);
	}

	for (i = 0; i < tabsCount; i++)
	{
		if (!mb_TabsWasChanged)
		{
			if (mp_tabs[i].Current != tabs[i].Current
			        || mp_tabs[i].Type != tabs[i].Type
			        || mp_tabs[i].Modified != tabs[i].Modified
			  )
				mb_TabsWasChanged = TRUE;
			else if (wcscmp(mp_tabs[i].Name, tabs[i].Name)!=0)
				mb_TabsWasChanged = TRUE;
		}

		mp_tabs[i] = tabs[i];
	}

	mn_tabsCount = tabsCount; mn_ActiveTab = nActiveTab;
	SC.Unlock(); // больше не нужно

	//if (mb_TabsWasChanged && mp_tabs && mn_tabsCount) {
	//    CheckPanelTitle();
	//    CheckFarStates();
	//    FindPanels();
	//}

	// Передернуть gpConEmu->mp_TabBar->..
	if (gpConEmu->isValid(mp_VCon))    // Во время создания консоли она еще не добавлена в список...
	{
		// На время появления автотабов - отключалось
		gpConEmu->mp_TabBar->SetRedraw(TRUE);
		gpConEmu->mp_TabBar->Update();
	}
}

INT_PTR CRealConsole::renameProc(HWND hDlg, UINT messg, WPARAM wParam, LPARAM lParam)
{
	CRealConsole* pRCon = NULL;
	if (messg == WM_INITDIALOG)
		pRCon = (CRealConsole*)lParam;
	else
		pRCon = (CRealConsole*)GetWindowLongPtr(hDlg, DWLP_USER);

	switch (messg)
	{
		case WM_INITDIALOG:
		{
			SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hClassIcon);
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hClassIconSm);

			gpConEmu->OnOurDialogOpened();
			_ASSERTE(pRCon!=NULL);
			SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pRCon);

			HWND hEdit = GetDlgItem(hDlg, tNewTabName);

			int nLen = 0;
			if (pRCon->ms_RenameFirstTab[0])
			{
				nLen = lstrlen(pRCon->ms_RenameFirstTab);
				SetWindowText(hEdit, pRCon->ms_RenameFirstTab);
			}
			else
			{
				ConEmuTab tab = {};
				if (pRCon->GetTab(0, &tab))
				{
					nLen = lstrlen(tab.Name);
					SetWindowText(hEdit, tab.Name);
				}
			}

			SendMessage(hEdit, EM_SETSEL, 0, nLen);

			SetFocus(hEdit);

			return FALSE;
		}

		case WM_COMMAND:
			if (HIWORD(wParam) == BN_CLICKED)
			{
				switch (LOWORD(wParam))
				{
					case IDOK:
						{
							wchar_t* pszNew = GetDlgItemText(hDlg, tNewTabName);
							pRCon->RenameTab(pszNew);
							SafeFree(pszNew);
							EndDialog(hDlg, IDOK);
							return TRUE;
						}
					case IDCANCEL:
					case IDCLOSE:
						renameProc(hDlg, WM_CLOSE, 0, 0);
						return TRUE;
				}
			}
			break;

		case WM_CLOSE:
			gpConEmu->OnOurDialogClosed();
			EndDialog(hDlg, IDCANCEL);
			break;

		case WM_DESTROY:
			break;

		default:
			return FALSE;
	}

	return FALSE;
}

void CRealConsole::DoRenameTab()
{
	if (!this)
		return;

	if (GetActiveTab() > 0)
	{
		MBox(L"Only first tab of console can be renamed in current version");
		return;
	}

	DontEnable de;
	INT_PTR iRc = DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_RENAMETAB), ghWnd, renameProc, (LPARAM)this);
	if (iRc == IDOK)
	{
		//gpConEmu->mp_TabBar->Update(); -- уже, в RenameTab(...)
	}
}

// Запустить Elevated копию фара с теми же папками на панелях
void CRealConsole::AdminDuplicate()
{
	if (!this) return;

	DuplicateRoot(false, true);
}

bool CRealConsole::DuplicateRoot(bool bSkipMsg /*= false*/, bool bRunAsAdmin /*= false*/, LPCWSTR asNewConsole /*= NULL*/, LPCWSTR asApp /*= NULL*/, LPCWSTR asParm /*= NULL*/)
{
	ConProcess* pProc = NULL, *p = NULL;
	int nCount = GetProcesses(&pProc);
	if (nCount < 2)
	{
		SafeFree(pProc);
		if (!bSkipMsg)
		{
			DisplayLastError(L"Nothing to duplicate, root process not found", -1);
		}
		return false;
	}
	bool bOk = false;
	DWORD nServerPID = GetServerPID(true);
	for (int k = 0; k <= 1 && !p; k++)
	{
		for (int i = 0; i < nCount; i++)
		{
			if (pProc[i].ProcessID == nServerPID)
				continue;

			if (isConsoleService(pProc[i].Name))
				continue;

			if (!k)
			{
				if (pProc[i].ParentPID != nServerPID)
					continue;
			}
			p = pProc+i;
			break;
		}
	}
	if (!p)
	{
		if (!bSkipMsg)
		{
			DisplayLastError(L"Can't find root process in the active console", -1);
		}
	}
	else
	{
		wchar_t szConfirm[255];
		_wsprintf(szConfirm, SKIPLEN(countof(szConfirm)) L"Do you want to duplicate tab with root '%s'?", p->Name);
		if (bSkipMsg || ((MsgBox(szConfirm, MB_OKCANCEL|MB_ICONQUESTION) == IDOK)))
		{
			bool bRootCmdRedefined = false;
			RConStartArgs args;
			args.AssignFrom(&m_Args);

			// If user want to run anything else, just inheriting current console state
			if (asApp && *asApp)
			{
				bRootCmdRedefined = true;
				SafeFree(args.pszSpecialCmd);
				if (asParm)
					args.pszSpecialCmd = lstrmerge(asApp, L" ", asParm);
				else
					args.pszSpecialCmd = lstrdup(asApp);
			}
			else if (asParm && *asParm)
			{
				bRootCmdRedefined = true;
				lstrmerge(&args.pszSpecialCmd, L" ", asParm);
			}


			if (asNewConsole && *asNewConsole)
			{
				lstrmerge(&args.pszSpecialCmd, L" ", asNewConsole);
				bRootCmdRedefined = true;
			}

			// Нужно оставить там "new_console", иначе не отключается подтверждение закрытия например
			wchar_t* pszCmdRedefined = bRootCmdRedefined ? lstrdup(args.pszSpecialCmd) : NULL;
			
			args.bDetached = TRUE;
			CVirtualConsole *pVCon = gpConEmu->CreateCon(&args);

			if (pVCon)
			{
				CRealConsole* pRCon = pVCon->RCon();

				size_t cchCmdLen = (bRootCmdRedefined ? _tcslen(pszCmdRedefined) : 0);
				size_t cbMaxSize = sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_DUPLICATE) + cchCmdLen*sizeof(wchar_t);
				
				CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_DUPLICATE, cbMaxSize);
				pIn->Duplicate.hGuiWnd = ghWnd;
				pIn->Duplicate.nGuiPID = GetCurrentProcessId();
				pIn->Duplicate.nAID = pRCon->GetMonitorThreadID();
				pIn->Duplicate.bRunAs = bRunAsAdmin;
				pIn->Duplicate.nWidth = pRCon->mp_RBuf->TextWidth();
				pIn->Duplicate.nHeight = pRCon->mp_RBuf->TextHeight();
				pIn->Duplicate.nBufferHeight = pRCon->mp_RBuf->GetBufferHeight();

				BYTE nTextColorIdx /*= 7*/, nBackColorIdx /*= 0*/, nPopTextColorIdx /*= 5*/, nPopBackColorIdx /*= 15*/;
				pRCon->PrepareDefaultColors(nTextColorIdx, nBackColorIdx, nPopTextColorIdx, nPopBackColorIdx, true);
				pIn->Duplicate.nColors = (nTextColorIdx) | (nBackColorIdx << 8) | (nPopTextColorIdx << 16) | (nPopBackColorIdx << 24);

				if (pszCmdRedefined)
					_wcscpy_c(pIn->Duplicate.sCommand, cchCmdLen+1, pszCmdRedefined);

				// Go
				CESERVER_REQ* pOut = ExecuteHkCmd(p->ProcessID, pIn, ghWnd);
				int nFRc = (pOut->DataSize() >= sizeof(DWORD)) ? (int)(pOut->dwData[0]) : -100;
				if (nFRc != 0)
				{
					if (!bSkipMsg)
					{
						_wsprintf(szConfirm, SKIPLEN(countof(szConfirm)) L"Duplicate tab with root '%s' failed, code=%i?", p->Name, nFRc);
						DisplayLastError(szConfirm, -1);
					}
				}
				else
				{
					bOk = true;
				}
				ExecuteFreeResult(pOut);
				ExecuteFreeResult(pIn);
			}

			SafeFree(pszCmdRedefined);
		}
	}
	SafeFree(pProc);
	return bOk;
}

void CRealConsole::RenameTab(LPCWSTR asNewTabText /*= NULL*/)
{
	if (!this)
		return;

	lstrcpyn(ms_RenameFirstTab, asNewTabText ? asNewTabText : L"", countof(ms_RenameFirstTab));
	gpConEmu->mp_TabBar->Update();
	mp_VCon->OnTitleChanged();
}

void CRealConsole::RenameWindow(LPCWSTR asNewWindowText /*= NULL*/)
{
	if (!this)
		return;

	DWORD dwServerPID = GetServerPID(true);
	if (!dwServerPID)
		return;

	if (!asNewWindowText || !*asNewWindowText)
		asNewWindowText = gpConEmu->GetDefaultTitle();

	int cchMax = lstrlen(asNewWindowText)+1;
	CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_SETCONTITLE, sizeof(CESERVER_REQ_HDR)+sizeof(wchar_t)*cchMax);
	if (pIn)
	{
		_wcscpy_c((wchar_t*)pIn->wData, cchMax, asNewWindowText);

		DWORD dwTickStart = timeGetTime();
		
		CESERVER_REQ *pOut = ExecuteSrvCmd(dwServerPID, pIn, ghWnd);
		
		gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);

		ExecuteFreeResult(pOut);
		ExecuteFreeResult(pIn);
	}
}

int CRealConsole::GetRootProcessIcon()
{
	return gpSet->isTabIcons ? mn_RootProcessIcon : -1;
}

int CRealConsole::GetTabCount(BOOL abVisibleOnly /*= FALSE*/)
{
	if (!this)
		return 0;

	#ifdef HT_CONEMUTAB
	WARNING("После перехода на «свои» табы отдавать и те, которые сейчас недоступны");
	if (gpSet->isTabsInCaption)
	{
		//_ASSERTE(FALSE);
	}
	#endif
	
	if (abVisibleOnly)
	{
		// Если не хотят показывать все доступные редакторы/вьювера, а только активное окно
		if (!gpSet->bShowFarWindows)
			return 1;
	}
	
	if (((mn_ProgramStatus & CES_FARACTIVE) == 0))
		return 1; // На время выполнения команд - ТОЛЬКО одна закладка

	TODO("Обработать gpSet->bHideDisabledTabs, вдруг какие-то табы засерены");
	//if (mn_tabsCount > 1 && gpSet->bHideDisabledConsoleTabs)
	//{
	//	int nCount = 0;
	//	for (int i = 0; i < mn_tabsCount; i++)
	//	{
	//		if (CanActivateFarWindow(i))
	//			nCount++;
	//	}
	//	if (nCount == 0)
	//	{
	//		_ASSERTE(nCount>0);
	//		nCount = 1;
	//	}
	//	return nCount;
	//}

	return max(mn_tabsCount,1);
}

int CRealConsole::GetActiveTab()
{
	if (!this)
		return 0;

	int nCount = GetTabCount();
	return (mn_ActiveTab < nCount) ? mn_ActiveTab : 0;
}

// (Panels=1, Viewer=2, Editor=3) |(Elevated=0x100) |(NotElevated=0x200) |(Modal=0x400)
CEFarWindowType CRealConsole::GetActiveTabType()
{
	int nType = 0;
	if (!mp_tabs)
	{
		nType = 1;
	}
	else
	{
		int iModal = -1;
		for (int i = 0; i < mn_tabsCount; i++)
		{
			if (mp_tabs[i].Modal)
			{
				iModal = i;
				break;
			}
		}

		int iActive = (iModal != -1) ? iModal : GetActiveTab();
		if (iActive >= 0 && iActive < mn_tabsCount)
		{
			nType = mp_tabs[iActive].Type;
			if (mp_tabs[iActive].Modal)
				nType |= 0x400;
		}
	}

	TODO("Надо/не надо?");
	if (isAdministrator() && (gpSet->isAdminShield() || gpSet->isAdminSuffix()))
	{
		if (gpSet->isAdminShield())
		{
			nType |= fwt_Elevated;
		}
	}

	return nType;
}

void CRealConsole::UpdateTabFlags(/*IN|OUT*/ ConEmuTab* pTab)
{
	if (ms_RenameFirstTab[0])
		pTab->Type |= fwt_Renamed;

	if (isAdministrator() && (gpSet->isAdminShield() || gpSet->isAdminSuffix()))
	{
		if (gpSet->isAdminShield())
		{
			pTab->Type |= fwt_Elevated;
		}
	}
}

// Если такого таба нет - pTab НЕ ОБНУЛЯТЬ!!!
bool CRealConsole::GetTab(int tabIdx, /*OUT*/ ConEmuTab* pTab)
{
	if (!this)
		return false;

	//if (hGuiWnd)
	//{
	//	if (tabIdx == 0)
	//	{
	//		pTab->Pos = 0; pTab->Current = 1; pTab->Type = 1; pTab->Modified = 0;
	//		int nMaxLen = min(countof(TitleFull) , countof(pTab->Name));
	//		GetWindowText(hGuiWnd, pTab->Name, nMaxLen);
	//		return true;
	//	}
	//	return false;
	//}

	if (!mp_tabs || tabIdx<0 || tabIdx>=mn_tabsCount || hGuiWnd)
	{
		// На всякий случай, даже если табы не инициализированы, а просят первый -
		// вернем просто заголовок консоли
		if (tabIdx == 0)
		{
			pTab->Pos = 0; pTab->Current = 1; pTab->Type = 1; pTab->Modified = 0;
			int nMaxLen = min(countof(TitleFull) , countof(pTab->Name));
			lstrcpyn(pTab->Name, ms_RenameFirstTab[0] ? ms_RenameFirstTab : TitleFull, nMaxLen);
			UpdateTabFlags(pTab);
			return true;
		}

		return false;
	}

	// На время выполнения DOS-команд - только один таб
	if ((mn_ProgramStatus & CES_FARACTIVE) == 0 && tabIdx > 0)
		return false;

	// Есть модальный редактор/вьювер?
	int iModal = -1;
	if (mn_tabsCount > 1)
	{
		for (int i = 0; i < mn_tabsCount; i++)
		{
			if (mp_tabs[i].Modal)
			{
				iModal = i;
				break;
			}
		}
		/*
		if (iModal != -1)
		{
			if (tabIdx == 0)
				tabIdx = iModal;
			else
				return false; // Показываем только модальный редактор/вьювер
			TODO("Доделать для новых табов, чтобы история не сбивалась");
		}
		*/
	}

	memmove(pTab, mp_tabs+tabIdx, sizeof(ConEmuTab));

	// Rename tab. Пока только первый (панель/cmd/powershell/...)
	if ((tabIdx == 0) && ms_RenameFirstTab[0])
	{
		lstrcpyn(pTab->Name, ms_RenameFirstTab, countof(pTab->Name));
	}

	// Если панель единственная - точно показываем заголовок консоли
	if (((mn_tabsCount == 1) || (mn_ProgramStatus & CES_FARACTIVE) == 0) && pTab->Type == 1)
	{
		// И есть заголовок консоли
		if (TitleFull[0] && (ms_RenameFirstTab[0] == 0))  
		{
			int nMaxLen = min(countof(TitleFull) , countof(pTab->Name));
			lstrcpyn(pTab->Name, TitleFull, nMaxLen);
		}
	}

	if (pTab->Name[0] == 0)
	{
		if (ms_PanelTitle[0])  // скорее всего - это Panels?
		{
			// 03.09.2009 Maks -> min
			int nMaxLen = min(countof(ms_PanelTitle) , countof(pTab->Name));
			lstrcpyn(pTab->Name, ms_PanelTitle, nMaxLen);
		}
		else if (mn_tabsCount == 1 && TitleFull[0])  // Если панель единственная - точно показываем заголовок консоли
		{
			// 03.09.2009 Maks -> min
			int nMaxLen = min(countof(TitleFull) , countof(pTab->Name));
			lstrcpyn(pTab->Name, TitleFull, nMaxLen);
		}
	}

	wchar_t* pszAmp = pTab->Name;
	int nCurLen = _tcslen(pTab->Name), nMaxLen = countof(pTab->Name)-1;
	
	#ifdef HT_CONEMUTAB
	WARNING("После перевода табов на ручную отрисовку - эту часть с амперсандами можно будет убрать");
	if (gpSet->isTabsInCaption)
	{
		//_ASSERTE(FALSE);
	}
	#endif

	while ((pszAmp = wcschr(pszAmp, L'&')) != NULL)
	{
		if (nCurLen >= (nMaxLen - 2))
		{
			*pszAmp = L'_';
			pszAmp ++;
		}
		else
		{
			size_t nMove = nCurLen-(pszAmp-pTab->Name)+1; // right part of string + \0
			wmemmove_s(pszAmp+1, nMove, pszAmp, nMove);
			nCurLen ++;
			pszAmp += 2;
		}
	}

	if ((iModal != -1) && (tabIdx != iModal))
		pTab->Type |= fwt_Disabled;

	UpdateTabFlags(pTab);

	return true;
}

int CRealConsole::GetModifiedEditors()
{
	int nEditors = 0;
	
	if (mp_tabs && mn_tabsCount)
	{
		for (int j = 0; j < mn_tabsCount; j++)
		{
			if ((mp_tabs[j].Type == /*Editor*/3) && (mp_tabs[j].Modified != 0))
				nEditors++;
		}
	}

	return nEditors;
}

void CRealConsole::CheckPanelTitle()
{
#ifdef _DEBUG

	if (mb_DebugLocked)
		return;

#endif

	if (mp_tabs && mn_tabsCount)
	{
		if ((mn_ActiveTab >= 0 && mn_ActiveTab < mn_tabsCount) || mn_tabsCount == 1)
		{
			WCHAR szPanelTitle[CONEMUTABMAX];

			if (!GetWindowText(hConWnd, szPanelTitle, countof(ms_PanelTitle)-1))
				ms_PanelTitle[0] = 0;
			else if (szPanelTitle[0] == L'{' || szPanelTitle[0] == L'(')
				lstrcpy(ms_PanelTitle, szPanelTitle);
		}
	}
}

DWORD CRealConsole::CanActivateFarWindow(int anWndIndex)
{
	if (!this)
		return 0;

	WARNING("CantActivateInfo: Хорошо бы при отображении хинта 'Can't activate tab' сказать 'почему'");

	DWORD dwPID = GetFarPID();

	if (!dwPID)
	{
		return -1; // консоль активируется без разбора по вкладкам (фара нет)
		//if (anWndIndex == mn_ActiveTab)
		//return 0;
	}

	// Если есть меню: ucBoxDblDownRight ucBoxDblHorz ucBoxDblHorz ucBoxDblHorz (первая или вторая строка консоли) - выходим
	// Если идет процесс (в заголовке консоли {n%}) - выходим
	// Если висит диалог - выходим (диалог обработает сам плагин)

	if (anWndIndex < 0 || anWndIndex >= mn_tabsCount)
	{
		AssertCantActivate((anWndIndex>=0 && anWndIndex<mn_tabsCount));
		return 0;
	}

	// Добавил такую проверочку. По идее, у нас всегда должен быть актуальный номер текущего окна.
	if (mn_ActiveTab == anWndIndex)
		return (DWORD)-1; // Нужное окно уже выделено, лучше не дергаться...

	if (isPictureView(TRUE))
	{
		AssertCantActivate("isPictureView"==NULL);
		return 0; // При наличии PictureView переключиться на другой таб этой консоли не получится
	}

	if (!GetWindowText(hConWnd, TitleCmp, countof(TitleCmp)-2))
		TitleCmp[0] = 0;

	// Прогресс уже определился в другом месте
	if (GetProgress(NULL)>=0)
	{
		AssertCantActivate("GetProgress>0"==NULL);
		return 0; // Идет копирование или какая-то другая операция
	}

	//// Копирование в FAR: "{33%}..."
	////2009-06-02: PPCBrowser показывает копирование так: "(33% 00:02:20)..."
	//if ((TitleCmp[0] == L'{' || TitleCmp[0] == L'(')
	//    && isDigit(TitleCmp[1]) &&
	//    ((TitleCmp[2] == L'%' /*&& TitleCmp[3] == L'}'*/) ||
	//     (isDigit(TitleCmp[2]) && TitleCmp[3] == L'%' /*&& TitleCmp[4] == L'}'*/) ||
	//     (isDigit(TitleCmp[2]) && isDigit(TitleCmp[3]) && TitleCmp[4] == L'%' /*&& TitleCmp[5] == L'}'*/))
	//   )
	//{
	//    // Идет копирование
	//    return 0;
	//}

	if (!mp_RBuf->isInitialized())
	{
		AssertCantActivate("Buf.isInitiazed"==NULL);
		return 0; // консоль не инициализирована, ловить нечего
	}
		
	if (mp_RBuf != mp_ABuf)
	{
		AssertCantActivate("mp_RBuf != mp_ABuf"==NULL);
		return 0; // если активирован доп.буфер - менять окна нельзя
	}

	BOOL lbMenuOrMacro = FALSE;

	if (mp_tabs && mn_ActiveTab >= 0 && mn_ActiveTab < mn_tabsCount)
	{
		// Меню может быть только в панелях
		if (mp_tabs[mn_ActiveTab].Type == 1/*WTYPE_PANELS*/)
		{
			lbMenuOrMacro = mp_RBuf->isFarMenuOrMacro();
		}
	}

	// Если строка меню отображается всегда:
	//  0-строка начинается с "  " или с "R   " если идет запись макроса
	//  1-я строка ucBoxDblDownRight ucBoxDblHorz ucBoxDblHorz или "[0+1]" ucBoxDblHorz ucBoxDblHorz
	//  2-я строка ucBoxDblVert
	// Наличие активного меню определяем по количеству цветов в первой строке.
	// Неактивное меню отображается всегда одним цветом - в активном подсвечиваются хоткеи и выбранный пункт

	if (lbMenuOrMacro)
	{
		AssertCantActivate(lbMenuOrMacro==FALSE);
		return 0;
	}

	// Если висит диалог - не даем переключаться по табам
	if (mp_ABuf && (mp_ABuf->GetDetector()->GetFlags() & FR_FREEDLG_MASK))
	{
		AssertCantActivate("FR_FREEDLG_MASK"==NULL);
		return 0;
	}

	AssertCantActivate(dwPID!=0);
	return dwPID;
}

BOOL CRealConsole::ActivateFarWindow(int anWndIndex)
{
	if (!this)
		return FALSE;

	DWORD dwPID = CanActivateFarWindow(anWndIndex);

	if (!dwPID)
	{
		return FALSE;
	}
	else if (dwPID == (DWORD)-1)
	{
		return TRUE; // Нужное окно уже выделено, лучше не дергаться...
	}

	BOOL lbRc = FALSE;
	//DWORD nWait = -1;
	CConEmuPipe pipe(dwPID, 100);

	if (pipe.Init(_T("CRealConsole::ActivateFarWindow")))
	{
		DWORD nData[2] = {(DWORD)anWndIndex,0};

		// Если в панелях висит QSearch - его нужно предварительно "снять"
		if (!mn_ActiveTab && (mp_ABuf && (mp_ABuf->GetDetector()->GetFlags() & FR_QSEARCH)))
			nData[1] = TRUE;

		DEBUGSTRCMD(L"GUI send CMD_SETWINDOW\n");
		if (pipe.Execute(CMD_SETWINDOW, nData, 8))
		{
			DEBUGSTRCMD(L"CMD_SETWINDOW executed\n");	

			WARNING("CMD_SETWINDOW по таймауту возвращает последнее считанное положение окон (gpTabs).");
			// То есть если переключение окна выполняется дольше 2х сек - возвратится предыдущее состояние
			DWORD cbBytesRead=0;
			//DWORD tabCount = 0, nInMacro = 0, nTemp = 0, nFromMainThread = 0;
			ConEmuTab* tabs = NULL;
			CESERVER_REQ_CONEMUTAB TabHdr;
			DWORD nHdrSize = sizeof(CESERVER_REQ_CONEMUTAB) - sizeof(TabHdr.tabs);

			//if (pipe.Read(&tabCount, sizeof(DWORD), &cbBytesRead) &&
			//	pipe.Read(&nInMacro, sizeof(DWORD), &nTemp) &&
			//	pipe.Read(&nFromMainThread, sizeof(DWORD), &nTemp)
			//	)
			if (pipe.Read(&TabHdr, nHdrSize, &cbBytesRead))
			{
				tabs = (ConEmuTab*)pipe.GetPtr(&cbBytesRead);
				_ASSERTE(cbBytesRead==(TabHdr.nTabCount*sizeof(ConEmuTab)));

				if (cbBytesRead == (TabHdr.nTabCount*sizeof(ConEmuTab)))
				{
					SetTabs(tabs, TabHdr.nTabCount);
					if ((anWndIndex >= 0) && ((DWORD)anWndIndex < TabHdr.nTabCount) && (TabHdr.nTabCount > 0))
					{
						if (tabs[anWndIndex].Current)
							lbRc = TRUE;
					}
				}

				MCHKHEAP;
			}

			pipe.Close();
			// Теперь нужно передернуть сервер, чтобы он перечитал содержимое консоли
			UpdateServerActive(); // пусть будет асинхронно, задержки раздражают

			// И MonitorThread, чтобы он получил новое содержимое
			ResetEvent(mh_ApplyFinished);
			mn_LastConsolePacketIdx--;
			SetMonitorThreadEvent();

			//120412 - не будем ждать. больше задержки раздражают, нежели возможное отставание в отрисовке окна
			//nWait = WaitForSingleObject(mh_ApplyFinished, SETSYNCSIZEAPPLYTIMEOUT);
		}
	}

	return lbRc;
}

BOOL CRealConsole::IsConsoleThread()
{
	if (!this) return FALSE;

	DWORD dwCurThreadId = GetCurrentThreadId();
	return dwCurThreadId == mn_MonitorThreadID;
}

void CRealConsole::SetForceRead()
{
	SetMonitorThreadEvent();
}

// Вызывается из TabBar->ConEmu
void CRealConsole::ChangeBufferHeightMode(BOOL abBufferHeight)
{
	if (!this)
		return;
		
	TODO("Тут бы не высоту менять, а выполнять подмену буфера на длинный вывод последней команды");
	// Пока, для совместимости, оставим как было
	_ASSERTE(mp_ABuf==mp_RBuf);
	mp_ABuf->ChangeBufferHeightMode(abBufferHeight);
}

#if 0
void CRealConsole::SetBufferHeightMode(BOOL abBufferHeight, BOOL abIgnoreLock/*=FALSE*/)
{
	if (!this)
		return;
		
	if (hGuiWnd)
	{
		// Кнопка на тулбаре должна дергать ShowOtherWindow (CConEmuMain::AskChangeBufferHeight)
		// а эта функция предназначена для установки переменной и только!
		_ASSERTE(hGuiWnd==NULL);
		//ShowOtherWindow(hGuiWnd, abBufferHeight ? SW_HIDE : SW_SHOW);
		//mp_VCon->Invalidate();
		return;
	}

	_ASSERTE(mp_ABuf==mp_RBuf);
	mp_ABuf->SetBufferHeightMode(abBufferHeight, abIgnoreLock);
}
#endif

HANDLE CRealConsole::PrepareOutputFileCreate(wchar_t* pszFilePathName)
{
	wchar_t szTemp[200];
	HANDLE hFile = NULL;

	if (GetTempPath(200, szTemp))
	{
		if (GetTempFileName(szTemp, L"CEM", 0, pszFilePathName))
		{
			hFile = CreateFile(pszFilePathName, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

			if (hFile == INVALID_HANDLE_VALUE)
			{
				pszFilePathName[0] = 0;
				hFile = NULL;
			}
		}
	}

	return hFile;
}

BOOL CRealConsole::PrepareOutputFile(BOOL abUnicodeText, wchar_t* pszFilePathName)
{
	BOOL lbRc = FALSE;
	//CESERVER_REQ_HDR In = {0};
	//const CESERVER_REQ *pOut = NULL;
	//MPipe<CESERVER_REQ_HDR,CESERVER_REQ> Pipe;
	//_ASSERTE(sizeof(In)==sizeof(CESERVER_REQ_HDR));
	//ExecutePrepareCmd(&In, CECMD_GETOUTPUT, sizeof(CESERVER_REQ_HDR));
	//WARNING("Выполнять в главном сервере? Было в ms_ConEmuC_Pipe");
	//Pipe.InitName(gpConEmu->GetDefaultTitle(), L"%s", ms_MainSrv_Pipe, 0);

	//if (!Pipe.Transact(&In, In.cbSize, &pOut))
	//{
	//	MBoxA(Pipe.GetErrorText());
	//	return FALSE;
	//}

	MFileMapping<CESERVER_CONSAVE_MAPHDR> StoredOutputHdr;
	MFileMapping<CESERVER_CONSAVE_MAP> StoredOutputItem;

	CESERVER_CONSAVE_MAPHDR* pHdr = NULL;
	CESERVER_CONSAVE_MAP* pData = NULL;

	StoredOutputHdr.InitName(CECONOUTPUTNAME, (DWORD)hConWnd); //-V205
	if (!(pHdr = StoredOutputHdr.Open()) || !pHdr->sCurrentMap[0])
	{
		DisplayLastError(L"Stored output mapping was not created!");
		return FALSE;
	}

	DWORD cchMaxBufferSize = min(pHdr->MaxCellCount, (DWORD)(pHdr->info.dwSize.X * pHdr->info.dwSize.Y));

	StoredOutputItem.InitName(pHdr->sCurrentMap); //-V205
	size_t nMaxSize = sizeof(*pData) + cchMaxBufferSize * sizeof(pData->Data[0]);
	if (!(pData = StoredOutputItem.Open(FALSE,nMaxSize)))
	{
		DisplayLastError(L"Stored output data mapping was not created!");
		return FALSE;
	}

	CONSOLE_SCREEN_BUFFER_INFO storedSbi = pData->info;



	HANDLE hFile = PrepareOutputFileCreate(pszFilePathName);
	lbRc = (hFile != NULL);

	if ((pData->hdr.nVersion == CESERVER_REQ_VER) && (pData->hdr.cbSize > sizeof(CESERVER_CONSAVE_MAP)))
	{
		//const CESERVER_CONSAVE* pSave = (CESERVER_CONSAVE*)pOut;
		UINT nWidth = storedSbi.dwSize.X;
		UINT nHeight = storedSbi.dwSize.Y;
		size_t cchMaxCount = min((nWidth*nHeight),pData->MaxCellCount);
		//const wchar_t* pwszCur = pSave->Data;
		//const wchar_t* pwszEnd = (const wchar_t*)(((LPBYTE)pOut)+pOut->hdr.cbSize);
		CHAR_INFO* ptrCur = pData->Data;
		CHAR_INFO* ptrEnd = ptrCur + cchMaxCount;

		//if (pOut->hdr.nVersion == CESERVER_REQ_VER && nWidth && nHeight && (pwszCur < pwszEnd))
		if (nWidth && nHeight && pData->Succeeded)
		{
			DWORD dwWritten;
			char *pszAnsi = NULL;
			const CHAR_INFO* ptrRn = NULL;
			wchar_t *pszBuf = (wchar_t*)malloc((nWidth+1)*sizeof(*pszBuf));
			pszBuf[nWidth] = 0;

			if (!abUnicodeText)
			{
				pszAnsi = (char*)malloc(nWidth+1);
				pszAnsi[nWidth] = 0;
			}
			else
			{
				WORD dwTag = 0xFEFF; //BOM
				WriteFile(hFile, &dwTag, 2, &dwWritten, 0);
			}

			BOOL lbHeader = TRUE;

			for (UINT y = 0; y < nHeight && ptrCur < ptrEnd; y++)
			{
				UINT nCurLen = 0;
				ptrRn = ptrCur + nWidth - 1;

				while (ptrRn >= ptrCur && ptrRn->Char.UnicodeChar == L' ')
				{
					ptrRn --;
				}

				nCurLen = ptrRn - ptrCur + 1;

				if (nCurLen > 0 || !lbHeader)    // Первые N строк если они пустые - не показывать
				{
					if (lbHeader)
					{
						lbHeader = FALSE;
					}
					else if (nCurLen == 0)
					{
						// Если ниже строк нет - больше ничего не писать
						ptrRn = ptrCur + nWidth;

						while (ptrRn < ptrEnd && ptrRn->Char.UnicodeChar == L' ') ptrRn ++;

						if (ptrRn >= ptrEnd) break;  // Заполненных строк больше нет
					}

					CHAR_INFO* ptrSrc = ptrCur;
					wchar_t* ptrDst = pszBuf;
					for (UINT i = 0; i < nCurLen; i++, ptrSrc++)
						*(ptrDst++) = ptrSrc->Char.UnicodeChar;

					if (abUnicodeText)
					{
						if (nCurLen>0)
							WriteFile(hFile, pszBuf, nCurLen*2, &dwWritten, 0);

						WriteFile(hFile, L"\r\n", 2*sizeof(wchar_t), &dwWritten, 0); //-V112
					}
					else
					{
						nCurLen = WideCharToMultiByte(CP_ACP, 0, pszBuf, nCurLen, pszAnsi, nWidth, 0,0);

						if (nCurLen>0)
							WriteFile(hFile, pszAnsi, nCurLen, &dwWritten, 0);

						WriteFile(hFile, "\r\n", 2, &dwWritten, 0);
					}
				}

				ptrCur += nWidth;
			}
		}
	}

	if (hFile != NULL && hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);

	//if (pOut && (LPVOID)pOut != (LPVOID)cbReadBuf)
	//    free(pOut);
	return lbRc;
}

void CRealConsole::SwitchKeyboardLayout(WPARAM wParam, DWORD_PTR dwNewKeyboardLayout)
{
	if (!this) return;

	if (hGuiWnd && dwNewKeyboardLayout)
	{
		#ifdef _DEBUG
		WCHAR szMsg[255];
		_wsprintf(szMsg, SKIPLEN(countof(szMsg)) L"SwitchKeyboardLayout/GUIChild(CP:%i, HKL:0x" WIN3264TEST(L"%08X",L"%X%08X") L")\n",
				  (DWORD)wParam, WIN3264WSPRINT(dwNewKeyboardLayout));
		DEBUGSTRLANG(szMsg);
		#endif

		CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_LANGCHANGE, sizeof(CESERVER_REQ_HDR) + sizeof(DWORD));
		if (pIn)
		{
			pIn->dwData[0] = (DWORD)dwNewKeyboardLayout;
			
			DWORD dwTickStart = timeGetTime();
			
			CESERVER_REQ *pOut = ExecuteHkCmd(mn_GuiWndPID, pIn, ghWnd);
			
			gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteHkCmd", pOut);
			
			ExecuteFreeResult(pOut);
			ExecuteFreeResult(pIn);
		}

		DEBUGSTRLANG(L"SwitchKeyboardLayout/GUIChild - finished\n");
	}

	if (ms_ConEmuC_Pipe[0] == 0) return;

	if (!hConWnd) return;

	//#ifdef _DEBUG
	//	WCHAR szMsg[255];
	//	_wsprintf(szMsg, SKIPLEN(countof(szMsg)) L"CRealConsole::SwitchKeyboardLayout(CP:%i, HKL:0x" WIN3264TEST(L"%08X",L"%X%08X") L")\n",
	//	          (DWORD)wParam, WIN3264WSPRINT(dwNewKeyboardLayout));
	//	DEBUGSTRLANG(szMsg);
	//#endif

	if (gpSetCls->isAdvLogging > 1)
	{
		WCHAR szInfo[255];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"CRealConsole::SwitchKeyboardLayout(CP:%i, HKL:0x%08I64X)",
		          (DWORD)wParam, (unsigned __int64)(DWORD_PTR)dwNewKeyboardLayout);
		LogString(szInfo);
	}

	// Сразу запомнить новое значение, чтобы не циклиться
	mp_RBuf->SetKeybLayout(dwNewKeyboardLayout);
	
	#ifdef _DEBUG
	WCHAR szMsg[255];
	_wsprintf(szMsg, SKIPLEN(countof(szMsg)) L"SwitchKeyboardLayout/Console(CP:%i, HKL:0x" WIN3264TEST(L"%08X",L"%X%08X") L")\n",
			  (DWORD)wParam, WIN3264WSPRINT(dwNewKeyboardLayout));
	DEBUGSTRLANG(szMsg);
	#endif

	// В FAR при XLat делается так:
	//PostConsoleMessageW(hFarWnd,WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_FORWARD, 0);
	PostConsoleMessage(hConWnd, WM_INPUTLANGCHANGEREQUEST, wParam, (LPARAM)dwNewKeyboardLayout);
}

void CRealConsole::Paste(CEPasteMode PasteMode /*= pm_Standard*/, LPCWSTR asText /*= NULL*/, bool abNoConfirm /*= false*/, bool abCygWin /*= false*/)
{
	if (!this)
		return;

	if (!hConWnd)
		return;

	WARNING("warning: Хорошо бы слямзить из ClipFixer кусочек по корректировке текста. Настройка?");

#if 0
	// Не будем пользоваться встроенным WinConsole функционалом. Мало контроля.
	PostConsoleMessage(hConWnd, WM_COMMAND, SC_PASTE_SECRET, 0);
#else

	wchar_t* pszBuf = NULL;

	if (asText != NULL)
	{
		if (!*asText)
			return;
		pszBuf = lstrdup(asText);
	}
	else
	{
		HGLOBAL hglb = NULL;
		LPCWSTR lptstr = NULL;
		wchar_t szErr[128] = {}; DWORD nErrCode = 0;

		// из буфера обмена
		if (!OpenClipboard(NULL))
		{
			nErrCode = GetLastError();
			wcscpy_c(szErr, L"Can't open Windows clipboard");
		}
		else if ((hglb = GetClipboardData(CF_UNICODETEXT)) == NULL)
		{
			nErrCode = GetLastError();
			//wcscpy_c(szErr, L"Clipboard does not contains text.\nNothing to paste.");
			szErr[0] = 0;
			TODO("Сделать статусное сообщение с таймаутом");
			//this->SetConStatus(L"Clipboard does not contains text. Nothing to paste.");
		}
		else if ((lptstr = (LPCWSTR)GlobalLock(hglb)) == NULL)
		{
			nErrCode = GetLastError();
			wcscpy_c(szErr, L"Can't lock CF_UNICODETEXT");
		}
		else
		{
			pszBuf = lstrdup(lptstr);
			GlobalUnlock(hglb);
		}

		// Done
		CloseClipboard();

		if (!pszBuf)
		{
			if (*szErr)
				DisplayLastError(szErr, nErrCode);
			return;
		}
	}

	if (abCygWin && pszBuf && *pszBuf)
	{
		wchar_t* pszCygWin = DupCygwinPath(pszBuf, false);
		if (pszCygWin)
		{
			SafeFree(pszBuf);
			pszBuf = pszCygWin;
		}
	}

	if (!pszBuf)
	{
		MBoxAssert(pszBuf && "lstrdup(lptstr) = NULL");
		return;
	}
	else if (!*pszBuf)
	{
		// Если текст "пустой" то и делать ничего не надо
		SafeFree(pszBuf);
		return;
	}

	// Теперь сформируем пакет
	wchar_t szMsg[128];
	LPCWSTR pszEnd = pszBuf + _tcslen(pszBuf);

	// Смотрим первую строку / наличие второй
	wchar_t* pszRN = wcspbrk(pszBuf, L"\r\n");
	if (pszRN && (pszRN < pszEnd))
	{
		if (PasteMode == pm_FirstLine)
		{
			*pszRN = 0; // буфер наш, что хотим - то и делаем )
			pszEnd = pszRN;
		}
		else if (gpSet->isPasteConfirmEnter && !abNoConfirm)
		{
			wcscpy_c(szMsg, L"Pasting text involves <Enter> keypress!\nContinue?");

			if (MsgBox(szMsg, MB_OKCANCEL|MB_ICONEXCLAMATION, GetTitle()) != IDOK)
			{
				goto wrap;
			}
		}
	}

	if (gpSet->nPasteConfirmLonger && !abNoConfirm && ((size_t)(pszEnd - pszBuf) > (size_t)gpSet->nPasteConfirmLonger))
	{
		_wsprintf(szMsg, SKIPLEN(countof(szMsg)) L"Pasting text length is %u chars!\nContinue?", (DWORD)(pszEnd - pszBuf));

		if (MsgBox(szMsg, MB_OKCANCEL|MB_ICONQUESTION, GetTitle()) != IDOK)
		{
			goto wrap;
		}
	}

	//INPUT_RECORD r = {KEY_EVENT};

	// Отправить в консоль все символы из: pszBuf
	if (pszEnd > pszBuf)
	{
		PostString(pszBuf, pszEnd-pszBuf);
	}
	else
	{
		_ASSERTE(pszEnd > pszBuf);
	}

wrap:
	SafeFree(pszBuf);
#endif
}

bool CRealConsole::isConsoleClosing()
{
	if (!gpConEmu->isValid(this))
		return true;

	if (m_ServerClosing.nServerPID
	        && m_ServerClosing.nServerPID == mn_MainSrv_PID
	        && (GetTickCount() - m_ServerClosing.nRecieveTick) >= SERVERCLOSETIMEOUT)
	{
		// Видимо, сервер повис во время выхода? Но проверим, вдруг он все-таки успел завершиться?
		if (WaitForSingleObject(mh_MainSrv, 0))
		{
			#ifdef _DEBUG
			wchar_t szTitle[128], szText[255];
			_wsprintf(szTitle, SKIPLEN(countof(szTitle)) L"ConEmu, PID=%u", GetCurrentProcessId());
			_wsprintf(szText, SKIPLEN(countof(szText))
			          L"This is Debug message.\n\nServer hung. PID=%u\nm_ServerClosing.nServerPID=%u\n\nPress Ok to terminate server",
			          mn_MainSrv_PID, m_ServerClosing.nServerPID);
			MessageBox(NULL, szText, szTitle, MB_ICONSTOP|MB_SYSTEMMODAL);
			#else
			_ASSERTE(m_ServerClosing.nServerPID==0);
			#endif

			TerminateProcess(mh_MainSrv, 100);
		}

		return true;
	}

	if ((hConWnd == NULL) || mb_InCloseConsole)
		return true;

	return false;
}

void CRealConsole::CloseConfirmReset()
{
	mn_CloseConfirmedTick = 0;
	mb_CloseFarMacroPosted = false;
}

bool CRealConsole::isCloseConfirmed(LPCWSTR asConfirmation, bool bForceAsk /*= false*/)
{
	if (!gpSet->isCloseConsoleConfirm)
		return true;

	if (!bForceAsk && gpConEmu->isCloseConfirmed())
		return true;

	int nBtn = 0;
	{
		ConfirmCloseParam Parm;
		Parm.nConsoles = 1;
		Parm.nOperations = (GetProgress(NULL,NULL)>=0) ? 1 : 0;
		Parm.nUnsavedEditors = GetModifiedEditors();
		Parm.asSingleConsole = asConfirmation ? asConfirmation : hGuiWnd ? gsCloseGui : gsCloseCon;
		Parm.asSingleTitle = Title;

		nBtn = ConfirmCloseConsoles(Parm);

		//DontEnable de;
		////nBtn = MessageBox(gbMessagingStarted ? ghWnd : NULL, szMsg, Title, MB_ICONEXCLAMATION|MB_YESNOCANCEL);
		//nBtn = MessageBox(gbMessagingStarted ? ghWnd : NULL,
		//	asConfirmation ? asConfirmation : gsCloseAny, Title, MB_OKCANCEL|MB_ICONEXCLAMATION);
	}

	if (nBtn != IDYES)
	{
		CloseConfirmReset();
		return false;
	}

	mn_CloseConfirmedTick = GetTickCount();
	return true;
}

void CRealConsole::CloseConsoleWindow(bool abConfirm)
{
	if (abConfirm)
	{
		if (!isCloseConfirmed(hGuiWnd ? gsCloseGui : gsCloseCon))
			return;
	}

	mb_InCloseConsole = TRUE;
	if (hGuiWnd)
	{
		PostConsoleMessage(hGuiWnd, WM_CLOSE, 0, 0);
	}
	else
	{
		PostConsoleMessage(hConWnd, WM_CLOSE, 0, 0);
	}
}

void CRealConsole::CloseConsole(bool abForceTerminate, bool abConfirm, bool abAllowMacro /*= true*/)
{
	if (!this) return;

	_ASSERTE(!mb_ProcessRestarted);

	// Для Terminate - спрашиваем отдельно
	// Don't show confirmation dialog, if this method was reentered (via GuiMacro call)
	if (abConfirm && !abForceTerminate && !mb_InPostCloseMacro)
	{
		if (!isCloseConfirmed(NULL))
			return;
	}
	#ifdef _DEBUG
	else
	{
		// при вызове из RecreateProcess, SC_SYSCLOSE (там уже спросили)
		UNREFERENCED_PARAMETER(abConfirm);
	}
	#endif

	ShutdownGuiStep(L"Closing console window");

	// Сброс background
	CESERVER_REQ_SETBACKGROUND BackClear = {};
	bool lbCleared = false;
	//mp_VCon->SetBackgroundImageData(&BackClear);

	if (abAllowMacro && mb_InPostCloseMacro)
	{
		abAllowMacro = false;
		LogString(L"Post close macro in progress, disabling abAllowMacro");
	}

	if (hConWnd)
	{
		if (!IsWindow(hConWnd))
		{
			_ASSERTE(FALSE && !mb_WasForceTerminated && "Console window was abnormally terminated?");
			return;
		}

		if (gpSet->isSafeFarClose && !abForceTerminate && abAllowMacro)
		{
			DWORD nFarPID = GetFarPID(TRUE/*abPluginRequired*/);

			if (nFarPID)
			{
				// Set flag immediately
				mb_InPostCloseMacro = true;

				LPCWSTR pszMacro = gpSet->SafeFarCloseMacro(fmv_Default);
				_ASSERTE(pszMacro && *pszMacro);

				BOOL lbExecuted = FALSE;

				// GuiMacro выполняется безотносительно "фар/не фар"
				if (*pszMacro == GUI_MACRO_PREFIX/*L'#'*/)
				{
					// Для удобства. Она сама позовет CConEmuMacro::ExecuteMacro
					PostMacro(pszMacro, TRUE);
					lbExecuted = TRUE;
				}

				// FarMacro выполняется асинхронно, так что не будем смотреть на "isAlive"
				if (!lbExecuted && nFarPID /*&& isAlive()*/)
				{
					mb_InCloseConsole = TRUE;
					gpConEmu->DebugStep(_T("ConEmu: Execute SafeFarCloseMacro"));

					if (!lbCleared)
					{
						lbCleared = true;
						mp_VCon->SetBackgroundImageData(&BackClear);
					}

					// Async, иначе ConEmu зависнуть может
					PostMacro(pszMacro, TRUE);

					lbExecuted = TRUE;

					gpConEmu->DebugStep(NULL);
				}

				// Don't return to false, otherwise it is impossible
				// to close "Hanged Far instance"
				// -- // Post failed? Continue to simple close
				//mb_InPostCloseMacro = false;

				if (lbExecuted)
					return;
			}
		}

		if (abForceTerminate && GetActivePID() && !mn_InRecreate)
		{
			// Спросить, чего хотим:
			// a) Нажать крестик в окне консоли
			// b) кильнуть активный процесс
			BOOL lbTerminateSucceeded = FALSE;
			ConProcess* pPrc = NULL;
			int nPrcCount = GetProcesses(&pPrc);
			if ((nPrcCount > 0) && pPrc)
			{
				DWORD nActivePID = GetActivePID();
				// Пусть TerminateProcess зовет главный сервер (ConEmuC.exe),
				// альтернативный может закрываться, или не отвечать...
				DWORD dwServerPID = GetServerPID(true);
				if (nActivePID && dwServerPID)
				{
					wchar_t szActive[64] = {};
					for (int i = 0; i < nPrcCount; i++)
					{
						if (pPrc[i].ProcessID == nActivePID)
						{
							lstrcpyn(szActive, pPrc[i].Name, countof(szActive));
							break;
						}
					}
					if (!*szActive)
						wcscpy_c(szActive, L"<Not found>");
					//	_wsprintf(szActive, SKIPLEN(countof(szActive)) L"PID=%u", nActivePID);

					// Спросим
					wchar_t szMsg[255];
					_wsprintf(szMsg, SKIPLEN(countof(szMsg)) 
						//L"Do you want to close %s (Yes),\n"
						//L"or terminate (kill) active process (No)?\n"
						//L"\nActive process '%s' PID=%u",
						L"Kill active process '%s' PID=%u?",
						//hGuiWnd ? L"active program" : L"RealConsole",
						szActive, nActivePID);
					int nBtn = abConfirm ? 0 : IDOK;
					if (abConfirm)
					{
						DontEnable de;
						nBtn = MessageBox(gbMessagingStarted ? ghWnd : NULL, szMsg, Title, MB_ICONEXCLAMATION|MB_OKCANCEL);
					}

					if (nBtn == IDOK)
					{
						//Terminate
						CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_TERMINATEPID, sizeof(CESERVER_REQ_HDR)+sizeof(DWORD));
						if (pIn)
						{
							pIn->dwData[0] = nActivePID;
							DWORD dwTickStart = timeGetTime();
							
							CESERVER_REQ *pOut = ExecuteSrvCmd(dwServerPID, pIn, ghWnd);
							
							gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);
							
							if (pOut)
							{
								if (pOut->hdr.cbSize == sizeof(CESERVER_REQ_HDR) + 2*sizeof(DWORD))
								{
									// Сразу, чтобы не пытаться еще и крестик нажать
									lbTerminateSucceeded = TRUE;
									// Если ошибка - покажем
									if (pOut->dwData[0] == FALSE)
									{
										_wsprintf(szMsg, SKIPLEN(countof(szMsg)) 
											L"TerminateProcess(%u) failed, code=0x%08X",
											nActivePID, pOut->dwData[1]);
										MBox(szMsg);
									}
								}
								ExecuteFreeResult(pOut);
							}
							ExecuteFreeResult(pIn);
						}
					}
					else
					{
						return;
					}
				}
			}
			if (pPrc)
				free(pPrc);
			if (lbTerminateSucceeded)
				return;
		}

		if (!lbCleared)
		{
			lbCleared = true;
			mp_VCon->SetBackgroundImageData(&BackClear);
		}

		CloseConsoleWindow(false/*NoConfirm - уже*/);
	}
	else
	{
		m_Args.bDetached = FALSE;

		if (mp_VCon)
			CConEmuChild::ProcessVConClosed(mp_VCon);
	}
}

// Разрешено только в фаре
BOOL CRealConsole::CanCloseTab(BOOL abPluginRequired /*= FALSE*/)
{
	if (abPluginRequired)
	{
		if (!isFar(TRUE/* abPluginRequired */) /*&& !GuiWnd()*/)
			return FALSE;
	}
	return TRUE;
}

// для фара - Мягко (с подтверждением) закрыть текущий таб.
// для GUI  - WM_CLOSE, пусть само разбирается
// для остальных (cmd.exe, и т.п.) WM_CLOSE в консоль. Скорее всего, она закроется сразу
void CRealConsole::CloseTab()
{
	if (!this)
	{
		_ASSERTE(this);
		return;
	}

	// Если консоль "зависла" после рестарта (или на старте)
	// То нет смысла пытаться что-то послать в сервер, которого нет
	if (!mn_MainSrv_PID && !mh_MainSrv)
	{
		//Sleep(500); // Подождать чуть-чуть, может сервер все-таки появится?

		StopSignal();

		return;
	}

	if (GuiWnd())
	{
		if (!isCloseConfirmed(gsCloseGui))
			return;
		PostConsoleMessage(GuiWnd(), WM_CLOSE, 0, 0);
	}
	else
	{
		// Проверить, можно ли послать макрос, чтобы закрыть таб (фар/не фар)?
		BOOL bCanCloseMacro = CanCloseTab(TRUE);
		if (bCanCloseMacro && !isAlive())
		{
			wchar_t szInfo[255];
			_wsprintf(szInfo, SKIPLEN(countof(szInfo))
				L"Far Manager (PID=%u) is not alive.\nClose realconsole window instead of posting Macro?",
				GetFarPID(TRUE));
			int nBrc = MessageBox(NULL, szInfo, gpConEmu->GetDefaultTitle(), MB_ICONEXCLAMATION|MB_YESNOCANCEL);
			switch (nBrc)
			{
			case IDCANCEL:
				// Отмена
				return;
			case IDYES:
				bCanCloseMacro = FALSE;
				break;
			}

			if (bCanCloseMacro)
			{
				CEFarWindowType tabtype = GetActiveTabType();
				LPCWSTR pszConfirmText =
					((tabtype & fwt_TypeMask) == fwt_Editor) ? gsCloseEditor :
					((tabtype & fwt_TypeMask) == fwt_Viewer) ? gsCloseViewer :
					gsCloseCon;

				if (!isCloseConfirmed(pszConfirmText))
				{
					// Отмена
					return;
				}
			}
		}
		else
		{
			bool bSkipConfirm = false;
			LPCWSTR pszConfirmText = gsCloseCon;
			if (bCanCloseMacro)
			{
				CEFarWindowType tabtype = GetActiveTabType();
				pszConfirmText =
					((tabtype & fwt_TypeMask) == fwt_Editor) ? gsCloseEditor :
					((tabtype & fwt_TypeMask) == fwt_Viewer) ? gsCloseViewer :
					gsCloseCon;
				if (!gpSet->isCloseEditViewConfirm)
				{
					if (((tabtype & fwt_TypeMask) == fwt_Editor) || ((tabtype & fwt_TypeMask) == fwt_Viewer))
					{
						bSkipConfirm = true;
					}
				}
			}

			if (!bSkipConfirm && !isCloseConfirmed(pszConfirmText))
			{
				// Отмена
				return;
			}
		}


		if (bCanCloseMacro)
		{
			// Это для закрытия редакторов и прочая
			PostMacro(gpSet->TabCloseMacro(fmv_Default), TRUE/*async*/);
		}
		else
		{
			//PostConsoleMessage(hConWnd, WM_CLOSE, 0, 0);
			CloseConsole(false,false);
		}
	}
}

uint CRealConsole::TextWidth()
{
	_ASSERTE(this!=NULL);

	if (!this) return MIN_CON_WIDTH;
	return mp_ABuf->TextWidth();
}

uint CRealConsole::TextHeight()
{
	_ASSERTE(this!=NULL);

	if (!this) return MIN_CON_HEIGHT;
	return mp_ABuf->TextHeight();
}

uint CRealConsole::BufferWidth()
{
	TODO("CRealConsole::BufferWidth()");
	return TextWidth();
}

uint CRealConsole::BufferHeight(uint nNewBufferHeight/*=0*/)
{
	return mp_ABuf->BufferHeight(nNewBufferHeight);
}

void CRealConsole::OnBufferHeight()
{
	mp_ABuf->OnBufferHeight();
}

bool CRealConsole::isActive(bool abAllowGroup /*= false*/)
{
	if (!this || !mp_VCon)
		return false;

	return CVConGroup::isActive(mp_VCon, abAllowGroup);
}

// Проверяет не только активность, но и "в фокусе ли ConEmu"
bool CRealConsole::isInFocus()
{
	if (!this || !mp_VCon)
		return false;

	TODO("DoubleView: когда будет группировка ввода - чтобы курсором мигать во всех консолях");
	if (!CVConGroup::isActive(mp_VCon, false/*abAllowGroup*/))
		return false;

	if (!gpConEmu->isMeForeground(false, true))
		return false;

	return true;
}

bool CRealConsole::isVisible()
{
	if (!this) return false;

	if (!mp_VCon) return false;

	return gpConEmu->isVisible(mp_VCon);
}

// Результат зависит от распознанных регионов!
// В функции есть вложенные вызовы, например,
// mp_ABuf->GetDetector()->GetFlags()
void CRealConsole::CheckFarStates()
{
#ifdef _DEBUG

	if (mb_DebugLocked)
		return;

#endif
	DWORD nLastState = mn_FarStatus;
	DWORD nNewState = (mn_FarStatus & (~CES_FARFLAGS));

	if (!isFar(true))
	{
		nNewState = 0;
		setPreWarningProgress(-1);
	}
	else
	{
		// Сначала пытаемся проверить статус по закладкам: если к нам из плагина пришло,
		// что активен "viewer/editor" - то однозначно ставим CES_VIEWER/CES_EDITOR
		if (mp_tabs && mn_ActiveTab >= 0 && mn_ActiveTab < mn_tabsCount)
		{
			if (mp_tabs[mn_ActiveTab].Type != 1)
			{
				if (mp_tabs[mn_ActiveTab].Type == 2)
					nNewState |= CES_VIEWER;
				else if (mp_tabs[mn_ActiveTab].Type == 3)
					nNewState |= CES_EDITOR;
			}
		}

		// Если по закладкам ничего не определили - значит может быть либо панель, либо
		// "viewer/editor" но при отсутствии плагина в фаре
		if ((nNewState & CES_FARFLAGS) == 0)
		{
			// пытаемся определить "viewer/editor" по заголовку окна консоли
			if (wcsncmp(Title, ms_Editor, _tcslen(ms_Editor))==0 || wcsncmp(Title, ms_EditorRus, _tcslen(ms_EditorRus))==0)
				nNewState |= CES_EDITOR;
			else if (wcsncmp(Title, ms_Viewer, _tcslen(ms_Viewer))==0 || wcsncmp(Title, ms_ViewerRus, _tcslen(ms_ViewerRus))==0)
				nNewState |= CES_VIEWER;
			else if (isFilePanel(true, true))
				nNewState |= CES_FILEPANEL;
		}

		// Смысл в том, чтобы не сбросить флажок CES_MAYBEPANEL если в панелях открыт диалог
		// Флажок сбрасывается только в редактроре/вьювере
		if ((nNewState & (CES_EDITOR | CES_VIEWER)) != 0)
			nNewState &= ~(CES_MAYBEPANEL|CES_WASPROGRESS|CES_OPER_ERROR); // При переключении в редактор/вьювер - сбросить CES_MAYBEPANEL

		if ((nNewState & CES_FILEPANEL) == CES_FILEPANEL)
		{
			nNewState |= CES_MAYBEPANEL; // Попали в панель - поставим флажок
			nNewState &= ~(CES_WASPROGRESS|CES_OPER_ERROR); // Значит СЕЙЧАС процесс копирования не идет
		}

		if (m_Progress.Progress >= 0 && m_Progress.Progress <= 100)
		{
			if (m_Progress.ConsoleProgress == m_Progress.Progress)
			{
				// При извлечении прогресса из текста консоли - Warning ловить смысла нет
				setPreWarningProgress(-1);
				nNewState &= ~CES_OPER_ERROR;
				nNewState |= CES_WASPROGRESS; // Пометить статус, что прогресс был
			}
			else
			{
				setPreWarningProgress(m_Progress.Progress);

				if ((nNewState & CES_MAYBEPANEL) == CES_MAYBEPANEL)
					nNewState |= CES_WASPROGRESS; // Пометить статус, что прогресс был

				nNewState &= ~CES_OPER_ERROR;
			}
		}
		else if ((nNewState & (CES_WASPROGRESS|CES_MAYBEPANEL)) == (CES_WASPROGRESS|CES_MAYBEPANEL)
		        && (m_Progress.PreWarningProgress != -1))
		{
			if (m_Progress.LastWarnCheckTick == 0)
			{
				m_Progress.LastWarnCheckTick = GetTickCount();
			}
			else if ((mn_FarStatus & CES_OPER_ERROR) == CES_OPER_ERROR)
			{
				// для удобства отладки - флаг ошибки уже установлен
				_ASSERTE((nNewState & CES_OPER_ERROR) == CES_OPER_ERROR);
				nNewState |= CES_OPER_ERROR;
			}
			else
			{
				DWORD nDelta = GetTickCount() - m_Progress.LastWarnCheckTick;

				if (nDelta > CONSOLEPROGRESSWARNTIMEOUT)
				{
					nNewState |= CES_OPER_ERROR;
					//mn_LastWarnCheckTick = 0;
				}
			}
		}
	}

	if (m_Progress.Progress == -1 && m_Progress.PreWarningProgress != -1)
	{
		if ((nNewState & CES_WASPROGRESS) == 0)
		{
			setPreWarningProgress(-1); m_Progress.LastWarnCheckTick = 0;
			gpConEmu->UpdateProgress();
		}
		else if (/*isFilePanel(true)*/ (nNewState & CES_FILEPANEL) == CES_FILEPANEL)
		{
			nNewState &= ~(CES_OPER_ERROR|CES_WASPROGRESS);
			setPreWarningProgress(-1); m_Progress.LastWarnCheckTick = 0;
			gpConEmu->UpdateProgress();
		}
	}

	if (nNewState != nLastState)
	{
		#ifdef _DEBUG
		if ((nNewState & CES_FILEPANEL) == 0)
			nNewState = nNewState;
		#endif

		SetFarStatus(nNewState);
		gpConEmu->UpdateProcessDisplay(FALSE);
	}
}

// m_Progress.Progress не меняет, результат возвращает
short CRealConsole::CheckProgressInTitle()
{
	// Обработка прогресса NeroCMD и пр. консольных программ (если курсор находится в видимой области)
	// выполняется в CheckProgressInConsole (-> mn_ConsoleProgress), вызывается из FindPanels
	short nNewProgress = -1;
	int i = 0;
	wchar_t ch;

	// Wget [41%] http://....
	while((ch = Title[i])!=0 && (ch == L'{' || ch == L'(' || ch == L'['))
		i++;

	//if (Title[0] == L'{' || Title[0] == L'(' || Title[0] == L'[') {
	if (Title[i])
	{
		// Некоторые плагины/программы форматируют проценты по двум пробелам
		if (Title[i] == L' ')
		{
			i++;

			if (Title[i] == L' ')
				i++;
		}

		// Теперь, если цифра - считываем проценты
		if (isDigit(Title[i]))
		{
			if (isDigit(Title[i+1]) && isDigit(Title[i+2])
			        && (Title[i+3] == L'%' || (Title[i+3] == L'.' && isDigit(Title[i+4]) && Title[i+7] == L'%'))
			  )
			{
				// По идее больше 100% быть не должно :)
				nNewProgress = 100*(Title[i] - L'0') + 10*(Title[i+1] - L'0') + (Title[i+2] - L'0');
			}
			else if (isDigit(Title[i+1])
			        && (Title[i+2] == L'%' || (Title[i+2] == L'.' && isDigit(Title[i+3]) && Title[i+4] == L'%'))
			       )
			{
				// 10 .. 99 %
				nNewProgress = 10*(Title[i] - L'0') + (Title[i+1] - L'0');
			}
			else if (Title[i+1] == L'%' || (Title[i+1] == L'.' && isDigit(Title[i+2]) && Title[i+3] == L'%'))
			{
				// 0 .. 9 %
				nNewProgress = (Title[i] - L'0');
			}

			_ASSERTE(nNewProgress<=100);

			if (nNewProgress > 100)
				nNewProgress = 100;
		}
	}

	if (nNewProgress != m_Progress.Progress)
		logProgress(L"RCon::ProgressInTitle: %i", nNewProgress);

	return nNewProgress;
}

void CRealConsole::OnTitleChanged()
{
	if (!this) return;

	#ifdef _DEBUG
	if (mb_DebugLocked)
		return;
	#endif
	
	wcscpy(Title, TitleCmp);
	SetWindowText(mp_VCon->GetView(), TitleCmp);

	// Обработка прогресса операций
	//short nLastProgress = m_Progress.Progress;
	short nNewProgress;
	TitleFull[0] = 0;
	nNewProgress = CheckProgressInTitle();

	if (nNewProgress == -1)
	{
		// mn_ConsoleProgress обновляется в FindPanels, должен быть уже вызван
		// mn_AppProgress обновляется через Esc-коды, GuiMacro или через команду пайпа
		short nConProgr =
			((m_Progress.AppProgressState == 1) || (m_Progress.AppProgressState == 2)) ? m_Progress.AppProgress
			: (m_Progress.AppProgressState == 3) ? 0 // Indeterminate
			: m_Progress.ConsoleProgress;

		if ((nConProgr >= 0) && (nConProgr <= 100))
		{
			// Обработка прогресса NeroCMD и пр. консольных программ
			// Если курсор находится в видимой области
			// Use "m_Progress.ConsoleProgress" because "AppProgress" is not "our" detection
			// thats why we are not storing it in (common) member variable
			nNewProgress = m_Progress.ConsoleProgress;

			// Если в заголовке нет процентов (они есть только в консоли)
			// добавить их в наш заголовок
			wchar_t szPercents[5];
			_ltow(nConProgr, szPercents, 10);
			lstrcatW(szPercents, L"%");

			if (!wcsstr(TitleCmp, szPercents))
			{
				TitleFull[0] = L'{'; TitleFull[1] = 0;
				wcscat_c(TitleFull, szPercents);
				wcscat_c(TitleFull, L"} ");
			}
		}
	}

	wcscat_c(TitleFull, TitleCmp);
	// Обновляем на что нашли
	setProgress(nNewProgress);

	if ((nNewProgress >= 0 && nNewProgress <= 100) && isFar(true))
		setPreWarningProgress(nNewProgress);

	//SetProgress(nNewProgress);

	TitleAdmin[0] = 0;

	CheckFarStates();
	// иначе может среагировать на изменение заголовка ДО того,
	// как мы узнаем, что активировался редактор...
	TODO("Должно срабатывать и при запуске консольной программы!");

	if (Title[0] == L'{' || Title[0] == L'(')
		CheckPanelTitle();

	// заменил на GetProgress, т.к. он еще учитывает mn_PreWarningProgress
	nNewProgress = GetProgress(NULL);

	if (gpConEmu->isActive(mp_VCon) && wcscmp(GetTitle(), gpConEmu->GetLastTitle(false)))
	{
		// Для активной консоли - обновляем заголовок. Прогресс обновится там же
		setLastShownProgress(nNewProgress);
		gpConEmu->UpdateTitle();
	}
	else if (m_Progress.LastShownProgress != nNewProgress)
	{
		// Для НЕ активной консоли - уведомить главное окно, что у нас сменились проценты
		setLastShownProgress(nNewProgress);
		gpConEmu->UpdateProgress();
	}
	
	mp_VCon->OnTitleChanged(); // Обновить таб на таскбаре

	gpConEmu->mp_TabBar->Update(); // сменить заголовок закладки?
}

bool CRealConsole::isFilePanel(bool abPluginAllowed/*=false*/, bool abSkipEditViewCheck /*= false*/)
{
	if (!this) return false;

	if (Title[0] == 0) return false;

	// Функция используется в процессе проверки флагов фара, а там Viewer/Editor уже проверены
	if (!abSkipEditViewCheck)
	{
		if (isEditor() || isViewer())
			return false;
	}

	// Если висят какие-либо диалоги - считаем что это НЕ панель
	DWORD dwFlags = mp_ABuf ? mp_ABuf->GetDetector()->GetFlags() : FR_FREEDLG_MASK;

	if ((dwFlags & FR_FREEDLG_MASK) != 0)
		return false;

	// нужно для DragDrop
	if (_tcsncmp(Title, ms_TempPanel, _tcslen(ms_TempPanel)) == 0 || _tcsncmp(Title, ms_TempPanelRus, _tcslen(ms_TempPanelRus)) == 0)
		return true;

	if ((abPluginAllowed && Title[0]==_T('{')) ||
	        (_tcsncmp(Title, _T("{\\\\"), 3)==0) ||
	        (Title[0] == _T('{') && isDriveLetter(Title[1]) && Title[2] == _T(':') && Title[3] == _T('\\')))
	{
		TCHAR *Br = _tcsrchr(Title, _T('}'));

		if (Br && _tcsstr(Br, _T("} - Far")))
		{
			if (mp_RBuf->isLeftPanel() || mp_RBuf->isRightPanel())
				return true;
		}
	}

	//TCHAR *BrF = _tcschr(Title, '{'), *BrS = _tcschr(Title, '}'), *Slash = _tcschr(Title, '\\');
	//if (BrF && BrS && Slash && BrF == Title && (Slash == Title+1 || Slash == Title+3))
	//    return true;
	return false;
}

bool CRealConsole::isEditor()
{
	if (!this) return false;

	return GetFarStatus() & CES_EDITOR;
}

bool CRealConsole::isEditorModified()
{
	if (!this) return false;

	if (!isEditor()) return false;

	if (mp_tabs && mn_tabsCount)
	{
		for(int j = 0; j < mn_tabsCount; j++)
		{
			if (mp_tabs[j].Current)
			{
				if (mp_tabs[j].Type == /*Editor*/3)
				{
					return (mp_tabs[j].Modified != 0);
				}

				return false;
			}
		}
	}

	return false;
}

bool CRealConsole::isViewer()
{
	if (!this) return false;

	return GetFarStatus() & CES_VIEWER;
}

bool CRealConsole::isNtvdm()
{
	if (!this) return false;

	if (mn_ProgramStatus & CES_NTVDM)
	{
		// Наличие 16bit определяем ТОЛЬКО по WinEvent. Иначе не получится отсечь его завершение,
		// т.к. процесс ntvdm.exe не выгружается, а остается в памяти.
		return true;
		//if (mn_ProgramStatus & CES_FARFLAGS) {
		//  //mn_ActiveStatus &= ~CES_NTVDM;
		//} else if (isFilePanel()) {
		//  //mn_ActiveStatus &= ~CES_NTVDM;
		//} else {
		//  return true;
		//}
	}

	return false;
}

const RConStartArgs& CRealConsole::GetArgs()
{
	return m_Args;
}

void CRealConsole::SetPaletteName(LPCWSTR asPaletteName)
{
	wchar_t* pszOld = m_Args.pszPalette;
	wchar_t* pszNew = NULL;
	if (asPaletteName && *asPaletteName)
		pszNew = lstrdup(asPaletteName);
	m_Args.pszPalette = pszNew;
	SafeFree(pszOld);
}

LPCWSTR CRealConsole::GetCmd(bool bThisOnly /*= false*/)
{
	if (!this) return L"";

	if (m_Args.pszSpecialCmd)
		return m_Args.pszSpecialCmd;
	else if (!bThisOnly)
		return gpSetCls->GetCmd(NULL, true);
	else
		return L"";
}

wchar_t* CRealConsole::CreateCommandLine(bool abForTasks /*= false*/)
{
	if (!this) return NULL;

	wchar_t* pszCmd = m_Args.CreateCommandLine(abForTasks);

	return pszCmd;
}

BOOL CRealConsole::GetUserPwd(const wchar_t** ppszUser, const wchar_t** ppszDomain, BOOL* pbRestricted)
{
	if (m_Args.bRunAsRestricted)
	{
		*pbRestricted = TRUE;
		*ppszUser = /**ppszPwd =*/ NULL;
		return TRUE;
	}

	if (m_Args.pszUserName /*&& m_Args.pszUserPassword*/)
	{
		*ppszUser = m_Args.pszUserName;
		_ASSERTE(ppszDomain!=NULL);
		*ppszDomain = m_Args.pszDomain;
		//*ppszPwd = m_Args.pszUserPassword;
		*pbRestricted = FALSE;
		return TRUE;
	}

	return FALSE;
}

void CRealConsole::logProgress(LPCWSTR asFormat, int V1, int V2)
{
	#ifndef _DEBUG
	if (!mp_Log)
		return;
	#endif

	wchar_t szInfo[100];
	_wsprintf(szInfo, SKIPLEN(countof(szInfo)-1) asFormat, V1, V2);
	if (*szInfo)
	{
		int nLen = lstrlen(szInfo);
		if (szInfo[nLen-1] != L'\n')
		{
			szInfo[nLen++] = L'\n'; szInfo[nLen] = 0;
		}
	}

	#ifdef _DEBUG
	if (!mp_Log)
	{
		DEBUGSTRPROGRESS(szInfo);
		return;
	}
	#endif
	LogString(szInfo, TRUE);
}

void CRealConsole::setProgress(short value)
{
	DEBUGTEST(if (m_Progress.Progress != value))
	{
		logProgress(L"RCon::setProgress(%i)", value);
		m_Progress.Progress = value;
	}
}

void CRealConsole::setLastShownProgress(short value)
{
	DEBUGTEST(if (m_Progress.LastShownProgress != value))
	{
		logProgress(L"RCon::setLastShownProgress(%i)", value);
		m_Progress.LastShownProgress = value;
	}
}

void CRealConsole::setPreWarningProgress(short value)
{
	DEBUGTEST(if (m_Progress.PreWarningProgress != value))
	{
		logProgress(L"RCon::setPreWarningProgress(%i)", value);
		m_Progress.PreWarningProgress = value;
	}
}

void CRealConsole::setConsoleProgress(short value)
{
	DEBUGTEST(if (m_Progress.ConsoleProgress != value))
	{
		logProgress(L"RCon::setConsoleProgress(%i)", value);
		m_Progress.ConsoleProgress = value;
	}
}

void CRealConsole::setLastConsoleProgress(short value, bool UpdateTick)
{
	DEBUGTEST(if (m_Progress.LastConsoleProgress != value))
	{
		logProgress(L"RCon::setLastConsoleProgress(%i,%u)", value, UpdateTick);
		m_Progress.LastConsoleProgress = value;
	}

	if (UpdateTick)
		m_Progress.LastConProgrTick = (value >= 0) ? GetTickCount() : 0;
}

void CRealConsole::setAppProgress(short AppProgressState, short AppProgress)
{
	logProgress(L"RCon::setAppProgress(%i,%i)", AppProgressState, AppProgress);

	DEBUGTEST(if (m_Progress.AppProgressState != AppProgressState))
		m_Progress.AppProgressState = AppProgressState;
	DEBUGTEST(if (m_Progress.AppProgress != AppProgress))
		m_Progress.AppProgress = AppProgress;
}

short CRealConsole::GetProgress(int* rpnState/*1-error,2-ind*/, BOOL* rpbNotFromTitle)
{
	if (!this)
		return -1;

	if (m_Progress.AppProgressState > 0)
	{
		if (rpnState)
		{
			*rpnState = (m_Progress.AppProgressState == 2) ? 1 : (m_Progress.AppProgressState == 3) ? 2 : 0;
		}
		if (rpbNotFromTitle)
		{
			//-- Если проценты пришли НЕ из заголовка, а из текста
			//-- консоли - то они "дописываются" в текущий заголовок, для таба
			////// Если пришло от установки через ESC-коды или GuiMacro
			//*rpbNotFromTitle = TRUE;
			*rpbNotFromTitle = FALSE;
		}
		return m_Progress.AppProgress;
	}

	if (rpbNotFromTitle)
	{
		//-- Если проценты пришли НЕ из заголовка, а из текста
		//-- консоли - то они "дописываются" в текущий заголовок, для таба
		//*rpbNotFromTitle = (mn_ConsoleProgress != -1);
		*rpbNotFromTitle = FALSE;
	}

	if (m_Progress.Progress >= 0)
		return m_Progress.Progress;

	if (m_Progress.PreWarningProgress >= 0)
	{
		// mn_PreWarningProgress - это последнее значение прогресса (0..100)
		// по после завершения процесса - он может еще быть не сброшен
		if (rpnState)
		{
			*rpnState = ((mn_FarStatus & CES_OPER_ERROR) == CES_OPER_ERROR) ? 1 : 0;
		}

		//if (mn_LastProgressTick != 0 && rpbError) {
		//	DWORD nDelta = GetTickCount() - mn_LastProgressTick;
		//	if (nDelta >= 1000) {
		//		if (rpbError) *rpbError = TRUE;
		//	}
		//}
		return m_Progress.PreWarningProgress;
	}

	return -1;
}

bool CRealConsole::SetProgress(short nState, short nValue, LPCWSTR pszName /*= NULL*/)
{
	if (!this)
		return false;

	bool lbOk = false;

	//SetProgress 3
	//  -- Set progress indeterminate state
	//SetProgress 4 <Name>
	//  -- Start progress for some long process
	//SetProgress 5 <Name>
	//  -- Stop progress started with "3"

	switch (nState)
	{
	case 0:
		setAppProgress(0, 0);
		lbOk = true;
		break;
	case 1:
		setAppProgress(1, min(max(nValue,0),100));
        lbOk = true;
        break;
    case 2:
		setAppProgress(2, (nValue > 0) ? min(max(nValue,0),100) : m_Progress.AppProgress);
    	lbOk = true;
    	break;
    case 3:
		setAppProgress(3, m_Progress.AppProgress);
    	lbOk = true;
    	break;
	case 4:
	case 5:
		_ASSERTE(FALSE && "TODO: Estimation of process duration");
		break;
	}

	if (lbOk)
	{
		// Показать прогресс в заголовке
		mb_ForceTitleChanged = TRUE;

		if (gpConEmu->isActive(mp_VCon))
			// Для активной консоли - обновляем заголовок. Прогресс обновится там же
			gpConEmu->UpdateTitle();
		else
			// Для НЕ активной консоли - уведомить главное окно, что у нас сменились проценты
			gpConEmu->UpdateProgress();
	}

	return lbOk;
}

void CRealConsole::UpdateGuiInfoMapping(const ConEmuGuiMapping* apGuiInfo)
{
	DWORD dwServerPID = GetServerPID(true);
	if (dwServerPID)
	{
		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_GUICHANGED, sizeof(CESERVER_REQ_HDR)+apGuiInfo->cbSize);
		if (pIn)
		{
			memmove(&(pIn->GuiInfo), apGuiInfo, apGuiInfo->cbSize);
			DWORD dwTickStart = timeGetTime();
			
			CESERVER_REQ *pOut = ExecuteSrvCmd(dwServerPID, pIn, ghWnd);
			
			gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);
			
			if (pOut)
				ExecuteFreeResult(pOut);
			ExecuteFreeResult(pIn);
		}
	}
}

// Полать в активный фар CMD_FARSETCHANGED
// Обновляются настройки: gpSet->isFARuseASCIIsort, gpSet->isShellNoZoneCheck;
void CRealConsole::UpdateFarSettings(DWORD anFarPID/*=0*/)
{
	if (!this) return;

	// В консоли может быть активна и другая программа
	DWORD dwFarPID = (mn_FarPID_PluginDetected == mn_FarPID) ? mn_FarPID : 0; // anFarPID ? anFarPID : GetFarPID();

	if (!dwFarPID) return;

	////int nLen = /*ComSpec\0*/ 8 + /*ComSpecC\0*/ 9 + 20 +  2*_tcslen(gpConEmu->ms_ConEmuExe);
	//wchar_t szCMD[MAX_PATH+1];
	////wchar_t szData[MAX_PATH*4+64];
	//
	//// Если определена ComSpecC - значит переопределен стандартный ComSpec
	//if (!GetEnvironmentVariable(L"ComSpecC", szCMD, MAX_PATH) || szCMD[0] == 0)
	//{
	//	if (!GetEnvironmentVariable(L"ComSpec", szCMD, MAX_PATH) || szCMD[0] == 0)
	//		szCMD[0] = 0;
	//}
	//
	//if (szCMD[0] != 0)
	//{
	//	// Только если это (случайно) не conemuc.exe
	//
	//	wchar_t* pwszCopy = wcsrchr(szCMD, L'\\'); if (!pwszCopy) pwszCopy = szCMD;
	//
	//#if !defined(__GNUC__)
	//#pragma warning( push )
	//#pragma warning(disable : 6400)
	//#endif
	//
	//	if (lstrcmpiW(pwszCopy, L"ConEmuC")==0 || lstrcmpiW(pwszCopy, L"ConEmuC.exe")==0
	//	        || lstrcmpiW(pwszCopy, L"ConEmuC64")==0 || lstrcmpiW(pwszCopy, L"ConEmuC64.exe")==0)
	//		szCMD[0] = 0;
	//
	//#if !defined(__GNUC__)
	//#pragma warning( pop )
	//#endif
	//}
	//
	//// ComSpec/ComSpecC не определен, используем cmd.exe
	//if (szCMD[0] == 0)
	//{
	//	wchar_t* psFilePart = NULL;
	//
	//	if (!SearchPathW(NULL, L"cmd.exe", NULL, MAX_PATH, szCMD, &psFilePart))
	//	{
	//		DisplayLastError(L"Can't find cmd.exe!\n", 0);
	//		return;
	//	}
	//}

	// [MAX_PATH*4+64]
	FAR_REQ_FARSETCHANGED *pSetEnvVar = (FAR_REQ_FARSETCHANGED*)calloc(sizeof(FAR_REQ_FARSETCHANGED),1); //+2*(MAX_PATH*4+64),1);
	//wchar_t *szData = pSetEnvVar->szEnv;
	pSetEnvVar->bFARuseASCIIsort = gpSet->isFARuseASCIIsort;
	pSetEnvVar->bShellNoZoneCheck = gpSet->isShellNoZoneCheck;
	pSetEnvVar->bMonitorConsoleInput = (gpSetCls->m_ActivityLoggingType == glt_Input);
	pSetEnvVar->bLongConsoleOutput = gpSet->AutoBufferHeight;

	gpConEmu->GetComSpecCopy(pSetEnvVar->ComSpec);

	//BOOL lbNeedQuot = (wcschr(gpConEmu->ms_ConEmuCExeFull, L' ') != NULL);
	//wchar_t* pszName = szData;
	//lstrcpy(pszName, L"ComSpec");
	//wchar_t* pszValue = pszName + _tcslen(pszName) + 1;

	//if (gpSet->AutoBufferHeight)
	//{
	//	if (lbNeedQuot) *(pszValue++) = L'"';

	//	lstrcpy(pszValue, gpConEmu->ms_ConEmuCExeFull);

	//	if (lbNeedQuot) lstrcat(pszValue, L"\"");

	//	lbNeedQuot = (szCMD[0] != L'"') && (wcschr(szCMD, L' ') != NULL);
	//	pszName = pszValue + _tcslen(pszValue) + 1;
	//	lstrcpy(pszName, L"ComSpecC");
	//	pszValue = pszName + _tcslen(pszName) + 1;
	//}

	//if (lbNeedQuot) *(pszValue++) = L'"';

	//lstrcpy(pszValue, szCMD);

	//if (lbNeedQuot) lstrcat(pszValue, L"\"");

	//pszName = pszValue + _tcslen(pszValue) + 1;
	//lstrcpy(pszName, L"ConEmuOutput");
	//pszName = pszName + _tcslen(pszName) + 1;
	//lstrcpy(pszName, !gpSet->nCmdOutputCP ? L"" : ((gpSet->nCmdOutputCP == 1) ? L"AUTO"
	//        : (((gpSet->nCmdOutputCP == 2) ? L"UNICODE" : L"ANSI"))));
	//pszName = pszName + _tcslen(pszName) + 1;
	//*(pszName++) = 0;
	//*(pszName++) = 0;

	// Выполнить в плагине
	CConEmuPipe pipe(dwFarPID, 300);
	int nSize = sizeof(FAR_REQ_FARSETCHANGED); //+2*(pszName - szData);

	if (pipe.Init(L"FarSetChange", TRUE))
		pipe.Execute(CMD_FARSETCHANGED, pSetEnvVar, nSize);

	//pipe.Execute(CMD_SETENVVAR, szData, 2*(pszName - szData));
	free(pSetEnvVar); pSetEnvVar = NULL;
}

void CRealConsole::UpdateTextColorSettings(BOOL ChangeTextAttr /*= TRUE*/, BOOL ChangePopupAttr /*= TRUE*/)
{
	if (!this) return;

	BYTE nTextColorIdx /*= 7*/, nBackColorIdx /*= 0*/, nPopTextColorIdx /*= 5*/, nPopBackColorIdx /*= 15*/;
	PrepareDefaultColors(nTextColorIdx, nBackColorIdx, nPopTextColorIdx, nPopBackColorIdx);

	CESERVER_REQ *pIn = ExecuteNewCmd(CECMD_SETCONCOLORS, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SETCONSOLORS));
    if (!pIn)
    	return;

	pIn->SetConColor.ChangeTextAttr = ChangeTextAttr;
	pIn->SetConColor.NewTextAttributes = (GetDefaultBackColorIdx() << 4) | GetDefaultTextColorIdx();
	pIn->SetConColor.ChangePopupAttr = ChangePopupAttr;
	pIn->SetConColor.NewPopupAttributes = ((mn_PopBackColorIdx & 0xF) << 4) | (mn_PopTextColorIdx & 0xF);
	pIn->SetConColor.ReFillConsole = !isFar();

	CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), pIn, ghWnd);

	ExecuteFreeResult(pIn);
	ExecuteFreeResult(pOut);
}

HWND CRealConsole::FindPicViewFrom(HWND hFrom)
{
	// !!! PicView может быть несколько, по каждому на открытый ФАР
	HWND hPicView = NULL;

	//hPictureView = FindWindowEx(ghWnd, NULL, L"FarPictureViewControlClass", NULL);
	// Класс может быть как "FarPictureViewControlClass", так и "FarMultiViewControlClass"
	// А вот заголовок у них пока один
	while ((hPicView = FindWindowEx(hFrom, hPicView, NULL, L"PictureView")) != NULL)
	{
		// Проверить на принадлежность фару
		DWORD dwPID, dwTID;
		dwTID = GetWindowThreadProcessId(hPicView, &dwPID);

		if (dwPID == mn_FarPID || dwPID == GetActivePID())
			break;

		UNREFERENCED_PARAMETER(dwTID);
	}

	return hPicView;
}

struct FindChildGuiWindowArg
{
	HWND  hDlg;
	DWORD nPID;
};

BOOL CRealConsole::FindChildGuiWindowProc(HWND hwnd, LPARAM lParam)
{
	struct FindChildGuiWindowArg *p = (struct FindChildGuiWindowArg*)lParam;
	DWORD nPID;
	if (IsWindowVisible(hwnd) && GetWindowThreadProcessId(hwnd, &nPID) && (nPID == p->nPID))
	{
		p->hDlg = hwnd;
		return FALSE; // fin
	}
	return TRUE;
}

// Заголовок окна для PictureView вообще может пользователем настраиваться, так что
// рассчитывать на него при определения "Просмотра" - нельзя
HWND CRealConsole::isPictureView(BOOL abIgnoreNonModal/*=FALSE*/)
{
	if (!this) return NULL;

	if (mn_GuiWndPID && !hGuiWnd)
	{
		// Если GUI приложение запущено во вкладке, и аттач выполняется НЕ из ConEmuHk.dll
		HWND hBack = mp_VCon->GetBack();
		HWND hChild = NULL;
		DEBUGTEST(DWORD nSelf = GetCurrentProcessId());
		_ASSERTE(nSelf != mn_GuiWndPID);

		while ((hChild = FindWindowEx(hBack, hChild, NULL, NULL)) != NULL)
		{
			DWORD nChild, nStyle;

			nStyle = ::GetWindowLong(hChild, GWL_STYLE);
			if (!(nStyle & WS_VISIBLE))
				continue;

			if (GetWindowThreadProcessId(hChild, &nChild))
			{
				if (nChild == mn_GuiWndPID)
				{
					break;
				}
			}
		}

		if ((hChild == NULL) && gpSet->isAlwaysOnTop)
		{
			// Если создается диалог (подключение PuTTY например)
			// то он тоже должен быть OnTop, иначе вообще виден не будет
			struct FindChildGuiWindowArg arg = {};
			arg.nPID = mn_GuiWndPID;
			EnumWindows(FindChildGuiWindowProc, (LPARAM)&arg);
			if (arg.hDlg)
			{
				DWORD nExStyle = GetWindowLong(arg.hDlg, GWL_EXSTYLE);
				if (!(nExStyle & WS_EX_TOPMOST))
				{
					//TODO: Через Сервер! А то прав может не хватить
					SetWindowPos(arg.hDlg, HWND_TOPMOST, 0,0,0,0, SWP_NOSIZE|SWP_NOMOVE);
				}
			}
		}

		if (hChild != NULL)
		{
			DWORD nStyle = ::GetWindowLong(hChild, GWL_STYLE), nStyleEx = ::GetWindowLong(hChild, GWL_EXSTYLE);
			RECT rcChild; GetWindowRect(hChild, &rcChild);
			SetGuiMode(mn_GuiAttachFlags|agaf_Self, hChild, nStyle, nStyleEx, ms_GuiWndProcess, mn_GuiWndPID, rcChild);
		}
	}

	if (hPictureView && (!IsWindow(hPictureView) || !isFar()))
	{
		hPictureView = NULL; mb_PicViewWasHidden = FALSE;
		gpConEmu->InvalidateAll();
	}

	// !!! PicView может быть несколько, по каждому на открытый ФАР
	if (!hPictureView)
	{
		// !! Дочерние окна должны быть только в окнах отрисовки. В главном - быть ничего не должно !!
		//hPictureView = FindWindowEx(ghWnd, NULL, L"FarPictureViewControlClass", NULL);
		//hPictureView = FindPicViewFrom(ghWnd);
		//if (!hPictureView)
		//hPictureView = FindWindowEx('ghWnd DC', NULL, L"FarPictureViewControlClass", NULL);
		hPictureView = FindPicViewFrom(mp_VCon->GetView());

		if (!hPictureView)    // FullScreen?
		{
			//hPictureView = FindWindowEx(NULL, NULL, L"FarPictureViewControlClass", NULL);
			hPictureView = FindPicViewFrom(NULL);
		}

		// Принадлежность процессу фара уже проверила сама FindPicViewFrom
		//if (hPictureView) {
		//    // Проверить на принадлежность фару
		//    DWORD dwPID, dwTID;
		//    dwTID = GetWindowThreadProcessId ( hPictureView, &dwPID );
		//    if (dwPID != mn_FarPID) {
		//        hPictureView = NULL; mb_PicViewWasHidden = FALSE;
		//    }
		//}
	}

	if (hPictureView)
	{
		WARNING("PicView теперь дочернее в DC, но может быть FullScreen?")
		if (mb_PicViewWasHidden)
		{
			// Окошко было скрыто при переключении на другую консоль, но картинка еще активна
		}
		else
		if (!IsWindowVisible(hPictureView))
		{
			// Если вызывали Help (F1) - окошко PictureView прячется (самим плагином), считаем что картинки нет
			hPictureView = NULL; mb_PicViewWasHidden = FALSE;
		}
	}

	if (mb_PicViewWasHidden && !hPictureView) mb_PicViewWasHidden = FALSE;

	if (hPictureView && abIgnoreNonModal)
	{
		wchar_t szClassName[128];

		if (GetClassName(hPictureView, szClassName, countof(szClassName)))
		{
			if (wcscmp(szClassName, L"FarMultiViewControlClass") == 0)
			{
				// Пока оно строго немодальное, но потом может быть по другому
				DWORD_PTR dwValue = GetWindowLongPtr(hPictureView, 0);

				if (dwValue != 0x200)
					return NULL;
			}
		}
	}

	return hPictureView;
}

HWND CRealConsole::ConWnd()
{
	if (!this) return NULL;

	return hConWnd;
}

HWND CRealConsole::GetView()
{
	if (!this) return NULL;

	return mp_VCon->GetView();
}

// Если работаем в Gui-режиме (Notepad, Putty, ...)
HWND CRealConsole::GuiWnd()
{
	if (!this)
		return NULL;
	return hGuiWnd;
}
DWORD CRealConsole::GuiWndPID()
{
	if (!this || !hGuiWnd)
		return 0;
	return mn_GuiWndPID;
}
bool CRealConsole::isGuiForceConView()
{
	return mb_GuiForceConView;
}

// При движении окна ConEmu - нужно подергать дочерние окошки
// Иначе PuTTY глючит с обработкой мышки
void CRealConsole::GuiNotifyChildWindow()
{
	if (!this || !hGuiWnd || mb_GuiExternMode)
		return;

	DEBUGTEST(BOOL bValid1 = IsWindow(hGuiWnd));

	if (!IsWindowVisible(hGuiWnd))
		return;

	RECT rcCur = {};
	if (!GetWindowRect(hGuiWnd, &rcCur))
		return;

	if (memcmp(&mrc_LastGuiWnd, &rcCur, sizeof(rcCur)) == 0)
		return; // ConEmu не двигалось

	StoreGuiChildRect(&rcCur); // Сразу запомнить

	HWND hBack = mp_VCon->GetBack();
	MapWindowPoints(NULL, hBack, (LPPOINT)&rcCur, 2);

	DEBUGTEST(BOOL bValid2 = IsWindow(hGuiWnd));

	SetOtherWindowPos(hGuiWnd, NULL, rcCur.left, rcCur.top, rcCur.right-rcCur.left+1, rcCur.bottom-rcCur.top+1, SWP_NOZORDER);
	SetOtherWindowPos(hGuiWnd, NULL, rcCur.left, rcCur.top, rcCur.right-rcCur.left, rcCur.bottom-rcCur.top, SWP_NOZORDER);

	DEBUGTEST(BOOL bValid3 = IsWindow(hGuiWnd));

	//RECT rcChild = {};
	//GetWindowRect(hGuiWnd, &rcChild);
	////MapWindowPoints(NULL, VCon->GetBack(), (LPPOINT)&rcChild, 2);

	//WPARAM wParam = 0;
	//LPARAM lParam = MAKELPARAM(rcChild.left, rcChild.top);
	////pRCon->PostConsoleMessage(hGuiWnd, WM_MOVE, wParam, lParam);

	//wParam = ::IsZoomed(hGuiWnd) ? SIZE_MAXIMIZED : SIZE_RESTORED;
	//lParam = MAKELPARAM(rcChild.right-rcChild.left, rcChild.bottom-rcChild.top);
	////pRCon->PostConsoleMessage(hGuiWnd, WM_SIZE, wParam, lParam);
}

void CRealConsole::GuiWndFocusStore()
{
	if (!this || !hGuiWnd)
		return;

	GUITHREADINFO gti = {sizeof(gti)};
	
	DWORD nPID = 0, nGetPID = 0, nErr = 0;
	BOOL bAttached = FALSE, bAttachCalled = FALSE;

	DWORD nTID = GetWindowThreadProcessId(hGuiWnd, &nPID);

	DEBUGTEST(BOOL bGuiInfo = )
	GetGUIThreadInfo(nTID, &gti);

	if (gti.hwndFocus)
	{
		GetWindowThreadProcessId(gti.hwndFocus, &nGetPID);
		if (nGetPID != GetCurrentProcessId())
		{
			mh_GuiWndFocusStore = gti.hwndFocus;

			GuiWndFocusThread(gti.hwndFocus, bAttached, bAttachCalled, nErr);

			_ASSERTE(bAttached);
		}
	}

	if (gpSetCls->isAdvLogging)
	{
		wchar_t szInfo[100];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"GuiWndFocusStore for PID=%u, hWnd=x%08X", nPID, (DWORD)mh_GuiWndFocusStore);
		gpConEmu->LogString(szInfo);
	}
}

// Если (bForce == false) - то по "настройкам", юзер мог просить переключиться в ConEmu
void CRealConsole::GuiWndFocusRestore(bool bForce /*= false*/)
{
	if (!this || !hGuiWnd)
		return;

	// Temp workaround for Issue 876: Ctrl+N and Win-Alt-Delete hotkey randomly break
	if (!gpSet->isFocusInChildWindows)
	{
		gpConEmu->setFocus();
		return;
	}

	BOOL bAttached = FALSE;
	DWORD nErr = 0;

	HWND hSetFocus = mh_GuiWndFocusStore ? mh_GuiWndFocusStore : hGuiWnd;

	if (hSetFocus && IsWindow(hSetFocus))
	{
		_ASSERTE(gpConEmu->isMainThread());

		BOOL bAttachCalled = FALSE;
		GuiWndFocusThread(hSetFocus, bAttached, bAttachCalled, nErr);
		//DWORD nTID = GetWindowThreadProcessId(hSetFocus, NULL);
		//if (nTID != mn_GuiAttachInputTID)
		//{
		//	bAttached = AttachThreadInput(nTID, GetCurrentThreadId(), TRUE);
		//	if (!bAttached)
		//		nErr = GetLastError();
		//	else
		//		mn_GuiAttachInputTID = nTID;
		//	bAttachCalled = TRUE;
		//}

		//SetForegroundWindow(hGuiWnd);
		SetFocus(hSetFocus);

		
		wchar_t sInfo[200];
		_wsprintf(sInfo, SKIPLEN(countof(sInfo)) L"GuiWndFocusRestore to x%08X, hGuiWnd=x%08X, Attach=%s, Err=%u",
			(DWORD)hSetFocus, (DWORD)hGuiWnd,
			bAttachCalled ? (bAttached ? L"Called" : L"Failed") : L"Skipped", nErr);
		DEBUGSTRFOCUS(sInfo);
        gpConEmu->LogString(sInfo);
	}
	else
	{
		DEBUGSTRFOCUS(L"GuiWndFocusRestore skipped");
	}
}

void CRealConsole::GuiWndFocusThread(HWND hSetFocus, BOOL& bAttached, BOOL& bAttachCalled, DWORD& nErr)
{
	if (!this || !hGuiWnd)
		return;

	bAttached = FALSE;
	nErr = 0;

	DWORD nMainTID = GetWindowThreadProcessId(ghWnd, NULL);

	bAttachCalled = FALSE;
	DWORD nTID = GetWindowThreadProcessId(hSetFocus, NULL);
	if (nTID != mn_GuiAttachInputTID)
	{
		// Надо, иначе не получится "передать" фокус в дочернее окно GUI приложения
		bAttached = AttachThreadInput(nTID, nMainTID, TRUE);
		if (!bAttached)
			nErr = GetLastError();
		else
			mn_GuiAttachInputTID = nTID;
		bAttachCalled = TRUE;
	}
	else
	{
		// Уже
		bAttached = TRUE;
	}
}

// Вызывается после завершения вставки дочернего GUI-окна в ConEmu
void CRealConsole::StoreGuiChildRect(LPRECT prcNewPos)
{
	if (!this || !hGuiWnd)
	{
		_ASSERTE(this && hGuiWnd);
		return;
	}

	RECT rcChild = {};

	if (prcNewPos)
	{
		rcChild = *prcNewPos;
	}
	else
	{
		GetWindowRect(hGuiWnd, &rcChild);
	}

	#ifdef _DEBUG
	if (memcmp(&mrc_LastGuiWnd, &rcChild, sizeof(rcChild)) != 0)
	{
		DEBUGSTRGUICHILDPOS(L"CRealConsole::StoreGuiChildRect");
	}
	#endif

	mrc_LastGuiWnd = rcChild;
}

void CRealConsole::SetGuiMode(DWORD anFlags, HWND ahGuiWnd, DWORD anStyle, DWORD anStyleEx, LPCWSTR asAppFileName, DWORD anAppPID, RECT arcPrev)
{
	if (!this)
	{
		_ASSERTE(this!=NULL);
		return;
	}

	if ((hGuiWnd != NULL) && !IsWindow(hGuiWnd))
	{
		hGuiWnd = mh_GuiWndFocusStore = NULL; // окно закрылось, открылось другое
	}

	if (hGuiWnd != NULL && hGuiWnd != ahGuiWnd)
	{
		Assert(hGuiWnd==NULL);
		return;
	}

	CVConGuard guard(mp_VCon);


	if (gpSetCls->isAdvLogging)
	{
		wchar_t szInfo[MAX_PATH*4];
		HWND hBack = guard->GetBack();
        _wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"SetGuiMode: BackWnd=x%08X, Flags=x%04X, PID=%u",
        	(DWORD)(DWORD_PTR)hBack, anFlags, anAppPID);

        for (int s = 0; s <= 3; s++)
        {
        	LPCWSTR pszLabel = NULL;
        	wchar_t szTemp[MAX_PATH-1] = {0};
        	RECT rc;

        	switch (s)
        	{
        	case 0:
        		if (!ahGuiWnd) continue;
        		GetWindowRect(hBack, &rc);
        		_wsprintf(szTemp, SKIPLEN(countof(szTemp)) L"hGuiWnd=x%08X, Style=x%08X, StyleEx=x%08X, Prev={%i,%i}-{%i,%i}, Back={%i,%i}-{%i,%i}",
        			(DWORD)(DWORD_PTR)ahGuiWnd, (DWORD)(DWORD_PTR)hBack, anStyle, anStyleEx,
        			arcPrev.left, arcPrev.top, arcPrev.right, arcPrev.bottom,
        			rc.left, rc.top, rc.right, rc.bottom);
        		break;
    		case 1:
    			pszLabel = L"File";
    			lstrcpyn(szTemp, asAppFileName, countof(szTemp));
    			break;
    		case 2:
    			if (!ahGuiWnd) continue;
    			pszLabel = L"Class";
    			GetClassName(ahGuiWnd, szTemp, countof(szTemp)-1);
    			break;
    		case 3:
    			if (!ahGuiWnd) continue;
    			pszLabel = L"Title";
    			GetWindowText(ahGuiWnd, szTemp, countof(szTemp)-1);
    			break;
        	}

        	wcscat_c(szInfo, L", ");
        	if (pszLabel)
        	{
	        	wcscat_c(szInfo, pszLabel);
	        	wcscat_c(szInfo, L"=");
        	}
        	wcscat_c(szInfo, szTemp);
        }

        gpConEmu->LogString(szInfo);
	}


	AllowSetForegroundWindow(anAppPID);

	// Вызывается два раза. Первый (при запуске exe) ahGuiWnd==NULL, второй - после фактического создания окна
	// А может и три, если при вызове ShowWindow оказалось что SetParent пролетел (XmlNotepad)
	if (hGuiWnd == NULL)
	{
		rcPreGuiWndRect = arcPrev;
	}
	// ahGuiWnd может быть на первом этапе, когда ConEmuHk уведомляет - запустился GUI процесс
	_ASSERTE((hGuiWnd==NULL && ahGuiWnd==NULL) || (ahGuiWnd && IsWindow(ahGuiWnd))); // Проверить, чтобы мусор не пришел...
	hGuiWnd = ahGuiWnd;
	GuiWndFocusStore();
	mn_GuiAttachFlags = anFlags;
	//mn_GuiApplicationPID = anAppPID;
	//mn_GuiWndPID = anAppPID;
	setGuiWndPID(anAppPID, PointToName(asAppFileName)); // устанавливает mn_GuiWndPID
	mn_GuiWndStyle = anStyle; mn_GuiWndStylEx = anStyleEx;
	mb_GuiExternMode = FALSE;
	ShowWindow(GetView(), SW_HIDE); // Остается видим только Back, в нем создается GuiClient

#ifdef _DEBUG
	mp_VCon->CreateDbgDlg();
#endif

	// Ставим после "hGuiWnd = ahGuiWnd", т.к. для гуй-приложений логика кнопки другая.
	if (isActive())
	{
		// Обновить на тулбаре статусы Scrolling(BufferHeight) & Alternative
		OnBufferHeight();
	}
	
	// Уведомить сервер (ConEmuC), что создано GUI подключение
	DWORD nSize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_ATTACHGUIAPP);
	CESERVER_REQ In;
	ExecutePrepareCmd(&In, CECMD_ATTACHGUIAPP, nSize);

	In.AttachGuiApp.nFlags = anFlags;
	In.AttachGuiApp.hConEmuWnd = ghWnd;
	In.AttachGuiApp.hConEmuDc = GetView();
	In.AttachGuiApp.hConEmuBack = mp_VCon->GetBack();
	In.AttachGuiApp.hAppWindow = ahGuiWnd;
	In.AttachGuiApp.Styles.nStyle = anStyle;
	In.AttachGuiApp.Styles.nStyleEx = anStyleEx;
	ZeroStruct(In.AttachGuiApp.Styles.Shifts);
	CorrectGuiChildRect(In.AttachGuiApp.Styles.nStyle, In.AttachGuiApp.Styles.nStyleEx, In.AttachGuiApp.Styles.Shifts);
	In.AttachGuiApp.nPID = anAppPID;
	if (asAppFileName)
		wcscpy_c(In.AttachGuiApp.sAppFileName, asAppFileName);
	
	DWORD dwTickStart = timeGetTime();
	
	CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(), &In, ghWnd);
	
	gpSetCls->debugLogCommand(&In, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);

	if (pOut) ExecuteFreeResult(pOut);
	
	if (hGuiWnd)
	{
		mb_InGuiAttaching = TRUE;
		HWND hDcWnd = mp_VCon->GetView();
		RECT rcDC; ::GetWindowRect(hDcWnd, &rcDC);
		MapWindowPoints(NULL, hDcWnd, (LPPOINT)&rcDC, 2);
		// Чтобы артефакты не появлялись
		ValidateRect(hDcWnd, &rcDC);
		
		DWORD nPID = 0;
		DWORD nTID = GetWindowThreadProcessId(hGuiWnd, &nPID);
		_ASSERTE(nPID == anAppPID);
		AllowSetForegroundWindow(nPID);
		UNREFERENCED_PARAMETER(nTID);
		
		/*
		BOOL lbThreadAttach = AttachThreadInput(nTID, GetCurrentThreadId(), TRUE);
		DWORD nMainTID = GetWindowThreadProcessId(ghWnd, NULL);
		BOOL lbThreadAttach2 = AttachThreadInput(nTID, nMainTID, TRUE);
		DWORD nErr = GetLastError();
		*/

		// Хм. Окошко еще не внедрено в ConEmu, смысл отдавать фокус в другое приложение?
		// SetFocus не сработает - другой процесс
		//SetOtherWindowFocus(hGuiWnd, TRUE/*use apiSetForegroundWindow*/);
		apiSetForegroundWindow(hGuiWnd);


		// Запомнить будущие координаты
		HWND hBack = mp_VCon->GetBack();
		RECT rcChild = {}; GetWindowRect(hBack, &rcChild);
		// AddMargin - не то
		rcChild.left += In.AttachGuiApp.Styles.Shifts.left;
		rcChild.top += In.AttachGuiApp.Styles.Shifts.top;
		rcChild.right += In.AttachGuiApp.Styles.Shifts.right;
		rcChild.bottom += In.AttachGuiApp.Styles.Shifts.bottom;
		StoreGuiChildRect(&rcChild);


		GetWindowText(hGuiWnd, Title, countof(Title)-2);
		wcscpy_c(TitleFull, Title);
		TitleAdmin[0] = 0;
		mb_ForceTitleChanged = FALSE;
		OnTitleChanged();

		mp_VCon->UpdateThumbnail(TRUE);
	}
}

void CRealConsole::CorrectGuiChildRect(DWORD anStyle, DWORD anStyleEx, RECT& rcGui)
{
	//WARNING!! Same as "GuiAttach.cpp: CorrectGuiChildRect"
	int nX = 0, nY = 0, nY0 = 0;
	if (anStyle & WS_THICKFRAME)
	{
		nX = GetSystemMetrics(SM_CXSIZEFRAME);
		nY = GetSystemMetrics(SM_CXSIZEFRAME);
	}
	else if (anStyleEx & WS_EX_WINDOWEDGE)
	{
		nX = GetSystemMetrics(SM_CXFIXEDFRAME);
		nY = GetSystemMetrics(SM_CXFIXEDFRAME);
	}
	else if (anStyle & WS_DLGFRAME)
	{
		nX = GetSystemMetrics(SM_CXEDGE);
		nY = GetSystemMetrics(SM_CYEDGE);
	}
	else if (anStyle & WS_BORDER)
	{
		nX = GetSystemMetrics(SM_CXBORDER);
		nY = GetSystemMetrics(SM_CYBORDER);
	}
	else
	{
		nX = GetSystemMetrics(SM_CXFIXEDFRAME);
		nY = GetSystemMetrics(SM_CXFIXEDFRAME);
	}
	if ((anStyle & WS_CAPTION) && gpSet->isHideChildCaption)
	{
		if (anStyleEx & WS_EX_TOOLWINDOW)
			nY0 += GetSystemMetrics(SM_CYSMCAPTION);
		else
			nY0 += GetSystemMetrics(SM_CYCAPTION);
	}
	rcGui.left -= nX; rcGui.right += nX; rcGui.top -= nY+nY0; rcGui.bottom += nY;
}

int CRealConsole::GetStatusLineCount(int nLeftPanelEdge)
{
	if (!this || !isFar())
		return 0;
	
	// Должен вызываться только при активном реальном буфере
	_ASSERTE(mp_RBuf==mp_ABuf);
	return mp_RBuf->GetStatusLineCount(nLeftPanelEdge);
}

// abIncludeEdges - включать 
int CRealConsole::CoordInPanel(COORD cr, BOOL abIncludeEdges /*= FALSE*/)
{
	if (!this || !mp_ABuf || (mp_ABuf != mp_RBuf))
		return 0;

#ifdef _DEBUG
	// Для отлова некорретных координат для "far /w"
	int nVisibleHeight = mp_ABuf->GetTextHeight();
	if (cr.Y > (nVisibleHeight+16))
	{
		_ASSERTE(cr.Y <= nVisibleHeight);
	}
#endif

	RECT rcPanel;

	if (GetPanelRect(FALSE, &rcPanel, FALSE, abIncludeEdges) && CoordInRect(cr, rcPanel))
		return 1;

	if (mp_RBuf->GetPanelRect(TRUE, &rcPanel, FALSE, abIncludeEdges) && CoordInRect(cr, rcPanel))
		return 2;

	return 0;
}

BOOL CRealConsole::GetPanelRect(BOOL abRight, RECT* prc, BOOL abFull /*= FALSE*/, BOOL abIncludeEdges /*= FALSE*/)
{
	if (!this || mp_ABuf != mp_RBuf)
	{
		if (prc)
			*prc = MakeRect(-1,-1);

		return FALSE;
	}

	return mp_RBuf->GetPanelRect(abRight, prc, abFull, abIncludeEdges);
}

bool CRealConsole::isFar(bool abPluginRequired/*=false*/)
{
	if (!this) return false;

	return GetFarPID(abPluginRequired)!=0;
}

// Должна вернуть true, если из фара что-то было запущено
// Если активный процесс и есть far - то false
bool CRealConsole::isFarInStack()
{
	if (mn_FarPID || mn_FarPID_PluginDetected || (mn_ProgramStatus & CES_FARINSTACK))
	{
		if (mn_ActivePID && (mn_ActivePID == mn_FarPID || mn_ActivePID == mn_FarPID_PluginDetected))
		{
			return false;
		}

		return true;
	}

	return false;
}

// Проверить, включен ли в фаре режим "far /w".
// В этом случае, буфер есть, но его прокруткой должен заниматься сам фар.
// Комбинации типа CtrlUp, CtrlDown и мышку - тоже передавать в фар.
bool CRealConsole::isFarBufferSupported()
{
	if (!this)
		return false;

	return (m_FarInfo.cbSize && m_FarInfo.bBufferSupport && (m_FarInfo.nFarPID == GetFarPID()));
}

// Проверить, разрешен ли в консоли прием мышиных сообщений
// Программы могут отключать прием через SetConsoleMode
bool CRealConsole::isSendMouseAllowed()
{
	if (!this || (mp_ABuf->m_Type != rbt_Primary))
		return false;

	if (mp_ABuf->GetConsoleMode() & ENABLE_QUICK_EDIT_MODE)
		return false;

	return true;
}

bool CRealConsole::isFarKeyBarShown()
{
	if (!isFar())
	{
		TODO("KeyBar в других приложениях? hiew?");
		return false;
	}

	bool bKeyBarShown = false;
	const CEFAR_INFO_MAPPING* pInfo = GetFarInfo();
	if (pInfo)
	{
		// Editor/Viewer
		if (isEditor() || isViewer())
		{
			WARNING("Эта настройка пока не отдается!");
			bKeyBarShown = true;
		}
		else
		{
			bKeyBarShown = (pInfo->FarInterfaceSettings.ShowKeyBar != 0);
		}
	}
	return bKeyBarShown;
}

bool CRealConsole::isSelectionAllowed()
{
	if (!this)
		return false;
	return mp_ABuf->isSelectionAllowed();
}

bool CRealConsole::isSelectionPresent()
{
	if (!this)
		return false;
	return mp_ABuf->isSelectionPresent();
}

bool CRealConsole::isMouseSelectionPresent()
{
	CONSOLE_SELECTION_INFO sel;
	if (!GetConsoleSelectionInfo(&sel))
		return false;
	return ((sel.dwFlags & CONSOLE_MOUSE_SELECTION) == CONSOLE_MOUSE_SELECTION);
}

bool CRealConsole::GetConsoleSelectionInfo(CONSOLE_SELECTION_INFO *sel)
{
	if (!isSelectionPresent())
	{
		memset(sel, 0, sizeof(*sel));
		return false;
	}
	return mp_ABuf->GetConsoleSelectionInfo(sel);
}

void CRealConsole::GetConsoleCursorInfo(CONSOLE_CURSOR_INFO *ci, COORD *cr)
{
	if (!this) return;

	if (ci)
		mp_ABuf->ConsoleCursorInfo(ci);

	if (cr)
		mp_ABuf->ConsoleCursorPos(cr);
}

void CRealConsole::GetConsoleScreenBufferInfo(CONSOLE_SCREEN_BUFFER_INFO* sbi)
{
	if (!this) return;
	mp_ABuf->ConsoleScreenBufferInfo(sbi);
}

//void CRealConsole::GetConsoleCursorPos(COORD *pcr)
//{
//	if (!this) return;
//	mp_ABuf->ConsoleCursorPos(pcr);
//}

// В дальнейшем надо бы возвращать значение для активного приложения
// По крайней мене в фаре мы можем проверить токены.
// В свойствах приложения проводником может быть установлен флажок "Run as administrator"
// Может быть соответствующий манифест...
// Хотя скорее всего это невозможно. В одной консоли не могут крутиться программы
// под разными аккаунтами (точнее elevated/non elevated)
bool CRealConsole::isAdministrator()
{
	if (!this)
		return false;

	if (m_Args.bRunAsAdministrator)
		return true;

	if (gpConEmu->mb_IsUacAdmin && !m_Args.bRunAsAdministrator && !m_Args.bRunAsRestricted && !m_Args.pszUserName)
		return true;

	return false;
}

BOOL CRealConsole::isMouseButtonDown()
{
	if (!this) return FALSE;

	return mb_MouseButtonDown;
}

// Аргумент - DWORD(!) а не DWORD_PTR. Это приходит из консоли.
void CRealConsole::OnConsoleKeyboardLayout(const DWORD dwNewLayout)
{
	_ASSERTE(dwNewLayout!=0 || gbIsWine);

	// LayoutName: "00000409", "00010409", ...
	// А HKL от него отличается, так что передаем DWORD
	// HKL в x64 выглядит как: "0x0000000000020409", "0xFFFFFFFFF0010409"

	// Информационно?
	mn_ActiveLayout = dwNewLayout;

	if (dwNewLayout)
	{
		gpConEmu->OnLangChangeConsole(mp_VCon, dwNewLayout);
	}
}

// Вызывается из CConEmuMain::OnLangChangeConsole в главной нити
void CRealConsole::OnConsoleLangChange(DWORD_PTR dwNewKeybLayout)
{
	if (mp_RBuf->GetKeybLayout() != dwNewKeybLayout)
	{
		if (gpSetCls->isAdvLogging > 1)
		{
			wchar_t szInfo[255];
			_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"CRealConsole::OnConsoleLangChange, Old=0x%08X, New=0x%08X",
			          (DWORD)mp_RBuf->GetKeybLayout(), (DWORD)dwNewKeybLayout);
			LogString(szInfo);
		}

		mp_RBuf->SetKeybLayout(dwNewKeybLayout);
		gpConEmu->SwitchKeyboardLayout(dwNewKeybLayout);
		
		#ifdef _DEBUG
		WCHAR szMsg[255];
		HKL hkl = GetKeyboardLayout(0);
		_wsprintf(szMsg, SKIPLEN(countof(szMsg)) L"ConEmu: GetKeyboardLayout(0) after SwitchKeyboardLayout = 0x%08I64X\n",
		          (unsigned __int64)(DWORD_PTR)hkl);
		DEBUGSTRLANG(szMsg);
		//Sleep(2000);
		#endif
	}
	else
	{
		if (gpSetCls->isAdvLogging > 1)
		{
			wchar_t szInfo[255];
			_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"CRealConsole::OnConsoleLangChange skipped, mp_RBuf->GetKeybLayout() already is 0x%08X",
			          (DWORD)dwNewKeybLayout);
			LogString(szInfo);
		}
	}
}

DWORD CRealConsole::GetConsoleStates()
{
	if (!this) return 0;

	// Что именно хотим узать? Real или Active?
	_ASSERTE(mp_ABuf==mp_RBuf);
	return mp_RBuf->GetConMode();
}


void CRealConsole::CloseColorMapping()
{
	m_TrueColorerMap.CloseMap();
	//if (mp_ColorHdr) {
	//	UnmapViewOfFile(mp_ColorHdr);
	//mp_ColorHdr = NULL;
	mp_TrueColorerData = NULL;
	//}
	//if (mh_ColorMapping) {
	//	CloseHandle(mh_ColorMapping);
	//	mh_ColorMapping = NULL;
	//}
	mb_DataChanged = true;
	mn_LastColorFarID = 0;
}

//void CRealConsole::CheckColorMapping(DWORD dwPID)
//{
//	if (!dwPID)
//		dwPID = GetFarPID();
//	if ((!dwPID && m_TrueColorerMap.IsValid()) || (dwPID != mn_LastColorFarID)) {
//		//CloseColorMapping();
//		if (!dwPID)
//			return;
//	}
//
//	if (dwPID == mn_LastColorFarID)
//		return; // Для этого фара - наличие уже проверяли!
//
//	mn_LastColorFarID = dwPID; // сразу запомним
//
//}

void CRealConsole::CreateColorMapping()
{
	if (!this)
	{
		_ASSERTE(this!=NULL);
		return;
	}

	BOOL lbResult = FALSE;
	wchar_t szErr[512]; szErr[0] = 0;
	//wchar_t szMapName[512]; szErr[0] = 0;
	AnnotationHeader *pHdr = NULL;
	_ASSERTE(mp_VCon->GetView()!=NULL);
	// 111101 - было "hConWnd", но GetConsoleWindow теперь перехватывается.
	m_TrueColorerMap.InitName(AnnotationShareName, (DWORD)sizeof(AnnotationInfo), (DWORD)mp_VCon->GetView()); //-V205
	
	WARNING("Удалить и переделать!");
	COORD crMaxSize = mp_RBuf->GetMaxSize();
	int nMapCells = max(crMaxSize.X,200) * max(crMaxSize.Y,200) * 2;
	DWORD nMapSize = nMapCells * sizeof(AnnotationInfo) + sizeof(AnnotationHeader);

	pHdr = m_TrueColorerMap.Create(nMapSize);
	if (!pHdr)
	{
		lstrcpyn(szErr, m_TrueColorerMap.GetErrorText(), countof(szErr));
		goto wrap;
	}
	pHdr->struct_size = sizeof(AnnotationHeader);
	pHdr->bufferSize = nMapCells;
	pHdr->locked = 0;
	pHdr->flushCounter = 0;

	//_ASSERTE(mh_ColorMapping == NULL);
	//swprintf_c(szMapName, AnnotationShareName, sizeof(AnnotationInfo), (DWORD)hConWnd);
	//mh_ColorMapping = OpenFileMapping(FILE_MAP_READ, FALSE, szMapName);
	//if (!mh_ColorMapping) {
	//	DWORD dwErr = GetLastError();
	//	swprintf_c (szErr, L"ConEmu: Can't open colorer file mapping. ErrCode=0x%08X. %s", dwErr, szMapName);
	//	goto wrap;
	//}
	//
	//mp_ColorHdr = (AnnotationHeader*)MapViewOfFile(mh_ColorMapping, FILE_MAP_READ,0,0,0);
	//if (!mp_ColorHdr) {
	//	DWORD dwErr = GetLastError();
	//	wchar_t szErr[512];
	//	swprintf_c (szErr, L"ConEmu: Can't map colorer info. ErrCode=0x%08X. %s", dwErr, szMapName);
	//	CloseHandle(mh_ColorMapping); mh_ColorMapping = NULL;
	//	goto wrap;
	//}
	pHdr = m_TrueColorerMap.Ptr();
	mp_TrueColorerData = (const AnnotationInfo*)(((LPBYTE)pHdr)+pHdr->struct_size);
	lbResult = TRUE;
wrap:

	if (!lbResult && szErr[0])
	{
		gpConEmu->DebugStep(szErr);
#ifdef _DEBUG
		MBoxA(szErr);
#endif
	}

	//return lbResult;
}

BOOL CRealConsole::OpenFarMapData()
{
	BOOL lbResult = FALSE;
	wchar_t szMapName[128], szErr[512]; szErr[0] = 0;
	DWORD dwErr = 0;
	DWORD nFarPID = GetFarPID(TRUE);

	// !!! обязательно
	MSectionLock CS; CS.Lock(&ms_FarInfoCS, TRUE);

	//CloseFarMapData();
	//_ASSERTE(m_FarInfo.IsValid() == FALSE);

	// Если сервер (консоль) закрывается - нет смысла переоткрывать FAR Mapping!
	if (m_ServerClosing.hServerProcess)
	{
		CloseFarMapData(&CS);
		goto wrap;
	}

	nFarPID = GetFarPID(TRUE);
	if (!nFarPID)
	{
		CloseFarMapData(&CS);
		goto wrap;
	}

	if (m_FarInfo.cbSize && m_FarInfo.nFarPID == nFarPID)
	{
		goto SkipReopen; // уже получен, видимо из другого потока
	}

	// Секция уже заблокирована, можно
	m__FarInfo.InitName(CEFARMAPNAME, nFarPID);
	if (!m__FarInfo.Open())
	{
		lstrcpynW(szErr, m__FarInfo.GetErrorText(), countof(szErr));
		goto wrap;
	}

	if (m__FarInfo.Ptr()->nFarPID != nFarPID)
	{
		_ASSERTE(m__FarInfo.Ptr()->nFarPID != nFarPID);
		CloseFarMapData(&CS);
		_wsprintf(szErr, SKIPLEN(countof(szErr)) L"ConEmu: Invalid FAR info format. %s", szMapName);
		goto wrap;
	}

SkipReopen:
	_ASSERTE(m__FarInfo.Ptr()->nProtocolVersion == CESERVER_REQ_VER);
	m__FarInfo.GetTo(&m_FarInfo, sizeof(m_FarInfo));

	m_FarAliveEvent.InitName(CEFARALIVEEVENT, nFarPID);

	if (!m_FarAliveEvent.Open())
	{
		dwErr = GetLastError();

		if (m__FarInfo.IsValid())
		{
			_ASSERTE(m_FarAliveEvent.GetHandle()!=NULL);
		}
	}

	lbResult = TRUE;
wrap:

	if (!lbResult && szErr[0])
	{
		gpConEmu->DebugStep(szErr);
		MBoxA(szErr);
	}

	UNREFERENCED_PARAMETER(dwErr);

	return lbResult;
}

BOOL CRealConsole::OpenMapHeader(BOOL abFromAttach)
{
	BOOL lbResult = FALSE;
	wchar_t szErr[512]; szErr[0] = 0;
	//int nConInfoSize = sizeof(CESERVER_CONSOLE_MAPPING_HDR);

	if (m_ConsoleMap.IsValid())
	{
		if (hConWnd == (HWND)(m_ConsoleMap.Ptr()->hConWnd))
		{
			_ASSERTE(m_ConsoleMap.Ptr() == NULL);
			return TRUE;
		}
	}

	//_ASSERTE(mh_FileMapping == NULL);
	//CloseMapData();
	m_ConsoleMap.InitName(CECONMAPNAME, (DWORD)hConWnd); //-V205

	if (!m_ConsoleMap.Open())
	{
		lstrcpyn(szErr, m_ConsoleMap.GetErrorText(), countof(szErr));
		//swprintf_c (szErr, L"ConEmu: Can't open console data file mapping. ErrCode=0x%08X. %s", dwErr, ms_HeaderMapName);
		goto wrap;
	}

	//swprintf_c(ms_HeaderMapName, CECONMAPNAME, (DWORD)hConWnd);
	//mh_FileMapping = OpenFileMapping(FILE_MAP_READ/*|FILE_MAP_WRITE*/, FALSE, ms_HeaderMapName);
	//if (!mh_FileMapping) {
	//	DWORD dwErr = GetLastError();
	//	swprintf_c (szErr, L"ConEmu: Can't open console data file mapping. ErrCode=0x%08X. %s", dwErr, ms_HeaderMapName);
	//	goto wrap;
	//}
	//
	//mp_ConsoleInfo = (CESERVER_CONSOLE_MAPPING_HDR*)MapViewOfFile(mh_FileMapping, FILE_MAP_READ/*|FILE_MAP_WRITE*/,0,0,0);
	//if (!mp_ConsoleInfo) {
	//	DWORD dwErr = GetLastError();
	//	wchar_t szErr[512];
	//	swprintf_c (szErr, L"ConEmu: Can't map console info. ErrCode=0x%08X. %s", dwErr, ms_HeaderMapName);
	//	goto wrap;
	//}

	if (!abFromAttach)
	{
		if (m_ConsoleMap.Ptr()->nGuiPID != GetCurrentProcessId())
		{
			_ASSERTE(m_ConsoleMap.Ptr()->nGuiPID == GetCurrentProcessId());
			WARNING("Наверное нужно будет передать в сервер код GUI процесса? В каком случае так может получиться?");
			// Передать через команду сервера новый GUI PID. Если пайп не готов сразу выйти
		}
	}

	if (m_ConsoleMap.Ptr()->hConWnd && m_ConsoleMap.Ptr()->bDataReady)
	{
		// Только если MonitorThread еще не был запущен
		if (mn_MonitorThreadID == 0)
		{
			_ASSERTE(mp_RBuf==mp_ABuf);
			mp_RBuf->ApplyConsoleInfo();
		}
	}

	lbResult = TRUE;
wrap:

	if (!lbResult && szErr[0])
	{
		gpConEmu->DebugStep(szErr);
		MBoxA(szErr);
	}

	return lbResult;
}

//void CRealConsole::CloseMapData()
//{
//	if (mp_ConsoleData) {
//		UnmapViewOfFile(mp_ConsoleData);
//		mp_ConsoleData = NULL;
//		lstrcpy(ms_ConStatus, L"Console data was not opened!");
//	}
//	if (mh_FileMappingData) {
//		CloseHandle(mh_FileMappingData);
//		mh_FileMappingData = NULL;
//	}
//	mn_LastConsoleDataIdx = mn_LastConsolePacketIdx = /*mn_LastFarReadIdx =*/ -1;
//	mn_LastFarReadTick = 0;
//}

void CRealConsole::CloseFarMapData(MSectionLock* pCS)
{
	MSectionLock CS;
	(pCS ? pCS : &CS)->Lock(&ms_FarInfoCS, TRUE);

	m_FarInfo.cbSize = 0; // сброс
	m__FarInfo.CloseMap();

	m_FarAliveEvent.Close();
}

void CRealConsole::CloseMapHeader()
{
	CloseFarMapData();
	//CloseMapData();
	m_GetDataPipe.Close();
	m_ConsoleMap.CloseMap();
	//if (mp_ConsoleInfo) {
	//	UnmapViewOfFile(mp_ConsoleInfo);
	//	mp_ConsoleInfo = NULL;
	//}
	//if (mh_FileMapping) {
	//	CloseHandle(mh_FileMapping);
	//	mh_FileMapping = NULL;
	//}

	if (mp_RBuf) mp_RBuf->ResetBuffer();
	if (mp_EBuf) mp_EBuf->ResetBuffer();
	if (mp_SBuf) mp_SBuf->ResetBuffer();

	mb_DataChanged = true;
}

bool CRealConsole::isAlive()
{
	if (!this)
		return false;

	if (GetFarPID(TRUE)!=0 && mn_LastFarReadTick /*mn_LastFarReadIdx != (DWORD)-1*/)
	{
		bool lbAlive;
		DWORD nLastReadTick = mn_LastFarReadTick;

		if (nLastReadTick)
		{
			DWORD nCurTick = GetTickCount();
			DWORD nDelta = nCurTick - nLastReadTick;

			if (nDelta < FAR_ALIVE_TIMEOUT)
				lbAlive = true;
			else
				lbAlive = false;
		}
		else
		{
			lbAlive = false;
		}

		return lbAlive;
	}

	return true;
}

LPCWSTR CRealConsole::GetConStatus()
{
	if (!this)
	{
		_ASSERTE(this);
		return NULL;
	}
	if (hGuiWnd)
		return NULL;
	return ms_ConStatus;
}

void CRealConsole::SetConStatus(LPCWSTR asStatus, bool abResetOnConsoleReady /*= false*/, bool abDontUpdate /*= false*/)
{
	if (gpSetCls->isAdvLogging)
	{
		wchar_t szPrefix[128];
		_wsprintf(szPrefix, SKIPLEN(countof(szPrefix)) L"CRealConsole::SetConStatus, hView=x%08X: ", (DWORD)(DWORD_PTR)mp_VCon->GetView());
		wchar_t* pszInfo = lstrmerge(szPrefix, (asStatus && *asStatus) ? asStatus : L"<Empty>");
		if (mp_Log)
			LogString(pszInfo, TRUE);
		else
			gpConEmu->LogString(pszInfo);
		SafeFree(pszInfo);
	}

	lstrcpyn(ms_ConStatus, asStatus ? asStatus : L"", countof(ms_ConStatus));
	mb_ResetStatusOnConsoleReady = abResetOnConsoleReady;

	lstrcpyn(CRealConsole::ms_LastRConStatus, ms_ConStatus, countof(CRealConsole::ms_LastRConStatus));

	if (!abDontUpdate && isActive(false))
	{
		// Обновить статусную строку, если она показана
		if (gpSet->isStatusBarShow)
		{
			// Перерисовать сразу
			gpConEmu->mp_Status->UpdateStatusBar(true, true);
		}
		else if (!abDontUpdate && mp_VCon->GetView())
		{
			mp_VCon->Update(true);
		}

		if (mp_VCon->GetView())
		{
			mp_VCon->Invalidate();
		}
	}
}

void CRealConsole::UpdateCursorInfo()
{
	if (!this)
		return;

	if (!isActive())
		return;

	CONSOLE_SCREEN_BUFFER_INFO sbi = {};
	CONSOLE_CURSOR_INFO ci = {};
	//mp_RBuf->GetCursorInfo(&cr, &ci);
	mp_ABuf->ConsoleCursorInfo(&ci);
	mp_ABuf->ConsoleScreenBufferInfo(&sbi);
	
	gpConEmu->UpdateCursorInfo(&sbi, sbi.dwCursorPosition, ci);
}

bool CRealConsole::isNeedCursorDraw()
{
	if (!this)
		return false;

	if (GuiWnd())
	{
		// В GUI режиме VirtualConsole скрыта под GUI окном и видна только при "включении" BufferHeight
		if (!isBufferHeight())
			return false;
	}
	else
	{
		if (!hConWnd || !mb_RConStartedSuccess)
			return false;

		// Remove cursor from screen while selecting text with mouse
		CONSOLE_SELECTION_INFO sel = {};
		if (mp_ABuf->GetConsoleSelectionInfo(&sel) && (sel.dwFlags & CONSOLE_MOUSE_SELECTION))
			return false;

		COORD cr; CONSOLE_CURSOR_INFO ci;
		mp_ABuf->GetCursorInfo(&cr, &ci);
		if (!ci.bVisible || !ci.dwSize)
			return false;
	}

	return true;
}

// может отличаться от CVirtualConsole
bool CRealConsole::isCharBorderVertical(WCHAR inChar)
{
	if ((inChar != ucBoxDblHorz && inChar != ucBoxSinglHorz
	        && (inChar >= ucBoxSinglVert && inChar <= ucBoxDblVertHorz))
	        || (inChar >= ucBox25 && inChar <= ucBox75) || inChar == ucBox100
	        || inChar == ucUpScroll || inChar == ucDnScroll)
		return true;
	else
		return false;
}

bool CRealConsole::isCharBorderLeftVertical(WCHAR inChar)
{
	if (inChar < ucBoxSinglHorz || inChar > ucBoxDblVertHorz)
		return false; // чтобы лишних сравнений не делать

	if (inChar == ucBoxDblVert || inChar == ucBoxSinglVert
	        || inChar == ucBoxDblDownRight || inChar == ucBoxSinglDownRight
	        || inChar == ucBoxDblVertRight || inChar == ucBoxDblVertSinglRight
	        || inChar == ucBoxSinglVertRight
	        || (inChar >= ucBox25 && inChar <= ucBox75) || inChar == ucBox100
	        || inChar == ucUpScroll || inChar == ucDnScroll)
		return true;
	else
		return false;
}

// может отличаться от CVirtualConsole
bool CRealConsole::isCharBorderHorizontal(WCHAR inChar)
{
	if (inChar == ucBoxSinglDownDblHorz || inChar == ucBoxSinglUpDblHorz
			|| inChar == ucBoxDblDownDblHorz || inChar == ucBoxDblUpDblHorz
	        || inChar == ucBoxSinglDownHorz || inChar == ucBoxSinglUpHorz || inChar == ucBoxDblUpSinglHorz
	        || inChar == ucBoxDblHorz)
		return true;
	else
		return false;
}

bool CRealConsole::GetMaxConSize(COORD* pcrMaxConSize)
{
	bool bOk = false;

	//if (mp_ConsoleInfo)
	if (m_ConsoleMap.IsValid())
	{
		if (pcrMaxConSize)
			*pcrMaxConSize = m_ConsoleMap.Ptr()->crMaxConSize;

		bOk = true;
	}

	return bOk;
}

int CRealConsole::GetDetectedDialogs(int anMaxCount, SMALL_RECT* rc, DWORD* rf)
{
	if (!this || !mp_ABuf)
		return 0;

	return mp_ABuf->GetDetector()->GetDetectedDialogs(anMaxCount, rc, rf);
	//int nCount = min(anMaxCount,m_DetectedDialogs.Count);
	//if (nCount>0) {
	//	if (rc)
	//		memmove(rc, m_DetectedDialogs.Rects, nCount*sizeof(SMALL_RECT));
	//	if (rb)
	//		memmove(rb, m_DetectedDialogs.bWasFrame, nCount*sizeof(bool));
	//}
	//return nCount;
}

const CRgnDetect* CRealConsole::GetDetector()
{
	if (!this)
		return NULL;
	return mp_ABuf->GetDetector();
}

// Преобразовать абсолютные координаты консоли в координаты нашего буфера
// (вычесть номер верхней видимой строки и скорректировать видимую высоту)
bool CRealConsole::ConsoleRect2ScreenRect(const RECT &rcCon, RECT *prcScr)
{
	if (!this) return false;
	return mp_ABuf->ConsoleRect2ScreenRect(rcCon, prcScr);
}

DWORD CRealConsole::PostMacroThread(LPVOID lpParameter)
{
	PostMacroAnyncArg* pArg = (PostMacroAnyncArg*)lpParameter;
	if (pArg->bPipeCommand)
	{
		CConEmuPipe pipe(pArg->pRCon->GetFarPID(TRUE), CONEMUREADYTIMEOUT);
		if (pipe.Init(_T("CRealConsole::PostMacroThread"), TRUE))
		{
			gpConEmu->DebugStep(_T("PostMacroThread: Waiting for result (10 sec)"));
			pipe.Execute(pArg->nCmdID, pArg->Data, pArg->nCmdSize);
			gpConEmu->DebugStep(NULL);
		}
	}
	else
	{
		pArg->pRCon->PostMacro(pArg->szMacro, FALSE/*теперь - точно Sync*/);
	}
	free(pArg);
	return 0;
}

void CRealConsole::PostCommand(DWORD anCmdID, DWORD anCmdSize, LPCVOID ptrData)
{
	if (!this)
		return;
	if (mh_PostMacroThread != NULL)
	{
		DWORD nWait = WaitForSingleObject(mh_PostMacroThread, 0);
		if (nWait == WAIT_OBJECT_0)
		{
			CloseHandle(mh_PostMacroThread);
			mh_PostMacroThread = NULL;
		}
		else
		{
			// Должен быть NULL, если нет - значит завис предыдующий макрос
			_ASSERTE(mh_PostMacroThread==NULL);
			TerminateThread(mh_PostMacroThread, 100);
			CloseHandle(mh_PostMacroThread);
		}
	}

	size_t nArgSize = sizeof(PostMacroAnyncArg) + anCmdSize;
	PostMacroAnyncArg* pArg = (PostMacroAnyncArg*)calloc(1,nArgSize);
	pArg->pRCon = this;
	pArg->bPipeCommand = TRUE;
	pArg->nCmdID = anCmdID;
	pArg->nCmdSize = anCmdSize;
	if (ptrData && anCmdSize)
		memmove(pArg->Data, ptrData, anCmdSize);
	mh_PostMacroThread = CreateThread(NULL, 0, PostMacroThread, pArg, 0, &mn_PostMacroThreadID);	
	if (mh_PostMacroThread == NULL)
	{
		// Если не удалось запустить нить
		MBoxAssert(mh_PostMacroThread!=NULL);
		free(pArg);
	}
	return;
}

void CRealConsole::PostDragCopy(BOOL abMove)
{
	const CEFAR_INFO_MAPPING* pFarVer = GetFarInfo();

	wchar_t *mcr = (wchar_t*)calloc(128, sizeof(wchar_t));
	//2010-02-18 Не было префикса '@'
	//2010-03-26 префикс '@' ставить нельзя, ибо тогда процесса копирования видно не будет при отсутствии подтверждения

	if (pFarVer && ((pFarVer->FarVer.dwVerMajor > 3) || ((pFarVer->FarVer.dwVerMajor == 3) && (pFarVer->FarVer.dwBuild > 2850))))
	{
		// Если тянули ".." то перед копированием на другую панель сначала необходимо выйти на верхний уровень
		lstrcpyW(mcr, L"if APanel.SelCount==0 and APanel.Current==\"..\" then Keys('CtrlPgUp') end ");

		// Теперь собственно клавиша запуска
		// И если просили копировать сразу без подтверждения
		if (gpSet->isDropEnabled==2)
		{
			lstrcatW(mcr, abMove ? L"Keys('F6 Enter')" : L"Keys('F5 Enter')");
		}
		else
		{
			lstrcatW(mcr, abMove ? L"Keys('F6')" : L"Keys('F5')");
		}
	}
	else
	{
		// Если тянули ".." то перед копированием на другую панель сначала необходимо выйти на верхний уровень
		lstrcpyW(mcr, L"$If (APanel.SelCount==0 && APanel.Current==\"..\") CtrlPgUp $End ");
		// Теперь собственно клавиша запуска
		lstrcatW(mcr, abMove ? L"F6" : L"F5");

		// И если просили копировать сразу без подтверждения
		if (gpSet->isDropEnabled==2)
			lstrcatW(mcr, L" Enter "); //$MMode 1");
	}

	PostMacro(mcr, TRUE/*abAsync*/);
}

bool CRealConsole::GetFarVersion(FarVersion* pfv)
{
	if (!this)
		return false;

	DWORD nPID = GetFarPID(TRUE/*abPluginRequired*/);

	if (!nPID)
		return false;

	const CEFAR_INFO_MAPPING* pInfo = GetFarInfo();
	if (!pInfo)
	{
		_ASSERTE(pInfo!=NULL);
		return false;
	}
	
	if (pfv)
		*pfv = pInfo->FarVer;

	return (pInfo->FarVer.dwVerMajor >= 1);
}

bool CRealConsole::IsFarLua()
{
	FarVersion fv;
	if (GetFarVersion(&fv))
		return fv.IsFarLua();
	return false;
}

void CRealConsole::PostMacro(LPCWSTR asMacro, BOOL abAsync /*= FALSE*/)
{
	if (!this || !asMacro || !*asMacro)
		return;

	if (*asMacro == GUI_MACRO_PREFIX/*L'#'*/)
	{
		// Значит это GuiMacro, а не FarMacro.
		if (asMacro[1])
		{
			LPWSTR pszGui = lstrdup(asMacro+1);
			LPWSTR pszRc = ConEmuMacro::ExecuteMacro(pszGui, this);
			TODO("Показать результат в статусной строке?");
			SafeFree(pszGui);
			SafeFree(pszRc);
		}
		return;
	}

	DWORD nPID = GetFarPID(TRUE/*abPluginRequired*/);

	if (!nPID)
		return;

	const CEFAR_INFO_MAPPING* pInfo = GetFarInfo();
	if (!pInfo)
	{
		_ASSERTE(pInfo!=NULL);
		return;
	}
	
	if (pInfo->FarVer.IsFarLua())
	{
		if (lstrcmpi(asMacro, gpSet->RClickMacroDefault(fmv_Standard)) == 0)
			asMacro = gpSet->RClickMacroDefault(fmv_Lua);
		else if (lstrcmpi(asMacro, gpSet->SafeFarCloseMacroDefault(fmv_Standard)) == 0)
			asMacro = gpSet->SafeFarCloseMacroDefault(fmv_Lua);
		else if (lstrcmpi(asMacro, gpSet->TabCloseMacroDefault(fmv_Standard)) == 0)
			asMacro = gpSet->TabCloseMacroDefault(fmv_Lua);
		else if (lstrcmpi(asMacro, gpSet->SaveAllMacroDefault(fmv_Standard)) == 0)
			asMacro = gpSet->SaveAllMacroDefault(fmv_Lua);
	}

	if (abAsync)
	{
		if (mb_InCloseConsole)
			ShutdownGuiStep(L"PostMacro, creating thread");

		if (mh_PostMacroThread != NULL)
		{
			DWORD nWait = WaitForSingleObject(mh_PostMacroThread, 0);
			if (nWait == WAIT_OBJECT_0)
			{
				CloseHandle(mh_PostMacroThread);
				mh_PostMacroThread = NULL;
			}
			else
			{
				// Должен быть NULL, если нет - значит завис предыдующий макрос
				_ASSERTE(mh_PostMacroThread==NULL);
				TerminateThread(mh_PostMacroThread, 100);
				CloseHandle(mh_PostMacroThread);
			}
		}

		size_t nLen = _tcslen(asMacro);
		size_t nArgSize = sizeof(PostMacroAnyncArg) + nLen*sizeof(*asMacro);
		PostMacroAnyncArg* pArg = (PostMacroAnyncArg*)calloc(1,nArgSize);
		pArg->pRCon = this;
		pArg->bPipeCommand = FALSE;
		_wcscpy_c(pArg->szMacro, nLen+1, asMacro);
		mh_PostMacroThread = CreateThread(NULL, 0, PostMacroThread, pArg, 0, &mn_PostMacroThreadID);	
		if (mh_PostMacroThread == NULL)
		{
			// Если не удалось запустить нить
			MBoxAssert(mh_PostMacroThread!=NULL);
			free(pArg);
		}
		return;
	}

	if (mb_InCloseConsole)
		ShutdownGuiStep(L"PostMacro, calling pipe");

#ifdef _DEBUG
	DEBUGSTRMACRO(asMacro); DEBUGSTRMACRO(L"\n");
#endif
	CConEmuPipe pipe(nPID, CONEMUREADYTIMEOUT);

	if (pipe.Init(_T("CRealConsole::PostMacro"), TRUE))
	{
		//DWORD cbWritten=0;
		gpConEmu->DebugStep(_T("Macro: Waiting for result (10 sec)"));
		pipe.Execute(CMD_POSTMACRO, asMacro, (_tcslen(asMacro)+1)*2);
		gpConEmu->DebugStep(NULL);
	}

	if (mb_InCloseConsole)
		ShutdownGuiStep(L"PostMacro, done");
}

void CRealConsole::Detach(bool bPosted /*= false*/, bool bSendCloseConsole /*= false*/)
{
	if (!this)
		return;

	LogString(L"CRealConsole::Detach", TRUE);

	if (hGuiWnd)
	{
		if (!bPosted)
		{
			if (MessageBox(NULL, L"Detach GUI application from ConEmu?", GetTitle(), MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2) != IDYES)
				return;

			RECT rcGui = {}; //rcPreGuiWndRect;
			GetWindowRect(hGuiWnd, &rcGui); // Логичнее все же оставить приложение в том же месте
			rcPreGuiWndRect = rcGui;

			ShowGuiClientExt(1, TRUE);

			ShowOtherWindow(hGuiWnd, SW_HIDE, FALSE/*синхронно*/);
			SetOtherWindowParent(hGuiWnd, NULL);
			SetOtherWindowPos(hGuiWnd, HWND_NOTOPMOST, rcGui.left, rcGui.top, rcGui.right-rcGui.left, rcGui.bottom-rcGui.top, SWP_SHOWWINDOW);

			mp_VCon->PostDetach(bSendCloseConsole);
			return;
		}

		//#ifdef _DEBUG
		//WINDOWPLACEMENT wpl = {sizeof(wpl)};
		//GetWindowPlacement(hGuiWnd, &wpl); // дает клиентские координаты
		//#endif
		
		HWND lhGuiWnd = hGuiWnd;
		//RECT rcGui = rcPreGuiWndRect;
		//GetWindowRect(hGuiWnd, &rcGui); // Логичнее все же оставить приложение в том же месте

		//ShowGuiClientExt(1, TRUE);
	
		//ShowOtherWindow(lhGuiWnd, SW_HIDE, FALSE/*синхронно*/);
		//SetOtherWindowParent(lhGuiWnd, NULL);
		//SetOtherWindowPos(lhGuiWnd, HWND_NOTOPMOST, rcGui.left, rcGui.top, rcGui.right-rcGui.left, rcGui.bottom-rcGui.top, SWP_SHOWWINDOW);

		// Сбросить переменные, чтобы гуй закрыть не пыталось
		hGuiWnd = mh_GuiWndFocusStore = NULL;
		setGuiWndPID(0, NULL);
		//mb_IsGuiApplication = FALSE;

		//// Закрыть консоль
		//CloseConsole(false, false);

		// Уведомить сервер, что нужно закрыться
		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_DETACHCON, sizeof(CESERVER_REQ_HDR)+2*sizeof(DWORD));
		pIn->dwData[0] = (DWORD)lhGuiWnd;
		pIn->dwData[1] = bSendCloseConsole;
		DWORD dwTickStart = timeGetTime();
		
		CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(true), pIn, ghWnd);
		
		gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);
		
		ExecuteFreeResult(pOut);
		ExecuteFreeResult(pIn);

		// Поднять отцепленное окно "наверх"
		apiSetForegroundWindow(lhGuiWnd);
	}
	else
	{
		if (!bPosted)
		{
			if (MessageBox(NULL, L"Detach console from ConEmu?", GetTitle(), MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2) != IDYES)
				return;

			mp_VCon->PostDetach(bSendCloseConsole);
			return;
		}
	
		//ShowConsole(1); -- уберем, чтобы не мигало
		isShowConsole = TRUE; // просто флажок взведем, чтобы не пытаться ее спрятать
		RECT rcScreen;
		if (GetWindowRect(mp_VCon->GetView(), &rcScreen) && hConWnd)
			SetOtherWindowPos(hConWnd, HWND_NOTOPMOST, rcScreen.left, rcScreen.top, 0,0, SWP_NOSIZE);

		// Уведомить сервер, что он больше не наш
		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_DETACHCON, sizeof(CESERVER_REQ_HDR)+2*sizeof(DWORD));
		DWORD dwTickStart = timeGetTime();
		pIn->dwData[0] = 0;
		pIn->dwData[1] = bSendCloseConsole;
		
		CESERVER_REQ *pOut = ExecuteSrvCmd(GetServerPID(true), pIn, ghWnd);
		
		gpSetCls->debugLogCommand(pIn, FALSE, dwTickStart, timeGetTime()-dwTickStart, L"ExecuteSrvCmd", pOut);
		
		if (pOut)
			ExecuteFreeResult(pOut);
		else
			ShowConsole(1);
		
		ExecuteFreeResult(pIn);

		//SetLastError(0);
		//BOOL bVisible = IsWindowVisible(hConWnd); -- проверка обламывается. Не успевает...
		//DWORD nErr = GetLastError();
		//if (hConWnd && !bVisible)
		//	ShowConsole(1);
	}

	// Чтобы случайно не закрыть RealConsole?
	m_Args.bDetached = TRUE;
	
	CConEmuChild::ProcessVConClosed(mp_VCon);
}

const CEFAR_INFO_MAPPING* CRealConsole::GetFarInfo()
{
	if (!this) return NULL;

	//return m_FarInfo.Ptr(); -- нельзя, может быть закрыт в другом потоке!

	if (!m_FarInfo.cbSize)
		return NULL;
	return &m_FarInfo;
}

/*LPCWSTR CRealConsole::GetLngNameTime()
{
	if (!this) return NULL;
	return ms_NameTitle;
}*/

bool CRealConsole::InCreateRoot()
{
	return (this && mb_InCreateRoot && !mn_MainSrv_PID);
}

bool CRealConsole::InRecreate()
{
	return (this && mb_ProcessRestarted);
}

// Можно ли к этой консоли прицепить GUI приложение
BOOL CRealConsole::GuiAppAttachAllowed(LPCWSTR asAppFileName, DWORD anAppPID)
{
	if (!this)
		return false;
	// Если даже сервер еще не запущен
	if (InCreateRoot())
		return false;

	// Интересуют только клиентские процессы!
	int nProcCount = GetProcesses(NULL, true);
	DWORD nActivePID = GetActivePID();

	// Вызывается два раза. Первый (при запуске exe) ahGuiWnd==NULL, второй - после фактического создания окна

	if (nProcCount > 0 && nActivePID == anAppPID)
		return true; // Уже подцепились именно к этому приложению, а сейчас было создано окно

	// Иначе - проверяем дальше
	if ((nProcCount > 0) || (nActivePID != 0))
		return false;

	// Проверить, подходит ли asAppFileName к ожидаемому
	LPCWSTR pszCmd = GetCmd();
	if (pszCmd && *pszCmd && asAppFileName && *asAppFileName)
	{
		wchar_t szApp[MAX_PATH+1];
		CmdArg  szArg;
		LPCWSTR pszArg = NULL, pszApp = NULL, pszOnly = NULL;

		while (pszCmd[0] == L'"' && pszCmd[1] == L'"')
			pszCmd++;

		pszOnly = PointToName(pszCmd);

		// Теперь то, что запущено (пришло из GUI приложения)
		pszApp = PointToName(asAppFileName);
		lstrcpyn(szApp, pszApp, countof(szApp));
		wchar_t* pszDot = wcsrchr(szApp, L'.'); // расширение?
		CharUpperBuff(szApp, lstrlen(szApp));

		if (NextArg(&pszCmd, szArg, &pszApp) == 0)
		{
			// Что пытаемся запустить в консоли
			CharUpperBuff(szArg.ms_Arg, lstrlen(szArg));
			pszArg = PointToName(szArg);
			if (lstrcmp(pszArg, szApp) == 0)
				return true;
			if (!wcschr(pszArg, L'.') && pszDot)
			{
				*pszDot = 0;
				if (lstrcmp(pszArg, szApp) == 0)
					return true;
				*pszDot = L'.';
			}
		}

		// Может там кавычек нет, а путь с пробелом
		szArg.Set(pszOnly);
		CharUpperBuff(szArg.ms_Arg, lstrlen(szArg));
		if (lstrcmp(szArg, szApp) == 0)
			return true;
		if (pszArg && !wcschr(pszArg, L'.') && pszDot)
		{
			*pszDot = 0;
			if (lstrcmp(pszArg, szApp) == 0)
				return true;
			*pszDot = L'.';
		}

		return false;
	}

	_ASSERTE(pszCmd && *pszCmd && asAppFileName && *asAppFileName);
	return true;
}

void CRealConsole::ShowPropertiesDialog()
{
	if (!this)
		return;
	
	// Если в RealConsole два раза подряд послать SC_PROPERTIES_SECRET,
	// то при закрытии одного из двух (!) открытых диалогов - ConHost падает!
	// Поэтому, сначала попытаемся найти диалог настроек, и только если нет - посылаем msg
	HWND hConProp = NULL;
	wchar_t szTitle[255]; int nDefLen = _tcslen(CEC_INITTITLE);
	// К сожалению, так не получится найти окно свойств, если консоль была
	// создана НЕ через ConEmu (например, был запущен Far, а потом сделан Attach).
	while ((hConProp = FindWindowEx(NULL, hConProp, (LPCWSTR)32770, NULL)) != NULL)
	{
		if (GetWindowText(hConProp, szTitle, countof(szTitle))
			&& szTitle[0] == L'"' && szTitle[nDefLen+1] == L'"'
			&& !wmemcmp(szTitle+1, CEC_INITTITLE, nDefLen))
		{
			apiSetForegroundWindow(hConProp);
			return; // нашли, показываем!
		}
	}

	POSTMESSAGE(hConWnd, WM_SYSCOMMAND, SC_PROPERTIES_SECRET/*65527*/, 0, TRUE);
}

//void CRealConsole::LogShellStartStop()
//{
//	// Пока - только при наличии Far-плагина
//	DWORD nFarPID = GetFarPID(TRUE);
//
//	if (!nFarPID)
//		return;
//
//	if (!mb_ShellActivityLogged)
//	{
//		OPENFILENAME ofn; memset(&ofn,0,sizeof(ofn));
//		ofn.lStructSize=sizeof(ofn);
//		ofn.hwndOwner = ghWnd;
//		ofn.lpstrFilter = _T("Log files (*.log)\0*.log\0\0");
//		ofn.nFilterIndex = 1;
//
//		if (ms_LogShellActivity[0] == 0)
//		{
//			lstrcpyn(ms_LogShellActivity, gpConEmu->WorkDir(), MAX_PATH-32);
//			int nCurLen = _tcslen(ms_LogShellActivity);
//			_wsprintf(ms_LogShellActivity+nCurLen, SKIPLEN(countof(ms_LogShellActivity)-nCurLen)
//			          L"\\ShellLog-%u.log", nFarPID);
//		}
//
//		ofn.lpstrFile = ms_LogShellActivity;
//		ofn.nMaxFile = countof(ms_LogShellActivity);
//		ofn.lpstrTitle = L"Log CreateProcess...";
//		ofn.lpstrDefExt = L"log";
//		ofn.Flags = OFN_ENABLESIZING|OFN_NOCHANGEDIR
//		            | OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT;
//
//		if (!GetSaveFileName(&ofn))
//			return;
//
//		mb_ShellActivityLogged = true;
//	}
//	else
//	{
//		mb_ShellActivityLogged = false;
//	}
//
//	//TODO: Обновить бы поле m_ConsoleMap<CESERVER_CONSOLE_MAPPING_HDR>.sLogCreateProcess
//	// Выполнить в плагине
//	CConEmuPipe pipe(nFarPID, 300);
//
//	if (pipe.Init(L"LogShell", TRUE))
//	{
//		LPCVOID pData; wchar_t wch = 0;
//		UINT nDataSize;
//
//		if (mb_ShellActivityLogged && ms_LogShellActivity[0])
//		{
//			pData = ms_LogShellActivity;
//			nDataSize = (_tcslen(ms_LogShellActivity)+1)*2;
//		}
//		else
//		{
//			pData = &wch;
//			nDataSize = 2;
//		}
//
//		pipe.Execute(CMD_LOG_SHELL, pData, nDataSize);
//	}
//}

//bool CRealConsole::IsLogShellStarted()
//{
//	return mb_ShellActivityLogged && ms_LogShellActivity[0];
//}

DWORD CRealConsole::GetConsoleCP()
{
	/*return con.m_dwConsoleCP;*/
	return mp_RBuf->GetConsoleCP();
}

DWORD CRealConsole::GetConsoleOutputCP()
{
	/*return con.m_dwConsoleOutputCP;*/
	return mp_RBuf->GetConsoleOutputCP();
}

DWORD CRealConsole::GetConsoleMode()
{
	/*return con.m_dwConsoleMode;*/
	return mp_RBuf->GetConMode();
}

ExpandTextRangeType CRealConsole::GetLastTextRangeType()
{
	return mp_ABuf->GetLastTextRangeType();
}

void CRealConsole::setGuiWndPID(DWORD anPID, LPCWSTR asProcessName)
{
	mn_GuiWndPID = anPID;

	if (asProcessName != ms_GuiWndProcess)
	{
		lstrcpyn(ms_GuiWndProcess, asProcessName ? asProcessName : L"", countof(ms_GuiWndProcess));
	}

	if (gpSetCls->isAdvLogging)
	{
		wchar_t szInfo[MAX_PATH+100];
		_wsprintf(szInfo, SKIPLEN(countof(szInfo)) L"setGuiWndPID: PID=%u, '%s'", anPID, ms_GuiWndProcess);
		gpConEmu->LogString(szInfo);
	}
}

void CRealConsole::CtrlWinAltSpace()
{
	if (!this)
	{
		return;
	}

	static DWORD dwLastSpaceTick = 0;

	if ((dwLastSpaceTick-GetTickCount())<1000)
	{
		//if (hWnd == ghWnd DC) MBoxA(_T("Space bounce recieved from DC")) else
		//if (hWnd == ghWnd) MBoxA(_T("Space bounce recieved from MainWindow")) else
		//if (hWnd == gpConEmu->m_Back->mh_WndBack) MBoxA(_T("Space bounce recieved from BackWindow")) else
		//if (hWnd == gpConEmu->m_Back->mh_WndScroll) MBoxA(_T("Space bounce recieved from ScrollBar")) else
		MBoxA(_T("Space bounce recieved from unknown window"));
		return;
	}

	dwLastSpaceTick = GetTickCount();
	//MBox(L"CtrlWinAltSpace: Toggle");
	ShowConsoleOrGuiClient(-1); // Toggle visibility
}

void CRealConsole::AutoCopyTimer()
{
	if (gpSet->isCTSAutoCopy && isSelectionPresent())
	{
		mp_ABuf->DoSelectionFinalize(true);
	}
}
