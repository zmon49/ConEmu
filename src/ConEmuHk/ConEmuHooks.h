
/*
Copyright (c) 2009-2011 Maximus5
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


#pragma once

//#define DEBUGLOG(s) //DEBUGSTR(s)
//#define DEBUGLOGSIZE(s) DEBUGSTR(s)
//#define DEBUGLOGLANG(s) //DEBUGSTR(s) //; Sleep(2000)
//
//#ifdef _DEBUG
////CRITICAL_ SECTION gcsHeap;
////#define MCHKHEAP { Enter CriticalSection(&gcsHeap); int MDEBUG_CHK=_CrtCheckMemory(); _ASSERTE(MDEBUG_CHK); LeaveCriticalSection(&gcsHeap); }
////#define MCHKHEAP HeapValidate(ghHeap, 0, NULL);
////#define MCHKHEAP { int MDEBUG_CHK=_CrtCheckMemory(); _ASSERTE(MDEBUG_CHK); }
////#define HEAP_LOGGING
//#else
////#define MCHKHEAP
//#endif




#define CSECTION_NON_RAISE

#include <Windows.h>
#include <WinCon.h>


/*  Global  */
extern DWORD   gnSelfPID;
extern HWND    ghConWnd;
extern HWND    ghConEmuWnd; // Root! window
extern HWND    ghConEmuWndDC; // ConEmu DC window
extern DWORD   gnServerPID; // PID ������� (���������������� �� ������, ��� �������� Dll)


#include "../common/common.hpp"
#include "../common/ConEmuCheck.h"
#include "../common/WinObjects.h"

//extern MFileMapping<CESERVER_CONSOLE_MAPPING_HDR> *gpConMap;
//extern CESERVER_CONSOLE_MAPPING_HDR* gpConInfo;
CESERVER_CONSOLE_MAPPING_HDR* GetConMap();