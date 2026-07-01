#if 0   // 1 to enable debug messages tracking DFA progress
   #define DFA_DEBUG 1
   #define DEBUGCODE(code) code;
#else
   #define DEBUGCODE(code)
#endif

#if 0
*****************************************
Synopsis: Regular expression engine with ERE syntax for ASCII text.

Syntax: {}[]()^$.|*+?\  \d\D  \s\S  \w\W \<\>  \n  (?:)
    d (digit), s (space), w (word), <> (begin/end word),  (?:) non-capturing
See below for details.

A non-recursive back tracking regular expression engine.

Two C files: zkRE.[ch], thread safe, public domain

Limitations:
 - NO support for non-ASCII text
 - Group values can differ from recursive engines (eg PCRE) or POSIX RE
    * Results can differ: eg "(a|ab)(bc|c)" match "abcabc" 
      --> \1=="a", \2=="bc" not \1=="ab", \2=="c"
    * Count and index can differ
      eg "a(b)|c(d)|a(e)f" match"aef" --> \1=="e", not \1==\2=="", \3=="e"
 - Some group closures not supported, eg (a+b)+c, (a|b)+
 - The width of the match tree is limited: viewing a match as a breadth
   first search, the number of nodes/level is limited (".*a" match
   "1234a67890" is width 11, ".*(a|b)" doubles the width. The compiler
   tweaks and the VM prunes to control growth, not always successfully.

Tests: 750+ hand written tests, 221 are Henry Spencer's regular
expression tests (10 of which were modified).

Examples:
// clang egRE.c zkRE.c
// clang will compile tail call VM, gcc & MSVC won't so they get a big switch

#include <stdio.h>
#include <string.h>
#include "zkRE.h"

static void doRE(char *re, char *text, int flags){
   char *tags[2 * RE_MAX_TAG], *ptr;   // tags is optional
   Byte  dfa[2000];
   int   n,s;

   n = sizeof(dfa);
   if( (ptr = regExpCompile(re,dfa,&n)) ){ printf("%s\n",ptr); return; }
   printf("%s --> %d byte DFA\n",re,n);
   if(regExpMatch(dfa,text,flags,tags,0)){
      printf("Match: %s  %s",re,text);
      if(tags[1]){
	 n = tags[RE_MAX_TAG + 1] - tags[1];
         strncpy((char *)dfa,tags[1],n);
         dfa[n] = '\0';
         printf("\t\\1 == %s",dfa);
      }
      printf("\n");
   }
}
int main(int argc, char* argv[]){
   doRE("(ab|a)bc","abc",0x0);          // match \1 == "a"   (21 byte DFA)
   doRE("(dog|cat)\\1","catcat",0x0);   // match             (25 byte DFA)
   doRE("(a.c){1,2}","abcadcaec",0x0);  // match \1 == "adc" (17 byte DFA)
   doRE("(ab*c)+","abbbcacab",0x0);     // match \1 == "ac"  (20 byte DFA)
   doRE("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaaaaa",0x0);     // match              (102 byte DFA)
   doRE("(test\\w*)","it was a testing time",RE_SEARCH);  // \1-->"testing"
   return 0;
}
---------------------------------------
// The following is also text in zkRE.c
zkl: var d=File("VM/zkRE.c").read()   // has (65O) 253-0001. in last line
Data(103,515)

zkl: var r=RegExp(0''(\d{3}-|\(\d{3}\)\s+)(\d{3}-\d{4})')
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.057324
zkl: r.matched
L(L(103379,14),"(65O) ","253-0001")    // 65"O" is zero, don't match here

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$')
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.011371 // short circuits by noting that ABC...XYZ is not contained in text
        // ie strstr() and fail 2,786 times, up to this comment @ line 2,786

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZaaa$')  // aaa is AAA
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
2e-05 // strstr(ABCDE...XYZAAA) ONCE and fail
 // Meta fail: this comment is in search text, aaa/AAA to avoid false match
*************************************
#endif

/*
 * zkRE.c - Regular expression pattern matching and searching
 *
 * UTF-8:  No.  Eight bit safe but multibyte characters are going to cause
 *   problems, as in works but wrong results.
 * 
 * By:  Ozan S. Yigit (oz), Dept. of Computer Science, York University
 * Mods, oh so many mods (such as "|", CLOP, STR and HOLDS, tail call engine,
 *   {}, "back tracking" for "|?*+").
 * C Durland craigd@zenkinetic.com
 *
 * These routines are the PUBLIC DOMAIN equivalents of regex routines as
 * found in 4.nBSD UN*X, with minor extensions.
 * Since moved towards Extended Regular Syntax (ERE) and PCRE behavior.
 * 
 * These routines are derived from various implementations found in software
 * tools books, and Conroy's grep.  They are NOT derived from
 * licensed/restricted software.  For more interesting/academic/complicated
 * implementations, see Henry Spencer's regexp routines, or GNU Emacs
 * pattern matching module.
 * Note: I think I'm gettting closer to parity with ERE engines (CD).
 *
 * DFA = Deterministic Finite Automata
 * Routines (also in zkRE.h) See also regcomp(3), regexec(3):
 *  regExpCompile: Compile a regular expression into a DFA.
 *	char *regExpCompile(char *pattern, Byte *dfa, int *dfaSz)
 *	Returns: NULL if OK, else error string
 *	1000 is a decent DFA size for run of the mill expressions,
 *	3000 will handle some pretty nasty machine generated ones.
 *	I like to use a big buffer and post allocate.
 *  regExpMatch: Run the DFA to match a pattern.
 *	int regExpMatch(Byte *dfa, char *textToMatch
 *	unsigned int flags, char *tags[], ReErrorPacket *ep);
 *  dfaDump: Print the DFA.
 *  See the actual routines for what the paramaters are and zkRE.h for flags.

 * Regular Expressions:
 *   [1]  char		Matches itself, unless it is a special
 *                      character (metachar): . \ [ ( | * + ? ^ $ sometimes {
 *
 *   [2]  .		Matches any character.
 *
 *   [3]  \		Matches the character following it, except
 *			when followed by one of: 123456789<>dDsSwW
 *			See [9 - 13]
 *			It is used as an escape character for all other
 *			meta-characters, and itself.  When used in a set
 *			([4]), it is treated as an ordinary character.
 *
 *   [4]  [set]		Matches one of the characters in the set.
 *                      If the first character in the set is "^",
 *                      it matches a character NOT in the set. A
 *                      shorthand S-E is used to specify a set of
 *                      characters S upto E, inclusive. The special
 *                      characters "]" and "-" have no special
 *                      meaning if they appear as the first chars
 *                      in the set.
 *                      Example:   Matches:
 *			--------   -------
 *			[a-z]	   Any lowercase alpha.
 *
 *			[^]-]      Any char except ] and -.
 *
 *			[^A-Z]     Any char except uppercase alpha.
 *
 *			[a-zA-Z]   Any alpha.
 *
 *                      [a-b-c] == [a-bb-c] == [a-c]
 *                      [a-a] == [a]
 *			[-abc] == [abc-]  Match -, a, b, c.
 *
 *			[]] == ]   Match "]"
 *			[]-]	   Match only "-]".  This is a set ([-])
 *				   and a character (]).
 *			[z-a]      Nothing and is an error.
 *
 *   [5]  *		Any regular expression form [1 - 4, 10 - 12, 14], 
 *			followed by closure char (*) matches zero or more
 *			matches of that form. Not [9] because I'm lazy..
 *                      Greedy: the longest match is given preference.
 *	 (a.b)*		Note for [14]: Only if [14] does not contain any of
 *			  [5 - 8, 15]. ie the tag can not branch and the
 *			  closure width must be constant, so no "(a+b)+c"
 *			  or "(a|b)+".
 *			The compiler needs to make more effort to determine
 *			  which tags can be closed.
 *                      Back tracks. See [18].
 *
 *   [6]  +		Same as [5], except it matches one or more.
 *                      Greedy: the longest match is given preference.
 *                      Back tracks. See [18].
 *
 *   [7]  ?		None or one, otherwise same as [5]
 *                      Greedy: the longest match is given preference.
 *                      Back tracks. See [18].
 *
 *   [8]  {n}		Match exactly n times == {n,n}
 *	  {min,}	Match min or more times. {min,0} == error
 *	  {,max}	Match up to max times == {0,max}
 *	  {0,max}	Match "at most" max times. {0,0} == error
 *	  {m,n}		Match at least min times, but not more than max times.
 *	  {0} {,0} {0,0} Invalid. Because I say so.
 *			If not one of these forms, match the characters.
 *			   eg {,} --> \{,\}, "{m,n}" --> \{m,n\}
 *			m>n==error, m or n > 255 == error
 *			x* y+ z? is equivalent to x{0,} y{1,} z{0,1}
 *			A variant of [5], back tracks. See [18].
 *              
 *   [9]  \1 \2\ \3	A \ followed by a digit 1 to 9 matches [verbatum] 
 *			whatever a previously tagged regular expression ([14]) 
 *			matched.
 *
 *  [10]  \d \D		Match (or not) digit: [0-9], [^0-9]
 *
 *  [11]  \s \S		Match (or not) whitespace: Typically "\t\n\r\f\x0b"
 *  
 *  [12]  \w \W		Match (or not) alphanumeric (word): 
 *			Typically "[a-zA-Z_]". Varies as app can change
 *			the definition.
 *			
 *  [13]  \<		A regular expression starting with a \< construct
 *	  \>		and ending with a \> construct, restricts the
 *			pattern matching to the beginning of a word,
 *			or the end of a word.  A word is defined to be
 *			a character string beginning and/or ending with
 *			[12].  It must also be preceded and/or followed by
 *			any character outside those mentioned.
 *
 *  [14]  (		A regular expression in the form [1] to [13], enclosed
 *			  as (form) matches what form matches. The
 *			  enclosure creates a set of tags, used for [9]
 *			  and for pattern substution. The tagged forms are
 *			  numbered starting from 1.
 *			They also group "|"s [15], ie () has higher
 *			  precedence than |
 *	  (?:		Tagless (non-capturing) group: match not saved.
 *
 *  [15]  A|B     	Matches subexpression A, or failing that, matches B.
 *                      Longest match wins as does first to match all text.
 *			Preference is left to right but not guaranteed,
 *			  there are cases where a "right" path can match
 *			  all text before a "left" path.
 *      		() has precedence. 
 *      		Back tracks. See [18].
 *
 *  [16]		A composite regular expression xy where x and y
 *                      are in the form [1] to [14] matches the longest
 *                      match of x followed by a match for y.
 *
 *  [17]  ^		A regular expression starting with a ^ character
 *	  $		and/or ending with a $ character, restricts the
 *                      pattern matching to the beginning of the text,
 *                      or the end of text. [anchors]
 *                      ^(abc$|^cde)$
 *			Does not know about '\n'.
 *
 *  [18]  "Back tracking". The RE is traversed and the path with the
 *	  longest total match (the path that consumes the greatest amount
 *	  of text) wins. Precedence is given to greedy behavior ([5-8] and
 *	  left to right in OR clauses ([15], eager). If multiple paths
 *	  have the same "winning" length, the first to get there wins. The
 *	  first to match the entire text wins (the longest possible). A
 *	  dynamic tree is built during match with [5-8 & 14] as branches.
 *	  This behavior is designed to mimic "traditional" RE behavior
 *	  (recursive decent engines) with the least amout of
 *	  effort/recursion.
 *	  It is NOT Posix behavior.
 *
 * Acknowledgements:
 *   HCR's Hugh Redelmeier has been most helpful in various stages of
 *   development.  He convinced me to include BOW and EOW constructs,
 *   originally invented by Rob Pike at the University of Toronto.
 * References:
 *   Software tools		Kernighan & Plauger
 *   Software tools in Pascal	Kernighan & Plauger
 *   Grep [rsx-11 C dist]	David Conroy
 *   ed - text editor		Un*x Programmer's Manual
 *   Advanced editing on Un*x	B. W. Kernighan
 *   RegExp routines		Henry Spencer
 *   "Regular Expression Matching Can Be Simple And Fast"
 *      https://swtch.com/~rsc/regexp/regexp1.html 
 *   "Regular Expression Matching: the Virtual Machine Approach"
 *      https://swtch.com/~rsc/regexp/regexp2.html
 *   https://en.wikipedia.org/wiki/Regular_expression
 *   The Stack Overflow Regular Expressions FAQ:
 *      https://stackoverflow.com/questions/22937618/reference-what-does-this-regex-mean/22944075#22944075
 *   https://wiki.haskell.org/Regular_expressions#.28apple.7Corange.29
 *   https://wiki.haskell.org/Regex_Posix : Posix compliant Regex bugs
 *   https://regexr.com/
 *   https://regex101.com/
 * 
 * Notes:
 *  This implementation uses a bit-set representation for character sets for
 *    speed and compactness.  Each character is represented by one bit in a
 *    N-bit block.  Thus, SET or NSET always takes a constant M bytes in the
 *    internal dfa, and SET/NSET does a single bit comparison to locate the
 *    character in the set. N is 128 for 7 bits ASCII and 256 for 8 bit
 *    data. Thus M is 16 or 32 bytes.
 *  Put CLO in front of what gets closed for ease of interpreting.
 *  Put END at end of what gets closed to limit recursion.
 *  As I was adding back tracking, I read Russ Cox's papers on regular
 *    expressions & Thompson NFAs and changed my approach (ie said "ohh, I
 *    like that!) and multi-threaded the engine (see references).
 *    Minimal recursion.
 * Examples:
 *	pattern:	foo*.*
 *	compile:	CHR f CHR o CLO CHR o END CLO ANY END END
 *	matches:	fo foo fooo foobar fobar foxx ...
 *
 *	pattern:	fo[ob]a[rz]	
 *	compile:	CHR f CHR o SET bitset CHR a SET bitset END
 *	matches:	fobar fooar fobaz fooaz
 *
 *	pattern:	foo\\+
 *	compile:	CHR f CHR o CHR o CLOP CHR \ END END
 *	matches:	foo\ foo\\ foo\\\  ...
 *
 *	pattern:	(foo)[1-3]\1	(same as foo[1-3]foo)
 *	compile:	BOT 1 CHR f CHR o CHR o EOT 1 SET bitset REF 1 END
 *	matches:	foo1foo foo2foo foo3foo
 *
 *	pattern:	(fo.*)-\1
 *	compile:	BOT 1 CHR f CHR o CLO ANY END EOT 1 CHR - REF 1 END
 *	matches:	foo-foo fo-fo fob-fob foobar-foobar ...
 *
 *	pattern:	(?:ab|a)bc
 *	compile:	NODE CHR a CHR b AORB 0 0 CHR a EDON CHR b CHR c END
 *	matches:	abc abbc
 *
 *	pattern:	a?a?a?a?a?a?a?a?a?a?aaaaaaaaaa    "a?"*n + "a"*n, n=10
 *	compile:	ONE CHR a END <repeat> STR 11 aaaaaaaaaa\0 END
 *	matches:	aaaaaaaaaa  "a"*(x >= n)
 *	This is Russ Cox's nasty case that causes great discomfort for
 *	recursive engines. Not an issue for me although I can dead lock
 *	(:-). 20 fibers can handle n==19, 30 Fibers can handle n==29
 *	quickly.
 *	
 *	See mndfa (search below) for an example of encoding a DFA into an
 *	array and running it. Don't do that because I *will* change the op
 *	codes, use regExpCompile() at runtime.
 */

//#define EXTEND	// \bnfrt: "\n" --> newline, "\t" --> tab, etc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HOME_BREW_CTYPE_H	// your versions of isalnum, isdigit, isspace
   #include "char.h"	// kinda silly to hardwire the name
#else
   #include <ctype.h>	// most of us
#endif

#include "zkRE.h"	// constants, types and prototypes for this code

#define END	 0
#define CHR	 1	// character		:: CHR <character>
#define ANY	 2	// .			:: ANY
#define SET	 3	// set: [...]		:: SET bitset
#define NSET	 4	// not set: [^...]	:: SET bitset
#define BOL	 5	// beginning of text: ^ :: BOL
#define EOL	 6	// $: end of text	:: EOL
#define BOT	 7	// (: beginning of tag	:: BOT <n>  Open Tag
#define EOT	 8	// ): end of tag	:: EOT <n>  Close/set tag
#define BOW	 9	// \<: beginning of word
#define EOW	10	// \>: end of word	:: EOW
#define REF	11	// \1 .. \9: tag reference :: REF <1-9>
#define DIGIT	12	// \d: match isdigit()	:: DIGIT
#define N_DIGIT	13	// \D: match !isdigit()
#define SPACE	14	// \s: match isspace()
#define N_SPACE	15	// \S: match !isspace()
#define ALPHA	16	// \w: match isalnum() + "_" (Alphanumeric+)
#define N_ALPHA	17	// \W: match !\w
#define CLO	18	// *: closure: none or more :: CLO  dfa END
#define ONE	19	// ?: none or one	    :: ONE  dfa END
#define CLOP	20	// +:  one or more	    :: CLOP dfa END
#define CLOMN	21	// a{m,n}		    :: CLOMN M N dfa END
#define AORB	22	// A|B, (A|B) :: AORB <open tags> <hops to sibling OR>
#define NODE	23	// Begin node: a node is a group with OR, tree vertex
#define EDON	24	// End node
#define STR	25	// Match string :: STR <byte len><string>
#define HOLDS	26   // OPTIONAL. text must hold STR :: HOLDS <top hops to STR>
#define PACMAN	27	// Next closure op doesn't fork :: PACMAN CLO
//#define DOTSTAR	28	// .* CHR | STR :: DOTSTAR CHR a | DOTSTAR STR n text

#define LAST_OP  27	// so I can do sanity checks

   /* Notes on HOLDS: An attempt to fail fast if text doesn't hold a string.
    * This assumes memmem/strstr is "reasonably" quick and the DFA would not
    * be, eg, matching a big text with ".*expression". Or actually
    * searching: move==1.
    */
#define DO_HOLDS   1
#define ANDTHENULL 1	// gotta be 1 for Windows as they don't have memmem(3)


#define IS_ALPHA(c)	(isalnum(c) || c == '_')     // upper/lower + digits

/* ******************************************************************** */
/* **************************** Bit Tables **************************** */
/* ******************************************************************** */

/* 
 * Bit table:  a string of bits stored in an array of char
 *     .-----------------------------------.
 *     |01234567|89012345|67890123|45678901|
 *     `-----------------------------------'
 *   bits: [0]      [1]      [2]      [3]
 * To find what bucket the nth bit is in (8 bits per bucket):
 *       bit_bucket(n) = bits[n/8]
 *   It might be a good idea to restrict n so it doesn't index outside its
 *     range (only works if number of bits is a power of 2):
 *       n = n & ((max_n - 1) & ~7)  where max_n is a power of 2
 *     The ~7 is just to get rid of the lower bits that won't do anything
 *	 anyway.
 * The nth bit in the bucket is n mod 8 (ie the lower 3 bits of n (0-7) are
 *   the bit position):
 *       bit_flag(n) = 1 << (n & 7)
 * To find the state of the nth bit (0 == off and !0 == on):
 *       bit_bucket(n) & bit_flag(n)
 * To set the nth bit:
 *       bits[bit_bucket(n)] |= bit_flag(n)
 * Notes:
 *   The bits are stored in a character array so that the array can be
 *     easily copied without worrying about alignment (ie can use a loop as
 *     well as memcpy()).
 *   This is based on two's complement math.
 */

/* The following defines are for character set size. 128 for straight ASCII,
 *   256 for Euro ASCII (8 bit characters).
 */
#define MAXCHR	 256		//  128 or  256
#define BLKIND	0xf8		// 0x78 or 0xf8

/* The following defines are not meant to be changeable.
 * They are for readability only.
 */
#define CHRBIT	8
#define BITBLK	MAXCHR/CHRBIT		// 16 or 32 bytes
#define BITIND	0x7

    /* Add or check to see if character is in bit table (character set).
     * Note:
     *   When calling these routines, make sure c is an unsigned char (or
     *     int) so if it has the high bit set, casting it to an int won't
     *     make it a large negative number.
     */
#define ISINSET(bittab,c) ((bittab)[((c) & BLKIND)>>3] & (1<<((c) & BITIND)))
#define CHSET(  bittab,c)  (bittab)[((c) & BLKIND)>>3] |= 1<<((c) & BITIND)

static void chset(Byte *bitTable, Byte c){ CHSET(bitTable,c); }

static Byte *dfaScanForward(Byte *dfa, int stopAt, int inThisNode);

#if HOME_BREW_CTYPE_H	// isalnum, isdigit, isspace
   #define IS_WORD(c)	isword(c)	// not in <ctype.h>
#else
   static Byte wordTable[BITBLK]; 	// bit table for word definition
   static int  wordTableDefined = 0;

   #define IS_WORD(c)	ISINSET(wordTable,c)
#endif

    // This code has optimizations that assume match is case sensitive
#define CEQ(a,b) 	((a) == (b))		// character ==


  /////////////////////////////////////////////////////////////////////////
 //////////////////// Compile expression to DFA //////////////////////////
/////////////////////////////////////////////////////////////////////////

#define RE_SLOP	50		// dfa overflow protection

#define BADPAT(dfa,msg)	return (dfa[1] = END, (char *)msg)
#define STORE(x)	(*mp++ = x)  // RE_SLOP guards against overflow
 
      // info for (), | and STR
typedef struct{ int sz, maxSz; Byte *mp; char string[270]; } Str;
typedef struct{ 
   int   tagc,    nodeId, orCnt, hasOR, cosmetic, forks, vwidth;
   Byte *orAddr, *botAddr;
}Tag;

static UChar *storeCHR(Byte *mp, char, Str *, Byte *sp, Byte **lp);
static Byte *chr2str(
   UChar *lp, UChar *mp, Str *str, int movePrev,
   int tagi, Tag *tagstk);
int packRat(Byte *sp, UChar *p);

    /* Compile RE to internal format & store in dfa[]
     * Input:
     *   pat:   Pointer to regular expression string to compile.
     *   dfa:   Pointer to dfa[*dfaSz] where DFA will be stored
     *   dfaSz: Pointer to size of buffer allocated for DFA.
     *          Returns size actually used. You can aloocate & copy dfa.
     * Returns:
     *   NULL:  RE compiled OK.
     *   	Pointer to error message. *DON'T* use the resulting DFA!
     *   	!!! would be nice to know if dfa overflow
     *   dfaSz: Modified to size of actual DFA
     */
char *regExpCompile(char *pattern, Byte dfa[], int *dfaSz){
      // tagstk holds which tag is open & 
      // the previous & current siblings of the OR tree
   Tag tagstk[RE_MAX_TAG] = { 0 }; // subpat tag & OR tree stack

   UChar *pat = (UChar *)pattern, *p; // pattern pointers
   Byte
     *mp = dfa + 1,	// dfa pointer for STORE
     *sp = dfa + 1,	// another one
     *lp,		// saved pointer
     *endDFA = dfa + *dfaSz - RE_SLOP,	// space available w/overflow checking
      bittab[BITBLK] = { 0 };	// bit table for SET
   int
      tagi   = 0,	// tag stack index
      tagc   = 1,	// actual tag count
      ortagc = 0,	// keep tag count in sync with ORs
      topor  = 0,	// top level OR, ie outside of any group
      n,z;
   Tag *tp;
   Str  str;
	// id for OR tree node, unique id for every group
   unsigned nodeId = 0, orCnt = 0;

   if(*dfaSz < (30 + RE_SLOP))
      BADPAT(dfa,"regExpCompile: dfa too small to hold anything meaningful");

   str.sz = str.maxSz = 0;

   #if !HOME_BREW_CTYPE_H
	// Build a bit table definition of a word. Done once.
        // Not thread safe so I call during VM construction
	// Not thread safe
   if(!wordTableDefined){
      wordTableDefined = 1;
      for(n = 0; n <= 0xff; n++)
         if(IS_ALPHA(n)) CHSET(wordTable,n);
   }
   #endif //HOME_BREW_CTYPE_H

   if(pat == 0 || *pat == '\0')
      BADPAT(dfa,"regExpCompile: Bad regular expression");

   *dfa = 0;	// DFA flags

   for(p = pat; *p; p++){
      lp = mp;			// start of next dfa state
      switch(*p){
	 case '.': STORE(ANY); break;		// match any character
	 case '^':		// match beginning of line: "^..", "..|^.."
	    STORE(BOL); 
	    tagstk[tagi].forks = 1;	// (^)* == infinite loop
	    break;
	 case '$':			// match end of line: "..$", "..$|.."
	    STORE(EOL);
	    tagstk[tagi].forks = 1;		// if we are actually in a tag
	    break;
	 case '[':				// match a set of characters
	    if(p[2]==']' && p[1]!='^'){
	       // [.] --> CHR . Russ says people do that
	       mp = storeCHR(mp,p[1],&str,sp,&lp);
	       p += 2;
	       break;
	    }
	    if(*++p == '^'){ STORE(NSET); p++; } else STORE(SET);
	    if(*p == ']') chset(bittab,*p++);	// real bracket, match ']'
	    if(*p == '-') chset(bittab,*p++);	// real dash,    match '-'
 	    while(*p && *p != ']'){	// won't CHSET(0)
	       if(*p == '-' && *(p+1) != '\0' && *(p+1) != ']'){  // [a-z]
		  int c1, c2;
		  p++;
		  c1 = *(p-2);		// 'a'
		  c2 = *p++;		// 'z'
		  if(c1 > c2)		// something like [z-a]
		     BADPAT(dfa,"regExpCompile: Empty set");
		    // remember that 'a' has already been put into bittab
		  while(++c1 <= c2) chset(bittab,c1);	// build bit table
	       }
	    #ifdef EXTEND
	       //else if(*p == '\\' && *(p+1)) { p++; chset(bittab,*p++); }
	       else if(*p == '\\' && *(p+1)){
		  char c = *++p;
		  switch(c){
		     case 'b': c = '\b'; break;
		     case 'n': c = '\n'; break;
		     case 'f': c = '\f'; break;
		     case 'r': c = '\r'; break;
		     case 't': c = '\t'; break;
		  }
		  chset(bittab,c); p++;
	       }
	    #endif
	       else chset(bittab,*p++);
	    } // while
	    if(*p == '\0') BADPAT(dfa,"regExpCompile: Missing ]");
		// store table and clear for next use
	    for(n = 0; n < BITBLK; bittab[n++] = '\0') STORE(bittab[n]);
	    break;
	 case '*': z = CLO;  goto clo;	// match 0 or more of preceding RE
	 case '+': z = CLOP; goto clo;	// match 1 or more.  Note: x+ == xx*
	 case '?': z = ONE;		// match none or one
	 clo:
	 {
	    int pacman, hz;

	    if(p == pat) BADPAT(dfa,"regExpCompile: Empty closure");
	    n = (*sp==CHR);	// remember this for later

		// equivalence: x** == x*, CLO CLO --> CLO
	  //if(*sp==CLO || *sp==CLOP) break;
	    if(*sp==CLO || *sp==CLOP || *sp==ONE) // just no
	    	BADPAT(dfa,"regExpCompile: ** not allowed");

	 #if 0	//#ifdef ONE
	    if(*sp == ONE) break;	// equivalence: x?? == x
	 #endif
	    switch(*sp){	// some redundancy here for CYA
	       default: BADPAT(dfa,"regExpCompile: Invalid closure");
	       case CHR:   case ANY:     case SET:   case NSET: 
	       case DIGIT: case N_DIGIT: case SPACE: case N_SPACE:
	       case ALPHA: case N_ALPHA: case EOT:
	          break;
	    }

	    pacman = packRat(sp,p); // gonna fork or just consume?
	    lp	   = sp;		// previous opcode

	    if(z==CLOP && *sp==ANY)  // ".+" --> "..*" as it is a special case
	       { z = CLO; STORE(ANY); lp++; }

	    if(*sp==EOT){
	       // (abc)* --> CLO BOT 11 BOT 1 CHR a CHR b CHR c EOT 1 END
	       int sz;

	       tp = &tagstk[tagi + 1];	// (abc)<--sp
	       if(tp->forks || (tp->vwidth && !pacman))
		  BADPAT(dfa,"regExpCompile: (a*b)*c, (a|b)*: Invalid closure");

	       lp = tp->botAddr; tp->botAddr += 2;
	       sz = mp - lp;	// alt: call dfaScanForward() at runtime, ick
	       if(sz > 0xfe) BADPAT(dfa,"regExpCompile: (a)*: a too long");

	       hz = 3 + pacman;
	       memmove(lp + hz, lp, sz);  // open hole for CLO BOT sz 
	       sp = mp + hz; mp = lp; 
	       if(pacman) STORE(PACMAN);
	       STORE(z);  STORE(BOT); STORE(sz + 1);
	       mp = sp;   STORE(END);
	    }else{	// a* --> CLO CHR a END
	       hz = 1 + pacman;
	       memmove(lp + hz, lp, mp - lp);	// open hole for CLO
	       sp = mp + hz; mp = lp; 
	       if(pacman) STORE(PACMAN);
	       STORE(z); mp = sp; STORE(END);
	    }
	    tagstk[tagi].forks  = !pacman;	// if we are actually in a tag
	    tagstk[tagi].vwidth = 1;

	    // lp --> CLO|CLOP|ONE CHR a END
	       // pack strings? "ab*" --> CHR a CHR b --> CHR a CLO CHR b END
	    if(n){	// multi op op messes with check at end of switch
	       str.sz--;	// "123456+" --> STR(12345) CLOP 6
	       lp = chr2str(lp,mp, &str, 1, tagi,tagstk); // --> CLO|CLOP|ONE
	       mp = str.mp;
	    }
	    sp = lp;	// CLO|CLOP|ONE
	    // leave lp & sp pointing to CLO|CLOP|ONE so can check for **
	    // and want them pointing to vaild non CHR ops
	    break;
         }
	 case '{':	// {M,N}, {N}, {,N}, {M,}  invalid form --> CHR {
	 {
	    char *tags[2 * RE_MAX_TAG];
	    int   M = 0, N = 0, pacman = 0, hz;
	    Byte  mndfa[] = { // RegExp(0''{(?:(\d+)|(\d*),(\d*))}') eat dogfood
	      0,CHR,'{',NODE,BOT,1,PACMAN,CLOP,DIGIT,END,EOT,1,  AORB,0,0,
		 	     BOT,1,PACMAN,CLO, DIGIT,END,EOT,1,  CHR,',',
	                     BOT,2,PACMAN,CLO, DIGIT,END,EOT,2,
			EDON,
		CHR,'}',END };
	    	
	    z = 0;
	    if(regExpMatch(mndfa,(char*)p,0,tags,0)){
	       // {,} z==0, {m,} z==1, {,n} z==2, {m,n} z==3, {n} z==4
	       char **eopat = &tags[RE_MAX_TAG]; // atoi stops at [^0-9]
	       if(tags[1]!=eopat[1])     { M = atoi(tags[1]); z=1;  }
	       if(!tags[2])		 { N = M;	      z=4;  } //{n}
	       else if(tags[2]!=eopat[2]){ N = atoi(tags[2]); z|=2; }
	       if(!z){ mp = storeCHR(mp,'{',&str,sp, &lp); break; } // {,}

	       switch(*sp){	// some redundancy here for CYA
		  default: BADPAT(dfa,"regExpCompile: Invalid closure");
		  case CHR:   case ANY:     case SET:   case NSET: 
		  case DIGIT: case N_DIGIT: case SPACE: case N_SPACE:
		  case ALPHA: case N_ALPHA: case EOT:
		     break;
	       }

	       if(p == pat) BADPAT(dfa,"regExpCompile: Empty closure");
	       p      = (UChar *)eopat[0] - 1;	// '}'
	       pacman = packRat(sp,p);	// gonna fork or just consume?

	       if(M!=N || z==1){// {n}(4) & {n,n}(3) don't fork(), {0,}(1) does
		  tagstk[tagi].forks  = !pacman; // if we are actually in a tag
		  tagstk[tagi].vwidth = 1;	 // a{2,3} 
	       }else pacman = 0;	// {n}: fixed width match
	    }else{
	       mp = storeCHR(mp,'{',&str,sp,&lp);   // context matters
	       break;			     // not CLOMN, just text
	    }
	    if(M>0xff || N>0xff || (M>N && N))
	       BADPAT(dfa,"regExpCompile: {m,n}: m <= n < 256");
	    if((M==0 && N==0) && z>1)  // {,0}(z==2), {0,0}(z==3), {0}(z==4)
	       BADPAT(dfa,"regExpCompile: {0} & {0,0}: Invalid");
	    
	    n  = (*sp==CHR);	// remember this for later
	    lp = sp;		// previous opcode

	    if(*sp==EOT){
	       // (a){1,2} --> CLOMN 1 2 BOT 7 BOT 1 CHR a CHR b EOT 1 END
	       int sz;

	       tp = &tagstk[tagi + 1];
	       if(tp->forks || (tp->vwidth && !pacman))
		  BADPAT(dfa,"regExpCompile: (a*){m,n}, (a|b){m,n}: Invalid closures");

	       lp = tp->botAddr; tp->botAddr += 2;
	       sz = mp - lp;
	       if(sz > 0xfe)
		  BADPAT(dfa,"regExpCompile: (a){m,n}: a too long");

	       hz = 5 + pacman;
	       memmove(lp + hz, lp, sz);  // open hole for CLOMN M N BOT sz 
	       sp = mp + hz; mp = lp; 
	       if(pacman) STORE(PACMAN);
	       STORE(CLOMN); STORE(M); STORE(N); STORE(BOT); STORE(sz + 1);
	       mp = sp; STORE(END);
	    }else{	// a{1,2} --> CLOMN 1 2 CHR a END
	       hz = 3 + pacman;
	       memmove(lp + hz, lp, mp - lp);  // open hole for CLOMN M N
	       sp = mp + hz; mp = lp;
	       if(pacman) STORE(PACMAN);
	       STORE(CLOMN); STORE(M); STORE(N);
	       mp = sp; STORE(END);	// END of CLOMN
	    }

	    // lp --> CLOMN m n CHR a END
	       // pack strings? "ab{3,4}" --> CHR a CHR b --> 
	       //			      CHR a CLOMN 3 4 CHR b END
	    if(n){	// multi op op messes with check at end of switch
	       str.sz--;	// "123456+" --> STR(12345) CLOP 6
	       lp = chr2str(lp,mp, &str, 1, tagi,tagstk); // --> CLOMN
	       mp = str.mp;
	    }
	    sp = lp;	// CLOMN
	    // leave lp & sp pointing to CLOMN so can check for **
	    // and want them pointing to vaild non CHR ops
	    break;
	 }
	 case '(':	// "(?:" == non-capturing group, ie no tag
	    z = 0; if(p[1]=='?' && p[2]==':'){ p += 2; z = 1; }
	    if(ortagc){ tagc = ortagc; ortagc = 0; }
	    if(tagc < RE_MAX_TAG){
	       nodeId++;  // I *might* worry about overflow with 16 bit ints
	       tp = &tagstk[++tagi];		// new node
	       memset(tp,0,sizeof(Tag));
	       tp->tagc = tagc; tp->botAddr = mp; tp->nodeId = nodeId; 
	       tp->cosmetic = z;
	       if(!z){ STORE(BOT); STORE(tagc++); }
	       else *mp = BOT;		// fake op for test at end of switch
	    }
	    else BADPAT(dfa,"regExpCompile: Too many () pairs");
	    break;
	 case ')':
	    // "(a|b|)" will match ""
	    if(*sp==BOT) BADPAT(dfa,"regExpCompile: Null pattern inside ()");
	      // "a)" --> <CHR a EOT n> or <CHR a EOT n EDON>
	    if(*sp==CHR){  // multi op op messes with check at end of switch
	       chr2str(mp,mp, &str, 0, tagi,tagstk);	// lp == mp
	       lp = mp = str.mp;
	    }
	    if(tagi > 0){
	       tp = &tagstk[tagi--];
	       if(!tp->cosmetic){ STORE(EOT); STORE(tp->tagc); }
	       else *mp = EOT;		// fake op for "(?:)*"
	    }
	    else BADPAT(dfa,"regExpCompile: Unmatched )");

	    if(tagi){	// child back propagates *?+{} : ((a*)), (a(b*c))+
	       if(tp->forks)  (tp - 1)->forks  = 1;	// don't stomp existing
	       if(tp->vwidth) (tp - 1)->vwidth = 1;
	    }
	       
	    nodeId = tagstk[tagi].nodeId;	// restore nodeId
	    tp     = &tagstk[tagi + 1];
	    if(tp->hasOR){	// dfa --> EOT n EDON
	       Byte *sav;
	       STORE(EDON);
	       sav = mp + 1; mp = tp->botAddr;
	       memmove(mp + 1, mp, sav - mp);  // open hole for NODE
	       STORE(NODE);	// using mp
	       sp  = lp + 1;	// point to EOT (right shifted 1 for NODE)
	       lp += 3;		// point to EDON (NODE + EOT n)
	       mp  = sav;	// after EDON
	    }
	    ortagc = 0;
	    break;
	 case '|':
	    // a|b|c|d == [a-d]
	    if(p==pat || !p[1]) BADPAT(dfa,"regExpCompile: Empty |");
	    switch(*sp){		// previous opcode
	       case BOL: case BOT: case BOW: case AORB:	// ^|, (|, \<|, ||
		  BADPAT(dfa,"regExpCompile: Invalid |");
	    }
	    orCnt++;
	    tp = &tagstk[tagi];	// current OR level
	    tp->forks = 1;		// if we are actually in a tag
	       // if previous OR at this level, link to here
	    if(tp->orAddr && (tp->nodeId == nodeId)){
	       n = (orCnt - tp->orCnt);  // # hops to here
	       //if(n > 0xfe) BADPAT(dfa,"regExpCompile: too many sub |s");
	       if(n > 0xfd) n = 0xff;	// I don't acutally rely on hop count
	       *(tp->orAddr) = n;
	    }

	       // I'm now the previous OR, fill out info for next OR
	    tp->orAddr  = mp + 2;
	    tp->orCnt   = orCnt; tp->hasOR = 1;

	    n = (tp->cosmetic ? 0 : tp->tagc);  // # tags in play
		// Store (AORB, open? tags, link to next AORB in node)
		// will update link at next sibling AORB
	        // Storing orCnt is not necessary, historical, yes/no OK
//if(!tp->forks then AORB doesn't need to fork, set in )  forces dup doNode
	    STORE(AORB); STORE(n); STORE(0);
	    ortagc = n + 1;

	    if(tagi==0) topor = 1;	// we'll deal with this in post
	    break;
	 case '\\':		// backrefs, word transitions, space, etc
	    switch(*++p){
	       case '\0': BADPAT(dfa,"regExpCompile: Bad quote");
	       case '<':  
	         STORE(BOW); 
		 tagstk[tagi].forks = 1;	// (\<)* == infinite loop
		 break;
	       case '>':
		  if(*sp == BOW)
		     BADPAT(dfa,"regExpCompile: Null pattern inside \\<\\>");
		  STORE(EOW);
		  tagstk[tagi].forks = 1;	// (\>)* == infinite loop
		  break;
	       case '1': case '2': case '3': case '4': case '5': case '6': 
	       case '7': case '8': case '9':
		  n = *p - '0';
		  if(tagi > 0 && tagstk[tagi].tagc == n)
		     BADPAT(dfa,"regExpCompile: Cyclical reference");
		  if(tagc > n && !tagstk[n].cosmetic){ STORE(REF); STORE(n); }
		  else BADPAT(dfa,"regExpCompile: Undetermined reference");
		  *dfa |= 2;	// DFA flag
		  break;
	       case 's': STORE(SPACE);	 break;
	       case 'S': STORE(N_SPACE); break;
	       case 'w': STORE(ALPHA);	 break;
	       case 'W': STORE(N_ALPHA); break;
	       case 'd': STORE(DIGIT);	 break;
	       case 'D': STORE(N_DIGIT); break;
	    #ifdef EXTEND
	       case 'b': STORE(CHR); STORE('\b'); break;
	       case 'f': STORE(CHR); STORE('\f'); break;
	       case 'n': STORE(CHR); STORE('\n'); break;
	       case 'r': STORE(CHR); STORE('\r'); break;
	       case 't': STORE(CHR); STORE('\t'); break;
	    #endif
	       default: mp = storeCHR(mp,*p,&str,sp,&lp); break;
	    } // switch
	    break;
	 default:
	    mp = storeCHR(mp,*p,&str,sp,&lp);   // an ordinary character
	    break;
      }// switch

      // see if we can convert a sequence of CHRs to a STR
      if(*sp==CHR){
      	 if(*lp!=CHR){  // CHR a !CHR, ASSUMES *lp is valid, ie wrote op
	    lp = chr2str(lp,mp, &str, 1, tagi,tagstk);
	    mp = str.mp;
	 }else	// if CHR a CHR b ... CHR z > 1 byte, split up
	    if(*lp==CHR && str.sz >= 250){  // chop long strings to Byte sized
	       // !!add additional check to pack if close to endDFA
	       /*        CHR a CHR b CHR c __  split at >=3, p=="defghi..."
	        * str.mp/         lp/   mp/
	        *        STR 3 a b c __
	        *     lp/         mp/
	        */
	       lp = chr2str(lp,mp, &str, 0, tagi,tagstk);
	       mp = str.mp;
	    }
      }// CHR to STR

      sp = lp;		// start of previous state

      if(mp > endDFA) BADPAT(dfa,"regExpCompile: Expression too long)");
   }// for

   if(tagi > 0) BADPAT(dfa,"regExpCompile: Unmatched (");

   if(*sp==CHR){	// "abc" --> STR(3)abc
      chr2str(mp,mp, &str, 0, 0,tagstk);
      mp = str.mp;
   }

   /*  a|b|c  --> (?:a|b|c)
    * ^a|b|c  --> (?:^a|b|c), ^ is not global
    *  a|b|c$ --> (?:a|b|c$), $ ""
    */
   if(topor){	// top level OR, wrap DFA in Node
      *dfa = 0;		// no more top ORs
      memmove(dfa + 1, dfa, (mp - dfa));	// move an extra byte, RE_SLOP
      *(dfa + 1) = NODE;
      mp++;
      STORE(EDON);
   }

   STORE(END);

   ////////////////////////////////////////// post process DFA

   #if DO_HOLDS	// Are there any long strings that have to be in any match?
   	/* STR at start of DFA == HOLDS
	 * HOLDS hops-to-STR	2 bytes
	 * We are adding to the DFA, we [at least] most of RE_SLOP so at 2
	 *   bytes, we have room for RE_SLOP/2 (== 25) HOLDS.
	 */
      #define HOLDS_MAX      4	// the max number of HOLDS I'll use
      #define HOLDS_MIN_STR 15  // min len of string that worth searching for
   if(!topor && str.maxSz >= HOLDS_MIN_STR){ // not one big OR & maybe long STRs
      Byte *dfa1 = dfa + 1, *dp;
      int   n, z, hops, sz, minSz = 0;
      struct{ Byte *str; int sz, hops; } strs[HOLDS_MAX] = { 0 },
          *smp = strs, *sp;

      	// find the HOLDS_MAX longest STRs outside of Nodes
	// skip if DFA starts with STR
      for(hops = 0, dp = dfa1; (dp = dfaScanForward(dp,STR,1)); ){
         sz = dp[1]; 
	 if(++hops > 0xfd) break;	// 1 byte worth of index
         if(hops && dp!=dfa1 && sz >= HOLDS_MIN_STR && sz > minSz){
	    smp->sz = sz; smp->str = dp; smp->hops = hops;
	    // find smallest slot
	    for(minSz = strs[0].sz, z = HOLDS_MAX, sp = smp = strs; z--; sp++)
	       if(sp->sz < minSz){ minSz = sp->sz; smp = sp; }
         }
	 dp += (sz + 2);	// op after STR
      }//for

      for(n = 0; n < HOLDS_MAX && strs[n].sz; n++){}	// count strings
      if(n){      	// insert HOLDS at start of DFA
         sz = n<<1;	// space needed for <HOLDS hops>*n
	 if(*dfa1 == BOL) dfa1++;	// don't move ^
	 memmove(dfa1 + sz, dfa1, (mp - dfa));
	 for(z = n, sp = strs, dp = dfa1; z--; sp++, dp += 2)
	    { dp[0] = HOLDS; dp[1] = sp->hops; }
	 mp += sz;
      }
   }
   #endif	// DO_HOLDS

   *dfaSz = (mp - dfa);		// in case you want to malloc() the dfa
   return 0;
}

    // store one CHR that may convert to STRing
static UChar *storeCHR(Byte *mp, char c, Str *str, Byte *sp, Byte **lp){
   #if 0	// just don't like this
   // .*a CLO ANY END --> DOTSTAR CHR a
   if(*sp==CLO && 	// .+ --> ..*
       (sp[1] == ANY && sp[2] == END) ){
      mp -= 3;
      STORE(DOTSTAR);
      *lp = mp;		// lp --> CHR, else PACMAN can garble
   }
   #endif
   if(str->sz == 0){ str->mp = mp; }
   str->string[str->sz++] = c;
   STORE(CHR); STORE(c);
   return mp;
}

   // ab --> CHR a CHR b CHR c 	6 bytes
   //    --> STR 2 abc\0	6 bytes  (still deciding len only, no \0)
   // 1234567890 --> CHR 1 CHR 2 ... CHR 0  20 bytes
   //            --> STR 11 1234567890\0    13 bytes\
   // 1 byte length, longer strings are split into multiple STRs else where
   // Returns: lp, str.mp
   /* Before:
    *          CHR a CHR b CHR c ANY __
    *  str->mp/               lp/ mp/
    * After (moveOp):
    *          STR 3 a b c ANY __
    *              dp-->lp/    \str->mp
    * Before:
    *          CHR a CHR b CHR c __
    *  str->mp/         lp/   mp/
    * After (moveOp == 0, lp ignored):
    *          STR 3 a b c __
    *  dp-->lp/            \str->mp
    *  
    * Size reduction for n (>=3) characters: 2n - (n + 3)
    * Str buffer can hold 255 characters, code upstairs handles overflow so
    *   I don't have to worry about that here.
    */
   /* Doing a post process with dfaScanForward and CHR counting would
    * be simpler (much less convoluted) but I'd also have to "garbage"
    * collect in the [unlikely] case I ran into the end of allocated DFA
    * space.  Here, converting on the fly, I still have the same issue, but
    * it is much closer to the limit, so I'm pretending not to
    * care.
    */
static Byte *chr2str(
   UChar *lp, UChar *mp, Str *str, int moveOp,
   int tagi, Tag *tagstk)
{
   int   sz = str->sz;	// strlen - \0
   Byte *dp = str->mp, *chr = dp;	// first CHR

   if(sz < 5){	// Too small to bother with. Min 3 (useful for testing)
      str->sz = 0;	// reset for next string
      str->mp = mp;
      return lp;	// no-op
   }

   #if ANDTHENULL
     str->string[sz++] = '\0';	// include \0
   #endif

   if(sz > str->maxSz) str->maxSz = sz;		// track for HOLDS

      /* Convert CHRs to STR. One byte for length.
       * sp: begining of previous op, lp: begining of op, mp: end of op
       * str->mp: CHR, dp --> op after STR
       */
   dp[0] = STR; dp[1] = sz; memcpy(&dp[2],str->string,sz);
   if(moveOp && lp!=mp){	// lp==mp == no op to move, "(?:"
      dp += (2 + sz);
      sz  = mp - lp;		// bytes of current op
      memmove(dp,lp,sz);	// slide current op
      str->mp    = dp + sz;
   }else str->mp = dp + sz + 2;

   str->sz = 0;		// reset for next string

     /* Adjust tag stack: abc|123|xyz --> 
      *		     CHR a CHR b CHR c CHR d OR 1 ..
      *   <pack> --> STR abcd OR 1 ..
      *   ie pack moves OR, which will be updated at next OR
      *		 --> STR abc OR 1 1 STR 123 OR 1 .. (updated previous OR)
      *	Ditto for botAddr as post pack, it may be used to insert NODE:
      *	  abc(123|xyz) -->
      *	        STR abc BOT 1 <moved BOT 1> STR 123 OR 1 0 STR xyz EOT 1
      *	    insert NODE in front of BOT 1 (ie need to know where BOT 1 is)
      *	    --> STR abc NODE BOT 1 STR 123 OR 1 0 STR xyz EOT 1 EDON
      */
   {  
      Tag *tp;
      int  n;

      sz = mp - str->mp;
      for(n = 0, tp = &tagstk[n]; n <= tagi; tp++, n++){
	 if(tp->orAddr  > chr) tp->orAddr  -= sz;
	 if(tp->botAddr > chr) tp->botAddr -= sz;
      }
   }

   return dp;
}

   /* Is closure multiple choice or just a consume?
    * Input:   p --> one of "*+?})"
    * Returns: 0 (need to fork), 1 (no fork needed, just consume)
    * 
    * Cases: Consume a while a (where a is CHR (a, [a], \d, \1) or tag):
    *   Node invalid, ie no (a|b)*c
    *   a*\0, (a)*\0, (a*)*, (a*b)*, a*$, a*$|b
    *   a*b  (a*)b  need lookahead for a*[b]  a*\d
    *   \d*a  \s*a  \w*1
    *   [a]*b  [^a]*a  need lookahead for a*[b]
    *   (ab{3 }c)
    *   (...|a*)b  need lookahead for (a*|..)b
    *   I just don't use (a.b)+c
    * No: a*a  (a*)*  a*\a  a*.  a*[a]  a**  a*?a  a*{2}a  a*(a)  (a*|b)a
    *     .*a  (ab)*ab
    *   (a*b)*b  (a*b)*c  ((ab)+c)+: too much like (ab)*ab
    * Punted on several ...$
    * 
    * Really want lookahead: if(*sp==CHR && lookahead()==CHR && CHR!=a)
    * 
    * Pacman post process? ARG messes up fork check, I could fail here but I
    *   don't have tag meta data
    */
int packRat(Byte *sp, UChar *p){
   int pacman = 0;
   
   p++;
   if(!*p) pacman = 1;  // "a*", (a*b)* but not (a*)* (ie PACMAN OK)
   else{
      #define _stuff_ "\\.[*+?{(|^"
      UChar *ptr = p, b;

      while(*ptr==')') ptr++; b = *ptr;
      if(!b) pacman = 1; 
      else{
	 if(!memchr(_stuff_,b,sizeof(_stuff_) - 1)){	// a*b, is b CHR?
	    switch(*sp){				// yes
	       case CHR:     pacman = sp[1] != b;	break;
	       case ALPHA:   pacman = (IS_WORD(b) == 0); break; // \w+1
	       case N_ALPHA: pacman = (IS_WORD(b) != 0); break;
	       case DIGIT:   pacman = (isdigit(b) == 0); break; // \d+a
	       case N_DIGIT: pacman = (isdigit(b) != 0); break;
	       case SPACE:   pacman = (isspace(b) == 0); break; // \s+a
	       case N_SPACE: pacman = (isspace(b) != 0); break;
	       case SET:     pacman = (ISINSET(sp + 1,b) == 0); break; // [a]+b
	       case NSET:    pacman = (ISINSET(sp + 1,b) != 0); break; // [^a]+b
   //	       case BOT:     pacman = (pmatch(m,c,sp,?,?,&stator) != 0); break;  // (a.b)+c
	    }
	 }
      }
   }
   return pacman;
}

//////////////////////////////////////////////////////////////////////////
////////////////////////// Run the DFA ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////

    // State for OR/Node morphed into packet of state info
typedef struct{ Byte *dfa; Byte /*edon,*/ badDFA, noHolds, theEnd; } Stator;

typedef struct Fiber{	// a "green" thread
   Byte    *dfa;
   UChar   *lp; 
   char    *bopat[2*RE_MAX_TAG], **eopat; // same layout as call to regExpMatch()
   struct Fiber *prev, *next;	// doubly linked list
   unsigned gid:32;		// group ID for ?+*
   unsigned id:16;	// id for debug, doesn't expand struct
   unsigned running:1, isor:1, tagged:1;
}Fiber;		// 368 bytes 64 bit pointers

#define MAX_FIBERS	20  // 20 works for me, not a speed issue, size is
typedef struct{
   int      sz, forked, notags, hasRefs;
   int	    biggusMatchus, winningGID, pacman, errorCode;
   Fiber   *first, *last, *freeList;	// linked lists of Fibers
   unsigned gid:32;		// group ID for ?+*, can roll over
   Fiber    fibers[MAX_FIBERS];
   UChar   *bol;	// begining of line (static)
   UChar   *ep;		// global so parent can know if matched happened
   UChar   *are;	// used by CLO/CLOP/ONE/CLOMN --> fork()
   char   **bopat;	// point to the "master" copy (which includes eopat)
   char    *errorMsg;
}MotherShip;		// 7,464 bytes 64 bit pointers

static UChar *pmatch(MotherShip *,UChar *lp, Byte *dfa, 
		     char *bopat[], char *eopat[], Stator *);
static void   initMotherShip(MotherShip *, UChar *bol, char *bopat[]);
static int    pullThread(MotherShip *);

#if DFA_DEBUG
    static Byte *_dfaaddr0;   // for debug: dfa - _dfaaddr0 --> address in DFA
    #define DFA_ADDR(_,dfa) (dfa - _dfaaddr0)
#endif

/* regExpMatch: Run dfa to find a match, either static or search.
 *
 * Special cases for start of DFA (some of):
 *  CHR (when searching)
 *	First locate the character without calling pmatch(), and if found,
 *	call pmatch() for the remaining string.
 *  END
 *	regExpCompile() failed, poor luser did not check for it. Fail fast.
 *
 * If a match is found, bopat[0] and eopat[0] are set to the beginning and
 *   the end of the matched fragment, respectively.
 *
 * Input:
 *   dfa:   DFA returned by regExpCompile()
 *   text:  String to match
 *   flags: See zkRE.h
 *      RE_MID : If text points into the middle of a bigger text,
 *        ie ^ is not text[0]
 *	If set, text[-1] MUST be at valid! A couple of REs will look there
 *	  if they can.
 *      RE_SEARCH : Move start forward on each fail trying to find a match.
 *   tags:  char *tags[2 * RE_MAX_TAG] or 0, these are the "(" ptrs into text
 *      if tags[0..RE_MAX_TAG - 1] != 0 then
 *        tags[n]-->start of match, tags[RE_MAX_TAG + n]-->end of match
 *      If tags==0, they are ignored and the match/search can be faster.
 *      tags are zero'd.
 *   ReErrorPacket: Filled in if match failed badly. Can == 0.
 * Returns:
 *   0: Fail, ReErrorPacket may have been set (it was cleared)
 *   1: Match, tags set
 */
int regExpMatch(Byte *dfa, char *text,
	      unsigned int flags, char *tags[], ReErrorPacket *epac)
{
   #define REX_FAIL	0
   #define REX_MATCHED	1

   Byte   *ap, *restartHere;
   UChar  *lp = (UChar *)text, *ep = 0, *bol = lp;	// for pmatch
   char  **bopat, **eopat, *fakeTags[2 * RE_MAX_TAG];
   UChar  *startMatch = lp;
   int	   sol = !(flags & RE_MID), move = (flags & RE_SEARCH);
   int	   notags = 0, dfaFlags;
   Stator  stator;		// OR state for moving around the DFA
   MotherShip m;

   if(epac) memset(epac,0,sizeof(ReErrorPacket));

   if(!dfa){
      if(epac){
	 epac->errorCode = RE_ERROR_BAD_DFA;
	 epac->errorMsg  = "regExpMatch(): NULL dfa";
      }
      return REX_FAIL;
   }

   DEBUGCODE( _dfaaddr0 = dfa; )
   dfaFlags = *dfa++;

   if(!tags){ tags = fakeTags; notags = 1; }
   bopat = tags; eopat = &tags[RE_MAX_TAG];

tiptop:		// start over, as in doing a search
   ap = dfa; restartHere = 0;

   initMotherShip(&m,bol,bopat);
   m.notags  = notags;
   m.hasRefs = dfaFlags & 2;

top:	// Restart after an OR or doing some look ahead, a clean match
	// See if I can help things along.
	// If HOLDS starts dfa & move, we'll do the HOLDS, if fail,
	//   actually do these tests
   memset(bopat,0,2*RE_MAX_TAG*sizeof(char *));	// wipe all tags

   memset(&stator, 0, sizeof(Stator));
   switch(*ap){
      case END: return REX_FAIL;	// munged automaton. fail always
      case BOL:				// anchored: match from BOL only
	 if(!sol) return REX_FAIL;
	 // ^ only allowed: "^a" or "a|^b" or "^a|^b", not "(^a)"
	 move = 0;		// anchored, no movement allowed
	 ap++;			// do this check only once
	 break;
      case CHR:				// ordinary char: locate it fast
	 if(move){
	    //(research|random) -->  memchr("r")
	    lp = (UChar *)strchr((char *)lp,*(ap + 1));
	    if(!lp) return REX_FAIL;	// if EoS, fail.
	    startMatch = lp;
	    // we will repeat CHR
	 }
	 break;
      case STR:
	 if(move){
	    #if ANDTHENULL
	       lp = (UChar *)strstr((char *)lp, (char *)(ap + 2));
	    #else
	       lp = memmem(lp,strlen((char *)lp), ap + 2, ap[1]);
	    #endif

	    if(!lp) return REX_FAIL;

	    startMatch = lp;

	    if(!restartHere){	// skip over STR, rather not repeat it
	       #if ANDTHENULL
	          lp += ap[1] - 1;
	       #else
	          lp += ap[1];
	       #endif
	       ap += ap[1] + 2;
	    }
	 }
	 break;
      case BOT:
	 if(move){	// "(c", "(string", "((((string"
	     int op = ap[2];	// BOT n CHR|STR|BOT
	     if(op==CHR || op==STR || op==BOT){
		if(!restartHere) restartHere = ap;  // gotta actually set tag
		ap += 2;
		goto top;	// look at next op
	     }
	 }
	 break;
   }// switch

   if(restartHere) ap = restartHere;
   ep = pmatch(&m,lp,ap,bopat,eopat,&stator);
   if(!ep){	// no match or Fibers queued
      if(stator.badDFA || stator.noHolds || m.errorCode){
      fail:
	 if(epac){
	    epac->errorCode = m.errorCode;
	    epac->errorMsg  = m.errorMsg;
	 }
	 return REX_FAIL;
      }

      if(m.forked){	// there *may* be Fibers to be run
	 if(!m.biggusMatchus && (2==pullThread(&m)))
	    goto fail;		// something bad happened
	 // if biggusMatchus, there will be live Fibers
	 if( (ep = m.ep) ) goto success;
      }

	// searching? match failed, move to next character and try again
      if(move && *lp && *++lp){
         // if BOL, test at top has turned off move
         //if(*dfa==BOL) return REX_FAIL;  // can't be at BoL anymore
	 #if DO_HOLDS
	 // If we get here all HOLDS have passed so we don't so them agan
	 // The first ops in a DFA: BOL HOLDS or HOLDS, but see comment above
	 // Yeah, but those HOLDS may be behind us and that matters
	 //while(*dfa==HOLDS) dfa += 2;
	 #endif	// DO_HOLDS

	 startMatch = lp;
	 sol        = 0;	// we are no longer at BoL, don't change bol!
	 goto tiptop;
      }

      return REX_FAIL;
   }// !ep
   // success!

success:
	// set open tags and tags that should have been opened to ""
   for(int n = RE_MAX_TAG, z = 0; --n; ){    // bopot[0] not a tag
      if(!z && bopat[n] && eopat[n]) z = 1;  // biggest valid tag
//      z |= (bopat[n] && eopat[n]);	     // biggest valid tag
      if(bopat[n] && !eopat[n]) eopat[n] = bopat[n];  // set unclosed tag to ""
      else if(z && !bopat[n])	// set missed tag to ""
	 bopat[n] = eopat[n] = (char *)ep;
   }    

   bopat[0] = (char *)startMatch; eopat[0] = (char *)ep;  // entire matched
      
   return REX_MATCHED;
}

    // Not void so caller can "return _regExpFail();"
static UChar *_regExpFail(
   MotherShip *m,char *msg, int errorCode, Stator *stator)
{
   stator->badDFA = 1;		// general signal fatal error has occured
   m->errorCode   = errorCode;
   m->errorMsg    = msg;
   return 0;		// longjmp() would be nice here
}


/***************************************************************************
*            Cooperative threads for "back tracking"			   *
***************************************************************************/

static void initMotherShip(MotherShip *m, UChar *bol, char *bopat[])
{
   Fiber *f;
   int    n;

   memset(m,0,sizeof(MotherShip));	// too lazy to verify I set everything
   m->bol      = bol; m->bopat = bopat;
   m->freeList = m->fibers; m->first = m->last = 0;
   for(n = 0, f = m->fibers; n++ < MAX_FIBERS; f++)
      { f->id = n; f->next = f + 1; }
   m->fibers[MAX_FIBERS - 1].next = 0;
}

#if 0		// debug routines
static int inList(MotherShip *m, int id){
   Fiber *f;

   for(f = m->first; f && f->id!=id; f = f->next) ;
   if(!f || f->id!=id) 
      { printf("DID NOT FIND FIBER #%d\n",id); return 0; }
   return 1;
}
static int listSz(MotherShip *m){
   Fiber *f;
   int    sz;

   for(sz = 0, f = m->first; f; f = f->next, sz++) ;
   return sz;
}
#endif

    /* Fork/create a thread to continue matching. After calling fork(),
     * your op should fail, the forked Fibers will carry on.
     * Returns:
     *   0: All queued and will run later. Carry on.
     *   1: A Fiber found *the* match. Or corrupt DFA (see MotherShip).
     *      You shouldn't fork anymore, match is done.
     *   2: Dead lock. Can't continue, stop what you are doing and bail.
     * TODO:
     *   -If dead lock: malloc() another block of Fibers.
     * GCC: fork is built-in function, can't use that name
     */
static int forkk(
   MotherShip *m, Stator *stator, Byte *dfa, 
   UChar  *lp, char *bopat[], unsigned gid, int isor)
{
   Fiber *f;

//   if(m->biggusMatchus) return 1;	// don't think this happens
//   if(m->errorCode)     return 1;	// doesn't happen
   
   /* Don't fork if gid has matched. This happens when .* matches a lot of
    * text: Fork all available Fibers, one of the greedier paths match, the
    * fork()er will fork() another batch etc until all matches are forked.
    * pullThread() also checks but hopefully this reduces thrashing
    */
   if(gid && gid==m->winningGID)
      { DEBUGCODE( printf("%d has alread won\n",gid); ) return 1; }

   /* If dfa & lp is the same as an existing Fiber, this is a duplicate and
    *   can be ignored.
    * Well, knock me over with a feather, this happens quite a bit
    *   and makes (?:a?)^na^n eg n==3 "a?a?a?aaa" fast ie no longer O(2^n)
    * However, as Russ notes, this messes with Refs as (..)*.*\1 forks a
    *    bunch at the same place with different values for \1.
    *    There are cases of same: dfa, lp & tags so can still ignore those.
    * Hint taken from:
    *   Russ Cox: "Regular Expression Matching: the Virtual Machine Approach"
    *   https://swtch.com/~rsc/regexp/regexp2.html
    */
   for(f = m->first; f; f = f->next)
      if(dfa == f->dfa && lp == f->lp)
	 //!!! would really like to only compare tags in play
	 if(!m->hasRefs || !memcmp(bopat,f->bopat,sizeof(char *)*RE_MAX_TAG)){
	    DEBUGCODE( printf("fork(): DUP  %ld:%s\n",DFA_ADDR(m,dfa),lp); )
	    return 0;
	 }

   if(m->sz == MAX_FIBERS){	// out of resources!
      // GC: run fibers hoping some die
      // This can be recursive: pullThread() causes fork(), repeat
      //   and dead lock if all Fibers are trying to fork()
      DEBUGCODE( printf("fork(): GC\n"); )
      if(1!=pullThread(m))	// run until can't run no more
	 // a match was found, don't need no steeking resources
	 // or DFA corruption, in either case, stop what you are doing
	 return 1;	// but check m->errorCode
      if(m->sz == MAX_FIBERS){
	 _regExpFail(m,"regExpMatch(): fork(): Dead lock",RE_ERROR_DEAD_LOCK,stator);
	 return 2;
      }
   }
   //if(m->biggusMatchus) return 1;	// doesn't happen

   f	       = m->freeList;
   m->freeList = f->next; 
   DEBUGCODE( if(f->dfa && f->dfa!=(Byte*)0x666) printf("NOT DEAD YET\n"); )

   if(m->sz){	// Queue Fibers: first in is first run
      m->last->next = f;
      f->prev       = m->last;
      f->next       = 0;
      m->last       = f;
   }else{	// empty list
      m->first = m->last = f; 
      f->next  = f->prev = 0;
   }

   m->sz++;
   m->forked = 1;	// trigger to start running fibers
   f->dfa = dfa; f->lp   = lp;
   f->gid = gid; f->isor = isor;

   f->eopat = &f->bopat[RE_MAX_TAG];
   memcpy(f->bopat,bopat,2*RE_MAX_TAG*sizeof(char *));

   DEBUGCODE( printf("Total fibers: %d,%d %ld:%-40s\n",m->sz,f->id,DFA_ADDR(m,dfa),lp); )

   return 0;
}

static void fiberDead(MotherShip *m, Fiber *f){
   Fiber *next = f->next;
   if(--m->sz){		// there were at least two Fibers in list
      if(m->first  == f){    // f is first
	 m->first   = next; // there were two so first has next
	 next->prev = 0;
      }else{		// f is middle or last
	 Fiber *prev = f->prev;
	 prev->next	     = next;
	 if(next) next->prev = prev;  // middle
	 else	  m->last    = prev; //  last
      }
   }else m->first = m->last = 0;	// empty list

   DEBUGCODE( f->dfa = (Byte *)0x666; )
   f->next     = m->freeList; f->running = 0; 
   f->gid      = f->isor = f->tagged = 0;
   m->freeList = f;
}

    /* Move from single threaded pmatch() (recursive desent / depth first
     *   search) to "parallel" pmatch()s (breadth first search, more NFA
     *   like behavior). Minimal recursion, *no* back tracking.
     *   DFA execution is forward only, a thread does not run an op more
     *   than once (athough other threads can run that same op). Semantics.
     * Each fiber represents a fork in the search path (OR, ?, *, +, {}).
     * Using a single thread and back tracking via recursion has some
     *   pathological edge cases. By running each path in parallel, we
     *   assume a match sooner than later. Worst case is still worst case.
     * Run Fibers until there are no Fibers to run (or can run).
     *   pmatch() runs a Fiber to the next fork, END (match) or fail.
     *   As a Fiber runs, Fibers will be added and deleted. pullThread()
     *     will be called recursively to free up Fibers (GC). Which means:
     *       -A dead lock can occur (Fiber needs Fibers to continue but all
     *        Fibers are in use).
     *       -MotherShip can change while a Fiber is being run.
     *       -pmatch() adds/delete Fibers (deletes indirectly via recursive
     *	      calls to pullThread()). ie the Fibers list changes
     *	      during pmatch(). f->next can change, there is no [valid] prev.
     *   A match (op END) might not end the match, as it may not be the most
     *     greedy / longest (arg!, extra work).
     * A call to pmatch() is a likely recursion event:
     *   pmatch()-->fork()/GC-->pullThread()-->pmatch() <repeat>
     *   although the depth is limited (to MAX_FIBERS) because each
     *   pullThread() stalls one Fiber.
     * Path to dead lock: A Fiber runs to a fork(). The new Fiber runs to a
     *   fork(). No Fibers available. So GC: that Fiber is stalled, run all
     *   the other Fibers in the hopes one or more will exit. One of those
     *   Fibers forks() (before any can exit, if any can). GC & repeat until
     *   all Fibers are stalled. So there can be recursion of depth
     *   ~MAX_FIBERS. Dead lock because the only way to free a Fiber is to
     *   run it to exit and it has to fork() to get there, it can't.
     *   Then the stack has to be unwound.
     * Returns:
     *   0: Match to end of text found. MotherShip->biggusMatchus & ep set.
     *      There are live Fibers.
     *   1: All Fibers that could run have been run.
     *      No Fibers == nothing happened.
     *      One or more matches may have been found but did not consume
     *	       all of text. MotherShip->ep set to end of longest match.
     *      There may be stalled fibers (in recursion). In fact, there may
     *         only be stalled Fibers and couldn't run anything.
     *      MotherShip->sz: >0: stalled Fibers, ==MAX_FIBERS: dead lock
     *   2: DFA corrupt. MotherShip notified.
     */
static int pullThread(MotherShip *m){
   Fiber  *f, *next;
   UChar  *ep;
   int	   ran = 0;
   Stator  stator;

   //if(m->biggusMatchus) return 0;	// doesn't happen

   for(f = m->first; m->sz; ){	    // run until list is empty
      if(m->errorCode) return 2;    // when backing out, error state is lost
      if(f->running) next = f->next;	 // a stalled Fiber, skip ie GC time
      else{			// we can run this Fiber
	 if(f->gid && f->gid == m->winningGID){
	    // prune Fibers that can't win from ?+* group
	    DEBUGCODE( printf("PRUNEd #%d  %u %u\n",f->id,f->gid,m->winningGID); )
	    next = f->next;
	    fiberDead(m,f); 
	    goto nextf;
	 }
	 memset(&stator, 0, sizeof(Stator)); ran = 1;
	 // pmatch() can add Fibers, which can hose traversal
	 f->running = 1;
	 ep = pmatch(m,f->lp,f->dfa,f->bopat,f->eopat,&stator);
	 f->running = 0;
	 if(m->biggusMatchus) return 0;	// gotta love recursion
	 next = f->next;  // fork() appends to list, ie next may have changed
	 if(ep){	// fiber is succeeding
	    if(stator.theEnd){	// a match was found, our job is done
	       m->winningGID = f->gid;
	       if(m->notags){	// don't care where the match is, only if match
		  m->ep = ep;
		  m->biggusMatchus = 1;		// really done
		  return 0;
	       }
	       if(ep > m->ep){  // longest match wins
		  m->ep = ep;
		  memcpy(m->bopat,f->bopat,2*RE_MAX_TAG*sizeof(char *));
		  if(!*ep){	// match can't get longer than that.
//		  if(!*ep || f->isor){	// match can't get longer than that, first OR wins
		     m->biggusMatchus = 1;
		     return 0;
		  }
	       }
	       fiberDead(m,f);
	       // see if another Fiber can find a longer path
	    }else{	// pmatch() yielded, swap out
	       f->lp  = ep;
	       f->dfa = stator.dfa;
	    }
	 }else{			// match fail (ep==0) --> Fiber dies
	    if(stator.badDFA)	// assume: total corruption & fail
	       // _regExpFail() sets stator.badDFA & MotherShip
	       return 2;
	    fiberDead(m,f);	// remove Fiber from list
	 }
      }
   nextf:
      if(!next){		// hit end of Fiber list, back to start
	 if(!ran) break;	// no Fibers or all Fibers stalled
	 f   = m->first;
	 ran = 0;
      }else f = next;
   }//for
   return 1;  // every Fiber that could run, has run
}

//////////////////////// Code used by tail call and manx pmatch()

    /* Process group that contains [at least one] OR:
     *   "(..|..|..)" --> NODE BOT .. AORB .. AORB .. EOT EDON
     *   "(?:..|..|..)" --> NODE   .. AORB .. AORB .. EDON
     * Input:
     *   DFA: points after NODE: .. OR [..OR..] EDON ..
     *   There is an OR in this node (ignoring sub nodes)
     *   Again, ignoring subnodes, the ORs are linear
     * Returns:
     *   stator->dfa points to the op after EDON
     *   0: Node failed. Upstream node might continue to next OR
     *      All clauses forked(), our work here is done.
     */
    /* Hmm, I find a difference of opinion, which wins:
     *   First clause to match or longest match?
     *   "(dog|dogs)" match "dogs" --> "dogs" or "dog"?
     *   "(a|ab)(bc|c)" match "abc" --> ("ab","c") or ("a","bc")
     *   PCRE, JavaScript: first
     *   Eighth Edition Unix library: leftmost longest
     *   POSIX: uggh
     *   Me: like *: longest wins, priorty eagar
     *     Maybe. In (a.+)|a(b)c && a(b)c|(a.+) match abc, a(b)c always wins
     *     because + forks and dies, moving ab to the front of the queue.
     */
    /* I *assume* (in several places as I can't think of a counter example)
     *   that a clause that consumes the most text will be in the longest
     *   overall path. ie in "(a|b)c", the longest a or b determines the
     *   longest path reguardless of c.
     * If this is true, then (cat|dog) doesn't need to fork and I can
     *   implement (dog|cat)*
     * Recusion (if clause forks eg (a*|b*)) is limited to the number of
     *   Nodes remaining. If that is OK, don't need to fork at all.
     * How to communicate success is an issue: via the MotherShip or
     *   fork(lp,END)
     */

#define OR_FIRST_WINS	0	// or longest(0)?

static UChar *doNode(
   MotherShip *m,UChar *lp, Byte *dfa, 
   char       *bopat[], Stator *_stator)
{
   int      nodeTag;
   int      moreOR = 1; // a Node has at least one OR and I haven't seen it yet
   Byte    *nextOR = dfaScanForward(dfa,AORB,1);
   Byte    *edon   = dfaScanForward(dfa,EDON,1);
   unsigned gid    = 0, isor = 0;
   Stator   stator;

   #if OR_FIRST_WINS
      gid = ++m->gid;	// group this branch as first wins
      //isor = 1;	// treat all ORs as the same group
   #endif

   // x(y|z) is one-pass, but (xy|xz) is not. Would need compiler support?
//if nofork hint is set, then can use old pmatch code
// which would allow Node to be closed. no speed advantage

   DEBUGCODE( printf("doNode: Start at %ld  more OR? %d\n",DFA_ADDR(m,dfa - 1),moreOR); )
   while(1){	// fork() each OR
      DEBUGCODE( printf("doNode: Fork to: %ld: \"%s\"\n",DFA_ADDR(m,dfa),lp); )
      if(forkk(m,&stator,dfa,	// *the* match was found, back out
	   lp,bopat,gid,isor)) return 0;

      // queue [next] OR (if there is another in this node).

      if(!moreOR){	// no more OR, this node has been processed
	 dfa = edon;
	 if(!dfa) break;	// ohh, that is bad
	 _stator->dfa = dfa + 1;	// ??? not looked at
	 return 0; // all branches queued, fake fail to signal run the Fibers
      }

      if(! (dfa = nextOR) ) break;	// there is supposed to be one
      DEBUGCODE( printf("doNode: Try next OR at: %ld more OR? %d\n",DFA_ADDR(m,dfa),dfa[2]); )
      nodeTag = dfa[1];	// clean up if node fails, 0 if "(?:"
      moreOR  = dfa[2];	// hops, >0 means more OR
      dfa    += 3;	// op after OR
      if(moreOR) nextOR = dfaScanForward(dfa,AORB,1);
      bopat[nodeTag]    = (char *)lp;  // not if (?:
   }// while
   return _regExpFail(m,"regExpMatch(): doNode(): bad dfa",RE_ERROR_BAD_DFA,_stator);
}

    /* A slighty different op_fork() (look at that first), where both lp and
     * # of characters between fork points are unknown (at compile time).
     * PACMAN --> just consume, only one choice, the longest one.
     * Some ickies here:
     *    I *really* do NOT want to recurse: (.*) would recurse strlen().
     *      Here we are dealing with a bigger expression, but still.
     *    The "normal" CLO & op_fork() assume the matches are one char, here
     *      the chunk size is unknown (at compile time anyway).
     *      Actually, it is <num match ops> characters.
     *    Since this is runtime, have to pmatch() to determine the chuck
     *    size, number of chunks and set tags.
     *      (.(.(.)))* 
     *      pmatch() will overwrite tags with the new set.
     *        fork() will copy them. But that is eager.
     *      compile() enforced that pmatch() can't fork() so I know a
     *        call to pmatch() will return match/no match and match length
     *        is constant.
     *    Even if chunk does not fork, it may not be a constant size: (ab*c).
     *      To deal with that case, I would have to have a stack of match
     *      points or recurse, see above.
     * 
     * Uggh, storage and order conflicts. Need to queue Fibers greedy first
     * but can only get there eagar first, which sets tags in reverse order.
     * We know the compiler does not allow forks or variable length matches
     * in the tag we are closing over which means the number of characters
     * matched is constant. So pmatch() to calculate the width (of each
     * match) and to find the end of the closure. Then back up and fork each
     * closure, capturing tags as needed.
     * 
     * (a.c)*d --> CLO BOT 10 BOT 1 CHR a ANY CHR c EOT 1 END END CHR D
     *			  dfa ^				      efa ^
     * Returns: 
     *   1: You should jump to next op, lp has been updated
     *   0: You are done, branches are forked, fail.
     */
static int gliderGun(MotherShip *ms, UChar **_lp, Byte *dfa,
     char *_bopat[], char *_eopat[], Stator *stator,	Byte *efa, int pacman)
{
   char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
   UChar   *lp = *_lp, *are = lp, *plp, *ep;
   int      step = 0, tags = (*dfa==BOT);	// (?:a)*
   unsigned gid;	// closure group id

   tags = (*dfa==BOT || dfaScanForward(dfa,BOT,0));	// (?:a)*
   memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));
   while(*lp && 
         (ep = pmatch(ms,lp,dfa,bopat,eopat,stator)) ) // find end of a* matches
      { plp = lp; lp = ep; if(!step) step = ep - are; }
   if(are==lp) return 1;	// only one --> no fork needed, carry on
   if(pacman){		// consume, want tags from last [successful] match
      *_lp = lp;	// pacman allows variable width matches
      if(tags){
	 if(*lp) pmatch(ms,plp,dfa,_bopat,_eopat,stator);
	 else memcpy(_bopat,bopat,2*RE_MAX_TAG*sizeof(char *));  // no matches
      }
      return 1; 
   }

   gid = ++ms->gid;	// group this branch so I can prune
   if(step){		// queue matches greedy first
      for(ep = lp; are < ep; ep -= step){
	 if(tags) pmatch(ms,ep - step,dfa,bopat,eopat,stator);	// set tags
	 if(forkk(ms,stator,efa,ep,bopat,gid,0)) return 0;
      }   
   }
   // the zero match case: queue args we were called with
   // this case has not set tags
   forkk(ms,stator,efa,are,_bopat,gid,0);
   return 0;
}
static int gliderGunN(MotherShip *ms, UChar **_lp, Byte *dfa,
     char *_bopat[], char *_eopat[], Stator *stator,
     Byte *efa, int N, int pacman)
{
   char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
   UChar   *lp = *_lp, *are  = lp, *ep;
   int      step = 0, tags = (*dfa==BOT);
   unsigned gid;

   if(N){
      memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));
      while(*lp && N-- && (ep = pmatch(ms,lp,dfa,bopat,eopat,stator)) )
         { step = ep - lp; lp = ep; }
      if(are==lp) return 1;
      if(pacman){
	 *_lp = lp; 
	 if(tags){
	    if(*lp) pmatch(ms,lp - step,dfa,_bopat,_eopat,stator);
	    else memcpy(_bopat,bopat,2*RE_MAX_TAG*sizeof(char *));
	 }
	 return 1; 
      }

      gid = ++ms->gid;	// group this branch so I can prune
      if(step){		// queue matches greedy first
	 for(ep = lp; are < ep; ep -= step){
	    if(tags) pmatch(ms,ep - step,dfa,bopat,eopat,stator);
	    if(forkk(ms,stator,efa,ep,bopat,gid,0)) return 0;
	 }   
      }
      forkk(ms,stator,efa,are,_bopat,gid,0);
   }
   return 0;
}

