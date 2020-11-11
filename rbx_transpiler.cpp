#include "rbx_transpiler.h"

#define DEBUG_TRANSPILER 0



namespace Celery
{
	namespace Rbx
	{
		namespace Transpiler
		{
			std::vector<GlobalCache> GlobalCaches;

			BOOL break_transpiler = 0;


			std::vector<uint32_t> convert_code(Proto* p, std::vector<int>& new_lineinfo, std::vector<size_t>& new_sizes, lu_byte& new_maxstacksize)
			{
				auto r_insts = std::vector<uint32_t>();

				if (p->sizecode == 0)
				{
					return r_insts;
				}

				// vanilla code, that we may tamper
				auto vanilla_code = std::vector<Instruction>(); 

				auto pairsprep_locs = std::vector<BOOL>(p->sizecode);
				auto relocations = std::vector<Relocation>();
				auto self_relocations = std::vector<SelfRelocation>();

				auto selfmap = std::map<int, SelfRelocation>();
				auto selfcomplete = std::map<int, bool>();

				auto OPTINDEX = [](auto i)
				{
					#if USE_GETGLOBAL_OPT == TRUE
					return GlobalCaches[i].newindex;
					#else
					return i;
					#endif
				};

				// Step through the vanilla code, before transpilation
				// Rebuild the entire vanilla code table.
				// 
				// We cannot simply remove an instruction here
				// Unless we completely readjust the stack later on.
				// 
				// We can make minor adjustments or prepare relocations
				// 
				for (int at = 0; at < p->sizecode; at++)
				{
					auto i = p->code[at];

					// The following works beautifully...
					// but it's forgetting about the LOADK's
					// we add ourselves throughout the 
					// transpilation :(
					// 
					#if USE_GETGLOBAL_OPT == TRUE

					auto kB = OPTINDEX(INDEXK(GETARG_B(i)));
					auto kC = OPTINDEX(INDEXK(GETARG_C(i)));
					auto kBx = GETARG_Bx(i);

					switch (GET_OPCODE(i))
					{
					case OP_LOADK:
					case OP_GETGLOBAL:
					case OP_SETGLOBAL:
						//printf("optimizing instruction. old index: %i. optimized index: %i\n", kBx, GlobalCaches[kBx].newindex);

						//SETARG_Bx(i, GlobalCaches[static_cast<uint16_t>(kBx)].newindex);

						break;
					case OP_SELF:
					case OP_GETTABLE:
						if (ISK(GETARG_C(i)))
						{
							//SETARG_C(i, GlobalCaches[kC].newindex);
							SETARG_C(i, GETARG_C(i) | BITRK);
						}

						break;
					case OP_SETTABLE:
					case OP_EQ:
					case OP_LE:
					case OP_LT:
						if (ISK(GETARG_B(i)))
						{
							//SETARG_B(i, GlobalCaches[kB].newindex);
							SETARG_B(i, GETARG_B(i) | BITRK);
						}

						if (ISK(GETARG_C(i)))
						{
							//SETARG_C(i, GlobalCaches[kC].newindex);
							SETARG_C(i, GETARG_C(i) | BITRK);
						}

						break;
					case OP_ADD:
					case OP_SUB:
					case OP_MUL:
					case OP_DIV:
					case OP_POW:
					case OP_MOD:
						if (ISK(GETARG_B(i)))
						{
							//SETARG_B(i, GlobalCaches[kB].newindex);
							SETARG_B(i, GETARG_B(i) | BITRK);
						}

						if (ISK(GETARG_C(i)) - 256)
						{
							//SETARG_C(i, GlobalCaches[kC].newindex);
							SETARG_C(i, GETARG_C(i) | BITRK);
						}

						break;
					}

					// apply changes in original code
					p->code[at] = i;
					
					#endif

					switch (GET_OPCODE(i))
					{

					case OP_SELF:
						break;
					case OP_CALL: case OP_TAILCALL:
						// Goal: Go from the current CALL instruction
						// backwards, until we hit an OP_SELF whos A
						// arg is the same as this OP_CALL's A arg.
						// You might be thinking this is a terrible idea
						// but it actually works better than if we started
						// at the OP_SELF and looked for a CALL. here's why:
						// 
						// Once we do hit an OP_SELF, the OP_SELF
						// HAS to belong to this call.
						// Once we do this, we MARK the OP_SELF as
						// completed, meaning it cannot be used
						// for ANY other OP_CALL's that we're looking
						// behind on
						// 
						for (int j = at; j >= 0; j--)
						{
							if (!selfcomplete[j] && GET_OPCODE(p->code[j]) == OP_SELF && GETARG_A(p->code[j]) == GETARG_A(i))
							{
								selfcomplete[j] = true;

								vanilla_code.push_back(p->code[j]);

								auto rel = SelfRelocation();
								rel.self = p->code[j];
								rel.selfindex = j;
								rel.call = i;
								rel.callindex = at;
								rel.prev = GETARG_Bx(p->code[j - 1]); // It is not always a LOADK

								selfmap[vanilla_code.size() - 1] = rel;

								self_relocations.push_back(rel);

								break;
							}
						}
						
						vanilla_code.push_back(i);

						break;
						
					case OP_TFORLOOP:
					{
						// TFORLOOP's we will use a PAIRSLOOP
						// in place of.
						// This means We MUST have a matching PAIRSPREP
						// PRIOR to this instruction. Thus,
						// we store this information.
						// We also need to SKIP/IGNORE the following jmp
						// attached to this TFORLOOP.
						auto vanilla_index = (at + GETARG_sBx(p->code[at + 1])) + 1;

						#if DEBUG_TRANSPILER == TRUE
						printf("%s\n", luaP_opnames[GET_OPCODE(p->code[vanilla_index])]);
						#endif

						if (GET_OPCODE(p->code[vanilla_index]) == OP_JMP)
						{
							pairsprep_locs[at] = TRUE;

							#if DEBUG_TRANSPILER == TRUE
							printf("Found corresponding JMP\n");
							#endif
						}

						vanilla_code.push_back(i);

						break;
					}
					default:
						vanilla_code.push_back(i);

						break;
					}
				}


				// Step through the vanilla code a second time, before transpilation.
				// IN THIS PHASE - We adjust the entire stack and any registers according
				// to the optimizations we've made
				for (int at = 0; at < vanilla_code.size(); at++)
				{
					auto i = vanilla_code[at];

					// OPTIONAL -- makes no impact
					// 

					// Step 1. OP_SELF Relocations
					// This is a safety precaution in-case of the event
					// that any sBx instruction landed somewhere between
					// an OP_SELF's old index and new (relocated) index.
					
					// Check all instructions that use sBx
					// for jumping a certain distance
					switch (GET_OPCODE(i))
					{
					case OP_JMP:
					case OP_FORLOOP:
					case OP_FORPREP:
					{
						auto destination_pos = (at + GETARG_sBx(i));

						#if DEBUG_TRANSPILER == TRUE
						printf("Destination of jmp: %s\n", luaP_opnames[GET_OPCODE(vanilla_code[destination_pos])]);
						#endif

						for (auto rel : self_relocations)
						{
							// OP_SELF's have been relocated already; check the old code index

							// does this sBx instruction land where OP_SELF
							// originally was?
							if (destination_pos == rel.selfindex)
							{
								// update this sBx to the new OP_SELF location
								
								printf("JMP'S DESTINATION IS ON OP_SELF\n");
								SETARG_sBx(vanilla_code[at], GETARG_sBx(vanilla_code[at] + (rel.callindex - at)));
								
								// Example:
								// 
								// JMP 2
								// GETGLOBAL
								// LOADK
								// SELF
								// LOADK
								// CALL
								// 
								// JMP 3 <--- update to the new OP_SELF location
								// GETGLOBAL
								// LOADK
								// LOADK
								// SELF
								// CALL
							}
							// Is this jmp's destination between the OP_SELF's
							// original location and its new location?
							else if (destination_pos > rel.selfindex && destination_pos < rel.callindex)
							{
								// let's decrement this jmp's sBx
								printf("JMP'S DESTINATION WAS BETWEEN OP_SELF OLD/NEW INDEX\n");
								SETARG_sBx(vanilla_code[at], GETARG_sBx(i) - 1);
								
								// Example:
								//
								// JMP 3
								// GETGLOBAL
								// SELF
								// GETGLOBAL
								// LOADK
								// LOADK
								// CALL
								// 
								// JMP 2 <--- decrement
								// GETGLOBAL
								// x
								// GETGLOBAL
								// LOADK
								// LOADK
								// SELF <--- moved here
								// CALL
							}
						}

						break;
					}
					}
				}

				auto upvalue_locs = std::vector<BOOL>(vanilla_code.size()); // for upvalue replacements

				bool open_reg_1 = false;
				bool open_reg_2 = false;
				bool open_reg_3 = false;

				bool close = false;


				new_lineinfo.push_back(p->lineinfo[0]);




				auto loadargs = NEW_UINST();

				if (p->is_vararg & VARARG_ISVARARG)
				{
					SET_OPCODEU(loadargs, R_LUAU::INITVA); // 0xA3

					#if DEBUG_TRANSPILER == TRUE
					std::cout << "INITVA\n";
					#endif
				}
				else
				{
					SET_OPCODEU(loadargs, R_LUAU::INIT); // 0xC0

					#if DEBUG_TRANSPILER == TRUE
					std::cout << "INIT\n";
					#endif
				}

				SETARGU_A(loadargs, p->numparams);
				r_insts.push_back(loadargs);



				for (int at = 0; at < vanilla_code.size(); at++)
				{
					new_sizes.push_back(r_insts.size());

					auto i = vanilla_code[at];
					
					#if DEBUG_TRANSPILER == TRUE
					std::cout << luaP_opnames[GET_OPCODE(i)];
					for (int spaces = lstrlenA(luaP_opnames[GET_OPCODE(i)]); spaces < 20; spaces++)
						std::cout << " ";
					std::cout << "; ";
					#endif



					switch (GET_OPCODE(i))
					{

					case OP_MOVE:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);

						if (upvalue_locs[at] == TRUE)
						{
							SET_OPCODEU(new_instr, R_LUAU::MARKUPVAL);
							SETARGU_A(new_instr, 1);
						}
						else if (upvalue_locs[at] == TRUE + 1)
						{
							SET_OPCODEU(new_instr, R_LUAU::MARKUPVAL);
							SETARGU_A(new_instr, 0);
						}
						else
						{
							SETARGU_A(new_instr, GETARG_A(i));
						}

						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						if (!upvalue_locs[at])
							std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "]";
						else
							std::cout << "MARK UPVALUE";
						#endif

						break;
					}
					case OP_LOADBOOL:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::LOADBOOL);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						SETARGU_C(new_instr, GETARG_C(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						if (GETARG_B(i))
							std::cout << "Stack[" << GETARG_A(i) << "] = true; ";
						else
							std::cout << "Stack[" << GETARG_A(i) << "] = false; ";

						if (GETARG_C(i))
							std::cout << "pc++;";
						#endif

						break;
					}
					case OP_LOADNIL:
					{
						auto A = GETARG_A(i);
						auto B = GETARG_B(i);

						while (B >= A)
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADNIL);
							SETARGU_A(new_instr, B--);
							r_insts.push_back(new_instr);

							#if DEBUG_TRANSPILER == TRUE
							std::cout << "Stack[" << B << "] = nil";
							#endif
						}
						
