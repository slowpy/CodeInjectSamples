/***************************************************************
Module name: InjectEx.cpp
Copyright (c) 2003 Robert Kuster

Notice:	If this code works, it was written by Robert Kuster.
		Else, I don't know who wrote it.

		Use it on your own risk. No responsibilities for
		possible damages of even functionality can be taken.
***************************************************************/

#include <windows.h>

#include "InjCode.h"
#include "resource.h"

//-----------------------------------------------
// shared data
//
#pragma data_seg (".shared")
int  g_count = 0;
#pragma data_seg ()

#pragma comment(linker,"/SECTION:.shared,RWS")


//-----------------------------------------------
// global variables & forward declarations
//
HWND	hStart;		// handle to start button
int		bInj = 0;	// code already injected?

BOOL CALLBACK MainDlgProc (HWND,UINT,WPARAM,LPARAM);


//-----------------------------------------------
// WinMain
//
int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{	
	if( !g_count )
		g_count++;
	else {
		::MessageBox( NULL, TEXT("Only one instance at once"),TEXT("  Warning"),MB_OK );
		return 0;
	}

	hStart = ::FindWindow (TEXT("Shell_TrayWnd"),NULL);			// get HWND of taskbar first
	hStart = ::FindWindowEx (hStart, NULL,TEXT("BUTTON"),NULL);	// get HWND of start button
	
	// display main dialog 
	::DialogBoxParam (hInstance, MAKEINTRESOURCE (IDD_WININFO), NULL, MainDlgProc, NULL);

	return 0;
}


//-----------------------------------------------
// MainDlgProc
// Notice: dialog procedure
//
BOOL CALLBACK MainDlgProc (HWND hDlg,	// handle to dialog box
						   UINT uMsg,      // message
						   WPARAM wParam,  // first message parameter
						   LPARAM lParam ) // second message parameter
{
	switch (uMsg) {
	case WM_COMMAND:
		if (!bInj) {
			bInj = InjectCode( hStart );
			if (bInj)
				::SetDlgItemText( hDlg, IDC_BUTTON, TEXT("Eject") );
		}
		else {
			bInj = !EjectCode();
			if (!bInj)
				::SetDlgItemText( hDlg, IDC_BUTTON, TEXT("Let's Inject") );
		}
		break;

	case WM_CLOSE:
		// be polite and restore the old window procedure (but  
		// note that we could leave the START button subclassed, too)
		if( bInj ) 
			bInj = !EjectCode( );

		if( bInj )
			::MessageBox( NULL, TEXT("Injected code could not be removed"),TEXT("  Error"),MB_OK );

		::EndDialog (hDlg, 0);
		break;		
	}

	return false;
}