////////////////////////////////////////////

    // Does this compiler support tail calls?
   // We'll use it to "thread" the code, ie goto addr of the op code
#if __clang__
   #define TAIL_CALL(f,args) [[clang::musttail]] return (f)(args);
#elif __GNUC__		// gcc
//   #define TAIL_CALL(f,args) [[gnu::musttail]]   return (f)(args);
//   my GCC does not like musttail: "warning: musttail attribute ignored"
#elif _MSC_VER		// MS Visual C
// [[msvc::musttail]] seems to be C++ only and __declspec(musttail)
//  is undefined
//   #define TAIL_CALL(f,args) [[msvc::musttail]]   return (f)(args);
//   #define TAIL_CALL(f,args) __declspec(musttail) return (f)(args);
#endif


/* pmatch: internal routine for the hard part
 *
 * This code is mostly snarfed from an early grep written by David Conroy.
 *   The backref and tag stuff, and various other mods are by oZ.
 *   AORB, STR, HOLDS, CLOP, CLOMN, the tail call version, fork, back
 *   tracking and general re-formating by C. Durland.
 *
 * special cases: (dfa[n], dfa[n+1])
 *  CLO ANY
 *    We KNOW ".*" will match ANYTHING upto the end of line.  Thus, go to
 *    the end of line straight, without calling pmatch() recursively.  As in
 *    the other closure cases, the remaining pattern must be matched by
 *    moving backwards on the string recursively, to find a match for xy (x
 *    is ".*" and y is the remaining pattern) where the match satisfies the
 *    LONGEST match for x followed by a match for y.
 *  CLO CHR
 *    Scan forward matching the single char without recursion, and at the
 *    point of failure, we execute the remaining dfa recursively, as
 *    described above.
 *  No longer recursive, fork a Fiber to hanle each branch.
 *    Longest x is preferred but is trumped by a longer total match. The
 *    preference is set by forking the longest x before the short x's, thus
 *    the longest x will finish before shorter ones and win in event of a
 *    tie: "(.+)(.+)" match("abcd"). See op_fork().
 *
 * At the end of a successful match, bopat[n] and eopat[n] are set to the
 *   beginning and end of subpatterns matched by tagged expressions (n = 1
 *   to 9).
 * 
 * Input:
 * Returns:
 *   stator->dfa
 *   0: No match
 *      stator->dfa: op after the op that failed
 *      stator->lp:  char after failed char
 *      if HOLDS failed stator->noHolds==1
 *      regExpFail() may have been called: stator->badDFA==1, -->dfa==garbage
 *   else:
 *     stator->dfa: op after the op that succeeded
 *     pointer to end of match
 */

    // skip values for CLO XXX to skip past the closure
