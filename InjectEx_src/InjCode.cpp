/***************************************************************
Module name: InjCode.cpp
Copyright (c) 2003 Robert Kuster

Notice:	If this code works, it was written by Robert Kuster.
		Else, I don't know who wrote it.

		Use it on your own risk. No responsibilities for
		possible damages of even functionality can be taken.
***************************************************************/

#include <windows.h>
#include "InjCode.h"

//---------------------------------------------------------------------
// The size of NewProc, InjectFunc and EjectFunc rounded up 
// to the next 32x value - just bo be on the safe side. 
// Obviously it is better to copy some bytes more than neccessary 
// rather than strip off the last instructions of our functions.
//
#ifdef _DEBUG
#define cbNewProc		224
#define cbInjectFunc	192
#define cbEjectFunc		64
#else
#define cbNewProc		160
#define cbInjectFunc	128
#define cbEjectFunc		64
#endif


//---------------------------------------------------------------------
// global variables
//
int		PID;			// PID of "explorer.exe"
BYTE	*pDataRemote;	// the address where INJDATA & NewProc reside 
						// in the remote process;
						// NewProc = pDataRemote + sizeof(INJDATA);

//---------------------------------------------------------------------
// INJDATA
// Notice: The data structure being injected.
//
typedef LONG		(WINAPI *SETWINDOWLONG)	  (HWND,int,LONG); 
typedef LRESULT		(WINAPI *CALLWINDOWPROC)  (WNDPROC,HWND,UINT,WPARAM,LPARAM);

typedef struct {
	// data initialized by our "InjectEx.exe" before 
	// injecting the structure into "explorer.exe"
	//
	SETWINDOWLONG	fnSetWindowLong;
	CALLWINDOWPROC	fnCallWindowProc;	

	HWND	hwnd;
	WNDPROC	fnNewProc;

	// data initialized by remote InjectFunc
	// and used by remote NewProc
	//
	WNDPROC 		fnOldProc;	
		
} INJDATA, *PINJDATA;


//---------------------------------------------------------------------
// InjectFunc
// Notice: - the remote copy of this function subclasses
//			 the START button;
//
//	Return value: 0 - failure
//				  1 - success
//
static DWORD WINAPI InjectFunc (INJDATA *pData) 
{
	// Subclass START button
	pData->fnOldProc = (WNDPROC)
		pData->fnSetWindowLong ( pData->hwnd, GWL_WNDPROC,(long)pData->fnNewProc );	

	return (pData->fnOldProc != NULL);
}


//---------------------------------------------------------------------
// EjectFunc
// Notice: - the remote copy of this function resets the window
//			 procedure of the START button to its original value;
//
//	Return value: 0 - failure
//				  1 - success
//
static DWORD WINAPI EjectFunc (INJDATA *pData) 
{
	return ( pData->fnSetWindowLong(pData->hwnd, GWL_WNDPROC,(long)pData->fnOldProc)
			!= NULL );
}


//---------------------------------------------------------------------
// NewProc
// Notice: - new window procedure for the START button;
//		   - it swaps the left and right mouse clicks;
//
//	- because we can't pass any parameters to this function, it first 
//	  calculates its own address in the remote process;
//	- ones it knows its own position it also knows the address of the 
//	  INJDATA structure, since INJDATA was placed immediately before it;
//	- NewProc then gets all values that could not be hard-coded into 
//	  it at compile time from INJDATA;
//
//
//	OFFSET = "distance" between the "pop ecx" instruction and 
//			 the entry point of NewProc; 
//	 - this value may change if you change either NewProc or some
//		compiler options (the /O2 switch for example - the default 
//		in release builds - generates a different functions prologue 
//		than the /O1 switch does -> the OFFSET changes too);
//	
//	How to get the correct OFFSET?
//	See accompanying "offset.txt".
//
#ifdef _DEBUG
#define OFFSET 14
#else
#define OFFSET 9
#endif

