////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IKInputKeyDefine.h
//  Creator     : HuaFei
//  Create Date : 2020
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

enum class EInputKey
{
	_None = 0,

	MouseLButton = 1,
	MouseRButton = 2,
	Cancel = 3,
	MouseMButton = 4,

	BackSpace = 8,
	Tab = 9,
	
	Clear = 0x0C,
	Return = 0x0D,

	Shift = 0x10,
	Control = 0x11,
	Menu = 0x12,
	Pause = 0x13,
	Capital = 0x14,

	Kana = 0x15,
	Hangeul = 0x15,  /* old name - should be here for compatibility */
	Hangul = 0x15,

	JunJa = 0x17,
	Final = 0x18,
	HanJa = 0x19,
	KanJi = 0x19,
	
	Escape = 0x1B,

	Convert = 0x1C,
	NonConvert = 0x1D,
	Accept = 0x1E,
	ModeChange = 0x1F,

	/* Printable keys */
	Space = 0x20,
	Prior = 0x21,
	Next = 0x22,
	End = 0x23,
	Home = 0x24,
	Left = 0x25,
	Up = 0x26,
	Right = 0x27,
	Down = 0x28,
	Select = 0x29,
	Print = 0x2A,
	Execute = 0x2B,
	Snapshot = 0x2C,
	Insert = 0x2D,
	Delete = 0x2E,
	Help = 0x2F,

	// Above the keyboard
	Num0 = 48,
	Num1,
	Num2,
	Num3,
	Num4,
	Num5,
	Num6,
	Num7,
	Num8,
	Num9 = 57,
	Semicolon = 59,/*;*/
	Equal = 61, /*=*/
	// A through Z and numbers 0 through 9.
	A = 65,
	B,
	C,
	D,
	E,
	F,
	G,
	H,
	I,
	J,
	K,
	L,
	M,
	N,
	O,
	P,
	Q,
	R,
	S,
	T,
	U,
	V,
	W,
	X,
	Y,
	Z = 90,

	a = 97,
	b,
	c,
	d,
	e,
	f,
	g,
	h,
	i,
	j,
	k,
	l,
	m,
	n,
	o,
	p,
	q,
	r,
	s,
	t,
	u,
	v,
	w,
	x,
	y,
	z,

	Sleep = 0x5F,

	NumPad0 = 0x60,
	NumPad1 = 0x61,
	NumPad2 = 0x62,
	NumPad3 = 0x63,
	NumPad4 = 0x64,
	NumPad5 = 0x65,
	NumPad6 = 0x66,
	NumPad7 = 0x67,
	NumPad8 = 0x68,
	NumPad9 = 0x69,
	Multiply = 0x6A,
	Add = 0x6B,
	Separator = 0x6C,
	Subtract = 0x6D,
	Decimal = 0x6E,
	Divide = 0x6F,
	F1 = 0x70,
	F2 = 0x71,
	F3 = 0x72,
	F4 = 0x73,
	F5 = 0x74,
	F6 = 0x75,
	F7 = 0x76,
	F8 = 0x77,
	F9 = 0x78,
	F10 = 0x79,
	F11 = 0x7A,
	F12 = 0x7B,
	F13 = 0x7C,
	F14 = 0x7D,
	F15 = 0x7E,
	F16 = 0x7F,
	F17 = 0x80,
	F18 = 0x81,
	F19 = 0x82,
	F20 = 0x83,
	F21 = 0x84,
	F22 = 0x85,
	F23 = 0x86,
	F24 = 0x87,
	
	LShift = 0xA0,
	RShift = 0xA1,
	LControl = 0xA2,
	RControl = 0xA3,
	LMenu = 0xA4,	//LALT
	RMenu = 0xA5,	//RALT
	
	// Total number of keys.
	KeyCount
};
