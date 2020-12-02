#include "routine_mgr.hpp"
#include "dx86.hpp"

static bool is_code(uintptr_t address)
{
	if (*reinterpret_cast<uint64_t*>(address) == 0 
	 && *reinterpret_cast<uint64_t*>(address + sizeof(uint64_t)) == 0
	){
		return false;
	}

	return true;
}

namespace routine_mgr
{
	std::vector<routine*>routines = {};

	routine::routine()
	{
		args_count = 0;
		old_function = 0;
		new_function = 0;
	}

	routine::routine(uintptr_t address, size_t n_args)
	{
		args_count = n_args;
		old_function = address;
		new_function = 0;
	}

	routine::~routine()
	{
		if (new_function != 0)
		{
			VirtualFree(reinterpret_cast<void*>(new_function), 0, MEM_RELEASE);
		}
	}

	uintptr_t routine::create()
	{
		new_function = create_routine(old_function, args_count);
		routines.push_back(this);
		return new_function;
	}


	conv get_conv(uintptr_t func, size_t n_expected_args)
	{
		// go to the very last epilogue of the function.
		// This marks the absolute end of the function.
		// 
		uintptr_t epilogue = get_prologue<next>(func + 1);

		while (get_return(epilogue) == -1)
		{
			epilogue--;
		}

		conv convention = ___cdecl; // default

		uintptr_t args = 0;
		uintptr_t func_start = func;
		uintptr_t func_end = epilogue;

		// determine what the convention is based on
		// the value of the return
		// 
		if (get_return(epilogue) > 0)
		{
			convention = ___stdcall;
			func_end += 2;
		}
		else
		{
			convention = ___cdecl;
		}

		// search for the highest ebp offset, which will 
		// indicate the number of args that were pushed
		// on the stack, rather than placed in ECX/EDX

		while (func_start < func_end)
		{
			auto i = disassembler::read(func_start);

			if (i.flags & OP_SRC_DEST || i.flags & OP_SINGLE)
			{
				auto src = i.src();
				auto dest = i.dest();

				if (dest.flags & OP_R32)
				{
					if (dest.flags & OP_IMM8 && dest.reg[0] == disassembler::R32_EBP && dest.imm8 != 4 && dest.imm8 < 0x7F)
					{
						// printf("arg offset: %02X\n", dest.imm8);

						if (dest.imm8 > args)
						{
							args = dest.imm8;
						}
					}
				}

				if (src.flags & OP_R32)
				{
					if (src.flags & OP_IMM8 && src.reg[0] == disassembler::R32_EBP && src.imm8 != 4 && src.imm8 < 0x7F)
					{
						// printf("arg offset: %02X\n", src.imm8);

						if (src.imm8 > args)
						{
							args = src.imm8;
						}
					}
				}
			}

			func_start += i.len;
		}

		// no pushed args were used, but we know there
		// is a 1 or 2 EBP arg difference, so it is either
		// a fastcall or a thiscall
		if (args == 0)
		{
			switch (n_expected_args)
			{
			case 1:
				return ___thiscall;
				break;
			case 2:
				return ___fastcall;
				break;
			}
		}

		args -= 8;
		args = (args / 4) + 1;

		if (args == n_expected_args - 1)
		{
			convention = ___thiscall;
		}
		else if (args == n_expected_args - 2)
		{
			convention = ___fastcall;
		}

		return convention;
	}


	uintptr_t create_routine(uintptr_t func, size_t n_args)
	{
		size_t size = 0;
		uint8_t data[128];

		auto new_func = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (new_func == NULL)
		{
			printf("Error while allocating memory\n");
			
			return func;
		}

		data[size++] = 0x55; // push ebp

		data[size++] = 0x8B; // mov ebp,esp
		data[size++] = 0xEC;

		switch (get_conv(func, n_args))
		{ 
		case ___cdecl:
		{
			for (int i = (n_args * 4) + 8; i > 8; i -= 4)
			{
				data[size++] = 0xFF; // push [ebp+??]
				data[size++] = 0x75;
				data[size++] = i - 4;
			}

			data[size++] = 0xE8; // call func
			*reinterpret_cast<uintptr_t*>(data + size, func - (new_func + size + 4));
			size += 4;

			data[size++] = 0x83; // add esp, (n_args * 4)
			data[size++] = 0xC4;
			data[size++] = n_args * 4;

			break;
		}
		case ___stdcall:
		{
			for (int i = (n_args * 4) + 8; i > 8; i -= 4)
			{
				data[size++] = 0xFF; // push [ebp+??]
				data[size++] = 0x75;
				data[size++] = i - 4;
			}

			data[size++] = 0xE8; // call func
			*reinterpret_cast<uintptr_t*>(data + size, func - (new_func + size + 4));
			size += 4;

			break;
		}
		case ___thiscall:
		{
			data[size++] = 0x51; // push ecx

			for (int i = n_args; i > 1; i--)
			{
				data[size++] = 0xFF; // push [ebp+??]
				data[size++] = 0x75;
				data[size++] = (i + 1) * 4;
			}

			data[size++] = 0x8B; // mov ecx,[ebp+08]
			data[size++] = 0x4D;
			data[size++] = 0x08;

			data[size++] = 0xE8; // call func
			*reinterpret_cast<uintptr_t*>(data + size, func - (new_func + size + 4));
			size += 4;

			data[size++] = 0x59; // pop ecx

			break;
		}
		case ___fastcall:
		{
			data[size++] = 0x51; // push ecx
			data[size++] = 0x52; // push edx

			for (int i = n_args; i > 2; i--)
			{
				data[size++] = 0xFF; // push [ebp+??]
				data[size++] = 0x75;
				data[size++] = (i + 1) * 4;
			}

			data[size++] = 0x8B; // mov ecx,[ebp+08]
			data[size++] = 0x4D;
			data[size++] = 0x08;

			data[size++] = 0x8B; // mov edx,[ebp+0C]
			data[size++] = 0x55;
			data[size++] = 0x0C;

			data[size++] = 0xE8; // call func
			*reinterpret_cast<uintptr_t*>(data + size, func - (new_func + size + 4));
			size += 4;

			data[size++] = 0x59; // pop ecx
			data[size++] = 0x5A; // pop edx

			break;
		}
		}

		data[size++] = 0x5D; // pop ebp
		data[size++] = 0xC3; // retn
		
		// if we wanted to convert to __stdcall...
		//data[size++] = 0xC2; // ret xx
		//data[size++] = n_args * 4;
		//data[size++] = 0x00;
		
		memcpy(reinterpret_cast<void*>(new_func), &data, size);

		return new_func;
	}

	void flush()
	{
		for (const auto& x : routines)
		{
			x->~routine();
		}

		routines.clear();
	}
}
