/***************************************************************
Module name: InjCode.h
Copyright (c) 2003 Robert Kuster

Notice:	If this code works, it was written by Robert Kuster.
		Else, I don't know who wrote it.

		Use it on your own risk. No responsibilities for
		possible damages of even functionality can be taken.
***************************************************************/
#if !defined INJCODE_EX_H
#define INJCODE_EX_H


//--------------------------------
// InjectCode & EjectCode
// Return values: 
//		0 - failure
//		1 - success
int InjectCodeA (HWND hWnd);
int InjectCodeW (HWND hWnd);
int EjectCode ();


#ifdef UNICODE
#define InjectCode InjectCodeW
#else
#define InjectCode InjectCodeA
#endif // !UNICODE

#endif // !defined(INJCODE_EX_H)

