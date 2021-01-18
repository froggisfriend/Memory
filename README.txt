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