						break;
					}
					case OP_LOADK:
					{
						auto o = &p->k[OPTINDEX(GETARG_Bx(i))];
						auto n = nvalue(o);

						if (ttisnumber(o)
						 && std::floor(n) == n
						 && n > 0.0
						 && n < 32767.0 // SHRT_MAX
						){
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADINT);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_sBx(new_instr, static_cast<int16_t>(n));
							r_insts.push_back(new_instr);

							#if DEBUG_TRANSPILER == TRUE
							std::cout << "Stack[" << GETARG_A(i) << "] = " << (int)static_cast<int16_t>(n) << ";";
							#endif

							break;
						}
						
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::LOADK);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_Bx(new_instr, OPTINDEX(GETARG_Bx(i)));
						r_insts.push_back(new_instr);
						
						#if DEBUG_TRANSPILER == TRUE
						if (ttisstring(&p->k[OPTINDEX(GETARG_Bx(i))]))
							std::cout << "Stack[" << GETARG_A(i) << "] = p->k[\"" << getstr(reinterpret_cast<TString*>(p->k[OPTINDEX(GETARG_Bx(i))].value.gc)) << "\"]";
						else
							std::cout << "Stack[" << GETARG_A(i) << "] = p->k[" << OPTINDEX(GETARG_Bx(i)) << "]";
						#endif