static LRESULT CALLBACK NewProc(
  HWND hwnd,      // handle to window
  UINT uMsg,      // message identifier
  WPARAM wParam,  // first message parameter
  LPARAM lParam )  // second message parameter
{	
	// calculate the location of INJDATA
	// (remember that INJDATA in the remote process
	// is placed immediately before NewProc)	
	INJDATA* pData;	
	_asm {				
		call	dummy
dummy:
		pop		ecx			// <- ECX contains the current EIP (instruction pointer);
		sub		ecx, OFFSET	// <- ECX contains the address of NewProc;
		mov		pData, ecx
	}
	pData--;


	//-------------------------------------
	// subclassing code starts here
	switch (uMsg) 
	{
		case WM_LBUTTONDOWN: uMsg = WM_RBUTTONDOWN; goto END;
		case WM_LBUTTONUP:	 uMsg = WM_RBUTTONUP;	goto END;	
		case WM_RBUTTONDOWN: uMsg = WM_LBUTTONDOWN; goto END;			
	}
	switch (uMsg) 
	{
		case WM_RBUTTONUP:   uMsg = WM_LBUTTONUP;	goto END;
		case WM_LBUTTONDBLCLK: uMsg = WM_RBUTTONDBLCLK; goto END;
		case WM_RBUTTONDBLCLK: uMsg = WM_LBUTTONDBLCLK; goto END;
	}
END:

	// call original window procedure
	return pData->fnCallWindowProc( pData->fnOldProc, hwnd,uMsg,wParam,lParam );		
}