#define ANYSKIP	 2 		// CLO ANY|DIGIT| END ..
#define CHRSKIP	 3		// CLO CHR chr END ..
#define SETSKIP (2 + BITBLK)	// CLO SET 16bytes END ..

#ifndef TAIL_CALL	// the big switch or gotos?  This the switch code
 	// If you are looking for comments, read the tail call code

static UChar *pmatch(MotherShip *ms,
   UChar *lp, Byte *dfa,
   char *bopat[], char *eopat[], Stator *stator)
{
  UChar
    *e,			// extra pointer for CLO
    *bp, *ep;		// beginning and ending of subpat
  UChar  *are;		// to save the line ptr
  int     op, c, n, z;

  while( (op = *dfa++) != END)		// END==0 
    switch(op){
      case CHR:	if(!CEQ(*lp++,*dfa++)) goto fail; break;
      case STR:
	 n = *dfa++;
	 #if ANDTHENULL
	    z = strncmp((char *)dfa, (char *)lp, n - 1);   // strncasecmp(3), _strnicmp(win)
	    lp += n - 1;
	 #else
	    //for(ep = lp, z = n, bp = dfa; z-- && *lp && CEQ(*ep++,*bp++); ) ;
	    //z++;	// want z==-1
	      /* Assumption: memcmp() is linear (as indicated by man(3)).
	       * If STR is longer than what is left in text, won't run off end
	       * of text because '\0' won't match.  Otherwise: */
	    //if(pat + n >= endLp) goto fail; // STR longer than remaining text
            z = memcmp(dfa,lp,n);
	    lp += n;
	 #endif // ANDTHENULL
	 dfa += n; 
	 if(z) goto fail;
	 break;
      case ANY: if(*lp++ == '\0') goto fail; break;
      case SET:
	 c = *lp++;
	 z = !ISINSET(dfa,c);	// ISINSET(dfa,0) is 0 since can't CHSET(0)
	 dfa += BITBLK;
	 if(z) goto fail;
	 break;
      case NSET:
	 z = ( (c = *lp++) == '\0' || ISINSET(dfa,c) );
	 dfa += BITBLK;
	 if(z) goto fail;
	 break;
      case BOT: // OR can reopen tags, slam the door
	 n = *dfa++; bopat[n] = (char *)lp; eopat[n] = 0; break;
      case EOT:	eopat[*dfa++] = (char *)lp;	 	  break;
      case REF:  // REF 1-9
	 n = *dfa++; bp = (UChar *)bopat[n]; ep = (UChar *)eopat[n];
	 if(!bp || !ep) goto fail;
	 while(bp < ep) if(*bp++ != *lp++) goto fail; // I use memcmp in op_REF
	 break;
      case EOL:		// EOL END or EOL AORB ..  if not in tag/node
	 if(*lp != '\0') goto fail;
	 break;		// next op: END or AORB
      case BOL:     if(lp!=ms->bol) goto fail;		  	  break;
      case DIGIT:   if(!*lp || !isdigit(*lp++)) goto fail;	  break;
      case N_DIGIT: if(!*lp ||  isdigit(*lp++)) goto fail;	  break;
      case SPACE:   if(!*lp || !isspace(*lp++)) goto fail;	  break;
      case N_SPACE: if(!*lp ||  isspace(*lp++)) goto fail;	  break;
      case ALPHA:   if(!*lp || !IS_WORD(*lp))   goto fail; lp++; break;
      case N_ALPHA: if(!*lp ||  IS_WORD(*lp))   goto fail; lp++; break;
      case BOW:
	 if(!(lp != ms->bol && IS_WORD(lp[-1])) && IS_WORD(*lp)) break;
	 goto fail;
      case EOW:		// 'w\0' is OK here
	 if((lp != ms->bol && IS_WORD(lp[-1])) && !IS_WORD(*lp)) break;
	 goto fail;
      case AORB: // <tag count><hops to sibling OR>
	   /* Reached OR == match success == done with match or Node
	    * OLD:        ^ .. AORB 0 .. AORB 0 ..().. $	     tag == 0
	    * .. NODE BOT n .. AORB n .. AORB n ..().. EOT n EDON .. tag == n
	    * .. NODE       .. AORB 0 .. AORB 0 ..()..       EDON .. tag cosmetic
	    * The hard way (if tagged):
	    *    Hop over remaining sibling ORs
	    *    Skip to EOT n (if in node, don't leave node)
	    *    Continue, tag will be closed at EOT
	    * The easy way:
	    *    Close tag n == EOT n
	    *    Could just return but doNode() would need to
	    *		forward dfa to EDON, it is easier just to do it here.
	    *    Skip to matching EDON. Could back up 2 to EOT.
	    *    Continue.  
	    */
	 DEBUGCODE( printf("OR1: %ld: tags(%d) Hops(%d)\n",DFA_ADDR(ms,dfa) - 1,*dfa,dfa[1]); )
	 if(*dfa)	// 0 is a flag/place holder, tag 0 is set upstairs
	    eopat[*dfa] = (char *)lp;	// close my tag: EOT n
	 if(!(dfa = dfaScanForward(dfa + 2,EDON,1)))
	       return _regExpFail(ms,"regExpMatch: AORB: No EDON.",RE_ERROR_BAD_DFA,stator);
	 DEBUGCODE( printf("OR2: jumped to %ld\n",DFA_ADDR(ms,dfa)); )
         break;	// --> EDON
      case NODE:
	 lp  = doNode(ms,lp,dfa,bopat,stator);
	 dfa = stator->dfa;
	 goto fail;
      case EDON: break;

      case CLOP: z = 1; goto clo;
      case CLO:  z = 0;
      clo:
      {
	 int pacman = ms->pacman; ms->pacman = 0;

	 are = lp;
	 n   = ANYSKIP;
	 switch(*dfa){
	    //case ANY:   while(*lp)		      lp++; break; // -->Eol
	    case ANY:     lp += strlen((char *)lp);	    break; // -->Eol
	    case DIGIT:   while( isdigit(*lp))	      lp++; break;
	    case N_DIGIT: while(!isdigit(*lp) && *lp) lp++; break;
	    case SPACE:   while( isspace(*lp))	      lp++; break;
	    case N_SPACE: while(!isspace(*lp) && *lp) lp++; break;
	    case ALPHA:   while( IS_WORD(*lp))	      lp++; break;
	    case N_ALPHA: while(!IS_WORD(*lp) && *lp) lp++; break;
	    case CHR:
	       c = *(dfa+1);		// we know c != '\0'
	       while(CEQ(*lp,c)) lp++;
	       n = CHRSKIP;
	       break;
	    case SET: case NSET:
	       while(*lp && (e = pmatch(ms,lp,dfa,bopat,eopat,stator))) lp = e;
	       //while(*lp && pmatch(ms,lp,dfa,bopat,eopat,stator)) lp++;
	       n = SETSKIP;
	       break;
	    case BOT:	// (abc)+ --> CLOP BOT sz BOT n CHR a Chr b Chr c EOT n END
	       n    = dfa[1];
	       dfa += 2;
	       if(z) lp = pmatch(ms,lp,dfa,bopat,eopat,stator); // a+ == aa*
	       if(lp && 
	          gliderGun(ms,&lp,dfa,bopat,eopat,stator,dfa + n, pacman))
		    break;
	       goto fail;	// are branches queued, our job is done
	       break;
	    default:
	       return _regExpFail(ms,"regExpMatch: closure: bad dfa.",RE_ERROR_BAD_DFA,stator);
	 }
	 dfa += n;
	 if(z==1){	// CLOP: zero matches --> fail else skip that case
	    if(are==lp) goto fail;
	    are++; 
	 }
	 if(pacman) break;

      fork:
         {
	    if(are==lp) break;	// only one, don't need to fork
	    n = ++ms->gid;	// group this branch so I can prune	
	    for(; are <= lp; lp--)  // greedy: longest match goes first
	       if(forkk(ms,stator,dfa,lp,bopat,n,0)) break;
	    goto fail;	// are branches queued, our job is done
	 }
	 break;
      }
      case ONE:   z = 1; goto clomn;
      case CLOMN: z = 0;
      clomn:
      {
	 UChar *tp;
	 int   M,N, star = 0, i, op, Z;
	 Byte  *efa;
	 int    pacman = ms->pacman; ms->pacman = 0;

	 n = ANYSKIP;

	 if(z){ M = 0;      N = 1;      op = dfa[0]; c = dfa[1]; }
	 else { M = dfa[0]; N = dfa[1]; op = dfa[2]; c = dfa[3]; dfa += 2;
		if(N==0){ star = 1; N = M; }
	      }
	 Z = N;
	 if(op==BOT){
	    n    = dfa[1];
	    dfa += 2;
	    efa  = dfa;	// because I increment dfa below
	    Z    = M;	// then let gliderGun do the rest
	 }
	 for(are = tp = lp, i = 0; i < Z && *lp; i++){
	    switch(op){
	       case ANY:			lp++; break;
	       case DIGIT:   if( isdigit(*lp))	lp++; break;
	       case N_DIGIT: if(!isdigit(*lp))	lp++; break;
	       case SPACE:   if( isspace(*lp))	lp++; break;
	       case N_SPACE: if(!isspace(*lp))	lp++; break;
	       case ALPHA:   if( IS_WORD(*lp))	lp++; break;
	       case N_ALPHA: if(!IS_WORD(*lp))	lp++; break;
	       case CHR:
		  if(CEQ(*lp,c)) lp++;	// we know c != '\0'
		  n = CHRSKIP;
		  break;
	       case SET: 	// repeat SET bittab END
		  if( (ep = pmatch(ms,lp,dfa,bopat,eopat,stator))) lp = ep;
		  n = SETSKIP;
		  break;
	       case NSET:
		  if( (ep = pmatch(ms,lp,dfa,bopat,eopat,stator))) lp = ep;
		  n = SETSKIP;
		  break;
	       case BOT:
		  if( (ep = pmatch(ms,lp,dfa,bopat,eopat,stator)) ) lp = ep;
		  break;
	       default:
		  return _regExpFail(ms,
		     "regExpMatch: closure: bad dfa.",RE_ERROR_BAD_DFA,stator);
	   }//switch
	   if(tp!=lp) tp  = lp;   // successful match
	   else	      break;	 // match fail
	   if(i<M)    are = lp; // first M matches are required, next are optional
	}//for
	if(i < M) goto fail;	// < min matches
	if(i==0 && n==ANYSKIP) // then switch maybe not done, skip size unknown
	   n = (dfaScanForward(dfa,END,0) - dfa + 1);

	if(i==M && !*lp){ dfa += n; break; } // {m,n}: only m matches

	if(star && op!=BOT){	// a{2,}, N=2 --> aaa* == aa+
	   ms->pacman = pacman;
	   goto clo;	// fork a*, dfa --> CHR a, have consumed N "a"s
	}

	dfa += n;	// next op

	if(M==N && !star) break;	// {n} and have matched n

	if(op==BOT){
	   lp = are;		// in case of PACMAN
	   if(star) i = gliderGun( ms,&lp,efa,bopat,eopat,stator,dfa,	    pacman);
	   else     i = gliderGunN(ms,&lp,efa,bopat,eopat,stator,dfa,N - M, pacman);
	   if(i) break;
	   goto fail;	// are branches queued, our job is done
	}

	if(pacman) break;
	goto fork;
      }
        break;
      #if DO_HOLDS
      case HOLDS:	// HOLDS hops-to-STR (base 1 in this Node)
	 for(n = *dfa, bp = dfa + 1; (bp = dfaScanForward(bp,STR,1)) && --n;
	     bp += (bp[1] + 1) ){}
	 if(!bp) return _regExpFail(ms,
			"regExpMatch: HOLDS could not find STR",RE_ERROR_BAD_DFA,stator);
	 dfa++;
	 #if ANDTHENULL
	    if(!strstr((char *)lp, (char *)(bp + 2)))	// strcasestr(3), Xwin
	 #else
	    if(!memmem(lp,strlen((char *)lp), bp + 2, bp[1]))
	 #endif
	    { stator->noHolds = 1; goto fail; }
         break;
      #endif // DO_HOLDS
      case PACMAN: ms->pacman = 1; break;
      default: return _regExpFail(ms,"regExpMatch: bad dfa.",RE_ERROR_BAD_DFA,stator);
    }// switch, while
    stator->theEnd = 1;		// hit END
    dfa--;			// point at END in case somebody continues

    // success!
   stator->dfa = dfa;
   return lp;

fail:
   stator->dfa = dfa;
   return 0;
}

