#pragma once
#include "../rbx_lua.h"

extern "C"
{
#include "../../Lua/lstate.h"
#include "../../Lua/lopcodes.h"
}

// Yes, how the hell did you know!
// I Took something useful from Calamari
// because it allows for 64-bit support
// in the future!
// but this small snippet must mean that I am fully a skid
// Even though it gets my own work and research accomplished.
// Rip! :-)

#define SIZEU_OP	8
#define SIZEU_A		8
#define SIZEU_B		8
#define SIZEU_C		8
#define SIZEU_Bx	(SIZEU_B + SIZEU_C)

#define POSU_OP		0
#define POSU_A		(POSU_OP + SIZEU_OP)
#define POSU_B		(POSU_A + SIZEU_A)
#define POSU_C		(POSU_B + SIZEU_B)
#define POSU_Bx		POSU_B

#define SIZEU_D		10
#define SIZEU_E		10
#define SIZEU_F		10
#define SIZEU_G		2
#define SIZEU_Ex	(SIZEU_E + SIZEU_F + SIZEU_G)

#define POSU_D		0
#define POSU_E		(POSU_D + SIZEU_D)
#define POSU_F		(POSU_E + SIZEU_E)
#define POSU_G		(POSU_F + SIZEU_F)

#if SIZEU_Bx < LUAI_BITSINT-1
#define MAXARGU_Bx        ((1<<SIZEU_Bx)-1)
#define MAXARGU_sBx        (MAXARGU_Bx>>1)         /* `sBx' is signed */
#else
#define MAXARGU_Bx        MAX_INT
#define MAXARGU_sBx        MAX_INT
#endif

#define MAXARGU_A        ((1<<SIZEU_A)-1)
#define MAXARGU_B        ((1<<SIZEU_B)-1)
#define MAXARGU_C        ((1<<SIZEU_C)-1)


#define GET_OPCODEU(i)	(cast(OpCode, ((i)>>POSU_OP) & MASK1(SIZEU_OP,0)))
#define SET_OPCODEU(i,o)	((i) = (((i)&MASK0(SIZEU_OP,POSU_OP)) | \
		((cast(Instruction, o)<<POSU_OP)&MASK1(SIZEU_OP,POSU_OP))))

#define GETARGU_A(i)	(cast(int, ((i)>>POSU_A) & MASK1(SIZEU_A,0)))
#define SETARGU_A(i,u)	((i) = (((i)&MASK0(SIZEU_A,POSU_A)) | \
		((cast(Instruction, u)<<POSU_A)&MASK1(SIZEU_A,POSU_A))))

#define GETARGU_B(i)	(cast(int, ((i)>>POSU_B) & MASK1(SIZEU_B,0)))
#define SETARGU_B(i,b)	((i) = (((i)&MASK0(SIZEU_B,POSU_B)) | \
		((cast(Instruction, b)<<POSU_B)&MASK1(SIZEU_B,POSU_B))))

#define GETARGU_C(i)	(cast(int, ((i)>>POSU_C) & MASK1(SIZEU_C,0)))
#define SETARGU_C(i,b)	((i) = (((i)&MASK0(SIZEU_C,POSU_C)) | \
		((cast(Instruction, b)<<POSU_C)&MASK1(SIZEU_C,POSU_C))))

#define GETARGU_Bx(i)	(cast(int, ((i)>>POSU_Bx) & MASK1(SIZEU_Bx,0)))
#define SETARGU_Bx(i,b)	((i) = (((i)&MASK0(SIZEU_Bx,POSU_Bx)) | \
		((cast(Instruction, b)<<POSU_Bx)&MASK1(SIZEU_Bx,POSU_Bx))))

#define GETARGU_sBx(i)  (int16_t)((i) >> 16)
#define SETARGU_sBx(i, x)  ((i) = ((i) & 0x0000ffff | ((x) << 16) & 0xffff0000))

#define GETARGU_sAx(i)  ((int32_t)(i) >> 8)
#define SETARGU_sAx(i, x)  ((i) = ((i) & 0x000000ff | ((x) << 8) & 0xffffff00))

#define NEW_UINST()	((cast(Instruction, 0)<<POS_OP) \
			| (cast(Instruction, 0)<<POSU_A) \
			| (cast(Instruction, 0)<<POSU_B) \
			| (cast(Instruction, 0)<<POSU_C))


namespace Celery
{
	namespace Rbx
	{
		namespace Transpiler
		{
			struct Relocation
			{
				Relocation()
				{
					fromindex = 0;
					toindex = 0;
					rawcodeindex = 0;
					shift = 0;
				}

				Relocation(size_t code_pos)
				{
					fromindex = 0;
					toindex = 0;
					rawcodeindex = code_pos;
					shift = 0;
				}

				uint32_t fromindex;
				uint32_t toindex;
				uint32_t rawcodeindex;
				uint32_t shift;
			};

			struct SelfRelocation
			{
				SelfRelocation()
				{
					prev = 0;
					self = 0;
					call = 0;
					selfindex = 0;
					callindex = 0;
				}
				
				uint32_t prev; // Bx value of a bound LOADK
				Instruction self;
				Instruction call;
				int selfindex;
				int callindex;
			};

			struct GlobalCache
			{
				int realindex;
				int newindex;
				int encoding;
				TValue old;
			};

			extern std::vector<GlobalCache> GlobalCaches;

			extern std::vector<uint32_t> convert_code(Proto*, std::vector<int>&, std::vector<size_t>&, lu_byte&);
		}
	}
}

