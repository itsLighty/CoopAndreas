#include "stdafx.h"
#include "CommandHooks.h"
#include "CCustomCommandMgr.h"
#include "Commands/CCommandAddChatMessage.h"

uint16_t nCommand = 0x0;
CRunningScript* pScript = nullptr;

bool ProcessCustomCommand()
{
	uint16_t commandNormalised = (nCommand & 0x7FFF);
	if (!CCustomCommandMgr::IsCustomCommand(commandNormalised))
	{
		return false;
	}

	if (!CCustomCommandMgr::HasCommand(commandNormalised))
	{
		char message[160];
		sprintf_s(message, sizeof message,
			"Unregistered custom opcode [%X] script name '%s', base ip '%p', cur ip '%p'", commandNormalised,
			pScript->m_szName, pScript->m_pBaseIP, pScript->m_pCurrentIP);
		MessageBoxA(nullptr, message, "Invalid opcode processing", MB_ICONERROR);

		// The parameter count of an unknown opcode is unknowable, so continuing
		// would desynchronize the instruction pointer. Stop only this script.
		pScript->m_bIsActive = false;
		return true;
	}

	pScript->m_pCurrentIP += 2;
	pScript->m_bNotFlag = (nCommand & 0x8000) != 0;
	return CCustomCommandMgr::ProcessCommand(commandNormalised, pScript);
}


void __declspec(naked) CRunningScript__Process_Hook() 
{
	__asm
	{
		mov ax, [ecx]

		mov nCommand, ax
		mov pScript, esi

		push eax
		push ecx
		push esi

		call ProcessCustomCommand

		test al, al
		jz process_orig_opcode 

		pop esi
		pop ecx
		pop eax
		push 0x469FB0
		ret

	process_orig_opcode:
		pop esi
		pop ecx
		pop eax
		push 0x469FBF
		ret
	}
}

//uintptr_t CRunningScript__ProcessOneCommand_Ret = 0x469EBF;
//uintptr_t CRunningScript__ProcessOneCommand_Exit = 0x469EF8;
//void __declspec(naked) CRunningScript__ProcessOneCommand_Hook()
//{
//	__asm
//	{
//		xor eax, eax
//		mov ax, [edx]
//
//		mov nCommand, ax
//		mov pScript, ecx
//
//		pushad
//		pushfd
//	}
//
//	if (ProcessCustomCommand(nCommand, pScript))
//	{
//		__asm
//		{
//			popfd
//			popad
//
//			jmp CRunningScript__ProcessOneCommand_Exit
//		}
//	}
//
//	__asm
//	{
//		popfd
//		popad
//
//		jmp CRunningScript__ProcessOneCommand_Ret
//	}
//}

void CommandHooks::InjectHooks()
{
	patch::RedirectJump(0x469FBA, CRunningScript__Process_Hook);
	///patch::RedirectJump(0x469EBA, CRunningScript__ProcessOneCommand_Hook);
}