#else // use TAIL_CALLs to goto op

    /* A "threaded" version of pmatch().
     * Rather than a big switch on op code values, jump from op code to op
     *   code (computed goto) in the hope the compiler & CPU can optimize
     *   better than a switch.
     * Is it faster? (clang):
     *   Not in my tests. It seem to be bit slower. 
     *   A test of 268 byte dfa and a [big] loop of just matching shows no
     *   difference. 
     *   n:=19; var r = RegExp( "a?"*n + "a"*n ); 
     *     with a big loop of r.search("a"*n) is a bit slower.
     *   Search for phone # at end of 100k file, see comment up top: no diff
     *   No fiber test was a tiny bit faster.
     *   Or the compiler can optimize the switch to match this.
     *   I have seen large (~30%) improvements in a switch --> tail
     *     call conversion in aother (larger) VM I wrote.
     *   And it (the tail call version) is quite a bit bigger (~13k)
     * I do like debugging the ops better than the switch.
     */

#define OP_ARG_LIST  MotherShip *ms, UChar *lp, Byte *dfa, \
	     char *bopat[], char *eopat[], Stator *stator

#define OP_ARGS	     ms, lp, dfa,     bopat,eopat,stator
#define OP_ARGSP     ms, lp, dfa + 1, bopat,eopat,stator

