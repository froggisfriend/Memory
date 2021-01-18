---> About <----

This api is designed for DLL exploits, to make editing memory
as easy as Pie.



---> Documentary <----

Once the library files are added to your project,
you can begin using the api in your code by including
any one of the following (depending on your needs):

memscan.hpp
memedit.hpp
routine_mgr.hpp


You NEED To intiate the disassembler before you can use any library functions.
Otherwise, it will break.
Use disassembler::open(GetCurrentProcess());
This will initiate the disassembler for your DLL.



Here's an example of setting up and running
a memory scan using memscan.hpp:


// create a new scan object,
// which is designated to the code section's
// beginning and end point by default
// 
auto functions_scan = new scanner::memscan();

// align our scan, to go +4 at a time.
// ONLY set this to 4 if you're scanning a
// string XREF/pointer. Otherwise, it must
// be set to 1 for generic AOB scans
// 
functions_scan->set_align(4);

// scan xrefs of the string ": bytecode version mismatch"
// which we've shortened for a VERY fast scan.
// '1' implies we will use the first string result
// to get xrefs of.
// 
functions_scan->scan_xrefs(": bytec", 1);

auto results = functions_scan->get_results();
auto first_xref = results.front(); /* get the first result, which is a random spot in a random function where the string ": bytec" was used. */
auto a_luaU_loadbuffer = get_prologue<behind>(first_xref); /* get the beginning address of the function */
  
delete functions_scan;

// display results
printf("luaU_loadbuffer: %p\n", raslr(a_luaU_loadbuffer));