//---------------------------------------------------------------------
// InjCode
// Notice: - copies InjectFunc, NewProc and INJDATA to the remote process;
//		   - starts the excecution of the remote InjectFunc, which
//			 in turn subclasses the START button;
//
//	Return value: 0 - failure
//				  1 - success
//
int InjCode (HWND hWnd, bool fUnicode)
{
	HANDLE		hProcess = 0; 
	HMODULE		hUser32  = 0;	// user32.dll
	BYTE		*pNewProcRemote;// The address of NewProc in the remote process.
	DWORD		*pCodeRemote;	// The address of InjectFunc in the remote process.
	HANDLE		hThread	   = 0;	// The handle and ID of the thread executing
	DWORD		dwThreadId = 0;	// the remote InjectFunc.

	int			nSuccess	= 0; // Subclassing succeded?
	DWORD dwNumBytesXferred = 0; // Number of bytes written to the remote process.


	hUser32 = GetModuleHandle(__TEXT("user32"));
	if (hUser32 == NULL)
		return 0;

	::GetWindowThreadProcessId( hWnd, (DWORD*)&PID );
	hProcess = ::OpenProcess(	PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | 
								PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
								FALSE, PID);
	if (hProcess == NULL)
		return 0;

	__try {
		// 1. Allocate memory in the remote process for the injected INJDATA & NewProc 
		//	  (note that they are placed right one after the other)
		// 2. Write a copy of NewProc and the initialized INJDATA to the allocated memory
		int size = sizeof(INJDATA) + cbNewProc;

		pDataRemote = (BYTE*) VirtualAllocEx( hProcess, 0, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE );		
		if ( pDataRemote == NULL ) {
			CloseHandle( hProcess );
			return 0;
		}
		pNewProcRemote = pDataRemote + sizeof(INJDATA);
		WriteProcessMemory( hProcess, pNewProcRemote, &NewProc, cbNewProc, &dwNumBytesXferred );
		
		
		// Initialize the INJDATA structure
		INJDATA DataLocal = {
			(SETWINDOWLONG)  GetProcAddress(hUser32, fUnicode ? "SetWindowLongW" : "SetWindowLongA"),
			(CALLWINDOWPROC) GetProcAddress(hUser32, fUnicode ? "CallWindowProcW": "CallWindowProcA"),
			hWnd,
			(WNDPROC) (pNewProcRemote)	// address of NewProc in remote process
		};
		
		if (DataLocal.fnSetWindowLong  == NULL || 			
			DataLocal.fnCallWindowProc == NULL)
			__leave;		
		
		// Copy initialized INJDATA to remote process
		WriteProcessMemory( hProcess, pDataRemote, &DataLocal, sizeof(INJDATA), &dwNumBytesXferred );


		// 1. Allocate memory in the remote process for InjectFunc
		// 2. Write a copy of InjectFunc to the allocated memory
		pCodeRemote = (PDWORD) VirtualAllocEx( hProcess, 0, cbInjectFunc, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
		if ( pCodeRemote == 0 )
			__leave;
		WriteProcessMemory( hProcess, pCodeRemote, &InjectFunc, cbInjectFunc, &dwNumBytesXferred );

			
		// Start execution of remote InjectFunc
		hThread = CreateRemoteThread(hProcess, NULL, 0, 
				(LPTHREAD_START_ROUTINE) pCodeRemote,
				pDataRemote, 0 , &dwThreadId);
		if ( hThread == 0 )
			__leave;

		WaitForSingleObject(hThread, INFINITE);
		GetExitCodeThread(hThread, (PDWORD) &nSuccess);
		if ( nSuccess )
			::MessageBeep(MB_OK);	// success
	}
	__finally {
		if( !nSuccess )		// failed? -> deallocate memory where the 
							// remote INJDATA & NewProc reside;
			VirtualFreeEx( hProcess, pDataRemote, 0, MEM_RELEASE );

		if ( pCodeRemote != 0 )	// deallocate remote InjectFunc
			VirtualFreeEx( hProcess, pCodeRemote, 0, MEM_RELEASE );

		if ( hThread != 0 )			
			CloseHandle(hThread);
	}
	CloseHandle( hProcess );
	return nSuccess;
}


//---------------------------------------------------------------------
// EjectCode
// Notice: - copies EjectFunc to the remote process and starts its execution;
//		   - the remote EjectFunc restores the old window procedure
//			 for the START button;
//
//	Return value: 0 - failure
//				  1 - success
//
int EjectCode ()
{
	HANDLE		hProcess;	
	DWORD		*pCodeRemote;
	HANDLE		hThread = NULL;
	DWORD		dwThreadId = 0;	

	int			nSuccess	= 0;
	DWORD dwNumBytesXferred = 0;

	if (pDataRemote == NULL)
		return 0;
	
	hProcess = ::OpenProcess(	PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | 
								PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
								FALSE, PID);
	if (hProcess == NULL)
		return 0;


	// 1. Allocate memory in the remote process for the EjectFunc
	// 2. Write a copy of EjectFunc to the allocated memory
	pCodeRemote = (PDWORD) VirtualAllocEx( hProcess, 0, cbEjectFunc, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	if( pCodeRemote == NULL ) {
		CloseHandle( hProcess );
		return 0;
	}
	WriteProcessMemory( hProcess, pCodeRemote, &EjectFunc, cbEjectFunc, &dwNumBytesXferred );
		
	// Start execution of the remote EjectFunc
	hThread = CreateRemoteThread(hProcess, NULL, 0, 
			(LPTHREAD_START_ROUTINE) pCodeRemote,
			pDataRemote, 0 , &dwThreadId);
	if ( hThread == NULL )
		goto END;

	WaitForSingleObject(hThread, INFINITE);	
	GetExitCodeThread(hThread, (PDWORD) &nSuccess);	

	if ( nSuccess == NULL )		// Failed to restore old window procedure?
		goto END;				// Then leave INJDATA and the NewProc (both 
								// are residing at the location pDataRemote,
								// on after the other) in the remote process!!

	if ( pDataRemote != NULL ) {
		VirtualFreeEx( hProcess, pDataRemote, 0, MEM_RELEASE );
		pDataRemote = NULL;
	}
	::MessageBeep(MB_OK);		// success

END:		
	if ( hThread != NULL )
		CloseHandle( hThread );

	VirtualFreeEx( hProcess, pCodeRemote, 0, MEM_RELEASE );
	CloseHandle( hProcess );

	return nSuccess;
}

///////////////////////////////////////////////////////////////////////////

int InjectCodeA (HWND hWnd)
{	
	return InjCode (hWnd, false);
}

int InjectCodeW (HWND hWnd)
{
	return InjCode (hWnd, true);
}

//////////////////////// End Of File //////////////////////////////////////