#define OP_SIG(opName) static UChar *opName(OP_ARG_LIST)

typedef UChar *(*OpAddr)(OP_ARG_LIST);
static OpAddr re_ops[];		// jump table

    // jump to next op:
#define JMP_NEXT_OP() TAIL_CALL(re_ops[*dfa],OP_ARGSP)
#define GOTO_OP(op)   TAIL_CALL(op,          OP_ARGS )

OP_SIG(pmatch){ JMP_NEXT_OP(); }

OP_SIG(op_success){ stator->dfa = dfa; return lp; } // usually: point to next op
OP_SIG(op_fail)   { stator->dfa = dfa; return  0; }
OP_SIG(op_END){ stator->theEnd = 1; dfa--; GOTO_OP(op_success); }
OP_SIG(op_CHR){	if(!CEQ(*lp++,*dfa++))     GOTO_OP(op_fail); JMP_NEXT_OP(); }
OP_SIG(op_STR){
   int n = *dfa++, s;
   #if ANDTHENULL
      s = strncmp((char *)dfa, (char *)lp, n - 1);   // strncasecmp(3), _strnicmp(win)
      lp += n - 1;
   #else
      //for(ep = lp, s = n, bp = dfa; s-- && *lp && CEQ(*ep++,*bp++); ) ;
      //s++;	// want s==-1
      /* Assumption: memcmp() is linear (as indicated by man(3)).
       * If STR is longer than what is left in text, won't run off end
       * of text because '\0' won't match.  Otherwise: */
      //if(pat + n >= endLp) GOTO_OP(op_fail); // STR longer than remaining text
      s = memcmp(dfa,lp,n);
      lp += n;
   #endif // ANDTHENULL
   dfa += n; 
   if(s) GOTO_OP(op_fail);
   JMP_NEXT_OP();
}
OP_SIG(op_ANY){ if(*lp++ == '\0') GOTO_OP(op_fail); JMP_NEXT_OP(); }
OP_SIG(op_SET){
   char c = *lp++;
   int  s = !ISINSET(dfa,c);	// ISINSET(dfa,0) is 0 since can't CHSET(0)
   dfa += BITBLK;
   if(s) GOTO_OP(op_fail);
   JMP_NEXT_OP();
}
OP_SIG(op_NSET){
   char c;
   int  s = ( (c = *lp++) == '\0' || ISINSET(dfa,c) );
   dfa += BITBLK;
   if(s) GOTO_OP(op_fail);
   JMP_NEXT_OP();
}
OP_SIG(op_BOT){
   int n = *dfa++;
   bopat[n] = (char *)lp; eopat[n] = 0; // OR can reopen tags, slam the door
   JMP_NEXT_OP();
}
OP_SIG(op_EOT){ eopat[*dfa++] = (char *)lp; JMP_NEXT_OP(); }
OP_SIG(op_REF){				// REF 1-9
   UChar *bp, *ep;	// beginning and ending of subpat
   int    n = *dfa++;
   bp = (UChar *)bopat[n]; ep = (UChar *)eopat[n];
   	/* OR may not have set a referenced tag: (?:(a)|b)\1) match(b)
	 *    In that case, doNode() has cleared the tag
	 * (a.)*\1 match(abc) forks two Fibers:
	 *    \1 match(c) \1=="ab" and \1 match(abc) \1 not set
	 */
   if(!bp || !ep) GOTO_OP(op_fail);
#if 0
   while(bp < ep) if(*bp++ != *lp++) GOTO_OP(op_fail);
#else
   n = ep - bp;
   if(memcmp(bp,lp,n)) GOTO_OP(op_fail);
   lp += n;
#endif

   JMP_NEXT_OP();
}

