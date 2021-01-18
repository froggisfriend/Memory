---> Recent Updates / Change Log <---

01/18/21 - 
- Fixed calling conventions; it is now a 100% accuracy rate (this currently excludes 64-bit args.
- I tweaked memscan to find end-sections more effectively, meaning it will find all strings for XREF's

Please check out the updated documentary for how to use this API,
which is going to be here. (read below)


---> About <---

This API is designed mostly for DLL exploits to make memory exploitation easier.
An EXE version was available but it's been replaced because this one packs alot more features.
(it may be usable in EXE's soon)

This contains a variety of things such as
a disassembler (dx86.hpp)
a memory scanner (memscan.hpp)
a memory editing lib (memedit.hpp)

and more.

See below on how to set up and use these tools.



---> Documentary <---

Once ALL of the library files are added to your project,
you can begin using the api in your code by including
any one of the following (depending on your needs):

memscan.hpp,
memedit.hpp,
...

Before you use anything in the API, you have to intiate the disassembler first.
Otherwise, many of the features will break.

disassembler::open(GetCurrentProcess());

This will initiate the disassembler for your DLL.
Put it in your main function before any other API functions are used.



---> memscan.hpp <---

Without further ado,
Here's an example of setting up and running
a memory scan using memscan.hpp:


// Create a new scan object
auto functions_scan = new scanner::memscan();

// Scan xrefs of the string ": bytecode version mismatch"
// which we've shortened for a VERY fast scan.
// '1' means it should use the first string result to get the xrefs of.
// 
functions_scan->scan_xrefs(": bytec", 1);

// Grab the results
auto results = functions_scan->get_results();

auto first_xref = results.front(); /* get the first result, which will be the location in a random function where the string ": bytec" was used. */
// It is assumed here that you know what the function is

auto a_luaU_loadbuffer = get_prologue<behind>(first_xref); /* get the beginning address of the function */
  
// clean up the scan object
delete functions_scan;

// display the result
printf("luaU_loadbuffer: %p\n", raslr(a_luaU_loadbuffer));


This is all it takes to scan for XREF's of a string.


Now, a basic AOB scan is even simpler:

auto functions_scan = new scanner::memscan();
functions_scan->scan_xrefs("558BEC????8B????83????5D");

auto results_list = functions_scan->get_results();

delete functions_scan;


Easy peasy



---> routine_mgr.hpp <---

This little API really analyses functions in memory.

We have routine_mgr::get_conv, which takes the function's address,
and the number of args that the function is expected to have.

If the address is for lua_getfield, then the expected args is 3.
If the address is for lua_pushvalue, then the expected args is 2.

This will return an enum representing the correct calling convention
for the function.

This has a 100% accuracy rate in determining the convention,
because of how it works.
There is no "guessing" the convention -- it returns exactly what it is.

Obviously the down side to this is you need to enter the amount of args,
But if the number of args change, you will
need to update your exploit either way because you'll have issues.




---> memedit.hpp <---

Ah, the editing utility. My favorite part.

This is packed with common api functions, but they have alot of different options.
Let's say you want the beginning address of a function, starting at a random point inside of it:

beginning = get_prologue<behind>(address);

Let's say you want the next function in memory, starting at index2adr:

next_function = get_prologue<next>(index2adr);

You'll notice almost every function here uses a "direction"
in the template, so you can navigate memory easily
and do a number of operations.

get_call<next>(address) <--- Goes to the next function being called in memory, starting at address
get_call<behind>(address) <--- Goes to the previous function being called,  starting at address
get_calls(address) <--- Returns all of the functions that are called inside of a function (address must be the function's prologue(start address))



There will be more examples listed here




