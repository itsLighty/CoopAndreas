#pragma once

#include "CCustomCommand.h"

#include <cstddef>

class CCustomCommandMgr
{
public:
	static constexpr uint16_t MIN_CUSTOM_COMMAND = 0x1D00;
	static constexpr uint16_t MAX_CUSTOM_COMMAND = 0x1DFF;
	static constexpr std::size_t CUSTOM_COMMAND_COUNT = MAX_CUSTOM_COMMAND - MIN_CUSTOM_COMMAND + 1;

	static inline CCustomCommand* m_commands[CUSTOM_COMMAND_COUNT]{};

	static bool IsCustomCommand(uint16_t opcode)
	{
		return opcode >= MIN_CUSTOM_COMMAND && opcode <= MAX_CUSTOM_COMMAND;
	}

	static bool HasCommand(uint16_t opcode)
	{
		return IsCustomCommand(opcode) && m_commands[opcode - MIN_CUSTOM_COMMAND] != nullptr;
	}

	static void RegisterCommand(uint16_t opcode, CCustomCommand* command)
	{
		assert(IsCustomCommand(opcode));
		assert(command != nullptr);
		if (!IsCustomCommand(opcode) || command == nullptr)
		{
			return;
		}

		std::size_t idx = opcode - MIN_CUSTOM_COMMAND;

		assert(m_commands[idx] == nullptr);
		if (m_commands[idx] != nullptr)
		{
			return;
		}
		m_commands[idx] = command;
	}

	static bool ProcessCommand(uint16_t opcode, CRunningScript* script)
	{
		if (!HasCommand(opcode) || script == nullptr)
		{
			return false;
		}

		std::size_t idx = opcode - MIN_CUSTOM_COMMAND;
		m_commands[idx]->Process(script);
		return true;
	}
};