OP_SIG(op_EOL){		// EOL END or EOL AORB ..  if not in tag/node
   if(*lp != '\0') GOTO_OP(op_fail);
   JMP_NEXT_OP();		// next op: END or AORB
}
OP_SIG(op_BOL){ if(lp!=ms->bol)  GOTO_OP(op_fail);	 JMP_NEXT_OP(); }
OP_SIG(op_DIGIT)
   { if(!*lp || !isdigit(*lp++)) GOTO_OP(op_fail);	 JMP_NEXT_OP(); }
OP_SIG(op_N_DIGIT)
   { if(!*lp ||  isdigit(*lp++)) GOTO_OP(op_fail);	 JMP_NEXT_OP(); }
OP_SIG(op_SPACE)
   { if(!*lp || !isspace(*lp++)) GOTO_OP(op_fail);	 JMP_NEXT_OP(); }
OP_SIG(op_N_SPACE)
   { if(!*lp ||  isspace(*lp++)) GOTO_OP(op_fail);	 JMP_NEXT_OP(); }
OP_SIG(op_ALPHA)
   { if(!*lp || !IS_WORD(*lp))   GOTO_OP(op_fail); lp++; JMP_NEXT_OP(); }
OP_SIG(op_N_ALPHA)
   { if(!*lp ||  IS_WORD(*lp))   GOTO_OP(op_fail); lp++; JMP_NEXT_OP(); }