						break;
					}
					case OP_GETGLOBAL:
					{
						#if USE_GETGLOBAL_OPT == TRUE

						if (p->k[OPTINDEX(GETARG_Bx(i))].tt == LUA_TCACHE)
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::GETGLOBALOPT);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_Bx(new_instr, OPTINDEX(GETARG_Bx(i)));
							r_insts.push_back(new_instr);
							r_insts.push_back(static_cast<int>(nvalue(&p->k[OPTINDEX(GETARG_Bx(i))])));

							#if DEBUG_TRANSPILER == TRUE
							if (ttisstring(&p->k[OPTINDEX(GETARG_Bx(i))]))
								std::cout << "Stack[" << GETARG_A(i) << "] = Gbl[\"" << getstr(reinterpret_cast<TString*>(p->k[OPTINDEX(GETARG_Bx(i))].value.gc)) << "\"];";
							if (p->k[OPTINDEX(GETARG_Bx(i))].tt == (LAST_TAG+4))
								std::cout << "Stack[" << GETARG_A(i) << "] = Gbl[" << std::hex << static_cast<DWORD>(nvalue(&p->k[OPTINDEX(GETARG_Bx(i))])) << "];";
							#endif

							break;
						}

						#endif

						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::GETGLOBAL);
						SETARGU_A(new_instr, GETARG_A(i));
						r_insts.push_back(new_instr);
						r_insts.push_back(OPTINDEX(GETARG_Bx(i)));

						#if DEBUG_TRANSPILER == TRUE
						if (ttisstring(&p->k[OPTINDEX(GETARG_Bx(i))]))
							std::cout << "Stack[" << GETARG_A(i) << "] = Gbl[\"" << getstr(reinterpret_cast<TString*>(p->k[OPTINDEX((GETARG_Bx(i)))].value.gc)) << "\"];";
						#endif


						break;
					}
					case OP_GETTABLE:
					{
						if (ISK(GETARG_C(i)))
						{
							auto o = &p->k[OPTINDEX(INDEXK(GETARG_C(i)))];
							auto n = nvalue(o);

							if (ttisnumber(o)
							 && std::floor(n) == n
							 && n > 0.0
							 && n < 128.0
							){
								// uses a number index
								auto new_instr = NEW_UINST();
								SET_OPCODEU(new_instr, R_LUAU::GETTABLEN);
								SETARGU_A(new_instr, GETARG_A(i));
								SETARGU_B(new_instr, GETARG_B(i));
								SETARGU_C(new_instr, n - 1);
								r_insts.push_back(new_instr);

								#if DEBUG_TRANSPILER == TRUE
								std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "][" << n << "];";
								#endif

								break;
							}
							else 
							{
								#if DEBUG_TRANSPILER == TRUE
								const char* str = "...";

								if (ttisstring(o))
									str = getstr(reinterpret_cast<TString*>(o->value.gc));

								if (std::string(str).find(" ") != std::string::npos)
									std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "][\"" << str << "\"];";
								else
									std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "]." << str << ";";
								#endif
							}

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::GETTABLEK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, GETARG_B(i));
							r_insts.push_back(new_instr);
							r_insts.push_back(OPTINDEX(INDEXK(GETARG_C(i))));

							break;
						}
						
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::GETTABLE);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						SETARGU_C(new_instr, GETARG_C(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "][Stack[" << GETARG_C(i) << "]];";
						#endif
						

						break;
					}
					case OP_GETUPVAL:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::GETUPVAL);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));

						if (upvalue_locs[at] == TRUE)
						{
							SET_OPCODEU(new_instr, R_LUAU::MARKUPVAL);
							SETARGU_A(new_instr, 2);
						}
						else if (upvalue_locs[at] == TRUE + 1)
						{
							SET_OPCODEU(new_instr, R_LUAU::MARKUPVAL);
							SETARGU_A(new_instr, 0);
						}

						r_insts.push_back(new_instr);
						
						#if DEBUG_TRANSPILER == TRUE
						if (!upvalue_locs[at])
							std::cout << "Stack[" << GETARG_A(i) << "] = UpValue[" << GETARG_B(i) << "];";
						else
							std::cout << "MARK UPVALUE";
						#endif

						break;
					}
					case OP_SETGLOBAL:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::SETGLOBAL);
						SETARGU_A(new_instr, GETARG_A(i));
						r_insts.push_back(new_instr);
						r_insts.push_back(OPTINDEX(GETARG_Bx(i)));
						
						#if DEBUG_TRANSPILER == TRUE
						if (ttisstring(&p->k[OPTINDEX(GETARG_Bx(i))]))
							std::cout << "global_env[\"" << getstr(reinterpret_cast<TString*>(p->k[OPTINDEX((GETARG_Bx(i)))].value.gc)) << "\"] = Stack[" << GETARG_A(i) << "];";
						#endif

						break;
					}
					case OP_SETTABLE:
					{
						auto base = GETARG_C(i);

						if (ISK(GETARG_C(i)))
						{
							// Place constant onto the stack
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto o = &p->k[OPTINDEX(INDEXK(GETARG_C(i)))];
							
							if (ttisboolean(o))
							{
								auto new_instr = NEW_UINST();
								SET_OPCODEU(new_instr, R_LUAU::LOADBOOL);
								SETARGU_A(new_instr, base);
								SETARGU_B(new_instr, o->value.b);
								SETARGU_C(new_instr, 0);
								r_insts.push_back(new_instr);
							}
							else {
								auto new_instr = NEW_UINST();
								SET_OPCODEU(new_instr, R_LUAU::LOADK);
								SETARGU_A(new_instr, base);
								SETARGU_B(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
								r_insts.push_back(new_instr);
							}
						}

						if (ISK(GETARG_B(i)))
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::SETTABLEK);
							SETARGU_A(new_instr, base);
							SETARGU_B(new_instr, GETARG_A(i));
							r_insts.push_back(new_instr);
							r_insts.push_back(OPTINDEX(INDEXK(GETARG_B(i))));
							
							#if DEBUG_TRANSPILER == TRUE
							const char* str = getstr(reinterpret_cast<TString*>(p->k[OPTINDEX(INDEXK(GETARG_B(i)))].value.gc));

							if (std::string(str).find(" ") != std::string::npos)
								std::cout << "Stack[" << GETARG_A(i) << "][\"" << str << "\"] = Stack[" << base << "];";
							else
								std::cout << "Stack[" << GETARG_A(i) << "]." << str << " = Stack[" << base << "];";
							#endif
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::SETTABLE);
							SETARGU_A(new_instr, base);
							SETARGU_B(new_instr, GETARG_A(i));
							SETARGU_C(new_instr, GETARG_B(i));
							r_insts.push_back(new_instr);

							#if DEBUG_TRANSPILER == TRUE
							std::cout << "Stack[" << GETARG_A(i) << "][Stack[" << GETARG_B(i) << "]] = Stack[" << base << "];";
							#endif
						}

						break;

					}
					case OP_SETUPVAL:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::SETUPVAL);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						std::cout << "UpValue[" << GETARG_B(i) << "] = Stack[" << GETARG_A(i) << "];";
						#endif

						break;
					}
					case OP_NEWTABLE:
					{
						int hash = 0;

						if (GETARG_B(i))
						{
							// In ROBLOX LuaU: B = 1 << (B - 1);
							hash = 2 ^ (GETARG_B(i) - 1);
						}

						auto new_instr = NEW_UINST();

						SET_OPCODEU(new_instr, R_LUAU::NEWTABLE);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, hash); // hash // 

						r_insts.push_back(new_instr);
						r_insts.push_back(GETARG_B(i));
						
						#if DEBUG_TRANSPILER == TRUE
						std::cout << "Stack[" << GETARG_A(i) << "] = {size = " << GETARG_B(i) << ", " << GETARG_C(i) << "};";
						#endif

						break;
					}
					case OP_SELF:
					{
						// Stack[1] := Stack[1]["children"]
						/*
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::GETTABLEK);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						if (ISK(GETARG_C(i)))
						{
							r_insts.push_back(new_instr);
							r_insts.push_back(OPTINDEX(INDEXK(GETARG_C(i))));
						}
						else {
							r_insts.push_back(new_instr);
							r_insts.push_back(selfmap[at].prev);
						}
						*/
						
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::SELF);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						if (ISK(GETARG_C(i)))
						{
							r_insts.push_back(OPTINDEX(INDEXK(GETARG_C(i))));

							#if DEBUG_TRANSPILER == TRUE
							if (ttisstring(&p->k[OPTINDEX(INDEXK(GETARG_C(i)))]))
							{
								std::cout << "Stack[" << GETARG_A(i) + 1 << "] := Stack[" << GETARG_B(i) << "];\n";
								std::cout << "Stack[" << GETARG_A(i) << "] := Stack[" << GETARG_B(i) << "][\"" << getstr(reinterpret_cast<TString*>(p->k[OPTINDEX(INDEXK(GETARG_C(i)))].value.gc)) << "\"]";
							}
							#endif
						}
						else
						{
							r_insts.push_back(selfmap[at].prev);

							#if DEBUG_TRANSPILER == TRUE
							if (ttisstring(&p->k[selfmap[at].prev]))
							{
								std::cout << "Stack[" << GETARG_A(i) + 1 << "] := Stack[" << GETARG_B(i) << "];\n";
								std::cout << "Stack[" << GETARG_A(i) << "] := Stack[" << GETARG_B(i) << "][\"" << getstr(reinterpret_cast<TString*>(p->k[selfmap[at].prev].value.gc)) << "\"]";
							}
							#endif
						}
						


						break;
					}
					case OP_UNM:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::UNM);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						break;
					}
					case OP_NOT:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::NOT);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						break;
					}
					case OP_LEN:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::LEN);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						std::cout << "Stack[" << GETARG_A(i) << "] = #Stack[" << GETARG_B(i) << "];";
						#endif

						break;
					}
					case OP_CONCAT:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::CONCAT);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						SETARGU_C(new_instr, GETARG_C(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "] .. Stack[" << GETARG_C(i) << "];";
						#endif

						break;
					}
					case OP_FORPREP: // case 45
					{
						auto base = GETARG_A(i);
						auto /*pos = p->maxstacksize;
						
						open_reg_1 = true;
						open_reg_2 = true;
						open_reg_3 = true;

						// 0 --> 1
						// 1 --> 2
						// 2 --> 0
						//for i=1,10,1 do
						//for 10,1,i=1 do
						// init_start --> str_limit
						// limit_start --> str_step
						// step_start --> str_init

						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, pos + 0);
						SETARGU_B(new_instr, base + 0);
						r_insts.push_back(new_instr);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, pos + 1);
						SETARGU_B(new_instr, base + 1);
						r_insts.push_back(new_instr);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, pos + 2);
						SETARGU_B(new_instr, base + 2);
						r_insts.push_back(new_instr);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, base + 2);
						SETARGU_B(new_instr, pos + 0);
						r_insts.push_back(new_instr);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, base + 0);
						SETARGU_B(new_instr, pos + 1);
						r_insts.push_back(new_instr);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, base + 1);
						SETARGU_B(new_instr, pos + 2);
						r_insts.push_back(new_instr);
						*/

						/*
						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::LONGJMP);
						SETARGU_A(new_instr, base);
						SETARGU_sBx(new_instr, 0);
						r_insts.push_back(new_instr);
						*/

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::FORPREP);
						SETARGU_A(new_instr, base);
						r_insts.push_back(new_instr);

						Relocation rel = Relocation(r_insts.size() - 1);
						rel.toindex = GETARG_sBx(i);
						rel.fromindex = at;
						//rel.shift = -6;
						relocations.push_back(rel);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, base + 3);
						SETARGU_B(new_instr, base + 2);
						r_insts.push_back(new_instr);
						
						
						#if DEBUG_TRANSPILER == TRUE
						std::cout << "FORPREP on indices " << base << ", " << base + 1 << ", " << base + 2;
						#endif



						break;

					}
					case OP_FORLOOP: // case 37
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::FORLOOP);
						SETARGU_A(new_instr, GETARG_A(i));
						r_insts.push_back(new_instr);

						Relocation rel = Relocation(r_insts.size() - 1);
						rel.fromindex = at;
						rel.toindex = GETARG_sBx(i);
						//rel.shift = 6; // comment out
						relocations.push_back(rel);
						

						#if DEBUG_TRANSPILER == TRUE
						std::cout << "FORLOOP";
						#endif

						break;
					}
					case OP_TFORLOOP:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::PAIRSLOOP);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_sBx(new_instr, 2);
						r_insts.push_back(new_instr);

						Relocation rel = Relocation(r_insts.size() - 1);
						rel.fromindex = at;
						rel.toindex = GETARG_sBx(p->code[at + 1]) + 1;
						relocations.push_back(rel);
						
						#if DEBUG_TRANSPILER == TRUE
						std::cout << "PAIRSLOOP being used";
						#endif

						//r_insts.push_back(makeiP(GETARG_C(i));
						break;

					}
					case OP_ADD:
					{
						auto base = GETARG_B(i);

						if (ISK(GETARG_B(i)))
						{
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, base);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i)) - 256) // <--- 256 should be 0 when converted to byte
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::ADDK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::ADD);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, GETARG_C(i));
							r_insts.push_back(new_instr);
						}

						break;
					}
					case OP_SUB:
					{
						auto base = GETARG_B(i);

						if (ISK(GETARG_B(i)))
						{
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, base);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i)) - 256) // <--- 256 should be 0 when converted to byte
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::SUBK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::SUB);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, GETARG_C(i));
							r_insts.push_back(new_instr);
						}

						break;

					}
					case OP_MUL:
					{
						auto base = GETARG_B(i);

						if (ISK(GETARG_B(i)))
						{
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, base);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i)) - 256) // <--- 256 should be 0 when converted to byte
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::MULK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::MUL);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, GETARG_C(i));
							r_insts.push_back(new_instr);
						}

						break;

					}
					case OP_DIV:
					{
						auto base = GETARG_B(i);

						if (ISK(GETARG_B(i)))
						{
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, base);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i)) - 256) // <--- 256 should be 0 when converted to byte
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::DIVK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::DIV);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, GETARG_C(i));
							r_insts.push_back(new_instr);
						}

						break;

					}
					case OP_POW:
					{
						auto base = GETARG_B(i);

						if (ISK(GETARG_B(i)))
						{
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, base);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i)) - 256) // <--- 256 should be 0 when converted to byte
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::POWK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::POW);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, GETARG_C(i));
							r_insts.push_back(new_instr);
						}

						break;

					}
					case OP_MOD:
					{
						auto base = GETARG_B(i);

						if (ISK(GETARG_B(i)))
						{
							base = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, base);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i)) - 256) // <--- 256 should be 0 when converted to byte
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::MODK);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}
						else
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::MOD);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_B(new_instr, base);
							SETARGU_C(new_instr, GETARG_C(i));
							r_insts.push_back(new_instr);
						}

						break;

					}
					case OP_JMP:
					{
						if (GET_OPCODE(vanilla_code[at - 1]) == OP_TFORLOOP)
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::JMP);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_sBx(new_instr, 0);
							r_insts.push_back(new_instr);

							break;
						}

						/*
						65 00 00 00 pc++;
						65 00 01 00 pc++; pc += 1;
						-- start
						*/
						if (!pairsprep_locs[at])
						{
							// Normal jmp
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::JMP);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_sBx(new_instr, 0);
							r_insts.push_back(new_instr);

							Relocation rel = Relocation(r_insts.size() - 1);
							rel.fromindex = at;
							rel.toindex = GETARG_sBx(i) + 1;
							rel.shift = -1;
							relocations.push_back(rel);
						}
						else {
							// Instead of a JMP we can use a PAIRSPREP
							// for the PAIRSLOOP which follows..
							// (which would normally be a TFORLOOP...
							// we also merge it with the JMP that follows)
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::PAIRSPREP);
							SETARGU_A(new_instr, GETARG_A(i));
							SETARGU_sBx(new_instr, 0);
							r_insts.push_back(new_instr);

							// 17 00 03 00 
							// 6F 07 05 00 
							// BC 05 04 00 
							// 04 00 00 00 
							// 30 05 04 00 <--- if we did a normal jmp relocation...it'd land here.
							// 06 00 00 00
							// FA 00 ?? ?? <--- We need it to land here though
							Relocation rel = Relocation(r_insts.size() - 1);
							rel.fromindex = at;
							rel.toindex = GETARG_sBx(i) + 1;
							rel.shift = -1;
							relocations.push_back(rel);
						}


						#if DEBUG_TRANSPILER == TRUE
						std::cout << "jump down " << GETARG_sBx(i) << " lines";
						#endif

						break;

					}
					case OP_EQ:
					{
						// example of OP_EQ:
						// if (v.Name == "Head") then
						//
						// if (A == B) then
						// 
						auto B = GETARG_B(i);
						auto C = GETARG_C(i);

						if (ISK(GETARG_B(i))) // <--- 256 should be 0 when converted to byte
						{
							B = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, B);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);

							#if DEBUG_TRANSPILER == TRUE 
							std::cout << "if (Stack[" << OPTINDEX(INDEXK(GETARG_B(i))) << "] == ";
							#endif
						}
						else {
							#if DEBUG_TRANSPILER == TRUE 
							std::cout << "if (Stack[" << GETARG_B(i) << "] == ";
							#endif
						}

						if (ISK(GETARG_C(i))) // <--- 256 should be 0 when converted to byte
						{
							if (open_reg_1) 
							{
								C = p->maxstacksize + 1;
								open_reg_2 = true;
							}
							else {
								C = p->maxstacksize + 0;
								open_reg_1 = true;
							}

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, C);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
							
							#if DEBUG_TRANSPILER == TRUE 
							std::cout << "Stack[" << OPTINDEX(INDEXK(GETARG_C(i))) << "]";
							#endif
						}
						else {
							#if DEBUG_TRANSPILER == TRUE 
							std::cout << "Stack[" << GETARG_C(i) << "]";
							#endif
						}

						auto new_instr = NEW_UINST();
						SETARGU_A(new_instr, B);
						SETARGU_sBx(new_instr, 2);

						if (GETARG_A(i) != 1)
							SET_OPCODEU(new_instr, R_LUAU::EQ);
						else
							SET_OPCODEU(new_instr, R_LUAU::NEQ);

						#if DEBUG_TRANSPILER == TRUE 
						std::cout << " != " << GETARG_A(i) << ") pc++;";
						#endif

						r_insts.push_back(new_instr);
						r_insts.push_back(C);

						break;

					}
					case OP_LT:
					{
						auto B = GETARG_B(i);
						auto C = GETARG_C(i);

						if (ISK(GETARG_B(i))) // <--- 256 should be 0 when converted to byte
						{
							B = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, B);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i))) // <--- 256 should be 0 when converted to byte
						{
							if (open_reg_1)
							{
								C = p->maxstacksize + 1;
								open_reg_2 = true;
							}
							else {
								C = p->maxstacksize + 0;
								open_reg_1 = true;
							}

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, C);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}

						auto new_instr = NEW_UINST();
						SETARGU_A(new_instr, B);
						SETARGU_sBx(new_instr, 2);

						if (GETARG_A(i) != 1)
							SET_OPCODEU(new_instr, R_LUAU::LT);
						else
							SET_OPCODEU(new_instr, R_LUAU::GT);

						r_insts.push_back(new_instr);
						r_insts.push_back(C);

						break;

					}
					case OP_LE:
					{
						auto B = GETARG_B(i);
						auto C = GETARG_C(i);

						if (ISK(GETARG_B(i))) // <--- 256 should be 0 when converted to byte
						{
							B = p->maxstacksize + 0;
							open_reg_1 = true;

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, B);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_B(i))));
							r_insts.push_back(new_instr);
						}

						if (ISK(GETARG_C(i))) // <--- 256 should be 0 when converted to byte
						{
							if (open_reg_1)
							{
								C = p->maxstacksize + 1;
								open_reg_2 = true;
							}
							else {
								C = p->maxstacksize + 0;
								open_reg_1 = true;
							}

							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::LOADK);
							SETARGU_A(new_instr, C);
							SETARGU_Bx(new_instr, OPTINDEX(INDEXK(GETARG_C(i))));
							r_insts.push_back(new_instr);
						}

						auto new_instr = NEW_UINST();
						SETARGU_A(new_instr, B);
						SETARGU_sBx(new_instr, 2);

						if (GETARG_A(i) != 1)
							SET_OPCODEU(new_instr, R_LUAU::LE);
						else
							SET_OPCODEU(new_instr, R_LUAU::GE);

						r_insts.push_back(new_instr);
						r_insts.push_back(C);

						break;

					}
					case OP_TEST:
					{
						// example of OP_TEST:
						// if (v:IsA("Accessory")) then
						// 
						// literally:
						// if (true or non-nil) then
						// 
						auto new_instr = NEW_UINST();
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_sBx(new_instr, 1);

						if (GETARG_C(i))
							SET_OPCODEU(new_instr, R_LUAU::TEST);
						else
							SET_OPCODEU(new_instr, R_LUAU::TESTJMP);

						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						std::cout << "if not (Stack[" << GETARG_A(i) << "] <=> " << GETARG_C(i) << ") then pc++;";
						#endif

						break;
					}
					case OP_TESTSET:
					{
						auto new_instr = NEW_UINST();
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_sBx(new_instr, 2);


						if (GETARG_C(i))
							SET_OPCODEU(new_instr, R_LUAU::TEST);
						else
							SET_OPCODEU(new_instr, R_LUAU::TESTJMP);

						r_insts.push_back(new_instr);

						new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::MOVE);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);
						
						#if DEBUG_TRANSPILER == TRUE
						std::cout << "if not (Stack[" << GETARG_A(i) << "] <=> " << GETARG_C(i) << ") then Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_B(i) << "] else pc++;";
						#endif

						break;
					}
					case OP_CALL: case OP_TAILCALL:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::CALL);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						SETARGU_C(new_instr, GETARG_C(i));
						r_insts.push_back(new_instr);

						#if DEBUG_TRANSPILER == TRUE
						std::cout << std::endl;

						std::cout << "CALL";
						for (int spaces = 4; spaces < 20; spaces++)
							std::cout << " ";
						std::cout << "; ";

						if (GETARG_B(i) == 0)
							std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_A(i) << "](...top);";
						else
						{
							std::cout << "Stack[" << GETARG_A(i) << "] = Stack[" << GETARG_A(i) << "](";
							for (int j = 1; j < GETARG_B(i); j++)
							{
								std::cout << "arg" << j;

								if (j < GETARG_B(i) - 1)
								{
									std::cout << ", ";
								}
							}
							std::cout << ");";
						}
						#endif

						break;
					}
					case OP_SETLIST:
					{
						int j = 1;

						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::SETLIST);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_A(i) + j);

						if (GETARG_B(i) == 0)
							SETARGU_C(new_instr, 0);
						else
							SETARGU_C(new_instr, GETARG_B(i) + 1);

						r_insts.push_back(new_instr);
						r_insts.push_back((GETARG_C(i) - 1) * LFIELDS_PER_FLUSH + j);

						break;
					}
					case OP_CLOSURE:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::CLOSURE);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_Bx(new_instr, GETARG_Bx(i));
						r_insts.push_back(new_instr);

						auto proto = p->p[GETARG_Bx(i)];

						if (proto->nups > 0)
						{
							close = true;

							for (int j = 0; j < proto->nups; j++)
							{
								auto mode = TRUE;

								upvalue_locs[at + j + 1] = mode;
							}
						}

						
						#if DEBUG_TRANSPILER == TRUE
						std::cout << "Stack[" << GETARG_A(i) << "] = closure(protos[" << GETARG_Bx(i) << "], ...);";
						#endif

						break;

					}
					case OP_CLOSE:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::CLOSE);
						SETARGU_A(new_instr, GETARG_A(i));
						r_insts.push_back(new_instr);

						break;
					}
					case OP_VARARG:
					{
						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::VARARG);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						break;
					}
					case OP_RETURN:
					{
						if (close)
						{
							auto new_instr = NEW_UINST();
							SET_OPCODEU(new_instr, R_LUAU::CLOSE);
							SETARGU_A(new_instr, 0);
							r_insts.push_back(new_instr);
						}

						auto new_instr = NEW_UINST();
						SET_OPCODEU(new_instr, R_LUAU::RETURN);
						SETARGU_A(new_instr, GETARG_A(i));
						SETARGU_B(new_instr, GETARG_B(i));
						r_insts.push_back(new_instr);

						break;
					}
					}

					// fix up lineinfo
					for (int i = 0; i < r_insts.size() - new_sizes.back(); i++)
					{
						// push the same lineinfo to fill in the blanks...
						new_lineinfo.push_back(p->lineinfo[at]);
					}

					#if DEBUG_TRANSPILER == TRUE
					std::cout << std::endl;
					#endif
				}

				// sizing starts at 0th instruction.
				/* example of how `new_sizes` table works:
				 0  INITVA
				 1  GETGLOBAL
				    (cache)
				 3  LOADK
				 4  CALL
				 5  JMP 2 <--- ***
				 6  GETGLOBAL <--- jmp start (0)
				    (cache)
				 8  LOADK <--- jmp (1)
				    GETTABLEK
				    (cache)
				 11 CALL <--- jmp (2) lands here
				 12 RETURN


				 ***
				 so to properly calculate sBx according to our new_sizes
				 table, we must do as follows:

				 rel.fromindex = at;
				 rel.toindex = GETARG_sBx(i);
				 so...
				 new_dist = new_sizes[rel.fromindex + rel.toindex] - new_sizes[rel.fromindex]

				 
				*/

				for (Relocation rel : relocations)
				{
					// at + GETARG_sBx(i) in most cases
					// vanilla_index = position/index in vanilla code
					auto vanilla_index = (rel.fromindex + rel.toindex);

					auto new_offset = (new_sizes[vanilla_index] - new_sizes[rel.fromindex]);
					new_offset += rel.shift;

					#if DEBUG_TRANSPILER == TRUE
					printf("sBx(%i) instruction lands on %s\n", rel.toindex, luaP_opnames[GET_OPCODE(vanilla_code[vanilla_index])]);
					#endif

					auto dist = static_cast<unsigned int>(new_offset);

					if (dist > 0x7FFF && dist < -0x7FFF)
					{
						if (GET_OPCODEU(r_insts[rel.rawcodeindex]) == R_LUAU::JMP)
						{
							printf("old jmp: %08X\n", r_insts[rel.rawcodeindex]);

							SET_OPCODEU(r_insts[rel.rawcodeindex], R_LUAU::LONGJMP);
							SETARGU_sAx(r_insts[rel.rawcodeindex], static_cast<signed int>(new_offset));

							printf("new jmp: %08X\n", r_insts[rel.rawcodeindex]);

							printf("%08X\n", r_insts[rel.rawcodeindex]);
						}
						else {
							SETARGU_sBx(r_insts[rel.rawcodeindex], static_cast<signed short>(new_offset));

							printf("LONGJMP required for %08X\n", r_insts[rel.rawcodeindex]);
						}
					}
					else 
					{
						SETARGU_sBx(r_insts[rel.rawcodeindex], static_cast<signed short>(new_offset));
					}
				}
				
				if (open_reg_1) new_maxstacksize++;
				if (open_reg_2) new_maxstacksize++;
				if (open_reg_3) new_maxstacksize++;


				/*
				for (int at = 0; at < r_insts.size(); at++)
				{
					if (at + 6 < r_insts.size())
					{
						if (GET_OPCODEU(r_insts[at + 0]) == R_LUAU::MOVE
						 && GET_OPCODEU(r_insts[at + 6]) == R_LUAU::FORPREP
						){
							printf("FOR LOOP FOUND\n");
							while (1)
							{
								auto x = r_insts[at];

								printf("%02X %02X %02X %02X\n", GET_OPCODEU(x), GETARGU_A(x), GETARGU_B(x), GETARGU_C(x));

								if (GET_OPCODEU(r_insts[at++]) == R_LUAU::FORLOOP)
								{
									break;
								}
							}
							printf("\n\n");
						}
					}
				}
				*/

				/*
				if (break_transpiler == 2)
				{
					system("PAUSE");
					break_transpiler = 0;
				}
				*/
				return r_insts;
			}
		}
	}
}
