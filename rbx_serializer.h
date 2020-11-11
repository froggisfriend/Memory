#pragma once
#include <Windows.h>
#include "../Transpiler/rbx_transpiler.h"
#include <sstream>
#include <iostream>
#include <vector>
#include <string>

extern "C"
{
#include "../../Lua/lstate.h"
#include "../../Lua/lauxlib.h"
#include "../../Lua/lualib.h"
}

enum {
	ConstantNil,
	ConstantBoolean,
	ConstantNumber,
	ConstantString,
	ConstantCache,
	ConstantTable
};

namespace stream
{
	void writeByte(std::ostringstream& ss, BYTE value);
	void writeInt(std::ostringstream& ss, int value);
	void writeDouble(std::ostringstream& ss, double value);
	void writeCompressedInt(std::ostringstream& ss, int value);
}

namespace Celery
{
	namespace Rbx
	{
		namespace Serializer
		{
			void writeProto(lua_State*, std::ostringstream&, Proto*, std::vector<Proto*>, std::vector<const char*>, std::map<int, int>, std::vector<int>);
			void getProtos(Proto*, std::vector<Proto*>& prototable);
			std::string load(lua_State*, Proto*);
			std::string Serialize(std::string source);
		}
	}
}