OP_SIG(op_BOW){
   if(!(lp != ms->bol && IS_WORD(lp[-1])) && IS_WORD(*lp)) JMP_NEXT_OP();
   GOTO_OP(op_fail);
}
OP_SIG(op_EOW){		// 'w\0' is OK here
   if((lp != ms->bol && IS_WORD(lp[-1])) && !IS_WORD(*lp)) JMP_NEXT_OP();
   GOTO_OP(op_fail);
}
OP_SIG(op_AORB){	// <tag count><hops to sibling OR>
	/* Reached OR == match success == done with Node or match
	 *  OLD:       ^ .. AORB 0 .. AORB 0 ..().. $	    tag == 0
	 * .. NODE BOT n .. AORB n .. AORB n ..().. EOT n EDON .. tag == n
	 * .. NODE       .. AORB 0 .. AORB 0 ..()..       EDON .. tag cosmetic
	 * The hard way (if tagged):
	 *    Hop over remaining sibling ORs
	 *    Skip to EOT n (if in node, don't leave node)
	 *    Continue, tag will be closed at EOT
	 * The easy way:
	 *    Close tag n == EOT n (if tag is not cosmetic)
	 *    Skip to matching EDON (assume intervening Nodes).
	 *    Continue.
	 */
   DEBUGCODE( printf("OR1: %ld: tags(%d) Hops(%d)\n",DFA_ADDR(ms,dfa) - 1,*dfa,dfa[1]); )
   if(*dfa)	// 0 is a flag/place holder, tag 0 is set upstairs
      eopat[*dfa] = (char *)lp;	// close my tag: EOT n
   if(!(dfa = dfaScanForward(dfa + 2,EDON,1)))
      return _regExpFail(ms,"regExpMatch: AORB: No EDON.",RE_ERROR_BAD_DFA,stator);
   DEBUGCODE( printf("OR2: jumped to %ld\n",DFA_ADDR(ms,dfa)); )
   //JMP_NEXT_OP();	// --> EDON, == dfa++; GOTO_OP(op_EDON)
   dfa++; JMP_NEXT_OP();  // EDON == no-op so skip it
}
OP_SIG(op_NODE){
   lp  = doNode(ms,lp,dfa,bopat,stator);
   dfa = stator->dfa;
   //if(!lp) GOTO_OP(op_fail); JMP_NEXT_OP();		// next op: EDON + 1
   GOTO_OP(op_fail);	// doNode() forks
}
//OP_SIG(op_EDON){ stator->edon = 1; GOTO_OP(op_success); }
OP_SIG(op_EDON){ JMP_NEXT_OP(); }    // flowed off the end of last OR

    /* Handle ? * +
     * are --> first match, lp --> last match. are==lp == one Fiber
     * We'll queue all matches of the branches and let pullThread() figure
     *   out which (if any) wins.
     * Queue matcheses in order we want to win (ie the greedy ones first):
     *   Longest match wins, priorty greedy 
     * We should assume that GC will run some Fibers before all are queued
     *   ie if more branches than available Fibers.
     *   We should also assume that we'll queue all our branches and one of
     *   those branches will queue its branches, ie queue order matters:
     *      var r=RegExp("(.+)(.+)"); r.search("abcd");
     *   has many longest matches (==4), many of which can be queued before
     *   any run.
     * We are playing by greedy/longest match wins rules so:
     *   If GC happens before we finish fork()ing:
     *   -If a Fiber matches, pullThread() might determine it is the
     *      longest match. We are done.
     *   -If any of our queued matches match:
     *   -We queued greedyest branch first so:
     *   -That match is a "better" match than any possible following match so:
     *   -done.  Seems to work but cost is too high - in my tests this case
     *      is very rare (twice in 580 tests). And those tests are poor.
     *      Pruning in pullThread() works much better, often the Fibers run
     *      after op_fork() is done.
     * Always fail, even if fork() finds the match as that match is a
     *   different Fiber, *we* didn't match.
     * It would be nice if to just carry one for one of the branches,
     *   avoiding a fork, ie forking all the eagar-er branches and
     *   continuing the greediest match but that fails if there is a GC as
     *   then the already forked branches now have higher priorty than the
     *   greediest. And can't continue the eagar-est branch as that would be
     *   the hightest priorty as it is still running (ie not queued).
     * 
     * "a*"  match "aaaaaaaaabbbcccc"
     *      ms->are ^    lp  ^
     */
OP_SIG(op_fork){
   UChar   *are = ms->are;
   unsigned gid;

   if(are==lp) JMP_NEXT_OP();	// only one, don't need to fork
   gid = ++ms->gid;	// group this branch so I can prune
   for(; are <= lp; lp--)
      if(forkk(ms,stator,dfa,lp,bopat,gid,0)) break; // match found, stop

   GOTO_OP(op_fail);	// are branches queued, our job is done
}
OP_SIG(op_CLO){ // both CLO:[5] (*: none or more) & CLOP:[6] (+: one or more)
   		// CLO ANY|CHR|SET ... END
   UChar *are = lp, *ep;
   int    sz  = ANYSKIP, c,  clop = (dfa[-1]==CLOP);
   int    pacman = ms->pacman; ms->pacman = 0;	// for this op only

   switch(*dfa){
      //case ANY:   while(*lp)		        lp++; break; // -->Eol
      case ANY:     lp += strlen((char *)lp);	      break; // -->Eol
      case DIGIT:   while( isdigit(*lp))	lp++; break;
      case N_DIGIT: while(!isdigit(*lp) && *lp) lp++; break;
      case SPACE:   while( isspace(*lp))	lp++; break;
      case N_SPACE: while(!isspace(*lp) && *lp) lp++; break;
      case ALPHA:   while( IS_WORD(*lp))	lp++; break;
      case N_ALPHA: while(!IS_WORD(*lp) && *lp) lp++; break;
      case CHR:
	 c = *(dfa + 1);	// we know c != '\0', *lp can be
	 while(CEQ(*lp,c)) lp++;
	 sz = CHRSKIP;
	 break;
      case SET: 	// repeat SET bittab END
	 while(*lp && (ep = op_SET(OP_ARGSP))) lp = ep;
	 sz = SETSKIP;
	 break;
      case NSET:
	 while(*lp && (ep = op_NSET(OP_ARGSP))) lp = ep;
	 sz = SETSKIP;
	 break;
      case BOT:	// (abc)+ --> CLOP BOT sz BOT n CHR a Chr b Chr c EOT n END
	 sz   = dfa[1];
	 dfa += 2;
	 if(clop) lp = pmatch(OP_ARGS); // a+ == aa*
	 if(lp && gliderGun(ms,&lp,dfa,bopat,eopat,stator,dfa + sz,pacman))
	    { dfa += sz; JMP_NEXT_OP(); }
	 GOTO_OP(op_fail);	// are branches queued, our job is done
	 break;
      default:
	 return _regExpFail(ms,"regExpMatch: closure: bad dfa.",RE_ERROR_BAD_DFA,stator);
   }
   dfa += sz;

   if(clop){	// CLOP: zero matches --> fail else skip that case
      if(are==lp) GOTO_OP(op_fail);
      are++; 
   }

   // Here it would be nice to know if there are STRs in the "main" path
   // so I could fail early. Then again, HOLDs should have checked that.

   // PACMAN will hopefully catch x*y so I don't have to fork and fail a
   // zillion times vs just continuing at "y" (as there are no other choices).

   if(pacman) JMP_NEXT_OP();	// x*y, we have consumed all "x"s
   ms->are = are; GOTO_OP(op_fork);
}
    /* []: {m,n} CLOMN m n ANY|CHR| ... ...
     * {n}	 match exactly n times == {n,n}
     * {min,0}	 match min or more times. 0 is magic
     * {,max} 	 match up to max times == {0,max}
     * {0,max}   match "at most" max times.
     * {min,max} match at least min times, but not more than max times.
     * {0,0}	 Disallowed
     * Compiler enforces m<=n
     * x* y+ z? is equivalent to x{0,} y{1,} z{0,1}.
     * m,n : one byte each
     * 
     * Both CLOMN:[8] ({m,n}) & ONE:[7] (? none or one)
     * CLOMN M N <exp> END
     * ONE <exp> END --> CLOMN 0 1 <exp>
     * CLOMN M N BOT sz <exp> END  : (abc){M,N}
     */
