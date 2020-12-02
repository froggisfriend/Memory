#include "memedit.hpp"

bool is_function(uintptr_t address)
{
    const auto uint8_ts = memread<uint8_t>(address, 3);

    // DLL export prologue...
    // some games dynamically allocate their own
    // copies of these functions, and they're not
    // 16-uint8_t aligned.
    // 
    if (memcmp(uint8_ts, { 0x8B, 0xFF, 0x55 }))
        return 1;

    if (address % 0x10 != 0)
        return 0;

    // run through the most common prologues
    // 
    if (
        memcmp(uint8_ts, { 0x53, 0x8B, 0xDC }) // push ebx  |  mov ebx, esp
     || memcmp(uint8_ts, { 0x55, 0x8B, 0xEC }) // push ebp  |  mov ebp, esp
     || memcmp(uint8_ts, { 0x56, 0x8B, 0xF4 }) // push esi  |  mov esi, esp
     || memcmp(uint8_ts, { 0x57, 0x8B, 0xFC }) // push edi  |  mov edi, esp
    ){ 
        return 1;
    }

    // Was the previous uint8_t an align?
    // this may be a naked function
    // 
    if (*reinterpret_cast<uint8_t*>(address - 1) == 0xCC)
    {
        return 1;
    }
    
    return 0;
}


uintptr_t is_call(uintptr_t address)
{
    // check if it's a call or a jmp
    //
    if (*reinterpret_cast<uint8_t*>(address) == 0xE8 // call rel32
     || *reinterpret_cast<uint8_t*>(address) == 0xE9 // jmp rel32
    ){
        // get the relative offset and its destination
        // 
        uintptr_t rel = *reinterpret_cast<uintptr_t*>(address + 1);
        uintptr_t func = address + 5 + rel;

        // check if it lies almost within code bounds
        // 
        if (func % 0x10 == 0 && func > *reinterpret_cast<uintptr_t*>(__readfsdword(0x30) + 8) && func < 0x07FFFFFF)
        {
            // Query the page now that it's more appropriate
            // to make this call
            // 
            MEMORY_BASIC_INFORMATION page = { 0 };
            VirtualQuery(reinterpret_cast<void*>(func), &page, sizeof(page));

            // Verify that this is IS part of the code page
            //
            if (page.State & MEM_COMMIT && page.Protect & PAGE_EXECUTE_READ)
            {
                // Verify that this is the beginning address of
                // a function.
                // 
                if (is_function(func)) 
                {
                    return func;
                }
            }
        }
    }

    return 0;
}

uint16_t get_return(uintptr_t address)
{
    auto bytes = memread<uint8_t>(address - 1, 2);

    uint8_t prev = bytes[0];
    uint8_t ep = bytes[1];

    // check if it's a common epilogue
    // 
    if (
        ep == 0xC2 // ret
     || ep == 0xC3 // retn
     || ep == 0xC9 // leave
    ){
        // run through the most common registers
        // that a function is initialized with...
        // to be sure that this is the EOF
        // 
        if (
            prev == 0x5B // pop ebx
         || prev == 0x5D // pop ebp
         || prev == 0x5E // pop esi
         || prev == 0x5F // pop edi
        ){
            switch (ep)
            {
            case 0xC2: // get the value from a 'ret' instruction
            {
                uint16_t r = memread<uint16_t>(address + 1);

                // double check that this is not a false reading
                // I guess
                if (r % 4 == 0 && r < 1024)
                {
                    return r;
                }
            }
            default:
                return 0;
            }
        }
    }

    return -1;
}


std::vector<uintptr_t> get_calls(uintptr_t func)
{
    std::vector<uintptr_t> calls = { };

    uintptr_t call;
    uintptr_t at = func;
    uintptr_t end = get_prologue<next>(func);

    while (at < end)
    {
        call = is_call(at++);

        if (call)
        {
            calls.push_back(call);
            call = 0;
        }
    }

    return calls;
}



void memcpy_safe_padded(void* destination, void* source, const size_t size)
{
    uint8_t source_buffer[8];

    if (size <= 8)
    {
        // Pad the source buffer with uint8_ts from destination
        memcpy(source_buffer, destination, 8);
        memcpy(source_buffer, source, size);

        // Perform an interlocked exchange on the
        // source and destination
        // 
        #ifndef NO_INLINE_ASM
        __asm
        {
            lea esi, source_buffer;
            mov edi, destination;

            mov eax, [edi];
            mov edx, [edi + 4];
            mov ebx, [esi];
            mov ecx, [esi + 4];

            lock cmpxchg8b[edi];
        }
        #else
        _InterlockedCompareExchange64(
             reinterpret_cast<ULONGLONG*>(destination), 
            *reinterpret_cast<ULONGLONG*>(source_buffer), 
            *reinterpret_cast<ULONGLONG*>(destination)
        );
        #endif
    }
}

bool memcmp(const std::vector<uint8_t>& uint8_ts_a, const std::vector<uint8_t>& uint8_ts_b)
{
    if (uint8_ts_a.size() != uint8_ts_b.size())
    {
        return false;
    }

    size_t count = 0;

    for (const auto& uint8_t : uint8_ts_a)
    {
        if (uint8_ts_b[count] != uint8_t)
        {
            break;
        }

        count++;
    }

    return count == uint8_ts_b.size();
}

bool memcmp(void* address, const std::vector<uint8_t>& uint8_ts_compare)
{
    auto uint8_ts = memread<uint8_t>(address, uint8_ts_compare.size());

    return memcmp(uint8_ts, uint8_ts_compare);
}

bool memcmp(uintptr_t address, const std::vector<uint8_t>& uint8_ts)
{
    return memcmp(reinterpret_cast<void*>(address), uint8_ts);
}
