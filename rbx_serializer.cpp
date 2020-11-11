#include "rbx_serializer.h"
#include "../Transpiler/rbx_transpiler.h"


namespace stream
{
	void writeByte(std::ostringstream& ss, BYTE value)
	{
		ss.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}
	
	void writeChar(std::ostringstream& ss, CHAR value)
	{
		ss.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	void writeInt(std::ostringstream& ss, int value)
	{
		ss.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	void writeDouble(std::ostringstream& ss, double value)
	{
		ss.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	void writeCompressedInt(std::ostringstream& ss, int value)
	{
		do
		{
			int v = value & 0x7F;
			value >>= 7;

			if (value)
				v |= 0x80;

			writeByte(ss, v);
		} while (value);
	}

}


namespace Celery
{
	namespace Rbx
	{
		namespace Serializer
		{
			void writeProto(lua_State* L, std::ostringstream& bytecode, Proto* p, std::vector<Proto*> prototable, std::vector<const char*> stringstable, std::map<int, int> stringsmap, std::vector<int>protomap)
			{
				#if USE_GETGLOBAL_OPT == TRUE

				auto ktable = std::vector<TValue>();
				auto oldstringsmap = stringsmap;

				stringsmap.clear();

				Transpiler::GlobalCaches = std::vector<Transpiler::GlobalCache>(p->sizek);

				for (int i = 0, caches = 0; i < p->sizek; i++)
				{
					int encoding = 0;
					int newindex = i + caches;

					ktable.push_back(p->k[i]);

					if (p->k[i].tt == LUA_TSTRING)
					{
						std::string name = getstr(reinterpret_cast<TString*>(p->k[i].value.gc));

						stringsmap[newindex] = oldstringsmap[i];

						if (name == "printidentity"
						 || name == "print"
						 || name == "warn"
						 || name == "error"
						){
							if (newindex < MAXARG_B && newindex < MAXARG_C)
							{
								encoding = ENCODE_CACHE(newindex);
								caches++;
								newindex++;
								
								TValue x;
								x.tt = LUA_TCACHE;
								x.value.n = static_cast<lua_Number>(encoding);
								ktable.push_back(x);

								printf("p->k[%s] index: %i, optimized index: %i\n", name.c_str(), i, newindex);
							}
						}

						Transpiler::GlobalCaches[i] = Transpiler::GlobalCache({
							i,				// realindex
							newindex,		// newindex
							encoding,		// encoding
							p->k[i]			// original TValue reference
						});
					}
				}

				// Set p->sizek to the updated constants table size
				p->sizek = ktable.size();

				// allocate the new constants table
				p->k = luaM_newvector(L, p->sizek, TValue);

				for (size_t i = 0; i < ktable.size(); i++)
				{
					p->k[i] = ktable[i];
				}
				
				#endif


				auto new_maxstacksize = p->maxstacksize;
				auto new_lineinfo = std::vector<int>();
				auto new_sizes = std::vector<size_t>();
				auto r_instructions = Celery::Rbx::Transpiler::convert_code(p, new_lineinfo, new_sizes, new_maxstacksize);
				
				#if USE_GETGLOBAL_OPT == TRUE
				// Perform possible cleanups...
				ktable.clear();
				Transpiler::GlobalCaches.clear();
				#endif


				stream::writeByte(bytecode, new_maxstacksize);
				stream::writeByte(bytecode, p->numparams);
				stream::writeByte(bytecode, p->nups);
				stream::writeByte(bytecode, p->is_vararg);

				stream::writeCompressedInt(bytecode, r_instructions.size());

				for (size_t i = 0; i < r_instructions.size(); i++)
				{
					stream::writeInt(bytecode, r_instructions[i]);
				}


				stream::writeCompressedInt(bytecode, p->sizek);

				for (int i = 0; i < p->sizek; i++)
				{
					auto o = &p->k[i];
					switch (o->tt)
					{
					case LUA_TNIL:
						stream::writeByte(bytecode, ConstantNil);
						break;
					case LUA_TBOOLEAN:
						stream::writeByte(bytecode, ConstantBoolean);
						stream::writeByte(bytecode, bvalue(o));
						break;
					case LUA_TNUMBER:
						stream::writeByte(bytecode, ConstantNumber);
						stream::writeDouble(bytecode, nvalue(o));
						break;
					case LUA_TSTRING:
						stream::writeByte(bytecode, ConstantString);
						stream::writeCompressedInt(bytecode, stringsmap[i]);
						break;
					case LUA_TCACHE:
						stream::writeByte(bytecode, ConstantCache);
						stream::writeInt(bytecode, static_cast<int>(nvalue(o)));
						break;
					}
				}

				stream::writeCompressedInt(bytecode, protomap.size());

				for (int proto_id : protomap)
				{
					stream::writeCompressedInt(bytecode, proto_id);
				}

				stream::writeCompressedInt(bytecode, 0); // name of this function (ID in stringstable)
				stream::writeByte(bytecode, USE_LINEINFO); // use lineinfo

				#if USE_LINEINFO == TRUE

				int compressKey = 0;

				#if OPTIMIZE_LINEINFO == TRUE
				compressKey = 0x18;

				// Determine whether to compress lineinfo
				for (int i = 1; i < new_lineinfo.size(); i++)
				{
					if (new_lineinfo[i] - new_lineinfo[i - 1] > UCHAR_MAX)
					{
						compressKey = 0;
						break;
					}
				}
				#endif

				int lineinfo_at = 0;
				int prev_lineinfo;
				int diff = 0;

				if (compressKey)
				{
					stream::writeByte(bytecode, compressKey);

					int n = (new_lineinfo.size() + 3) & 0xFFFFFFFC;
					int index = ((new_lineinfo.size() - 1) >> compressKey) + 1;
					//int x = n + 4 * index; //virtual index

					prev_lineinfo = new_lineinfo[lineinfo_at];

					while (lineinfo_at < new_lineinfo.size())
					{
						if (new_lineinfo[lineinfo_at] == prev_lineinfo)
						{
							stream::writeByte(bytecode, 0);
						}
						else {
							diff = new_lineinfo[lineinfo_at] - prev_lineinfo;

							stream::writeByte(bytecode, diff);
						}

						prev_lineinfo = new_lineinfo[lineinfo_at++];
					}

					for (int i = 0; i < index; i++)
					{
						stream::writeInt(bytecode, new_lineinfo[0]);
					}
				}
				else 
				{
					stream::writeByte(bytecode, compressKey);

					// Leave small lineinfos blank
					for (int i = 0; i < new_lineinfo.size(); i++)
					{
						stream::writeByte(bytecode, 0);
					}

					prev_lineinfo = 0;

					// Use large lineinfos
					while (lineinfo_at < new_lineinfo.size())
					{
						if (new_lineinfo[lineinfo_at] == prev_lineinfo)
						{
							stream::writeInt(bytecode, 0);
						}
						else {
							diff = new_lineinfo[lineinfo_at] - prev_lineinfo;

							if (lineinfo_at + 1 < new_lineinfo.size())
							{
								auto next_diff = new_lineinfo[lineinfo_at + 1] - new_lineinfo[lineinfo_at];

								if (next_diff < 0)
								{
									diff += next_diff;
								}
							}

							if (diff < 0)
							{
								diff = 0;
							}
							
							stream::writeInt(bytecode, diff);
						}

						prev_lineinfo = new_lineinfo[lineinfo_at++];
					}
				}

				#endif



				if (!(p->sizelocvars || p->sizeupvalues))
				{
					stream::writeByte(bytecode, 0);
				} 
				else 
				{
					stream::writeByte(bytecode, USE_DEBUGINFO);


					#if USE_DEBUGINFO == TRUE
					stream::writeCompressedInt(bytecode, p->sizelocvars);

					for (int i = 0; i < p->sizelocvars; i++)
					{
						std::string name = getstr(p->locvars[i].varname);

						int string_id = 0;

						for (string_id; string_id < stringstable.size(); string_id++)
						{
							if (stringstable[string_id] == name)
							{
								printf("Matched locvar %s with string ID: %i\n", name.c_str(), string_id + 1);

								break;
							}
						}

						stream::writeCompressedInt(bytecode, string_id + 1);
						stream::writeCompressedInt(bytecode, new_sizes[p->locvars[i].endpc]);
						stream::writeCompressedInt(bytecode, new_sizes[p->locvars[i].startpc]);
						stream::writeByte(bytecode, 0);
					}


					stream::writeCompressedInt(bytecode, p->sizeupvalues);

					for (int i = 0; i < p->sizeupvalues; i++)
					{
						std::string name = getstr(&p->upvalues[i]->tsv);

						int string_id = 0;

						for (string_id; string_id < stringstable.size(); string_id++)
						{
							if (stringstable[string_id] == name)
							{
								printf("Matched upvalue %s with string ID: %i\n", name.c_str(), string_id + 1);

								break;
							}
						}

					}
					#endif
				}
			}

			void getProtos(Proto* p, std::vector<Proto*>& prototable)
			{
				for (int i = p->sizep - 1; i >= 0; i--)
				{
					if (p->p[i]->sizep)
					{
						getProtos(p->p[i], prototable);
					}

					prototable.push_back(p->p[i]);
				}
			}

			std::string load(lua_State* L, Proto* p)
			{
				std::ostringstream bytecode;

				auto prototable = std::vector<Proto*>();
				auto stringstable = std::vector<const char*>();


				getProtos(p, prototable); // push back every nested proto
				// we start from the ABSOLUTE last proto created,
				// then work our way up to the initial/main proto

				prototable.push_back(p); // push back the main proto last


				auto protomap = std::vector<std::vector<int>>(prototable.size());
				auto stringsmap = std::vector<std::map<int, int>>(prototable.size());

				// Load the string table
				for (size_t at = 0; at < prototable.size(); at++)
				{
					Proto* p = prototable[at];

					for (int i = 0; i < p->sizep; i++)
					{
						for (int j = 0; j < prototable.size(); j++)
						{
							if (p->p[i] == prototable[j] && p->p[i] != p)
							{
								// protomap[proto_id] ---> { 1, 5, 7, ... } <--- nested protos
								protomap[at].push_back(j);
							}
						}
					}

					for (int i = 0; i < p->sizek; i++)
					{
						if (p->k[i].tt == LUA_TSTRING)
						{
							const char* str = getstr(reinterpret_cast<TString*>(p->k[i].value.gc));

							stringstable.push_back(str);

							// constant id ---> id(+ 1) for the string in stringstable
							stringsmap[at][i] = stringstable.size();
						}
					}

					#if USE_DEBUGINFO == TRUE
					for (int i = 0; i < p->sizelocvars; i++)
					{
						#if DEBUG_SERIALIZER == TRUE
						printf("Added locvar (name: `%s`)\n", getstr(p->locvars[i].varname));
						#endif
						
						stringstable.push_back(getstr(p->locvars[i].varname));
					}

					for (int i = 0; i < p->sizeupvalues; i++)
					{
						#if DEBUG_SERIALIZER == TRUE
						printf("Added upvalue (name: `%s`)\n", getstr(&p->upvalues[i]->tsv));
						#endif
						
						stringstable.push_back(getstr(&p->upvalues[i]->tsv));
					}
					#endif
				}

				// Start writing bytecode...
				stream::writeByte(bytecode, 1);

				// Write the string table length
				stream::writeCompressedInt(bytecode, stringstable.size());

				// Write the string table
				for (size_t s = 0; s < stringstable.size(); s++)
				{
					int str_length = lstrlenA(stringstable[s]);

					stream::writeCompressedInt(bytecode, str_length);

					// Write the string
					for (int at = 0; at < str_length; at++)
					{
						stream::writeByte(bytecode, static_cast<BYTE>(stringstable[s][at]));
					}
				}

				// # of protos
				stream::writeCompressedInt(bytecode, prototable.size());

				// Write all of the protos, including the initial proto.
				// We put our initial proto at the very end 
				// (its only a matter of preference)
				//for (int i = prototable.size() - 1; i >= 0; i--)
				for (size_t i = 0; i < prototable.size(); i++)
				{
					writeProto(L, bytecode, prototable[i], prototable, stringstable, stringsmap[i], protomap[i]);
				}

				// initial proto ID
				stream::writeCompressedInt(bytecode, prototable.size() - 1); // last one (the main one)

				return bytecode.str();
			}


			std::string Serialize(std::string source)
			{
				lua_State* L = lua_open();
				luaL_openlibs(L);

				if (luaL_loadbuffer(L, source.c_str(), source.length(), "=TempScript") == 0)
				{
					LClosure* lclosure = (LClosure*)(L->top - 1);
					Proto* p = lclosure->p;

					std::string output = Serializer::load(L, p);

					lua_close(L);
					return output;
				}

				lua_close(L);
				return "";
			}
		}


	}
}