OP_SIG(op_CLOMN){
   UChar *ep, *tp, *are; 	// for M==0
   int    c, sz = ANYSKIP, M,N, star = 0, i, op, Z;
   OpAddr opaddr;
   Byte  *efa;
   int    pacman = ms->pacman; ms->pacman = 0;
   
   if(dfa[-1]==ONE){ M = 0;      N = 1;	     op = dfa[0]; c = dfa[1]; }
   else		   { M = dfa[0]; N = dfa[1]; op = dfa[2]; c = dfa[3]; dfa += 2;
      		     if(N==0){ star = 1; N = M; }
		   }
   Z = N;
   if(op==SET) opaddr = op_SET; else if(op==NSET) opaddr = op_NSET;
   if(op==BOT){
      sz   = dfa[1];
      dfa += 2;
      efa  = dfa;	// because I increment dfa below
      Z    = M;		// then let gliderGun do the rest [of the matches]
   }
   for(are = tp = lp, i = 0; i < Z && *lp; i++){
      switch(op){
	 case ANY:     			 lp++; break;
	 case DIGIT:   if( isdigit(*lp)) lp++; break;
	 case N_DIGIT: if(!isdigit(*lp)) lp++; break;
	 case SPACE:   if( isspace(*lp)) lp++; break;
	 case N_SPACE: if(!isspace(*lp)) lp++; break;
	 case ALPHA:   if( IS_WORD(*lp)) lp++; break;
	 case N_ALPHA: if(!IS_WORD(*lp)) lp++; break;
	 case CHR:
	    if(CEQ(*lp,c)) lp++;	// we know c != '\0'
	    sz = CHRSKIP;
	    break;
	 case SET: case NSET: 	// repeat SET bittab END
	    if( (ep = opaddr(OP_ARGSP)) ) lp = ep;
	    sz = SETSKIP;
	    break;
	 case BOT:
	    if( (ep = pmatch(OP_ARGS)) ) lp = ep;
	    break;
	 default:
	    return _regExpFail(ms,
	       "regExpMatch: closure: bad dfa.",RE_ERROR_BAD_DFA,stator);
     }//switch
     if(tp!=lp) tp  = lp;     // successful match
     else	break;	     // match fail
     if(i<M)	are = lp;   // first M matches are required, next are optional
  }//for
  if(i < M) GOTO_OP(op_fail);	// < min matches
  if(i==0 && sz==ANYSKIP) // then switch maybe not done, skip size unknown
     sz = (dfaScanForward(dfa,END,0) - dfa + 1);

  if(i==M && !*lp){ dfa += sz; JMP_NEXT_OP(); }   //  {m,n}: only m matches

  if(star && op!=BOT){	// a{2,}, N=2 --> aaa* == aa+, have consumed aa
     ms->pacman = pacman;
     GOTO_OP(op_CLO);	// fork a*, dfa --> CHR a, have consumed N "a"s
  }

  dfa += sz;	// next op

  if(M==N && !star) JMP_NEXT_OP();  // {n} & matched n

  if(op==BOT){ // (abc){M,N}, matched M instances, now fork N - M more
     lp = are;		// in case of PACMAN
     if(star) i = gliderGun( ms,&lp,efa,bopat,eopat,stator,dfa,      pacman);
     else     i = gliderGunN(ms,&lp,efa,bopat,eopat,stator,dfa,N - M,pacman);
     if(i) JMP_NEXT_OP();
     GOTO_OP(op_fail);	// are branches queued, our job is done
  }

  if(pacman) JMP_NEXT_OP();
  ms->are = are; GOTO_OP(op_fork);
}
OP_SIG(op_PACMAN){
   // PACMAN CLO|CLOP|ONE|CLOMN ..  tell closure to consume, !fork
   ms->pacman = 1;
   JMP_NEXT_OP();
}
OP_SIG(op_HOLDS){	// HOLDS hops-to-STR (base 1 in this Node)
#if DO_HOLDS
   Byte *bp;
   int   n;

   for(n = *dfa, bp = dfa + 1; (bp = dfaScanForward(bp,STR,1)) && --n;
      bp += (bp[1] + 1) ){}
   if(!bp) return _regExpFail(ms,
		     "regExpMatch: HOLDS could not find STR",RE_ERROR_BAD_DFA,stator);
   dfa++;
   #if ANDTHENULL
      if(!strstr((char *)lp, (char *)(bp + 2)))	// strcasestr(3), Xwin
   #else
      if(!memmem(lp,strlen((char *)lp), bp + 2, bp[1]))
   #endif
      { stator->noHolds = 1; GOTO_OP(op_fail); }
#endif // DO_HOLDS
   JMP_NEXT_OP();
}
#if 0
static void dotStar(OP_ARG_LIST, int chr, unsigned gid){
   // scan to farthest a, fork, next farthest, fork, ..
   // this is going to suck for .*a match aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
   //    only in that recursion is deep but no work is done
   //    and non-DOTSTAR does way less work
   char *ep = (char *)lp;

   if(chr) ep = strchr(ep,dfa[1]);	// CHR
   else	   ep = strstr(ep,(char *)(dfa + 2));		// !!!ANDTHENULL
   if(!ep) return;
   lp = chr ? (UChar *)ep + 1 : (UChar *)ep + dfa[1] - 1;
   dotStar(OP_ARGS,chr,gid);	// find greedest

   // backing out of recursion
   dfa += chr ? CHRSKIP - 1 : (dfa[1] + 2);
   if(ep) forkk(ms,stator,dfa,lp,bopat,gid,0);
}
OP_SIG(op_DOTSTAR){	// DOTSTAR CHR a | DOTSTAR STR n text
   int	     chr = (*dfa==CHR);
   unsigned  gid = ++ms->gid;
   dotStar(OP_ARGS,chr,gid);
   GOTO_OP(op_fail);
}
#endif

//////////////////////

static OpAddr re_ops[] = {
  op_END,
  op_CHR,   op_ANY,     op_SET,    op_NSET,	// 2 - 4
  op_BOL,   op_EOL,     op_BOT,    op_EOT,	// 5 - 8
  op_BOW,   op_EOW,     op_REF,   	 	// 9 - 11
  op_DIGIT, op_N_DIGIT, op_SPACE,  op_N_SPACE, op_ALPHA, op_N_ALPHA, // 12
  op_CLO,   op_CLOMN,   op_CLO,    op_CLOMN,			     // 18
  op_AORB,  op_NODE,    op_EDON,				     // 22
  op_STR,   op_HOLDS,   op_PACMAN, //op_DOTSTAR,		     // 25
};

#endif	// TAIL_CALL


   // Returns: *dfa == stopAt or 0
static Byte *dfaScanForward(Byte *dfa,int stopAt, int inThisNode){
#if 0
   int n, lvl = 0;

   while(*dfa != END){
      if(*dfa == stopAt && (lvl==0 || !inThisNode)) return dfa;

      DEBUGCODE( if(*dfa > LAST_OP) return 0; )	// range check

      switch(*dfa++){
	 case STR: dfa += (*dfa + 1); break;
	 case CLOMN:
	    dfa += 2;
	    //fallthrough
	 case CLO: case CLOP: case ONE:
	    n = 1;  // CYA
	    switch(*dfa){
	       case SPACE: case N_SPACE:	// CLO SPACE END
	       case DIGIT: case N_DIGIT:
	       case ALPHA: case N_ALPHA:
	       case ANY:            n = ANYSKIP; break;

	       case CHR:            n = CHRSKIP; break;	// CLO CHR chr END
	       case SET: case NSET: n = SETSKIP; break;
	       #if DFA_DEBUG
	       default: return 0;
	       #endif
	    }
	    dfa += n;	// remember dfa++
	    break;
         #if DO_HOLDS
	 case HOLDS:
         #endif
	 case CHR: case BOT: case EOT: case REF: dfa++;		break;
         case AORB:				 dfa += 2;	break;
         case SET:  case NSET:			 dfa += BITBLK; break;

	 case NODE: lvl++; break;
	 case EDON: 
	    if(--lvl < 0 && inThisNode) return 0; // DFA fail
	    break;
      }// switch
   }// while

#else
   // yeah, no speed diff, my test REs are just too small.
   // Or, after optimization, same code? too lazy to look at asm
   #define M42	0x30	// magic number
   // END CHR     ANY     SET     NSET    BOL EOL BOT EOT BOW EOW REF DIGIT   N_DIGIT SPACE   N_SPACE ALPHA   N_ALPHA  CLO  ONE CLOP CLOMN AORB NODE EDON STR HOLDS PACMAN DOTSTAR
   static Byte opskp[] = {
      0,  1,      0,      BITBLK, BITBLK, 0,  0,  1,  1,  0,  0,  1,  0,      0,      0,      0,      0,      0,       M42, M42,M42, M42,  2,   0,   0,   0,  1,    0,     0 };
   static Byte skp2[]  = {
      1,  CHRSKIP,ANYSKIP,SETSKIP,SETSKIP,1,  1,  M42,1,  1,  1,  1,  ANYSKIP,ANYSKIP,ANYSKIP,ANYSKIP,ANYSKIP,ANYSKIP, 1,   1,  1,   0,    0,   1,   1,   1,  0,    0,     0 };

   int n, lvl = 0, op;

   while(*dfa != END){
      if(*dfa == stopAt && (lvl==0 || !inThisNode)) return dfa;

      DEBUGCODE( if(*dfa > LAST_OP) return 0; )	// range check

      n = opskp[op = *dfa++];
      if(n==M42){
	 // CLO       dfa  or  CLO       BOT sz dfa
	 // CLOMN m n dfa  or  CLOMN m n BOT sz dfa
	 if(op==CLOMN) dfa += 2;
	 if( (n = skp2[*dfa]) == M42) dfa += dfa[1];
	 else			      dfa += n;
	 continue;
      }
      switch(op){
	 default:	dfa += n;	   break;
	 case STR:	dfa += (*dfa + 1); break;
	 #if 0
	 case DOTSTAR:
	   if(*dfa==CHR) dfa += 2;
	   else		 dfa += dfa[1] + 2;
	   break;
	 #endif
	 case NODE:  lvl++;		   break;
	 case EDON: 
	    if(--lvl < 0 && inThisNode) return 0; // DFA fail
	    break;
      }// switch
   }// while
#endif

   if(*dfa == stopAt) return dfa;	// scan for END
   return 0;	// not found
}

#if 1
/* regExpSubs: substitute the matched portions of the src in dst.
 *	&	substitute the entire matched pattern.
 *	\digit	substitute a subpattern, with the given
 *		tag number. Tags are numbered from 1 to
 *		9. If the particular tagged subpattern
 *		does not exist, null is substituted.
 * 	!!!Note: if the line that was used regExpMatch() has gone byebye
 *	  then \digit will blow cookies since the tags point into the line.
 * I *really* don't like this routine but I use it so ...
 * Input:
 *   src:
 *   dst:
 * Returns:
 *   1:  Everything went as expected
 *   0:  Bad src or no match.
 */
int regExpSubs(char *src, char *dst, char *tags[]){
   char   c, *bp, *ep;
   int	   pin;
   char **bopat = tags, **eopat = &tags[RE_MAX_TAG];

   if(!tags[0]) return 0;

   while((c = *src++)){
      switch(c){
	 case '&': pin = 0; break;
	 case '\\': 
	    c = *src++;
	    if(c >= '0' && c <= '9') { pin = c - '0'; break; }
	 default: *dst++ = c; continue;
      }
      if((bp = bopat[pin]) && (ep = eopat[pin])){
	 while(*bp && bp < ep) *dst++ = *bp++;
	 if(bp < ep) return 0;
      }
   }
   *dst = '\0';
   return 1;
}
#endif

/* ******************************************************************** */
/* ******************* DFA Pretty Printer  **************************** */
/* ******************************************************************** */

// I dump DFAs a LOT so a pretty printer is worth the effort

#ifdef _MSC_VER
   #define strcat(dst,src) strcat_s(dst,sizeof(tab),src)
#endif

#define PRINT(op)    printf("%4ld: %s%s",       dfa - addr0, tab,op)
#define PRINTLN(op)  PRINT(op); printf("\n");
#define PRINTLNc(op) printf("%4ld: %s%s %c\n", dfa - addr0, tab,op, *dfa); dfa++
#define PRINTn(op)   printf("%4ld: %s%s%d",    dfa - addr0, tab,op, *dfa); dfa++
#define PRINTLNn(op) PRINTn(op); printf("\n");

static Byte *_dfaDump(Byte *dfa, Byte *addr0, int indent){
   Byte *dp;
   int   n;

   char tab[400];
   #define TABSZ 1
   #define TAB   "."	// TABSZ
   *tab = '\0'; for(n = indent; 0 < n--; ) strcat(tab,TAB);

  #define INDENT indent++; strcat(tab,TAB);
  #define DEDENT if(indent > 0) tab[--indent*TABSZ] = '\0';

   while(*dfa != END)
      switch(*dfa++){
	 case ONE:   PRINT("ONE ?");  goto clo;
	 case CLOP:  PRINT("CLOP +"); goto clo;
	 case CLOMN: printf("%4ld: %sCLOMN {%d,%d}",dfa - addr0, tab,*dfa,dfa[1]);
	    	     dfa += 2; goto clo;
	 case CLO:   PRINT("CLOSURE *");
	 clo:
	    switch(*dfa){
	       case CHR: 	        break;
	       case SPACE: case N_SPACE:
	       case DIGIT: case N_DIGIT:
	       case ALPHA: case N_ALPHA:
	       case ANY:		break;
	       case SET: case NSET:     break;
	       case BOT:
		  printf(" : BOT : tag size: %d",dfa[1]);
		  dfa += 2;
		  break;
	       default:
		  printf("\nBad dfa. opcode %o\n", dfa[-1]);
		  return 0;
	    }
	    printf("\n");
	    if(!(dfa = _dfaDump(dfa,addr0,indent + 1))) return 0;
	    break;
	 case CHR: PRINTLNc("CHR");	    break;
	 case STR:	// STR<byte><text>
	    PRINT("STR(");
	    n = *dfa++;		// size
	    #if ANDTHENULL
	       printf("%d)%s\n",n,(char *)dfa);
	    #else
	       printf("%d)",n);
	       for(int z = 0; z < n; z++) printf("%c",dfa[z]); printf("\n");
	    #endif
	    dfa += n;
	    break;
	 case ANY: PRINTLN("ANY .");      break;
	 case BOL: PRINTLN("BOL ^");      break;
	 case EOL: PRINTLN("EOL $");      break;

	 case SPACE:   PRINTLN("SPACE \\s");  break;
	 case N_SPACE: PRINTLN("!SPACE \\S"); break;
	 case DIGIT:   PRINTLN("DIGIT \\d");  break;
	 case N_DIGIT: PRINTLN("!DIGIT \\D"); break;
	 case ALPHA:   PRINTLN("ALPHA \\w");  break;
	 case N_ALPHA: PRINTLN("!ALPHA \\W"); break;

	 case BOT: PRINTLNn("BOT "); INDENT;  break;
	 case EOT: DEDENT; PRINTLNn("EOT "); break;
	 case BOW: PRINTLN("BOW \\<");	  break;
	 case EOW: PRINTLN("EOW //>");	  break;
	 case AORB: 
	    if((n = dfa[1])){
	       PRINT("OR: ");
	       if(n==0xff) printf("Tag(%d). Many hops to sibling OR",dfa[0]);
	       else        printf("Tag(%d). %d hop(s) to sibling OR",dfa[0],n);
	       dp = dfaScanForward(dfa + 2, AORB, 1);
	       printf(" at %ld\n",dp - addr0 + 1);
	    }else{ PRINT("OR: "); printf("Tags(%d). Nil\n",dfa[0]); }
	    dfa += 2;
	    break;
	 case NODE: PRINTLN("Begin NODE"); INDENT; break;
	 case EDON: DEDENT; PRINTLN("End NODE");   break;
	 case REF:  PRINTLNn("REF \\");		   break;
	 case SET:
	    PRINT("SET [");
	    for(n = 0; n < MAXCHR; n++) if(ISINSET(dfa,n)) printf("%c",n);
	    printf("]\n");
	    dfa += BITBLK;
	    break;
	 case NSET:
	    PRINT("Not SET [^");
	    for(n = 0; n < MAXCHR; n++) if(ISINSET(dfa,n)) printf("%c",n);
	    printf("]\n");
	    dfa += BITBLK;
	    break;
	 case PACMAN: PRINTLN("PACMAN"); break;
	 //case DOTSTAR:  PRINTLN("DOTSTAR");  break;
         #if DO_HOLDS
	 case HOLDS:
	    PRINTLNn("HOLDS: Top hops to STR: ");
	    break;
         #endif
	 default:
	    printf("Bad dfa. Opcode %d\n", dfa[-1]);
	    return 0;
      } // switch

    dfa++;
    PRINTLN("END");
    return dfa;
}

void dfaDump(Byte *dfa, int showSz){
   Byte *end, *addr0 = dfa;
   char *tab = "";

   PRINT("DFA flags: "); printf("%x\n",*dfa);
   end = _dfaDump(dfa + 1,dfa + 1,0);  // skip over OR hint
   if(end && showSz) printf("%ld bytes\n",end - dfa);
}


// (650) 253-0001. Russ Cox test squence:  (\d{3}-|\(\d{3}\\)\s+)(\d{3}-\d{4})
// search this file(103,515 bytes): 0.057324 sec: 1.8mb/sec
