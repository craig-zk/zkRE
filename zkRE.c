#if 0	// 1 to enable debug messages tracking DFA progress
   #define DFA_DEBUG 1
   #define DEBUGCODE(code) code;
#else
   #define DEBUGCODE(code)
#endif

// Formated for mono spaced font and [hard] tabs (==8)

/* Date stamp: 2026-09-01
*****************************************
Synopsis: Regular expression engine with ERE syntax for ASCII text.

Syntax: {}[]()^$.|*+?\  \d\D  \s\S  \w\W \<\>  \0 .. \9  (?:)
    d (digit), s (space), w (word), <> (begin/end word), (?:) non-capturing
See below for details.

A non-recursive back tracking regular expression engine.

Two C files: zkRE.[ch], no memory allocation, thread safe, public domain

Limitations:
 - NO support for non-ASCII (8 bit) text
 - Some group closures not supported, eg (a+)+a, (.+a)+b, (a+|(b|c))+
   To close a group, the group must have an "unambiguous" stopping point and
   no nested alternations.
 - Group values can differ from recursive engines (eg PCRE) or POSIX RE
    * Results can differ: eg "(a|ab)(bc|c)" match "abcabc" 
      --> \1=="a", \2=="bc" not \1=="ab", \2=="c"
    * Count and index can differ
      eg "a(b)|c(d)|a(e)f" match"aef" --> \1=="e", not \1==\2=="", \3=="e"
 - Alternation is longest match wins:
    (ab|abc) and (ab|abc)+ match "abc" --> "abc" vs "ab" PCRE
 - The width of the match tree is limited: viewing a match as a breadth
   first search, the number of nodes/level is limited: ".*a" match
   "1234a67890" is width 11, ".*(a|b)" doubles the width. The compiler
   tweaks and the VM prunes to control growth, not always successfully.
 - I *think* worst case search time is polynomial (in search text
   length) with some big powers: "(.*) (.*) (.*) (.*) (.*)" should be a
   lot worse than quadratic. But it is not: matching seems linear,
   searching seems to be quadratic (worst case is no match).
   The better engines are (O(m*n) m==[dn]fa.len, n==search text len).

Tests: 1,100+ hand written tests, 221 are Henry Spencer's regular
expression tests (10 of which were modified).

Examples:
// clang egRE.c zkRE.c
// clang will compile tail call VM, gcc & MSVC won't so they get a big switch
// No speed differences

#include <stdio.h>
#include <string.h>
#include "zkRE.h"

static void doRE(char *re, char *text, int flags){
   char *tags[2 * RE_MAX_TAG], *ptr;   // tags is optional
   Byte  dfa[2000];
   int   n,s;

   n = sizeof(dfa);
   if( (ptr = regExpCompile(re,dfa,&n,0)) ){ printf("%s\n",ptr); return; }
   printf("%s --> %d byte DFA\n",re,n);
   if(regExpMatch(dfa,text,tags,flags,0)){
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
   doRE("(ab|a)bc","abc",0x0);          // match \1 == "a"   (25 byte DFA)
   doRE("(dog|cat)\\1","catcat",0x0);   // match             (30 byte DFA)
   doRE("(a.c){1,2}","abcadcaec",0x0);  // match \1 == "adc" (22 byte DFA)
   doRE("(ab*c)+","abbbcacab",0x0);     // match \1 == "ac"  (24 byte DFA)
   doRE("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaaaaa",0x0);     // match             (106 byte DFA)
   doRE("(test\\w*)","it was a testing time",RE_SEARCH);  // \1 == "testing"
   return 0;
}
---------------------------------------
// More examples:

zkl: var r=RegExp(0''^(aa|a)+')		# 23 byte DFA
zkl: t:=Time.Clock.runTime; r.search("a"*100_000); Time.Clock.runTime-t
0.029371
zkl: r.matched
L(L(0,100000),"aa")

zkl: var r=RegExp(0''^(?:a+)+b')	// (?:a+)+a won't compile, ambiguous
zkl: t:=Time.Clock.runTime; r.search("a"*100_000 + "b"); Time.Clock.runTime-t
0.001033
zkl: r.matched
L(L(0,100001))

zkl: var r=RegExp(0''(?:.*) (?:.*) (?:.*) (?:.*) (?:.*)')	# 18 byte DFA
#zkl:var txt = ("a"*5000) + " b c d e"
zkl: var txt = ["a".."e"].apply('*(1_000)).concat(" ")  # "aaa bbb ccc ddd eee"
zkl: t:=Time.Clock.runTime; r.search(txt); Time.Clock.runTime-t
1.6e-05		# Linux anyway, it has memrchr(). Otherwise 0.002603 sec
zkl: r.matched
L(L(0,5004))
zkl: t:=Time.Clock.runTime; r.search("a"*5000); Time.Clock.runTime-t
7.5e-05	  # no match. Actually searching --> 0.002971 sec: quadratic time

//////////

zkl: var d=File("VM/zkRE.c").read()   // has (65O) 253-0001. in last line
Data(135,326)

zkl: var r=RegExp(0''(\d{3}-|\(\d{3}\)\s+)(\d{3}-\d{4})')	# 61 bytes
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.003635
zkl: r.matched
L(L(135190,14),"(65O) ","253-OOO1")    // 65"O" is zero, don't match here

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$')	# 69 bytes
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.016997 // short circuits by noting that ABC...XYZ is not contained in text
     // ie strstr() and fail 2,786 times, up to this comment @ character 2,786

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZaaa$')  // aaa is AAA
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
2e-05 // strstr(ABCDE...XYZAAA) ONCE and fail
 // Meta fail: this comment is in search text, aaa/AAA to avoid false match
*************************************
*/


/* zkRE.c - Regular expression pattern matching and searching
 *
 * UTF-8: No. Eight bit safe but multibyte characters in the DFA are going
 * to cause problems, as in works but wrong results, such as in sets, single
 * character closures. Searching UTF-8 should see no issues.
 * 
 * By:  Ozan S. Yigit (oz), Dept. of Computer Science, York University
 * Mods, oh so many mods (such as "|", CLOP, STR and HOLDS, tail call engine,
 *   {}, "back tracking" for "|?*+{}", fibers).
 * C Durland craigd@zenkinetic.com
 *
 * These routines are the PUBLIC DOMAIN equivalents of regex routines as
 * found in 4.nBSD UN*X, with minor extensions.
 * Since moved towards Extended Regular Syntax (ERE) and PCRE behavior, with
 * *no* plans for complete compatibility.
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
 *	unsigned int flags, char *tags[], ReErrorInfo *ep);
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
 *	 (a.b)*		Note for [14]: Only if [14] does not fork, so no
 *	 		  (.*a)+b. Does work:  (a.b)*  (a*b)*  (a|b)*
 *			The compiler & VM make efforts to reduce forking
 *			  but more work could to be done.
 *                      Back tracks. See [18].
 *
 *   [6]  +		One or more, otherwise same as [5]
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
 *			Typically "[A-Za-z0-9_]". Varies as app can change
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
 *                      (|) == noop/success
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
 *	  (recursive decent engines) with the least amount of
 *	  effort/recursion.
 *	  It is NOT Posix behavior and can conflict with PCRE.
 *
 * Acknowledgements:
 *   HCR's Hugh Redelmeier has been most helpful in various stages of
 *   development.  He convinced me to include BOW and EOW constructs,
 *   originally invented by Rob Pike at the University of Toronto.
 * References:
 *   Software Tools		Kernighan & Plauger
 *   Software Tools in Pascal	Kernighan & Plauger
 *   Grep [rsx-11 C dist]	David Conroy
 *   ed - text editor		Un*x Programmer's Manual
 *   Advanced editing on Un*x	B. W. Kernighan
 *   RegExp routines		Henry Spencer
 *   "Regular Expression Matching Can Be Simple And Fast"
 *      https://swtch.com/~rsc/regexp/regexp1.html 
 *   "Regular Expression Matching: the Virtual Machine Approach"
 *      https://swtch.com/~rsc/regexp/regexp2.html
 *   "Regex engine internals as a library"
 *      https://burntsushi.net/regex-internals/
 *   https://en.wikipedia.org/wiki/Regular_expression
 *   The Stack Overflow Regular Expressions FAQ:
 *      https://stackoverflow.com/questions/22937618/reference-what-does-this-regex-mean/22944075#22944075
 *   https://wiki.haskell.org/Regular_expressions#.28apple.7Corange.29
 *   https://wiki.haskell.org/Regex_Posix : Posix compliant Regex bugs
 *   https://regexr.com/
 *   https://regex101.com/
 *   https://www.debuggex.com/
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
 *	compile:	CHR f CHR o CLO CHR o END PACMAN CLO ANY END END
 *	matches:	fo foo fooo foobar fobar foxx ...
 *			PACMAN (optional) indicates back tracking not needed
 *			for .* (ie rest of text is matched)
 *
 *	pattern:	fo[ob]a[rz]	
 *	compile:	CHR f CHR o SET bitset CHR a SET bitset END
 *	matches:	fobar fooar fobaz fooaz
 *
 *	pattern:	foo\\+
 *	compile:	CHR f CHR o CHR o PACMAN CLOP CHR \ END END
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

#define _GNU_SOURCE	// memrchr (not on Windows)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HOME_BREW_CTYPE_H	// your versions of isalnum, isdigit, isspace, isword
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
#define CLOP	20	// +: one or more	    :: CLOP dfa END
#define CLOMN	21	// a{m,n}		    :: CLOMN M N dfa END
#define AORB	22	// A|B, (A|B) :: AORB <open tags> <hops to sibling OR>
#define NODE	23	// Begin node: a node is a group with OR, tree vertex
#define EDON	24	// End node
#define STR	25	// Match string :: STR <byte len><string>
#define HOLDS	26 // OPTIONAL. text must hold STR :: HOLDS <top hops to STR>
#define PACMAN	27	// Next closure op doesn't fork :: PACMAN CLO | CLOMN
#define DOTSTAR	28	// .* CHR | STR :: DOTSTAR CHR a | DOTSTAR STR n text
#define DOTSTAB 29	// .* b :: DOTSTAB b

#define LAST_OP  29	// so I can do sanity checks

#define PREFIX	0xf0	// Fake op: Eagar .*: PREFIX 3 abc\0 or PREFIX 1 '\0\0'
#define STAKE	0xf1	// Fake op used during compile


    // CLO flags for (a)*
#define CLO_PACMAN	0x01	// unambiguous stopping point: a*b (vs a*a)
#define CLO_VWIDTH	0x02	// variable width: (a*b)* or NODE forks
#define CLO_TAGS	0x04	// contains noncosmetic tags: (a)* vs (?:a)*
#define CLO_LONG	0x08	// size is two bytes

    // Skip values for CLO XXX to skip past the closure
#define ANYSKIP		2 		// CLO ANY|DIGIT|..    END
#define CHRSKIP		3		// CLO CHR chr	       END
#define SETSKIP		(2 + BITBLK)	// CLO SET 16/32bytes  END

   // constants for op sizes
#define ORSZ		 3 		// AORB tags moreor
#define BOTSZ		 2 		// BOT/EOT n
#define CLOHEADERSZ	 4 		// CLO/CLOP BOT sz flags
#define CLOMNHEADERSZ	 6 		// CLOMN M N BOT sz flags


   /* Notes on HOLDS: An attempt to fail fast if text doesn't hold a string.
    * This assumes memmem/strstr is "reasonably" quick and the DFA would not
    * be, eg, matching a big text with ".*expression". Or actually
    * searching: move==1.
    */
#define DO_HOLDS   1
#define ANDTHENULL 1 // 0|1: gotta be 1 for Windows as they don't have memmem(3)

#if __clang__ || __GNUC__
  #define DO_DOTSTAR   1	// !!!??? how to know if memrchr defined?
#else
  #define DO_DOTSTAR   0	// Windows does not have memrchr(3)
#endif

#define IS_ALPHA(c)	(isalnum(c) || c == '_')     // upper/lower + digits

#define PAYLOAD		0	// experimental


/* ******************************************************************** */
/* **************************** Bit Tables **************************** */
/* ******************************************************************** */

/* Bit table:  a string of bits stored in an array of char
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

    /* The following defines are for character set size. 128 for straight
     * ASCII, 256 for Euro ASCII (ISO/IEC 8859-1/Latin-1 8 bit characters).
     */
#define MAXCHR	 256		//  128 or  256
#define BLKIND	0xf8		// 0x78 or 0xf8

    // The following defines are not meant to be changeable.
    // They are for readability only.
#define CHRBIT	8
#define BITBLK	MAXCHR/CHRBIT		// 16 or 32 bytes
#define BITIND	0x7

    /* Add or check to see if character is in bit table (character set).
     * Note:
     *   When calling these routines, make sure c is an unsigned char (or
     *     int) so if it has the high bit set, casting it to an int won't
     *     make it a large negative number.
     *   If c >= MAXCHR, bad things probably happen. And nobody checks.
     * It would be nice to add a guard for the 7 bit case: ((c)&0x80==0) && ...
     *    Then I could use it in for 8 bits. If space was an issue
     */
#define ISINSET(bittab,c) ((bittab)[((c) & BLKIND)>>3] & (1<<((c) & BITIND)))
#define CHSET(  bittab,c)  (bittab)[((c) & BLKIND)>>3] |= 1<<((c) & BITIND)

static void chset(Byte bitTable[BITBLK], Byte c){ CHSET(bitTable,c); }

static Byte *dfaScanForward(Byte *dfa, int stopAt, int inThisNode, Byte **);

#if HOME_BREW_CTYPE_H	// isalnum, isdigit, isspace, isword
   #define IS_WORD(c)	isword(c)	// not in <ctype.h>
#else
   static Byte wordTable[BITBLK]; 	// bit table for word definition
   static int  wordTableDefined = 0;

   #define IS_WORD(c)	ISINSET(wordTable,c)
   //static int isword(int c){ return IS_WORD(c); }	// for debugging
#endif

    // This code has optimizations that assume match is case sensitive
#define CEQ(a,b) 	((a) == (b))		// character ==


  /////////////////////////////////////////////////////////////////////////
 //////////////////// Compile Expression to DFA //////////////////////////
/////////////////////////////////////////////////////////////////////////

#define RE_SLOP	50		// dfa overflow protection

#define STORE(x)	(*mp++ = x)  // RE_SLOP guards against overflow
 
      // info for and about (), |
typedef struct{ int sz, maxSz; Byte *mp; char string[270]; } Str;
typedef struct{ 
   int   tagc,    nodeId, orCnt, hasOR, cosmetic, hast, forks, vwidth;
   Byte *orAddr, *botAddr;
}Tag;

typedef struct{
   UChar *p0;
   Byte  *dfa, *endDFA, *mp, *digitSet, *tagLog, *stake, *sp;
   int    tagi, tagc, ortagc, topor;
   Str   *str;
   Tag   *tagstk;
   unsigned nodeId, orCnt;
   ReErrorInfo *epac;
}CompileState;		// since I'm not "properly" recursive

#define _BADPAT(dfa,msg)	return (dfa[1] = END, msg)
#define  BADPAT(dfa,msg)	return badpat(msg,p,cscape)
static char *badpat(char *msg, UChar *p, CompileState *cs){
   if(cs->epac){ // if I have a buffer, return the offset of the error
      ReErrorInfo *ep = cs->epac;
      int	     n, i;
      char	    *txt = ep->txt;

      n = cs->epac->n = p - cs->p0; 
      if(!n) *txt = '0';	// txt was zero'd
      // very low budget itoa, sprintf: "%d : %s".fmt(n,msg)
      for(i = 0; n; n /= 10, i++) txt[i] = (n % 10) + '0';	// n==0 -->""
      for(n = 0, i--; n < i; n++, i--)	// reverse digits
         { char d = txt[i]; txt[i] = txt[n]; txt[n] = d; }
      strcat(strcat(txt," : "),msg);
      cs->epac->errorMsg = txt;
      _BADPAT(cs->dfa,ep->txt);
   }
   _BADPAT(cs->dfa,msg);
}

static UChar *storeCHR(char, Byte *mp, Str *);
static char  *compile(UChar *p, CompileState *, int justLooking);
static int    packRat(UChar *p, Byte *, Tag *tp, Byte *b, CompileState *, char **error);
static Byte  *hooverPrefix(Byte *dfa, Byte *mp);
static Byte  *chr2str(
	   UChar *lp, Byte *mp, Str *, int movePrev, int tagi, Tag *tagstk);

#define COSMO_TAGS	(RE_MAX_TAG*5)	// (?:a): not a run time resource

   // DFA flags
#define DFAF_HAS_REFS	2

    /* Compile RE to internal format & store in dfa[]
     * Input:
     *   pat:   Pointer to regular expression string to compile.
     *   dfa:   Pointer to dfa[*dfaSz] where DFA will be stored
     *   dfaSz: Pointer to size of buffer allocated for DFA.
     *          Returns size actually used. You can allocate & copy dfa.
     *   ReErrorInfo: Filled in if match failed badly. Can == 0.
     *          Cleared in any event.
     * Returns:
     *   NULL:  RE compiled OK.
     *   	Pointer to error message. *DON'T* use the resulting DFA!
     *   	  Well, you can, I check..
     *   	If ReErrorInfo, the error message has the column of the
     *   	  offending character prepended, which is also in ->n.
     *   dfaSz: Modified to size of actual DFA
     */
char *regExpCompile(char *pattern, Byte dfa[], int *dfaSz, ReErrorInfo *epac){
      // tagstk holds which tag is open (see tagLog) & 
      // the previous & current siblings of the OR tree
   Tag   tagstk[COSMO_TAGS] = { 0 }; // subpat tag & OR tree stack
   char *s;	// error msg
   Byte
        *mp = dfa,		// dfa pointer for STORE
         digits[BITBLK]     = { 0 }, // [0-9] so can [0-9] --> DIGIT
         tagLog[RE_MAX_TAG] = { 0 }; // log of which tags hold data, are open
   int	 n, topor;		// top level OR, ie outside of any group
   Str   str;			// buffer for chr to string compression
   CompileState  cscape = { 0 };

   cscape.epac = epac;
   if(epac) memset(epac,0,sizeof(ReErrorInfo));

   if(*dfaSz < (20 + RE_SLOP))
      _BADPAT(dfa,"regExpCompile: dfa array too small to hold anything meaningful");
   DEBUGCODE( memset(dfa,0,*dfaSz); )	// makes my life easier

   STORE(0);			// DFA flags
   *mp = END;			// "initialize"
   str.sz = str.maxSz = 0;

   for(n = '0'; n <= '9'; n++) CHSET(digits,n);	// [0-9] --> DIGIT

   #if !HOME_BREW_CTYPE_H
	// Build a bit table definition of a word. Done once.
        // Not thread safe: if matters, call regExpCompile(".") very early
	// !!! just make this local and build every time like digits
   if(!wordTableDefined){
      wordTableDefined = 1;
      for(n = 0; n <= MAXCHR; n++) if(IS_ALPHA(n)) CHSET(wordTable,n);
   }
   #endif //HOME_BREW_CTYPE_H

   cscape.p0	   = (UChar *)pattern;
   cscape.dfa	   = dfa;
   cscape.mp	   = cscape.sp = mp;
   cscape.endDFA   = dfa + *dfaSz - RE_SLOP;  // space available w/overflow checking
   cscape.str	   = &str;
   cscape.tagstk   = tagstk; cscape.tagLog = tagLog; cscape.tagc = 1;
   cscape.digitSet = digits;
   // other fields have been initialized to zero
   
   if(pattern == 0 || *pattern == '\0')
      _BADPAT(dfa,"regExpCompile: \"\" is a bad regular expression");

   if( (s = compile((UChar *)pattern,&cscape,0)) ) return s;

   if(cscape.tagi > 0) _BADPAT(dfa,"regExpCompile: Unmatched (");

   topor = cscape.topor;	// will be used in post
   mp	 = cscape.mp;		// ""

   /*  a|b|c  --> (?:a|b|c)
    * ^a|b|c  --> (?:^a|b|c), ^ is not global
    *  a|b|c$ --> (?:a|b|c$), $ ""
    */
   if(topor){	// top level OR, wrap DFA in Node
      memmove(dfa + 1, dfa, (mp - dfa));	// move an extra byte, RE_SLOP
      *(dfa + 1) = NODE;
      mp++;
      STORE(EDON);
   }

   STORE(END);

   /////////////////////////////// post process DFA

   /* Ponder:
    * -Expand PREFIX: (abc|def) --> FAX 2 ABC\0DEF\0
    * -Convert a*abc to a*bc, the PACMAN applies
    * -Common prefix: (dog|dogs) --> dogs?, a(bc\d) --> abc
    * -Group holds: (cat|dog) --> HOLDS1OF(cat,dog)
    *   ??Search for any of: find any of ("cat","dog")
    *   ?Only not common combos ie don't sit in memchr/fork loop with lots
    *     of noops
    *   There are algorithms for multi string search such as "teddy",
    *     Aho-Corasick, Commentz-Walter
    *   Rust RE, ripgrep have good write ups.
    * -Common suffix???: (a|ba|cba) --> (|b|cb)a   and do what? holds?
    * -Disjoint sets: (abc|xyz) --> jmpTable(a:A, x:X) (A:bc|X:yz)
    *   Good for: don't need to fork
    *   Or compiler tells OR one op can decide which branch to continue
    *   Differerent from PACMAN, PACMAN can have multiple wins
    * -If RE ends with $, consider starting at end and repeat: back up, match
    * -If reverse(dfa) exists:
    *   -If RE has STR in middle and RE_SEARCH, strstr to that and match
    *   left and right. This can be dicy.
    *   Or if line oriented, back to start of line
    * - Line oriented RE_SEARCH: use STR to find a line, back up to start of
    *    line, match. No match --> repeat
    */

   mp = hooverPrefix(dfa,mp);	// for RE_SEARCH at runtime

   #if DO_HOLDS	// Are there any long strings that have to be in any match?
   	/* STR at start of DFA == HOLDS
	 * HOLDS hops-to-STR	2 bytes
	 * We are adding to the DFA, we have [at least] most of RE_SLOP so
	 *   at 2 bytes, we have room for RE_SLOP/2 (== 25) HOLDS.
	 * If the DFA starts with PREFIX, we'll add HOLDS after it.
	 * 
	 * RE_SEARCH: PREFIX somewhat midigates the needs for HOLDS as we
	 *   should be starting close to a match.
	 * STR at, or close to, the start of the DFA is a better gate keeper
	 *   than HOLDS.
	 * No PREFIX means use any string as RE_SEARCH would have to single
	 *   step. Note that DOTSTAR is not a CLO.
	     ???PACMAN CLOs are cheap.
	 * If PREFIX: a STR after a closure is a good candidate for HOLDS.
	 * !!!If the DFA starts with BOL (^), don't bother with HOLDS???
	     !!!??? what about ^.*hoho? I suspose DOTSTAR is fine
	 */
      #define HOLDS_MAX      4	// the max number of HOLDS I'll use
      #define HOLDS_MIN_STR 10  // min len of string that worth searching for
//   if(!topor && str.maxSz >= HOLDS_MIN_STR){ // not one big OR & maybe long STRs
   if(!topor && dfa[1]!=BOL && str.maxSz >= HOLDS_MIN_STR){ // not one big OR & maybe long STRs
      Byte *dfa1 = dfa + 1, *dp, 
           *clo1 = 0, clos[] = { CLO, CLOP, ONE, CLOMN, 0 };
      int   n, z, hops, sz, minSz = 0;
      struct{ Byte *str; int sz, hops; } strs[HOLDS_MAX] = { 0 },
           *smp = strs, *sp;

//    if(*dfa1 == BOL)    dfa1++;	// don't move ^
      if(*dfa1 == PREFIX){ // action starts after PREFIX
	 dfa1 += 3 + dfa1[1];
	 for(clo1 = 0, n = 0; clos[n]; n++)	// find first closure
	   if( (dp = dfaScanForward(dfa1,clos[n],0,0)) && (clo1==0 || dp < clo1))
	      clo1 = dp;
	 // if PREFIX && clo==0 (no closures), no HOLDS
      }else clo1 = dfa;		// no PREFIX

      if(clo1)
	  // find the HOLDS_MAX longest STRs outside of Nodes
	  // skip if DFA starts with STR
	 for(hops = 0, dp = dfa1; (dp = dfaScanForward(dp,STR,1,0)); ){
	    sz = dp[1]; 
	    if(++hops > 0xfd) break;	// 1 byte worth of index

	    if(hops && dp > clo1 && sz >= HOLDS_MIN_STR && sz > minSz){
	       smp->sz = sz; smp->str = dp; smp->hops = hops;
	       // find smallest slot
	       for(minSz = strs[0].sz, z = HOLDS_MAX, sp = smp = strs; 
		       z--; sp++)
		  if(sp->sz < minSz){ minSz = sp->sz; smp = sp; }
	    }
	    dp += (sz + 2);	// op after STR
	 }//for

      for(n = 0; n < HOLDS_MAX && strs[n].sz; n++){}	// count strings
      if(n){      	// insert HOLDS towards start of DFA
         sz = n<<1;	// space needed for <HOLDS hops>*n
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

    /* Actually convert text to DFA codes.
     * This is called in two places: 
     *  - Above: to compile the entire DFA
     *  - When compiling a closure, I need to look ahead to determine if
     *    PACMAN applies to closure-in-progress (packRat). I do this in
     *    place (I shall not allocate) - just continue compiling using
     *    "safe" assumptions. Which leads to lots of hackery to get enough
     *    info without moving ops past the point of recovery or generating
     *    bogus errors. Basically, restrict code movements to areas past
     *    "real" code (post STAKE).
     */
static char *compile(UChar *p, CompileState *cscape, int justLooking){
   Byte
     *dfa = cscape->dfa,
     *mp  = cscape->mp,	// dfa pointer for STORE
     *sp  = cscape->sp,	// previous op
     *lp,		// start of current op
     *tagLog = cscape->tagLog,   // log of which tags hold data, are open
      bittab[BITBLK] = { 0 };	 // bit table for SET
   int 
      tagi   = cscape->tagi,	// tag stack index
      tagc   = cscape->tagc,	// tag count
      ortagc = cscape->ortagc,	// keep tag count in sync with ORs
      n, z, seed = 0, pork = 0, porkPie; // justLooking: when is enough enough?
   Tag *tp, *tagstk = cscape->tagstk;
   Str *str = cscape->str;
	// id for OR tree node, unique id for every group
   unsigned nodeId = cscape->nodeId, orCnt = cscape->orCnt;

   for(; *p; p++){
      lp = mp;			// start of next dfa state
      switch(*p){
	 case '.': 		// match any character
	    STORE(ANY); seed = 1;
	    break;
	 case '^':		// match beginning of line: "^..", "..|^.."
	    STORE(BOL); 
	    tagstk[tagi].forks = 2;	// (^)* == infinite loop
	    seed = 1;
	    break;
	 case '$':			// match end of line: "..$", "..$|.."
	    STORE(EOL);
	    tagstk[tagi].forks = 2;	// ($)* == infinite loop
	    seed = 1;
	    break;
	 case '[':			// match a set of characters
	    //??? [\w-] --> wordTable + '-',etc,  \xHH-\xHH
	    if(p[2]==']' && p[1]!='^'){		// [^] --> error
	       // [.] --> CHR . Russ says people do that
	       mp = storeCHR(p[1],mp,str);
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
	    if(ISINSET(bittab,'5') && 0==memcmp(bittab,cscape->digitSet,BITBLK)){
	       // [0-9], [1234567890] --> DIGIT
	       mp = lp; STORE(DIGIT);
	       memset(bittab,0,BITBLK);
	    }else // store table and clear for next use
		for(n = 0; n < BITBLK; bittab[n++] = 0) STORE(bittab[n]);
	    seed = 1;
	    break;
	 case '?': z = ONE;  goto clo;	// match none or one
	 case '+': z = CLOP; goto clo;	// match 1 or more
	 case '*': z = CLO;		// match 0 or more of preceding RE
	 clo:
	 {	// TODO?: (x+x+)+y --> xxx*y
	    Byte b = 0;
	    int  pacman, hz;

	    if(p == cscape->p0) BADPAT(dfa,"regExpCompile: Empty closure");
	    n = (*sp==CHR);	// remember this for later

	    switch(*sp){	// some redundancy here for CYA
	       default: BADPAT(dfa,"regExpCompile: Invalid closure");
	       case CHR:   case ANY:     case SET:   case NSET: 
	       case DIGIT: case N_DIGIT: case SPACE: case N_SPACE:
	       case ALPHA: case N_ALPHA: case EOT: case EDON:
	          break;
	       case CLO: case CLOP: case ONE:
		  BADPAT(dfa,"regExpCompile: a** not allowed. (?:a*)* is.");
	    }

	    tp = &tagstk[tagi + 1];	// tag we might be in
	    if(justLooking){	// no more recursion
	       /* a+b*a: a* does not visit here, not closed. b* does with
	        *   STAKE at +, past a+ so STAKE won't move.
	        * (a+)?b --> (a+ --> BOT CHR a STAKE folding this into CLOP
	        *   would move STAKE, which causes problems. So, don't
	        *   close, (don't change lp/sp/mp), compile to (a)b which is
	        *   fine for finding b.
	        *   (a+)?b  -+-> (a!)b   -?-> (a+)!b
	        *   (a+)?b+ -+-> (a!)b+  -?-> (a+)!b+  -+-> (a+)?b!
	        * CLO is not definitive, it is not a "complete" suffix so,
	        *   in that case, compile the thing after.
	        * Why not not close anything? Because if b is CLO (*) it is
	        *   not a prefix whereas CLOP (+) is. a*b*a vs a+b*a
	        */
	       if(z==CLO || z==ONE) seed = 0;
	       if((*sp==EOT || *sp==EDON) && cscape->stake > tp->botAddr) break;
	       pacman = 0;
	    }else{	// compile b to determine PACMAN
	       char *s;
	       cscape->mp     = mp;   cscape->sp = sp;
	       cscape->tagi   = tagi;
	       cscape->tagc   = tagc;
	       cscape->ortagc = ortagc;
	       cscape->nodeId = nodeId; cscape->orCnt = orCnt;
	       pacman = packRat(p,sp,tp,&b,cscape, &s);	// recursion
	       if(s) return s;
	    }

	    if(*sp==EOT || *sp==EDON){
	       // CLO/CLOP/ONE BOT flags sz: 4 bytes
	       // Flags: 1 (PACMAN), 2 (wide), 4 (contains non-cosmetic tags)
	       //	 8 (2 bytes of size)   see CLO_ flags
	       // (abc)* --> CLO BOT 11 0       BOT 1     CHR a CHR b CHR c EOT 1 END
	       // (a*c)* --> CLO BOT 12 1       BOT 1 CLO CHR a END CHR c   EOT 1 END
	       // (a|b)* --> CLO BOT 14 1  NODE BOT 1 CHR a AORB 1 0 CHR b EOT 1 EDON END
	       int sz, flags = pacman;

	       // forks bits: 1 (actually forks), 2 (do not close this)
	       // NODE always forks but is a special case
	       #if PAYLOAD
	          if((tp->forks & 2) || (tp->forks && *sp!=EDON))
	       #else
	          if(tp->forks && !justLooking)
	       #endif
		  BADPAT(dfa,"regExpCompile: Invalid () closure: eg (.*b)*, (a|(b|c))* (nested |)");

	       lp = tp->botAddr;	// lp --> BOT or NODE BOT
	       /* tp->botAddr/orAddr shift right (by hz) but they will not
	        * used as chr-->str has happened by this point.
	        */
//???  (.)* --> DOTSTAR?

	       sz = mp - lp;	// alt: call dfaScanForward() at runtime, ick
	       if(sz > 0xfe) BADPAT(dfa,"regExpCompile: (a)*: a too long");
	       
	       /* Are any of the tags in this tag ("(((a)))*" vs "(?:((a)))*")
	        * "real" (not cosmetic)? In (?:(?:(a+)b)+c)+d / abcaabcd
	        * dfaScanForward would would miss this as it would not look
	        * in nested CLOs.
	        */
	       if(tp->hast)	 flags |= CLO_TAGS;
	       if(*sp == EDON)   flags |= (tp->forks ? CLO_VWIDTH : 0);
	       else	         flags |= (tp->vwidth && !pacman)<<1; // VWIDTH

	       hz = 4;
	       memmove(lp + hz, lp, sz);  // open hole for CLO BOT flags sz
	       sp = mp + hz; mp = lp; 
	       STORE(z); STORE(BOT); STORE(flags);
	       STORE(sz + 1);	// sz == dfa (being closed) len + 1
	       mp = sp;  STORE(END);
	    }else{	// sp --> a* <-- mp  --> CLO CHR a END
	       lp = sp;		// previous opcode
	       if(z==CLOP && *sp==ANY)  // ".+" --> "..*" : special case
		  { z = CLO; STORE(ANY); lp++; }

	       //#if DO_DOTSTAR	// argh, Windows.   Remember: .+ --> ..*
	       if(b && z==CLO){	// packRat special cases ".*b" & ".?b"
		  mp--;		// mp==sp,  .*b, (.*)b --> ANY STAKE CHR/EOT
		  if(sp[2]==CHR) STORE(DOTSTAR);	// post packRat code
		  else{		 STORE(DOTSTAB); STORE(b); }
	       }else
	       //#endif
	       {
		  hz = 1 + pacman;
		  memmove(lp + hz, lp, mp - lp);	// open hole for CLO
		  sp = mp + hz; mp = lp; 
		  if(pacman) STORE(PACMAN);
		  STORE(z); mp = sp; STORE(END);
	       }
	    }

	    tagstk[tagi].forks |= !pacman;	// if we are actually in a tag
	    tagstk[tagi].vwidth = 1;		// (tag)* is varible width

	    // lp ?--> CLO|CLOP|ONE CHR a END
	    // pack strings? "ab*" --> CHR a CHR b --> CHR a CLO CHR b END
	    if(n){	// multi op op messes with check at end of switch
	       str->sz--;	// "123456+" --> STR(12345) CLOP 6
	       lp = chr2str(lp,mp, str, 1, tagi,tagstk); // --> CLO|CLOP|ONE
	       mp = str->mp;
	    }
	    sp = lp;	// CLO|CLOP|ONE
	    // leave lp & sp pointing to CLO|CLOP|ONE so can check for **
	    // and want them pointing to vaild non CHR ops
	    break;
         }
	 case '{':	// {M,N}, {N}, {,N}, {M,}  invalid form --> CHR {
	 {
	    // {1,1} --> noop. {1,} --> CLOP. Somebody did this
	    char *tags[2 * RE_MAX_TAG];
	    int   M = 0, N = 0, pacman = 0, hz;
	    Byte  _;
	    Byte  mndfa[] = { // RegExp(0''{(?:(\d+)|(\d*),(\d*))}') eat dogfood
	      0,CHR,'{',NODE,BOT,1,PACMAN,CLOP,DIGIT,END,EOT,1,  AORB,0,0,
		 	     BOT,1,PACMAN,CLO, DIGIT,END,EOT,1,  CHR,',',
	                     BOT,2,PACMAN,CLO, DIGIT,END,EOT,2,
			EDON,
		CHR,'}',END };
	    	
	    z = 0;
	    if(regExpMatch(mndfa,(char *)p,tags,0,0)){
	       // {,} z==0, {m,} z==1, {,n} z==2, {m,n} z==3, {n} z==4
	       // PCRE:2: {,n} --> chrs
	       char **eopat = &tags[RE_MAX_TAG]; // atoi stops at [^0-9]
	       if(tags[1]!=eopat[1])     { M = atoi(tags[1]); z = 1; }
	       if(!tags[2])		 { N = M;	      z = 4; } // {n}
	       else if(tags[2]!=eopat[2]){ N = atoi(tags[2]); z|= 2; }
	       if(z==0){ mp = storeCHR('{',mp,str); break;   } // {,}

	       switch(*sp){	// some redundancy here for CYA
		  default: BADPAT(dfa,"regExpCompile: Invalid {} closure");
		  case CHR:   case ANY:     case SET:   case NSET: 
		  case DIGIT: case N_DIGIT: case SPACE: case N_SPACE:
		  case ALPHA: case N_ALPHA: case EOT: //case EDON:
		     break;
		  case CLOMN:	// too much work and adding (?:) isn't
		     /* [0-9a-f]{3}{1,2} --> (?:[0-9a-f]{3}){1,2}
		      * {}{} should be OK but *nobody* likes to see CLOMN
		      * CLOMN BOT: stake moves, dfaDump, op_CLOMN choke &
		      * probably dfaScanForward too.
		      */
		     BADPAT(dfa,"regExpCompile: {}{} --> (?:{}){}");
	       }

	       p  = (UChar *)eopat[0] - 1;	// '}'
	       tp = &tagstk[tagi + 1];
	       if(justLooking){
		  if(M==0) seed = 0;
		  if((*sp==EOT || *sp==EDON) && cscape->stake > tp->botAddr) break;
		  pacman = 0;		// no more recursion
	       }else{
		  char *s;
		  cscape->mp	 = mp;   cscape->sp = sp;
		  cscape->tagi   = tagi;
		  cscape->tagc   = tagc;
		  cscape->ortagc = ortagc;
		  cscape->nodeId = nodeId; cscape->orCnt = orCnt;
		  pacman = packRat(p,sp,tp,&_,cscape, &s);
		  if(s) return s;
	       }

	       if(M!=N || z==1){// {n}(4) & {n,n}(3) don't fork(), {0,}(1) does
		  tagstk[tagi].forks |= !pacman; // if we are actually in a tag
		  tagstk[tagi].vwidth = 1;	 // a{2,3} 
	       }else pacman = 0;	// {n}: fixed width match
	    }else{
	       mp = storeCHR('{',mp,str);   // context matters
	       break;			     // not CLOMN, just text
	    }
	    if(M>0xff || N>0xff || (M>N && N))
	       BADPAT(dfa,"regExpCompile: {m,n}: m <= n < 256");
	    if((M==0 && N==0) && z>1)  // {,0}(z==2), {0,0}(z==3), {0}(z==4)
	       BADPAT(dfa,"regExpCompile: {0} & {0,0}: Invalid");
	    
//??? .{0,} --> DOTSTAR?
	    n  = (*sp==CHR);	// remember this for later
	    lp = sp;		// previous opcode

	    if(*sp==EOT || *sp==EDON){	// EDON "in progress"
	       // CLOMN M N BOT flags sz: 6 bytes, flags as above
	       // Same as CLO with addition of M & N
	       //(a){1,2}  -->CLOMN 1 2 BOT  7 0   BOT 1 CHR a EOT 1 END
	       //(a*b){1,2}-->CLOMN 1 2 BOT 11 1   BOT 1 CLO CHR a END CHR b EOT 1 END
	       int sz, flags = pacman;

	       if(tp->forks && !justLooking)
		  BADPAT(dfa,"regExpCompile: Invalid {} closure: eg (.*a){m,n}, (a|b){m,n}");

	       if(!justLooking && (M>1 || N)){
		  // (a*){2,}b  fails to match aaab if a is PACMAN.
		  // {0,} & {1,} work (ie *,+,?) but not {m,n}
		  // a closure in the tail position is a prolem
		  // !!!??? how to do this with tag?  ((b*|c)){2,}
		  Byte *clo = 0;
		  *mp = END; dfaScanForward(tp->botAddr,END,1,&clo);
	       	  if(clo)	// there is a closure in the tail position
		     // (a{2}){3,4} OK
		     if(!(*clo==CLOMN && clo[1] && clo[1] == clo[2]))
			BADPAT(dfa,"regExpCompile: Invalid {} closure: eg (a+){2,} OK: (a+b){2,}");
	       }

	       lp = tp->botAddr;
	       sz = mp - lp;
	       if(sz > 0xfe) BADPAT(dfa,"regExpCompile: (a){m,n}: a too long");

	       if(tp->hast)    flags |= CLO_TAGS;
	       if(*sp == EDON) flags |= (tp->forks ? CLO_VWIDTH : 0);
	       else	       flags |= (tp->vwidth && !pacman)<<1;

	       hz = 6;
	       memmove(lp + hz, lp, sz); // open hole for CLOMN M N BOT flags sz
	       sp = mp + hz; mp = lp; 
	       STORE(CLOMN); STORE(M);     STORE(N); 
	       STORE(BOT);   STORE(flags); STORE(sz + 1);
	       mp = sp;      STORE(END);
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
	       str->sz--;	// "123456+" --> STR(12345) CLOP 6
	       lp = chr2str(lp,mp, str, 1, tagi,tagstk); // --> CLOMN
	       mp = str->mp;
	    }
	    sp = lp;	// CLOMN
	    // leave lp & sp pointing to CLOMN so can check for **
	    // and want them pointing to vaild non CHR ops
	    break;
	 }
	 case '(':	// "(?:" == non-capturing group, ie no tag
	    // (?i) --> case-insensitive mode, (?-i) to turn off
	    // TODO: (?#comment)
	    z = 0; if(p[1]=='?' && p[2]==':'){ p += 2; z = 1; }
	    if(ortagc){ tagc = ortagc; ortagc = 0; }
	    if(tagc < RE_MAX_TAG && tagi < (COSMO_TAGS - 1)){
	       nodeId++;  // I *might* worry about overflow with 16 bit ints
	       // justLooking: tag *might* be node, won't know until )
	       if(!pork){ pork = 1; porkPie = nodeId; }
	       tp = &tagstk[++tagi];		// assume new node
	       memset(tp,0,sizeof(Tag));
	       tp->tagc = tagc; tp->botAddr = mp; tp->nodeId = nodeId; 
	       tp->cosmetic = z;
	       if(!z){ STORE(BOT); STORE(tagc++); }	// tag is now open
	       else *mp = BOT;		// fake op for test at end of switch
	    }
	    else BADPAT(dfa,"regExpCompile: Too many () or (?:) pairs");
	    break;
	 case ')':
	    // "(a|b|)" will match ""
	    if(*sp==BOT) BADPAT(dfa,"regExpCompile: Null pattern inside ()");
	      // "a)" --> <CHR a EOT n> or <CHR a EOT n EDON>
	    if(*sp==CHR){  // multi op op messes with check at end of switch
	       chr2str(mp,mp, str, 0, tagi,tagstk);	// lp == mp
	       lp = mp = str->mp;
	    }
	    if(tagi > 0){
	       tp = &tagstk[tagi--]; n = tp->tagc;
	       if(!tp->cosmetic){ 
		  STORE(EOT); STORE(n);
		  tagLog[n] = 1;	// tag now holds data
		  tp->hast  = 1;	// tag is real (not cosmetic)
	       }
	       else{ *mp = EOT; mp[1] = 99; } // fake op for "(?:)*", 99 for debugging

	       if(tp->hasOR && *sp==AORB) tp->forks |= 2;	// (a|) --> 2
	    }
	    else BADPAT(dfa,"regExpCompile: Unmatched )");

	    if(tagi){	// child back propagates *?+{} : ((a*)), (a(b*c))+
	       Tag *tp_1 = (tp - 1);
	       if(tp->forks)  tp_1->forks  = tp->forks;
	       if(tp->vwidth) tp_1->vwidth = 1;
	       if(tp->hast)   tp_1->hast   = 1;
	       if(tp->hasOR)  tp_1->forks  = 1;	// (a|(b|c))*, nested OR
	    }
	       
	    nodeId = tagstk[tagi].nodeId;	// restore nodeId
	    tp     = &tagstk[tagi + 1];
	    if(tp->hasOR){	// dfa --> EOT n EDON
	       Byte *sav;

	       STORE(EDON);
	       if(!(justLooking &&	// (a*|b): Don't move STAKE
		    cscape->stake > tp->botAddr)){
	          sav = mp + 1;
		  mp  = tp->botAddr;	 // botAddr: put NODE before BOT
		  memmove(mp + 1, mp, sav - mp);  // open hole for NODE
		  STORE(NODE);	// using mp
		  sp = lp + 1;	// point to EOT, EDON if cosmetic
		  mp = sav;	// after EDON
		  lp = mp - 1;	// point to EDON
	       }
	    }
	    ortagc = 0;
	    if(pork && porkPie==tp->nodeId) pork = 0;	// now we know
	    break;
	 case '|':
	    // todo: a|b|c|d == [a-d]
	    // Bad: "|","a|","|b" "(|)*"  OK: "(|)","(a|)","(|b)","(||b)","(||)"
	    if(p==cscape->p0 || !p[1]) BADPAT(dfa,"regExpCompile: Empty |, (|) OK");
	    tp = &tagstk[tagi];	// current OR level
	    /* justLooking: if a is in node, b can't also be in node so this
	     * node doesn't count. Also ignore nested tags/nodes.
	     */
	    if(!pork){ pork = 1; porkPie = tp->nodeId; } // a|b has no tag
	    pork = 2;
	    switch(*sp){  // "$|" OK
	       case BOL: case BOW:	// ^|, \<|	????? why not?
		  BADPAT(dfa,"regExpCompile: Invalid |");
		  break;
	       case AORB: case BOT:      // (|, ||  what does (a||b)+ match? PCRE is confusion
		  tp->forks |= 2; break; // fake empty section, bad?: (|b)*
	    }
	    orCnt++;
	    //tp->forks  = 1;	// always, let ')' set
	    tp->vwidth = 1;	// almost always except a|b, a|bc is
	       // if previous OR at this level, link to here
	    if(tp->orAddr && (tp->nodeId == nodeId)){
	       n = (orCnt - tp->orCnt);  // # hops to here
	       //if(n > 0xfe) BADPAT(dfa,"regExpCompile: too many sub |s");
	       if(n > 0xfd) n = 0xff;	// I don't acutally rely on hop count
	       *(tp->orAddr) = n;
	    }

	       // I'm now the previous OR, fill out info for next OR
	    tp->orAddr = mp + 2;
	    tp->orCnt  = orCnt; tp->hasOR = 1;

	    if(tp->cosmetic) n = 0;  // # tags in play
	    else{
	       n = tp->tagc;	// < RE_MAX_TAG
	       z = n + 1; memset(tagLog + z, 0, RE_MAX_TAG - z);  // GC tags
	       // don't have to sweat ?->hast - doNodeGlider doesn't care
	    }
		// Store (AORB, open? tags, link to next AORB in node)
		// will update link at next sibling AORB
	        // Storing orCnt is not necessary, historical, yes/no OK
	    STORE(AORB); STORE(n); STORE(0);
	    ortagc = n + 1;

	    if(tagi==0) cscape->topor = 1;	// we'll deal with this in post
	    break;
	 case '\\':		// backrefs, word transitions, space, etc
	    switch(*++p){
	       case '\0': BADPAT(dfa,"regExpCompile: Bad quote");
	       case '<':  
	         STORE(BOW); 
		 tagstk[tagi].forks = 2;	// (\<)* == infinite loop
		 break;
	       case '>':
		  if(*sp == BOW)
		     BADPAT(dfa,"regExpCompile: Null pattern inside \\<\\>");
		  STORE(EOW);
		  tagstk[tagi].forks = 2;	// (\>)* == infinite loop
		  break;
	       case '1': case '2': case '3': case '4': case '5': case '6': 
	       case '7': case '8': case '9':
		  // TODO: .*\1 --> DOTSTAR REF, in post?
		  n = *p - '0';
		  if(!tagLog[n]) // !!!would be nice to know if tag is open
		     BADPAT(dfa,"regExpCompile: Reference not in scope");
		  STORE(REF); STORE(n);
		  *dfa |= DFAF_HAS_REFS;	// DFA flag: has REFs
		  break;
	       case 's': STORE(SPACE);	 break;
	       case 'S': STORE(N_SPACE); break;
	       case 'w': STORE(ALPHA);	 break;
	       case 'W': STORE(N_ALPHA); break;
	       case 'd': STORE(DIGIT);	 break;
	       case 'D': STORE(N_DIGIT); break;
	    #if 0
	       case 'x':	    // \xdd  two hex digits
		  n = 0;
		  if(*++p){	// h\0 or hh or abc
		     char *ptr, buf[5] = { p[0], p[1], 0 };
		     p++;
		     n = strtol(buf,&ptr,16);	// don't parse \x123
		     if(n && 2!=(ptr - buf)) n = 0;	// \xa\0, \x1x
		  }
		  if(!n) BADPAT(dfa,"regExpCompile: \\xHH, \\x00 is bad");

		  mp = storeCHR(n,mp,str);
		  break;
            #endif
	    #ifdef EXTEND
	       case 'b': mp = storeCHR('\b',mp,str); break;
	       case 'e': mp = storeCHR('\e',mp,str); break;
	       case 'f': mp = storeCHR('\f',mp,str); break;
	       case 'n': mp = storeCHR('\n',mp,str); break;
	       case 'r': mp = storeCHR('\r',mp,str); break;
	       case 't': mp = storeCHR('\t',mp,str); break;
	    #endif
	       default:  mp = storeCHR(*p,  mp,str); break;  // \a
	    } // \ switch
	    seed = 1;
	    break;
	 default:
	    mp = storeCHR(*p,mp,str);   // an ordinary character
	    seed = 1;
	    break;
      }// switch

      // see if we can convert a sequence of CHRs to a STR
      if(*sp==CHR){
      	 if(*lp!=CHR){  // CHR a !CHR, ASSUMES *lp is valid, ie wrote op
	    lp = chr2str(lp,mp, str, 1, tagi,tagstk);
	    mp = str->mp;
	 }else	// if CHR a CHR b ... CHR z > 1 byte, split up
	    if(*lp==CHR && str->sz >= 250){  // chop long strings to Byte sized
	       // !!add additional check to pack if close to endDFA
	       /*        CHR a CHR b CHR c __  split at >=3, p=="defghi..."
	        * str.mp/         lp/   mp/
	        *        STR 3 a b c __
	        *     lp/         mp/
	        */
	       lp = chr2str(lp,mp, str, 0, tagi,tagstk);
	       mp = str->mp;
	    }
      }// CHR to STR

      sp = lp;		// start of previous state/op

      if(mp > cscape->endDFA) BADPAT(dfa,"regExpCompile: Expression too long)");

      if(justLooking){
	 if(pork == 2) seed = 0;	// in a OR clause
	 // we have seed whatever b will be? Not if a*b* p=='b' or in OR
	 if(seed && !pork && !strchr("*+?{",p[1])) break;
      }
   }// for

   /* "abc" --> STR(3)abc
    * if justLooking, likely just part way through string (hit seed), don't
    * compress as that can move STAKE: colou?r   lookahead starts post ? (at
    * "r"), colou?r --> colour
    */
   if(!justLooking && *sp==CHR){
      chr2str(mp,mp, str, 0, 0,tagstk);
      mp = str->mp;
   }

   cscape->tagi = tagi;
   cscape->mp   = mp;

   if(justLooking && pork == 2) STORE(EDON); // a*|bcd: terminate tagless Node
   *mp = END;	// because I'll be calling dfaScanForward/dfaDump

   return 0;
}

    // Store one CHR that may convert to STRing
static UChar *storeCHR(char c, Byte *mp, Str *str){
   // save consecutive CHRs so I can compress them into a STR
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
   /* Doing a post process with dfaScanForward and CHR counting would be
    * simpler (much less convoluted) but I'd also have to "garbage" collect
    * in the [unlikely] event I ran into the end of allocated DFA space.
    * Here, converting on the fly, I still have the same issue, but it is
    * much closer to the limit, so I'm pretending not to care.
    */
static Byte *chr2str(
   UChar *lp, Byte *mp, Str *str, int moveOp, int tagi, Tag *tagstk)
{
   int   sz = str->sz;	// strlen - \0
   Byte *dp = str->mp, *chr = dp;	// first CHR

   if(sz < 4){	// Too small to bother with. Min 3 (useful for testing)
      str->sz = 0;	// reset for next string
      str->mp = mp;
      return lp;	// no-op
   }

   #if ANDTHENULL
     str->string[sz++] = '\0';	// include \0 in size
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

     /* Adjust tag stack: abc|123|xyz
      *	  CHR a CHR b CHR c OR 0 0 CHR 1 CHR 2  lp --> OR  mp --> CHR 1
      *	  --> <pack> --> STR abc OR 1 ..
      *   ie pack moves OR, which will be updated at next OR
      *	  --> STR abc OR 0 _1_ STR 123 OR 1 .. (updated previous OR)
      *	Ditto for botAddr as post pack, it may be used to insert NODE or CLO:
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

   /* Look for prefix character(s). This is for RE_SEARCH: we can memchr()
    *   to start points rather than character at a time. memchr has magic
    *   (muti character CPU instuctions) to give it wings.
    * Collect all prefix characters into a SET.
    * Examine the DFA only until a "character" is seen.
    * Examples:
    *   -"abc": Prefix of [a]
    *   -"RE starts with string": Use strstr instead of memchr
    *   -(a|ab|abc) --> a(|b|bc): Prefix of [a]
    *   -(a|b|c+): Prefix of [abc]
    *   -\d, [0-9] : 10 prefix characters: [0123456789]
    *   -(a*) --> [a.] == [] : "a" does not need to be in the search text
    *     which means it can not be an anchor.
    *   -a*b --> [ab] : "a" may or may not be in search text but "b" has
    *     to be. So "a" can be a maybe anchor in conjuction with "b".
    *    In general, closures have to be "anchored" to be a prefix.
    * Or: we are looking for suffixes: In a*b, can a* PACMAN?
    *   Pretty much the same, a suffix is the following prefix.
    *   Diff: CLO is not a prefix (a* --> []) but can be part of a suffix:
    *   Note: a*b+ is split at * == (a)(b+) when looking for pre/suffix
    *   -a*(b*|c)d -a-> [a] [bcd], ie a* stops at b, c or d and if a is none
    *     those, PACMAN: consume "a" while looking at "a" knowing that the
    *     stopping point is the only valid starting point for (b*|c)d.
    *   -(a*)*[bc]   (a*)+[bc] (a+)*[bc]  --> [a][bc] (twice)
    *   -!!! (a*){2,}[bc] --> [a][bc] BUT does NOT match aaab
    *     PCRE --> [0,4] ""
    *   Have to keep looking until the first "solid character" after
    *     consecutive CLOs.
    *   EOL==END
    * Input:
    *   bitTable: zero'd set that will be filled in
    *   suffix: 0 (no CLO), 1 (looking for PREFIX), 
    *           2+ (looking for prefix or suffix in a*b)
    * Returns: 0: no prefix
    */
static int _hooverPrefix(
   Byte *dfa, Byte bitTable[BITBLK], int suffix, 
   int *str, int *end, int *clod)
{
   int  n, z = 0, clo = 0, cloSz;

   if(clod) *clod = 0;

doitagain:
   for(; *dfa==EOT; dfa += BOTSZ) {}	// for packRat(): ))))))b
   for(; ; dfa++)
     if(*dfa==BOT)	 dfa++;		// (((((a
     else if(*dfa!=PACMAN) break;
   if(*dfa==END || *dfa==EOL){
      if(clo){
      	 if(suffix==1) return 0;	// closure not terminated by anchor
	 return z;
      }
      if(end) *end = 1;
      return 0;
   }
   clo = 0; cloSz = ANYSKIP;

premore:
   switch(*dfa){
      default: z = clo = 0; break;	// eg a*b*. or (a*)(b*)\1
      //case REF:	// this is the same as . : (a*)\1 --> [a][a]
      //case END: case EOL: covered above
      case CHR: chset(bitTable,dfa[1]);	       z = 1; cloSz = CHRSKIP; break;
      case STR: chset(bitTable,dfa[2]); *str = z = 1; cloSz = dfa[1];  break;
      case DIGIT:
	 #if MAXCHR==256
	 bitTable[6] = 0xff; bitTable[7] |= 0x3;	// MAXCHR==256
	 #else
	 for(n = 0; n<10; n++) chset(bitTable, n + '0');
	 #endif
	 z = 10;
	 break;
      case N_DIGIT:
	 for(z = 0; z < MAXCHR; z++) if(!isdigit(z))     CHSET(bitTable,z);
	 break;
      case SET:
	 dfa++;
	 for(z = 0; z < MAXCHR; z++) if(ISINSET(dfa,z))  CHSET(bitTable,z);
	 cloSz = SETSKIP;
	 break;
      case NSET:	    
	 dfa++;
	 for(z = 0; z < MAXCHR; z++) if(!ISINSET(dfa,z)) CHSET(bitTable,z);
	 cloSz = SETSKIP;
	 break;
      case SPACE:	// space, '\f', '\n', '\r', '\t', vertical tab
	 for(z = 0; z < MAXCHR; z++) if(isspace(z))	 CHSET(bitTable,z);
	 break;
      case N_SPACE:
	 for(z = 0; z < MAXCHR; z++) if(!isspace(z))	 CHSET(bitTable,z);
	 break;
      case ALPHA:   //case BOW: ???
	 for(z = 0; z < MAXCHR; z++) if(IS_WORD(z))	 CHSET(bitTable,z);
	 break;
      case N_ALPHA: //case EOW: ???
	 for(z = 0; z < MAXCHR; z++) if(!IS_WORD(z))	 CHSET(bitTable,z);
	 break;
      case CLO: case ONE:	// CLO, ONE, {0,} can *not* be PREFIX
	 //   except: (a*b)+c --> PREFIX is [ab] 
	 // but they *can* *sometimes* be suffixes
	 //   a+b? --> [a] [b$]   a+b?c -a-> [a] [bc] -b-> [b] [c]
	 //   a+(c*d) -a-> [a] [cd] -c-> [c] [d]
	 //   a*b?c* -a-> [a] [bc]  -b-> [b] [c]  -*-> [c] [$]
	 //   (a*)(b{0,4}|c)X  -*-> [a][Xbc]  -{}-> [b][Xc]
	 if(suffix) clo = 1;
	 else	    break;
	 //fallthrough
      case CLOP:
      clipClop:
	 if(dfa[1]!=BOT){ dfa++; goto premore; }	// a*b*
	 cloSz = dfa[3]; dfa += CLOHEADERSZ;
	 if(*dfa!=NODE){		// a*(b)*a, a*(?:b)*a, a*(b)*(c)*a
	    z = _hooverPrefix(dfa,bitTable,suffix,str,end,&n); // <= COSMO_TAGS
	    clo |= n;	// if a closure was seen, more info needed
	    break;
	 }
	 goto knowNode;				// a*(b|c)*
      case CLOMN:
	 if(dfa[1]==0){		// M==0 == CLO
	    if(suffix) clo = 1;
	    else       break;
	 }
	 dfa += 2;		// where CLO would be
	 goto clipClop;
      case NODE:
      knowNode:
      {
	 Byte *nextOR = dfaScanForward(++dfa,AORB,1,0);
	 int   clod;
	 z = 1;
	 if((*dfa == BOT && dfa[2] == AORB) ||		// (|b) 
	     *dfa == AORB){ z = 0; goto done; }		// (?:|b)
	 while(1){
	    n = _hooverPrefix(dfa,bitTable,suffix,str,end,&clod);
	    if(!n){ z = 0; goto done; }	// this clause no prefix --> OR has none
	    clo |= clod;
	    if(!nextOR) break;
	    dfa      = nextOR;
	    if(dfa[ORSZ] == AORB){ z = 0; goto done; }  // (a||b)
	    nextOR   = dfa[2] ? dfaScanForward(dfa + ORSZ,AORB,1,0) : 0;
	    dfa     += ORSZ;		// op after OR
	    if((*dfa == EOT && dfa[2] == EDON) ||	// (a|)
	        *dfa == EDON){ z = 0; goto done; }	// (?:a|)
	 }//while
	 if(clo){ dfa = dfaScanForward(dfa,EDON,1,0); cloSz = 1; }
      done: ;
      }
         *str = 0;
         break;
      case AORB: case EDON: if(clod) *clod = 1;	break;	// if clo
   }//switch

   if(clo){ dfa += cloSz; goto doitagain; }
   return z;
}

#define MAX_PREFIXES 15		// want (\d+|\(\d)+)
static Byte *hooverPrefix(Byte *dfa, Byte *mp){
   Byte prefixes[MAX_PREFIXES + 1], pset[BITBLK] = { 0 };
   int  c,n,sz, str = 0;

   if(dfa[1]==BOL) return mp;	// ^a == anchored so no prefix
   
   if(_hooverPrefix(++dfa,pset,1,&str,0,0)){
      for(c = 0, n = 0; c < MAXCHR && n <= MAX_PREFIXES; c++)
         if(ISINSET(pset,c)) prefixes[n++] = c;  // overfill for next test
      if(n<=MAX_PREFIXES){
	 if(n==1 && str) prefixes[0] = 0;
	 sz = 3 + n;	// PREFIX n <n chrs> 0
	 memmove(dfa + sz, dfa, (mp - dfa));
	 dfa[0] = PREFIX; dfa[1] = n; memcpy(dfa + 2, prefixes, n);
	 dfa[sz - 1] = 0;
	 mp += sz;
      }
   }
   return mp;
}

   /* Find prefixes and suffixes for op that *will* be (but isn't currently)
    * closed:  (dfa[sp or tp->botAddr .. mp]).
    * ie in a*: sp --> CHR a, (a)*: botAddr --> BOT 1 CHR a EOT 1 <-- sp
    * (?:a*b)* botAddr --> [PACMAN] CLO a END b END EOT 99 <-- sp/mp (fake op)
    * with no END <-- doesn't matter as _hooverPrefix() only looks at the
    * first op of "consequence", ie won't see phantom EOT ("()" invalid).
    *
    * Is closure multiple choice or just a consume?
    * Returns: 0 (need to fork), CLO_PACMAN (no fork needed, just consume)
    * 
    * Given A*B, we are looking to see if A & B are disjoint. If they are,
    *   we know the greedest A* is the winner (as A can not match B). The
    *   problem is how to determine A & B. Since we are looking at A*, that
    *   has been compiled so we can look at the DFA (for A). For B, compile
    *   ahead, just far enough to know what we need to know about B.
    * I'm looking at the simplist test of disjointness: the first
    *   "character" of A & B. Eg, (aa) and (ab) are disjoint but I don't see
    *   it.
    * Within a closure, a closure (*,?,{0,}, not +, {n,} is match anything
    *   because of the zero case. In (a+b)+c there will always be a test for
    *   a but not so in (a*b)+c. The former has one choice: consume a's (to
    *   b, == PACMAN), the latter, many: (!*\w)+c match abcd
    * "Normally", the prefix is the "thing" (eg a* --> [a], (abc)* --> [a]).
    *   If looking for a prefix in something that is a closure (eg (a*b)*c
    *   at *2), have to keep looking to find a "non-zero" thing: --> [ab]
    *   stopping at *2: (a*b*)*c has the same prefix.
    */ 
static int packRat(
   UChar *p, Byte *sp, Tag *tp, Byte *b, CompileState *cscape, char **error)
{
   Byte aset[BITBLK] = { 0 }, *d, *mp = cscape->mp;
   int n, pacman = 0, op = *sp, str = 0, end = 0;

   *b = 0; *error = 0; 
   if(op==ANY) goto suffix;	// check for .*b, .?b, .{}   . has no prefix

   // Since dfa is not zero'd, it is garbage after mp, so put an END to it.
   // Or previous look aheads may have left droppings
   if(*mp==EOT) mp += 2;	// fake EOT (EOT 99)?
   *mp = END;

   if(op==EOT || op==EDON)
	n = _hooverPrefix(tp->botAddr, aset,2,&str,0,0);  // (a)*, (a*b)*c
   else n = _hooverPrefix(sp,	       aset,0,&str,0,0);  //  a*
   if(n){	// found a, now find b
      DEBUGCODE( printf("ASET ["); for(n = 0; n < MAXCHR; n++) if(ISINSET(aset,n)) printf("%c",n); printf("]\n"); )

   suffix: ;
      Byte  bset[BITBLK] = { 0 }, 
           *tagLogSP, tagLog[RE_MAX_TAG];  // likely to be mangled
      Byte *stake;
      int   _,c;
      char *s;
      Str   str, *strSP;
      Tag   tagstk[COSMO_TAGS], *tagstkSP;

      // save copies of data that will be munged
      memcpy(tagLog,cscape->tagLog,RE_MAX_TAG);
         tagLogSP = cscape->tagLog; cscape->tagLog = tagLog;
      memcpy(tagstk,cscape->tagstk,sizeof(tagstk));
         tagstkSP = cscape->tagstk; cscape->tagstk = tagstk;
      memcpy(&str,cscape->str,sizeof(Str));
         strSP    = cscape->str;    cscape->str    = &str;
	 str.sz   = 0;	// we are now out of sync with chr2str(), just reset

//      *(stake = cscape->stake = mp++) = STAKE; cscape->mp = mp;
      stake = cscape->stake = mp; STORE(STAKE); cscape->mp = mp;
      *mp = END;	// in case somebody looks
      // compile ahead so I can find b. Don't compile leading * == a
      s = compile(p + 1,cscape,1);

      // restore data pointers to original
      cscape->tagLog = tagLogSP;
      cscape->tagstk = tagstkSP;
      cscape->str    = strSP;

      if(s){ *error = s; return 0; }

      /* Compile stops at text or, if in tag, compiles the tag (in case
       *   it is a NODE) and repeat.
       * If STAKE is in a NODE, skip to EDON and then look for suffix in
       *   the next thing. If STAKE AORB .. EDON (no opening NODE)
       *   or STAKE .. EDON
       * If STAKE in front of a NODE, look in NODE for suffix: STAKE NODE
       */
      stake++;
      if(*stake!=NODE && (d = dfaScanForward(stake,EDON,1,0)) ){
	 Byte *e;	// (a+|(b+|c)): at b : two dangling EDONs after STAKE
	 for(d++; (e = dfaScanForward(d,EDON,1,0)); d = e + 1) {}
	 stake = d;
      }
      n = _hooverPrefix(stake,bset,3,&_,&end,0);
      if(end) return CLO_PACMAN;	// a*, a*$, a*b
      if(n){
	 DEBUGCODE( printf("  BSET ["); for(n = 0; n < MAXCHR; n++) if(ISINSET(bset,n)) printf("%c",n); printf("]\n"); )

	 if(op==ANY){	    // .*b check:
	    for(c = n = 0; c < MAXCHR && n <= MAX_PREFIXES; c++)
	       if(ISINSET(bset,c)){ *b = c; n++; }		// DOTSTAB
	       //  if(ISINSET(bset,c)) prefixes[n++] = c;	// DOTSTAX
	    if(n>1) *b = 0;
	    return 0;	// leave post STAKE code. Or stake[1] = '\0';
	 }

	 for(pacman = CLO_PACMAN, n = 0; n < BITBLK; n++)
	    if(aset[n] & bset[n]){ pacman = 0; break; } // same chr in both
      }
      *stake = END;	// 'cause I don't want to see the tailings
   }
   return pacman;
}

//////////////////////////////////////////////////////////////////////////
////////////////////////// Run the DFA ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////


typedef struct    // State for OR/Node morphed into packet of state info
   { Byte *dfa, *moreOR; Byte /*edon,*/ badDFA, noHolds, theEnd, dup; } Stator;

   // If I were to malloc the Fibers vector, each Fiber would only 
   // have the actual tags actually used (vs RE_MAX_TAG)
typedef struct Fiber{	// a "green" thread
   Byte    *dfa;
   UChar   *lp; 
   char    *bopat[2*RE_MAX_TAG], **eopat; // same layout as call to regExpMatch()
   struct Fiber *prev, *next;	// doubly linked list
   #if PAYLOAD
   Byte *payloadA, *payloadB;	//!!! if this goes, make these 16 bit offsets
   #endif
   unsigned gid:32;		// group ID for ?+*
   unsigned id:16;		// id for debug, doesn't expand struct
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
   ReErrorInfo *epac;
}MotherShip;		// 7,464 bytes 64 bit pointers & 20 Fibers

static UChar *pmatch(MotherShip *, Byte *dfa, UChar *lp, 
		     char *bopat[], char *eopat[], Stator *);
static int    forkk( MotherShip *, Byte *dfa, UChar *lp, char *bopat[], Stator *, unsigned, Byte *, Byte *);
static void   initMotherShip(MotherShip *, UChar *bol, char *bopat[]);
static int    pullThread(MotherShip *);

#if DFA_DEBUG
    static Byte *_dfaaddr0;   // for debug: dfa - _dfaaddr0 --> address in DFA
    #define DFA_ADDR(_,dfa) (dfa - _dfaaddr0)
#endif

/* regExpMatch: Run dfa to find a match, either static or search.
 *
 * Special cases for start of DFA (some of):
 *  END
 *	regExpCompile() failed, poor luser did not check for it. Fail fast.
 *  CHR (RE_SEARCH)
 *	Locate the character without calling pmatch(), and if found, call
 *	  pmatch(). Because memchr/strchr/strstr/memmem are *much* faster
 *	  then pmatch. 
 *	If a match is not found, memchr to next character and repeat. Thus
 *	stepping through text in chunks vs character by character. Unless
 *	"a!" search "aaaaaaaaaaaaaaaa!" then ?slower? than pmatch.
 *  STR (RE_SEARCH)
 *	Similar to CHR but with STRs.
 *	strstr/memmem to start of string in text. If pmatch fails, move to
 *	  (start point)[1] (not end of start as overlaps are a thing),
 *	  repeat.
 *	aa\d search aaa2
 *  DIGIT (RE_SEARCH): "\d", "\d+", "(\d)", "(\d+)", (a|b|c), etc
 *	Similar to CHR but with 10 potential start points and too much
 *	state to maintain.
 *	Build list of start points.
 *	Sort left most first: 
 *	  pmatch(start point), next start point, repeat.
 *	This can be a huge win - 32x for (\d\d\d)\)\s+(\d{3}-\d{4})
 *	  18x for (\d{3}-|\(\d{3}\)\s+)(\d{3}-\d{4})
 *	  searching this file.
 *	Need to investigate multi-character search routines.
 *
 * If a match is found, bopat[0] and eopat[0] are set to the beginning and
 *   the end of the matched fragment, respectively.
 *
 * Input:
 *   dfa:   DFA returned by regExpCompile()
 *   text:  String to match
 *   flags: See zkRE.h
 *      RE_MID : If text points into the middle of a bigger text,
 *          ie ^ is not text[0]
 *	  If set, text[-1] MUST be at valid! A couple of OPs will look
 *	    there if they can.
 *      RE_SEARCH : Move start forward on each fail trying to find a match.
 *   tags:  char *tags[2 * RE_MAX_TAG] or 0, these are the "(" ptrs into text
 *      if tags[0..RE_MAX_TAG - 1] != 0 then
 *        tags[n]-->start of match, tags[RE_MAX_TAG + n]-->end of match
 *      If tags==0, they are ignored and the match/search can be faster.
 *      tags are zero'd.
 *   ReErrorInfo: Filled in if match failed badly. Can == 0.
 * Returns:
 *   0: Fail, ReErrorInfo may have been set (it was cleared)
 *   1: Match, tags set
 */
int regExpMatch(Byte *dfa, char *text, char *tags[],
	      unsigned int flags, ReErrorInfo *epac)
{
   #define REX_FAIL	0
   #define REX_MATCHED	1

   Byte   *afa, *pstr = 0;
   UChar  *lp = (UChar *)text, *ep = 0, *bol = lp;	// for pmatch
   char  **bopat, **eopat, *fakeTags[2 * RE_MAX_TAG];
   int	   move   = (flags & RE_SEARCH);
   int	   notags = 0, dfaFlags, zero = 0;
   char   *prefix_lps[MAX_PREFIXES] = { 0 };
   Stator  stator;		// OR state for moving around the DFA
   MotherShip m;

   #if !HOME_BREW_CTYPE_H	// also done in regExpCompile()
   if(!wordTableDefined){	// not thread safe
      wordTableDefined = 1;
      for(int n = 0; n <= MAXCHR; n++) if(IS_ALPHA(n)) CHSET(wordTable,n);
   }
   #endif //HOME_BREW_CTYPE_H

   if(epac) memset(epac,0,sizeof(ReErrorInfo));

   if(!dfa) return REX_FAIL;

   DEBUGCODE( _dfaaddr0 = dfa;  )
   dfaFlags = *dfa++;

   if(*dfa == END) return REX_FAIL;	// munged automaton. Never matches
   if(*dfa==BOL){	// anchored: match from BOL only
      if(flags & RE_MID) return REX_FAIL;	// text[0] not start of line
      move = 0;		// anchored, no movement allowed
      dfa++;		// do this check only once
   }

   if(!tags){ tags = fakeTags; notags = 1; }
   bopat = tags; eopat = &tags[RE_MAX_TAG];

tiptop:		// start over, as in doing a search
   afa = dfa;

   initMotherShip(&m,bol,bopat);
   m.notags  = notags;
   m.hasRefs = dfaFlags & DFAF_HAS_REFS;
   m.epac    = epac;

   memset(bopat,0,2*RE_MAX_TAG*sizeof(char *));	// wipe all tags

   bopat[0] = (char *)lp;	// need state for Fibers, prefix matching
   memset(&stator, 0, sizeof(Stator));

   // Restart point after look ahead, a clean match
   // See if I can help things along.
   if(*dfa==PREFIX){  // optimizations for RE_SEARCH
      int   sz       = dfa[1];
      Byte *prefixes = dfa + 2;
      
      afa = dfa + sz + 3;

      if(!move) goto onWithIt;	// !RE_SEARCH == no prefixing, gotta inc afa!
      if(pstr || *prefixes==0){	// STR
	 if(zero==0){
	    pstr = dfaScanForward(afa,STR,1,0);
            zero = 1;
	 }else lp = (UChar *)prefix_lps[0];

	 #if ANDTHENULL	// orginally because Windows does not have memmem
	    lp = (UChar *)strstr((char *)lp, (char *)(pstr + 2));
	 #else
	    // need an end pointer
	    lp = memmem(lp,strlen((char *)lp), pstr + 2, pstr[1]);
	 #endif
	 if(!lp) return REX_FAIL;

	 bopat[0]      = (char *)lp;
	 prefix_lps[0] = (char *)(lp + 1);	// STR can overlap

	 if(afa==pstr){		// skip over STR, rather not repeat it
	    int n = afa[1];
	    #if ANDTHENULL
	       lp += n - 1;
	    #else
	       lp += n;
	    #endif
	    afa += n + 2;
	 }
      }else{	// serialize prefix points
	 int   n, c;
	 char *ptr, **qtr;

	 if(zero==0){	// initialize to first start points, once
	    for(n = 0, c = 0; c < sz; c++)	// build sparse vector of start points
	       // memchr would be better, need end ptr to calc lengths
	       if( (ptr = strchr((char *)lp, prefixes[c])) )
		  prefix_lps[n++] = ptr;
	    if(!n) return REX_FAIL;
	    zero = n;		// size of sparse array of start points
	 }
	 // find left most start point
	 for(ptr = 0, qtr = prefix_lps, n = 0; n<zero; n++,qtr++)
	    if(*qtr && (!ptr || (*qtr < ptr))){ ptr = *qtr; c = n; }
	 if(ptr){
	    lp		  = (UChar *)ptr;  // left most remaining start point
	    bopat[0]	  = ptr;
	    prefix_lps[c] = strchr(ptr + 1, *ptr); // better: collapse null entries
	 }else return REX_FAIL;

	 // now match at start point
      }
   }
onWithIt:

   DEBUGCODE( if(move) printf("Starting search @ %s\n",lp) );
   ep = pmatch(&m,afa,lp,bopat,eopat,&stator);  // match or queue Fibers
   #if 0 	// or: (and next #if 0 goes away) ever so slightly slower?!?
      forkk(&m,afa,lp,bopat,&stator,0,0);
      pullThread(&m); ep = m.ep;
   #endif
   #if 0 // if Fibers *could* be queued, they will be and pmatch() won't match
      if(ep){
	 if(m.forked && (m.sz && !m.biggusMatchus)){  // unresolved business
	    // Might not be *the* match if there are Fibers to run
	    // Unless there is a better match, this one wins
	    m.ep = ep; pullThread(&m); ep = m.ep;
	 }
      }
   #endif
   if(!ep){	// no match or Fibers queued
      if(stator.badDFA || stator.noHolds || m.errorCode){
      fail:
	 return REX_FAIL;
      }

      if(m.forked){ // there *may* be Fibers to be run (unless I queue first thing)
	 if(m.sz && !m.biggusMatchus && (2==pullThread(&m)))
	    goto fail;		// something bad happened
	 // if biggusMatchus, there will be live Fibers
	 if( (ep = m.ep) ) goto success;  // ep was 0, Fiber may have matched
      }

      // RE_SEARCH match failed, move to next character and try again
      if(move && (zero || (*lp && *++lp))){  // still more text to search?
         // if BOL, test at top has turned off move
         //if(*dfa==BOL) return REX_FAIL;  // can't be at BoL anymore
	 #if DO_HOLDS
	 // If we get here all HOLDS have passed so we don't so them agan
	 // The first ops in a DFA: BOL HOLDS or HOLDS, but see comment above
	 // Yeah, but those HOLDS may be behind us and that matters
	 //while(*dfa==HOLDS) dfa += 2;
	 #endif	// DO_HOLDS

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

   eopat[0] = (char *)ep;  // entire matched, bopat[0] set up there pre-match

   return REX_MATCHED;
}

    // Not void so caller can "return _regExpFail();"
static UChar *_regExpFail(
   MotherShip *m,char *msg, int errorCode, Stator *stator)
{
   stator->badDFA = 1;		// general signal fatal error has occured
   m->errorCode   = errorCode;
   m->ep	  = 0;
   if(m->epac){
      m->epac->errorCode = errorCode;
      m->epac->errorMsg  = msg;
   }

   return 0;		// longjmp() would be nice here
}


/***************************************************************************
 *            Cooperative threads for "back tracking"			   *
 ***************************************************************************/

static void initMotherShip(MotherShip *m, UChar *bol, char *bopat[]){
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
     *   -If dead lock: malloc() another block of Fibers. The compiler could
     *     tell me how many would be needed (at any one time).
     * GCC: fork is built-in function, can't use that name
     */
static int forkk(
   MotherShip *m, Byte *dfa, UChar  *lp, char *bopat[], 
   Stator *stator, unsigned gid,   Byte *payloadA, Byte *payloadB)
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
      { DEBUGCODE( printf("GID %d has already won\n",gid); ) return 1; }

   /* If dfa & lp is the same as an existing Fiber, this is a duplicate and
    *   can be ignored.
    * Well, knock me over with a feather, this happens quite a bit
    *   and makes (?:a?)^na^n eg n==3 "a?a?a?aaa" fast ie no longer O(2^n)
    * However, as Russ notes, this messes with Refs as (..)*.*\1 forks a
    *    bunch at the same place with different values for \1.
    *    There are cases of same dfa, lp & tags so can still ignore those.
    * Hint taken from:
    *   Russ Cox: "Regular Expression Matching: the Virtual Machine Approach"
    *   https://swtch.com/~rsc/regexp/regexp2.html
    */
   for(f = m->first; f; f = f->next)
      if(dfa == f->dfa && lp == f->lp)
	 //!!! would really like to only compare tags in play
	 if(!m->hasRefs || !memcmp(bopat,f->bopat,sizeof(char *)*RE_MAX_TAG)){
	    DEBUGCODE( printf("forkk(): DUP  %ld:%s\n",DFA_ADDR(m,dfa),lp); )
	    stator->dup = 1;	// doNodeGlider() needs to know
	    return 0;
	 }

   if(m->sz == MAX_FIBERS){	// out of resources!
      // GC: run fibers hoping some die
      // This can be recursive: pullThread() causes fork(), repeat
      //   and dead lock if all Fibers are trying to fork()
      DEBUGCODE( printf("forkk(): GC\n"); )
      if(1!=pullThread(m))	// run until can't run no more
	 // a match was found, don't need no steeking resources
	 // or DFA corruption, in either case, stop what you are doing
	 return 1;	// but check m->errorCode
      if(m->sz == MAX_FIBERS){
	 _regExpFail(m,"regExpMatch(): forkk(): Dead lock",RE_ERROR_DEAD_LOCK,stator);
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
   f->gid = gid; f->isor = 0; //isor;

   f->eopat = &f->bopat[RE_MAX_TAG];
   memcpy(f->bopat,bopat,2*RE_MAX_TAG*sizeof(char *));

   #if PAYLOAD
   if(payloadA){ f->payloadA = payloadA; f->payloadB = payloadB; }
   #endif

   DEBUGCODE( printf("Total fibers: %d,%d %ld>%-.40s\n",m->sz,f->id,DFA_ADDR(m,dfa),lp); )

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

static UChar *doNode(MotherShip *, Byte *, UChar *, char *bopat[], Stator *, int);//!!!!!

    /* Move from single threaded pmatch() (recursive desent / depth first
     *   search) to "parallel" pmatch()s (breadth first search, more NFA
     *   like behavior). Minimal recursion, *no* back tracking.
     *   DFA execution is forward only, a thread does not run an op more
     *   than once (athough other threads can run that same op). Semantics.
     *   If I understand Russ's VM paper correctly, the fibers array is 
     *   ThreadList (clist & nlist) in the Thompson & Pike VMs. Fixed
     *   because I don't want to allocate. The/A big difference is I don't
     *   run in lock step, which leads to more fibers used (ie clo count !=
     *   max fibers).
     * Each fiber represents a fork in the search path (OR, ?, *, +, {}).
     * I *think* this the point where the DFA moves to NFA and I'm doing the
     *   equivalent a lazy conversion of the NFA to DFA.
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
     *	      during pmatch(). f->next, f->prev can change.
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
     *   2: DFA corrupt. _regExpFail() was called, 
     *      MotherShip notified, m.ep == 0
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
	 ep = pmatch(m,f->dfa,f->lp,f->bopat,f->eopat,&stator);
	 f->running = 0;
	 if(m->biggusMatchus) return 0;	// gotta love recursion
	 next = f->next;  // fork() appends to list, ie next may have changed
	 if(ep){	// fiber is succeeding
	    if(stator.theEnd){	// a match was found, our job is done
            #if PAYLOAD
	       if(f->payloadA){
		  if(*ep){	// infinite recursion: (aa|a)* match "a"*10, doesn't prune
		     char *_bopat[2*RE_MAX_TAG];
		     if(m->sz==MAX_FIBERS) return 1; // infinite recursion
		     memcpy(_bopat,f->bopat,2*RE_MAX_TAG*sizeof(char *));
		     doNode(m,f->payloadA,ep,_bopat,&stator,1);  // this is very bad recursion
		  }
		  f->dfa = f->payloadB; f->payloadA = f->payloadB = 0;
		  f->lp  = ep;
		  goto nextf; 
	       }
	    #endif
	       if(f->gid) m->winningGID = f->gid;
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
     *   "(dog|dogs)" match "dogs" --> "dogs" or "dog" PCRE?
     *   "(a|ab)(bc|c)" match "abc" --> ("ab","c") or ("a","bc")
     *   PCRE, JavaScript: first
     *   Eighth Edition Unix library: leftmost longest
     *   POSIX: uggh
     *   Me: like *: longest wins, priorty eagar
     *     Maybe. In (a.+)|a(b)c && a(b)c|(a.+) match abc, a(b)c always wins
     *     because + forks and dies, moving a(b)c to the front of the queue.
     *   a|b|c: I *would* like to fork b & c and continue with a but if GC
     *     happens before I can continue, b or c can win, pre-empting a (a,
     *     b & c total winners). Also I *MIGHT NOT* be a Fiber (ie initial
     *     call to pmatch()), which means I can stomp on the MotherShip
     *     after Fibers have set state.
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
     * Note: (a|b)*c: If I put two dfa pointers (to NODE & c) in Fiber as a
     *   payload, then, if/when the Fiber succeeds (hits BOT END), process
     *   NODE (again), it forks Fibers with payloads, then continue at c. Do
     *   this for all legs of OR. However, this really falls on its face as
     *   it serializes match, I can't detect duplicates and devolves into
     *   recursive descent. Run out of Fibers --> death loop of GC/fork
     *   (aa|a)* match "a"*7 dead locks whereas nodeGlider can do 1000+ fast
     *     (by not doing anthing as it is mostly dups, looks linear).
     *   THIS ONLY works if longest match wins as it is eagar.
     * 
     * Ponder: PACMAN here would mean that all clause prefixes are disjoint
     * and the union is disjoint from the next suffix (eg (a|b+)c), 
     * child closures are PACMAN and non-zero (+, {1,}). If that is the
     * case, which ever clause advances is the winning clause.
     * No forking necessary.
     */

#define OR_LONGEST_WINS	1	// or first: 0?

static UChar *doNode(	// dfa[-1] == NODE
   MotherShip *m, Byte *dfa, UChar *lp,
   char       *bopat[], Stator *_stator, int payload)
{
   int      nodeTag;
   int      moreOR = 1; // a Node has at least one OR and I haven't seen it yet
   Byte    *nextOR = dfaScanForward(dfa,AORB,1,0);	// (?:|b), dfa --> |
   Byte    *edon   = dfaScanForward(dfa,EDON,1,0);
   unsigned gid    = 0; //, isor = 0;
   Stator   stator;
   #if PAYLOAD
   Byte *plA = (payload ? dfa : 0), *plB = (edon + 2);
   #endif

   #if !OR_LONGEST_WINS
      gid = ++m->gid;	// group this branch as first wins
      //isor = 1;	// treat all ORs as the same group
   #endif

   #if PAYLOAD
      if(payload==2 && forkk(m,plB,lp,bopat,&stator,gid,0,0)) return 0;
   #endif

   DEBUGCODE( printf("doNode: Start at %ld  more OR? %d\n",DFA_ADDR(m,dfa - 1),moreOR); )
   while(1){	// fork() each OR
      DEBUGCODE( printf("doNode: Fork to: %ld: \"%s\"\n",DFA_ADDR(m,dfa),lp); )
      if(forkk(m,dfa,lp,bopat,	// *the* match was found, back out
      #if PAYLOAD
	   &stator,gid, plA,plB)) return 0;
      #else
	   &stator,gid, 0,0)) return 0;
     #endif

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
      if(moreOR) nextOR = dfaScanForward(dfa,AORB,1,0); // (?:a||c) dfa --> | #2
      if(nodeTag != 0) bopat[nodeTag] = (char *)lp;  // not if (?: or 0
   }// while
   return _regExpFail(m,"regExpMatch(): doNode(): bad dfa",RE_ERROR_BAD_DFA,_stator);
}
    /* doNode where pmatch doesn't fork, the compiler says it won't. But we do.
     * For (abc|a.c|a*c)*
     * How is this supposed to work? Soooo many different ways
     *   Record match for all OR clauses
     *   eg: (ab|abc)+  match "abc" --> (0,3) "abc" ambiguous, 
     *           PCRE --> (0,2) "ab" (my longest wins rule)
     *       (ab|abc)+d match "abcd" --> (0,4) "abc", matches PCRE
     *   Need all possible matches
     * Since this is alternation, match order doesn't matter or is left to
     *   right, we can fork as we go.
     * Does *not* handle sub-alternation (nested OR): (a|(b|c)|d)* forks.
     *   It would if nested OP was PACMAN (see doNode()),
     *   I don't think payload would work here.
     * Returns:
     *   >=0 : Count of matches
     *    -1 : error
     */
//typedef struct{ UChar *bopat, *eopat; Byte *dfa; } NG;
static int doNodeGlider(
   MotherShip *m, Byte *dfa, UChar *lp, char *bopat[], UChar **eps, int N,
   Byte *efa, unsigned gid)
{
   int     nodeTag, n = 0;
   int     moreOR = 1;
   Byte   *nextOR = dfaScanForward(dfa,AORB,1,0);
   char  **eopat  = &bopat[RE_MAX_TAG];
   UChar  *ep;
   Stator  stator;

   DEBUGCODE( printf("doNodeGlider: Start at %ld  more OR? %d\n",DFA_ADDR(m,dfa),nextOR!=0); )
   while(1){	// fork() each OR
      DEBUGCODE( printf("doNodeGlider: DFA: %ld: \"%s\"\n",DFA_ADDR(m,dfa),lp); )
      memset(&stator, 0, sizeof(Stator));
      if( (ep = pmatch(m,dfa,lp,bopat,eopat,&stator)) && lp!=ep){
	 if(n==N){
	    _regExpFail(m,"regExpMatch(): doNodeGlider(): cache full",RE_ERROR_EOM,&stator);
	    return 0;
	 }
	 //if(ep!=lp){	// (a||c)*  --> dup but why fork?  Doesn't seem to happen
#if 1
	 if(forkk(m,efa,ep,bopat,&stator,gid,0,0)) return 0;
	 if(!stator.dup) eps[n++] = ep;

#else	// for the other nodeGlider  !!! dead code
	 check for dups (of ep), need cache start and size
         ng->bopat = lp;
         ng->eopat = ep;
         ng->dfa   = dfa;
         ng++; n++;
#endif
      //}
      }

      if(!moreOR){	// no more OR, this node has been processed
	 if(!dfa) break;	// ohh, that is bad
	 return n;
      }

      if(! (dfa = nextOR) ) break;	// there is supposed to be one
      DEBUGCODE( printf("doNodeGlider: Try next OR at: %ld more OR? %d\n",DFA_ADDR(m,dfa),dfa[2]); )
      nodeTag = dfa[1];	// clean up if node fails, 0 if "(?:"
      moreOR  = dfa[2];	// hops, >0 means more OR
      dfa    += 3;	// op after OR
      nextOR  = moreOR ? dfaScanForward(dfa,AORB,1,0) : 0;
      if(nodeTag != 0) bopat[nodeTag] = (char *)lp;  // not if (?:
   }// while
   _regExpFail(m,"regExpMatch(): doNodeGlider(): bad dfa",RE_ERROR_BAD_DFA,&stator);
   return -1;
}

    /* A slighty different op_fork() (look at that first), where both lp and
     * # of characters between fork points are unknown (at compile time).
     * PACMAN --> just consume, only one choice, the longest one.
     * Some ickies here:
     *   I *really* do NOT want to recurse: (.)* would recurse strlen().
     *   I *really* do NOT want to cache match points.
     *   But I also really want (a*b)*. So I have strategies: 
     *     1 char width: a* : op_CLO(): Calculation
     *     Fixed width matches: (a.b)*: gliderGun(): Calculation (this code)
     *     Variable width matches: (a*b)*: widerGlider(): Cache
     *     NOTE: fixed/variable width means pmatch(dfa) does *not* fork.
     *       Examples: (abc), (a.c), (a*c)<--pacman   Not: (.*a)
     *   The "normal" CLO & op_fork() assume the matches are one char, here
     *     the chunk size is unknown (at compile time anyway).
     *     Actually, it is <num match ops> characters: (a.b) == 3, (a*b) == ?
     *   Since this is runtime, have to pmatch() to determine the chuck
     *   size, number of chunks and set tags.
     *     (.(.(.)))* 
     *     pmatch() will overwrite tags with the new set.
     *       fork() will copy them. But that is eager.
     *     compile() enforced that pmatch() can't fork() so I know a
     *       call to pmatch() will return match/no match and match length
     *       is constant.
     *   (a*)* is a double pacman: PACMAN CLO tag PACMAN CLO a
     *     which means neither CLOs fork so gliderGun can do the work. Note
     *     that pmatch might both succeed and NOT advance.
     * 
     * I keep reading about back references can cause exponential time:
     * O(n^(2k)) where
     *   n is the length of the input string.
     *   k is the number of backreferences in the regex
     * with an example of (a*)(b*)\1\2 matching abababab --> [0,4] "a", "b"
     * n^4 time. I think that is the example, DDG AI had it mangled.
     * Matching aaaaaa is a much worse case and starts to really flail over
     * "a"*500,000, thrashing fibers. Backreferences are the problem as
     * (a*)(b*).. is fast (0.001sec vs 0.8sec). Interestingly, "\1." is the
     * same time as "\1\2". Still, I'm seeing quadratic, not higher
     * polynomial time increases. 
     * Since this is a match fail, match time == search time.
     * Backreferences, for me, are constants, same as STR so I don't how
     * they can be a problem. What am I missing? I know it is NP-complete
     * but don't know of an example. I *may* be dodging *a bullet because I
     * only keep one level of tags and, if needed, re-calculate them. Or,
     * more likely, I don't compile the "bad/hard" cases (such as recursive
     * backrefs or (a*)*a).
     * 
     * Uggh: (a*){2,}[bc] match aaab    PCRE --> [0,4] ""
     * I PACMAN "a" so this fails. Do I care? (a*)*[bc] & (a*)+[bc] work
     * 
     * Uggh, storage and order conflicts. Need to queue Fibers greedy first
     * but can only get there eagar first, which sets tags in reverse order.
     * We know the compiler (for gliderGun) does not allow forks or variable
     * length/width matches in the tag we are closing over which means the
     * number of characters matched is constant. So pmatch() to calculate
     * the width (of each match) and to find the end of the closure. Then
     * back up and fork each closure, capturing tags as needed.
     * 
     * (a.c)*d --> CLO BOT 0 10 BOT 1 CHR a ANY CHR c EOT 1 END CHR d
     *			    dfa ^                           efa ^
     * Returns: 
     *   1: You should jump to next op, lp has been updated
     *   0: You are done, branches are forked, fail.
     */
static int gliderGun(MotherShip *ms, Byte *dfa, UChar **_lp, 
     char *_bopat[], Stator *stator, Byte *efa, int tags, int pacman)
{
   char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
   UChar   *lp   = *_lp, *are = lp, *plp, *ep;
   int      step = 0;
   unsigned gid;	// closure group id

   memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));
   // Consume <a> matches: Compiler says pmatch moves or fails. Unless (a*)*
   while(*lp && (ep = pmatch(ms,dfa,lp,bopat,eopat,stator)) && lp!=ep)
      { plp = lp; lp = ep; if(!step) step = ep - are; }  // step overflow?!?
   if(are==lp) return 1;   // a* match "b": no matches, don't fork, carry on
   if(pacman){		// consume, want tags from last [successful] match
      *_lp = lp;	// pacman allows variable width matches
      if(tags){
	 char **_eopat = &_bopat[RE_MAX_TAG];
	 if(*lp) pmatch(ms,dfa,plp,_bopat,_eopat,stator);
	 else memcpy(_bopat,bopat,2*RE_MAX_TAG*sizeof(char *));  // no matches
      }
      return 1; 
   }

   gid = ++ms->gid;	// group this branch so I can prune
   if(step){		// queue matches greedy first
      for(ep = lp; are < ep; ep -= step){  // ep == are + n*step
	 if(tags) pmatch(ms,dfa,ep - step,bopat,eopat,stator);  // set tags
	 if(forkk(ms,efa,ep,bopat,stator,gid,0,0)) return 0;
      }   
   }
   // the zero match case: queue args we were called with
   // this case has not set tags
   forkk(ms,efa,are,_bopat,stator,gid,0,0);
   return 0;
}
static int gliderGunN(MotherShip *ms, Byte *dfa, UChar **_lp, 
     char *_bopat[], Stator *stator,  Byte *efa, int N, int tags, int pacman)
{
   if(N){
      char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
      UChar   *lp = *_lp, *are  = lp, *ep;
      int      step = 0;
      unsigned gid;

      memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));
      while(*lp && N-- && (ep = pmatch(ms,dfa,lp,bopat,eopat,stator)) && lp!=ep)
         { step = ep - lp; lp = ep; }
      if(are==lp) return 1;
      if(pacman){
	 *_lp = lp; 
	 if(tags){
	    char **_eopat = &_bopat[RE_MAX_TAG];
	    if(*lp) pmatch(ms,dfa,lp - step,_bopat,_eopat,stator);
	    else memcpy(_bopat,bopat,2*RE_MAX_TAG*sizeof(char *));
	 }
	 return 1; 
      }

      gid = ++ms->gid;	// group this branch so I can prune
      if(step){		// queue matches greedy first
	 for(ep = lp; are < ep; ep -= step){
	    if(tags) pmatch(ms,dfa,ep - step,bopat,eopat,stator);
	    if(forkk(ms,efa,ep,bopat,stator,gid,0,0)) return 0;
	 }   
      }
      forkk(ms,efa,are,_bopat,stator,gid,0,0);
   }
   return 0;
}
   // gliderGun but caching matches so I can handle variable width matches
   // I *hate* to duplicate so much code but what to do?
   // !DO NOT call if pacman! That case == gliderGun
static int widerGlider(MotherShip *ms, Byte *dfa, UChar **_lp, 
     char *_bopat[], Stator *stator,   Byte *efa, int tags)
{
   char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
   UChar   *lp = *_lp, *are = lp, *ep, *plp = 0, *cache[500];
   int      n = 0;
   unsigned gid;	// closure group id

   cache[n++] = are;
   memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));
   while(*lp && (ep = pmatch(ms,dfa,lp,bopat,eopat,stator)) && lp!=ep){
      if(plp){ // last cache slot is lp, don't need to cache it
	 if(n==sizeof(cache)/sizeof(UChar *)){
	    _regExpFail(ms,"regExpMatch(): widerGlider(): cache full",RE_ERROR_EOM,stator);
	    return 0;
	 }
	 cache[n++] = lp;
      }
      plp = lp;
      lp  = ep;
   }
   if(are==lp) return 1;

   gid = ++ms->gid;	// group this branch so I can prune
   if(plp){		// queue matches greedy first
      for(ep = lp; n--; ep = cache[n]){
	 if(tags) pmatch(ms,dfa,cache[n],bopat,eopat,stator);  // set tags
	 if(forkk(ms,efa,ep,bopat,stator,gid,0,0)) return 0;
      }   
   }
   forkk(ms,efa,are,_bopat,stator,gid,0,0);
   return 0;
}
   // !DO NOT call if pacman! That case == gliderGunN
static int widerGliderN(MotherShip *ms, Byte *dfa, UChar **_lp, 
     char *_bopat[], Stator *stator,    Byte *efa, int N, int tags)
{
   if(N){
      char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
      UChar   *lp = *_lp, *are  = lp, *ep, *plp = 0, *cache[500];
      int      n = 0;
      unsigned gid;

      cache[n++] = are;
      memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));
      while(*lp && N-- && (ep = pmatch(ms,dfa,lp,bopat,eopat,stator))  && lp!=ep){
	 if(plp){	// last cache slot is lp, don't need to cache it
	    if(n==sizeof(cache)/sizeof(UChar *)){
	       _regExpFail(ms,"regExpMatch(): widerGliderN(): cache full",RE_ERROR_EOM,stator);
	       return 0;
	    }
	    cache[n++] = lp;
	 }
	 plp = lp;
	 lp  = ep;
      }
      if(are==lp) return 1;

      gid = ++ms->gid;	// group this branch so I can prune
      if(plp){		// queue matches greedy first
	 for(ep = lp; n--; ep = cache[n]){
	    if(tags) pmatch(ms,dfa,cache[n],bopat,eopat,stator);  // set tags
	    if(forkk(ms,efa,ep,bopat,stator,gid,0,0)) return 0;
	 }   
      }
      forkk(ms,efa,are,_bopat,stator,gid,0,0);
   }
   return 0;
}

    /* gliderGun/widerGlider for Node (alternation) closures.
     * Gotta keep state: a minimum of two rows of the search tree: (a|b)* has
     *   [up to] two children, each of those two can have two ...
     * Since this is alternation, match priorty is longest or left most so
     *   we can fork as we go, greatly reducing the amount of state we need
     *   save. This does conflict with PCRE.
??????left most-> conflicts with * == greedy?
     * We need to track where an alternation search ends because that is
     *   where the child search starts.
     * 
     * Does not handle nested NODEs: (a|(b+|c))+
     * 
     * Pacman means only the greedest match need proceed, previous matches
     *   can not result in longer matches. There can only be one: if ANY
     *   (a|b)+ fiber stops at c, they will all have the same match match:
     *   the past has no effect on future match. ie no need to fork
     *   Well, ignornig tags, refs and duplicates on the way to the end.
     * Given the duplicate elimination and ref checking forkk does, I'm
     * having a hard time getting motivated to write the code (a proto
     * works). And tag syncing (would have to add another row so I can
     * re-gen the correct tags). And I *have* to remove dups (see next
     * comment) which means digging into MotherShip.
     * 
     * Russ's nasty case revisited: (aa|a)* match "a"*N  == (a?)* 
     *   (--> PACMAN so it is fast. Dup elimination makes it fast even if
     *   not PACMAN (!PACMAN is a bit faster). I don't handle (a?)*a, it
     *   forks).
     * Solution is the same: don't fork duplicates (which forkk handles).
     * And don't cache them all (doNodeGlider()). Caching two rows, after
     *   prune, forks 2*(N+1) times and cache a max of 6. Lucky for me the
     *   Fibers die quickely but caching the tree isn't going to work (ie no
     *   greedy behavior (as done above), gotta be eagar which means longest
     *   wins.
     * 
     * n:=11; re:=n.pump(List,'+(1),'*.fp("a")).concat("|") : "(%s)*".fmt(_);
     * var r=RegExp(re)		--> (a|aa|aaa|aaaa|aaaaa)*
     * r.search("a")*100) --> doNodeGlider(): cache full  (cache[200/500])
     * n==17 --> (a)*: a too long
     */
static int nodeGlider(MotherShip *ms, Byte *dfa, UChar **_lp, 
     char *_bopat[], Stator *stator,  Byte *efa, int clop, int pacman)
{
   char  *bopat[2*RE_MAX_TAG];
   UChar *lp = *_lp, *are = lp;
   UChar *cache[500];
   int    n = 0, csz = sizeof(cache)/sizeof(UChar *);
   unsigned gid = 0;	// closure group id

   if(!*lp) return 1;	// a* match "" matches

   dfa += 1;	// skip over NODE

   memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));  //??? everytime??

#if OR_LONGEST_WINS
   gid = 0;
#else
   gid = ++ms->gid;	// (a.|a..)+ match abc --> "ab"(gid), "abc"(0)
#endif

   // build first level of searchtree
   if(-1 == (n = doNodeGlider(ms,dfa,lp,bopat,cache,csz, efa,gid)) )
      return 0;
   if(clop && !n) return 0;	// first round is required

   while(n){   // Build rolling window of two adjunct rows of the search tree
      UChar  **cpp;
      int  s,kk,lsz = n;
      
      for(kk = n, cpp = cache; kk--; cpp++){
	 if(*cpp){
#if OR_LONGEST_WINS
	    gid = 0;
#else
	    gid = ++ms->gid;	// !!!???each level is a new group
#endif
	    // build next level of search trees <<-- tree branches at each OR
	    s = doNodeGlider(ms,dfa,*cpp,bopat,&cache[n],csz - n, efa,gid);
	    if(s == -1) return 0; // error, I assume won't happen after above call
	    n += s;	// start of sibling level
	 }
      }
      // remove previous level
      memmove(cache,&cache[lsz],(n - lsz)*sizeof(UChar *));  // is move zero fast?
      n -= lsz;
   }

   if(!clop) forkk(ms,efa,are,_bopat,stator,0,0,0);  // fork the no match case
   return 0;
}
#if 0
// not usable: does not prune, does greedy "correctly"
static int nodeGlider(MotherShip *ms, Byte *dfa, UChar **_lp, 
     char *_bopat[], Stator *stator,  Byte *efa, int clop, int tags)
{
   char    *bopat[2*RE_MAX_TAG], **eopat = &bopat[RE_MAX_TAG];
   UChar   *lp = *_lp, *are = lp;
   NG	    cache[200];
   int      n = 0, s, tagi = 0, csz = sizeof(cache)/sizeof(NG);
   unsigned gid;	// closure group id

   dfa += 1;	// skip over NODE

   if(tags && *dfa==BOT) tagi = dfa[1];	//!!! maybe misses nested tags? tags iffy
   memcpy(bopat,_bopat,2*RE_MAX_TAG*sizeof(char *));  //??? everytime??

   // build the *entire* tree of start points for efa
   if(-1 == (n = doNodeGlider(ms,dfa,lp,bopat,cache,csz)) ) return 0;
   if(clop && !n) return 0;	// first round is required
   for(int one = 0, two = n; one < two; one = two, two = n)
      for(int k = one; k < two; k++){
	 NG *ptr = &cache[k];
	 s = doNodeGlider(ms,dfa,ptr->eopat,bopat,&cache[n],csz - n);
	 if(s == -1) return 0;
	 n += s;
      }

   gid = ++ms->gid;	// (a..|a.)+ match abc --> "ab" (gid) or "abc" (0)
   for(NG *cp = &cache[n - 1]; n--; cp--){
      if(tags){
	 bopat[tagi] = (char *)cp->bopat;
      	 pmatch(ms,cp->dfa,cp->bopat,bopat,eopat,stator);  // set tags
      }
      if(forkk(ms,efa,cp->eopat,bopat,stator,gid,0,0)) return 0;
   }   
   forkk(ms,efa,are,_bopat,stator,gid,0,0);
   return 0;
}
#endif

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
 * Input:
 * Returns:
 *   0: No match
 *      stator->dfa: op after the op that failed
 *      stator->lp:  char after failed char
 *      if HOLDS failed stator->noHolds==1
 *      _regExpFail may have been called: stator->badDFA==1, -->dfa==garbage
 *   else:
 *     Pointer to end of match
 *     stator->dfa: op after the op that succeeded
 *     At the end of a successful match, bopat[0] and eopat[0] are set to
 *       the beginning and end of the total text matched. bopat[n] and
 *       eopat[n] are set to the beginning and end of subpatterns matched by
 *       tagged expressions (n = 1 to RE_MAX_TAG).
 */

#ifndef TAIL_CALL	// the big switch or gotos?  This the switch code
 	// If you are looking for comments, read the tail call code

static UChar *pmatch(MotherShip *ms,
   Byte *dfa, UChar *lp,
   char *bopat[], char *eopat[], Stator *stator)
{
  UChar
    *e,			// extra pointer for CLO
    *bp, *ep;		// beginning and ending of subpat
  UChar  *are;		// to save the line ptr
  int     op, c, n, z;

  while( (op = *dfa++) != END)		// END==0 
    switch(op){
      case CHR:	if(!CEQ(*lp++,*dfa++)) goto fail; break;	// op_CHR
      case STR:							// op_STR
	 n = *dfa++;
	 #if ANDTHENULL
	    z = strncmp((char *)dfa, (char *)lp, n - 1);   // strncasecmp(3), _strnicmp(win)
	    lp += n - 1;
	 #else
            z = memcmp(dfa,lp,n);
	    lp += n;
	 #endif // ANDTHENULL
	 dfa += n; 
	 if(z) goto fail;
	 break;
      case ANY: if(*lp++ == '\0') goto fail; break;	// op_ANY
      case SET:						// op_SET
	 c = *lp++;
	 z = !ISINSET(dfa,c);	// ISINSET(dfa,0) is 0 since can't CHSET(0)
	 dfa += BITBLK;
	 if(z) goto fail;
	 break;
      case NSET:					// op_NSET
	 z = ( (c = *lp++) == '\0' || ISINSET(dfa,c) );
	 dfa += BITBLK;
	 if(z) goto fail;
	 break;
      case BOT:		// op_BOT, OR can reopen tags, slam the door
	 n = *dfa++; bopat[n] = (char *)lp; eopat[n] = 0; break;
      case EOT:	eopat[*dfa++] = (char *)lp;	 	  break;    // op_EOT
      case REF:			// op_REF, REF 1-9
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
      case BOW:			// op_BOW
	 if(!(lp != ms->bol && IS_WORD(lp[-1])) && IS_WORD(*lp)) break;
	 goto fail;
      case EOW:		// op_EOW, 'w\0' is OK here
	 if((lp != ms->bol && IS_WORD(lp[-1])) && !IS_WORD(*lp)) break;
	 goto fail;
      case AORB:	 // op_AORB, <tag count><hops to sibling OR>
	 DEBUGCODE( printf("OR1: %ld: tags(%d) Hops(%d)\n",DFA_ADDR(ms,dfa) - 1,*dfa,dfa[1]); )
	 if(*dfa)	// 0 is a flag/place holder, tag 0 is set upstairs
	    eopat[*dfa] = (char *)lp;	// close my tag: EOT n
	 if(!(dfa = dfaScanForward(dfa + 2,EDON,1,0)))
	       return _regExpFail(ms,"regExpMatch: AORB: No EDON.",RE_ERROR_BAD_DFA,stator);
	 DEBUGCODE( printf("OR2: jumped to %ld\n",DFA_ADDR(ms,dfa)); )
         break;	// --> EDON
      case NODE:			// op_NODE
	 lp  = doNode(ms,dfa,lp,bopat,stator,0);
	 dfa = stator->dfa;
	 goto fail;
      case EDON: break;			// op_EDON

      case CLOP: z = 1; goto clo;
      case CLO:  z = 0;
      clo:				// op_CLO
      {
	 int f, pacman = ms->pacman; ms->pacman = 0;

	 are = lp;
	 n   = ANYSKIP;
	 switch(*dfa){
	    case ANY:     lp += strlen((char *)lp);	    break; // -->Eol
	    case DIGIT:   while( isdigit(*lp))	      lp++; break;
	    case N_DIGIT: while(!isdigit(*lp) && *lp) lp++; break;
	    case SPACE:   while( isspace(*lp))	      lp++; break;
	    case N_SPACE: while(!isspace(*lp) && *lp) lp++; break;
	    case ALPHA:   while( IS_WORD(*lp))	      lp++; break;
	    case N_ALPHA: while(!IS_WORD(*lp) && *lp) lp++; break;
	    case CHR:
	       c = dfa[1];		// we know c != '\0'
	       while(CEQ(*lp,c)) lp++;
	       n = CHRSKIP;
	       break;
	    case SET: case NSET:
	       while(*lp && (e = pmatch(ms,dfa,lp,bopat,eopat,stator))) lp = e;
	       n = SETSKIP;
	       break;
	    case BOT:
	       f    = dfa[1];
	       n    = dfa[2];
	       dfa += 3;
	       pacman = (f & CLO_PACMAN);

	       if(*dfa==NODE){
		  if(lp && nodeGlider(ms,dfa,&lp,bopat,stator, dfa + n, z, pacman))
		     { dfa += n; goto cloNextOp; }
	       }else{
		  if(z) lp = pmatch(ms,dfa,lp,bopat,eopat,stator); // a+ == aa*
		  c  = (f & CLO_VWIDTH);
		  f &= CLO_TAGS;
		  if(lp && (c ? widerGlider(ms,dfa,&lp,bopat,stator, dfa + n,f)
			      : gliderGun(  ms,dfa,&lp,bopat,stator, dfa + n,f,pacman) ))
		    { dfa += n; goto cloNextOp; }
	       }
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

      fork:		// op_fork
         {
	    if(are==lp) break;	// only one, don't need to fork
	    n = ++ms->gid;	// group this branch so I can prune	
	    for(; are <= lp; lp--)  // greedy: longest match goes first
	       if(forkk(ms,dfa,lp,bopat,stator,n,0,0)) break;
	    goto fail;	// are branches queued, our job is done
	 }
      cloNextOp:
	 break;
      }
      case ONE:   z = 1; goto clomn;
      case CLOMN: z = 0;		// op_CLOMN
      clomn:
      {
	 UChar *tp;
	 int   M,N, star = 0, i, op, Z, flags;
	 Byte  *efa;
	 int    pacman = ms->pacman; ms->pacman = 0;

	 n = ANYSKIP;

	 if(z){ M = 0;      N = 1;      op = dfa[0]; c = dfa[1]; }
	 else { M = dfa[0]; N = dfa[1]; op = dfa[2]; c = dfa[3]; dfa += 2;
		if(N==0){ star = 1; N = M; }
	      }
	 Z = N;
	 if(op==BOT){
	    flags = dfa[1];
	    n     = dfa[2];
	    dfa  += 3;
	    efa   = dfa; // because I increment dfa below
	    Z     = M;	 // then let gliderGun do the rest
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
		  if( (ep = pmatch(ms,dfa,lp,bopat,eopat,stator))) lp = ep;
		  n = SETSKIP;
		  break;
	       case NSET:
		  if( (ep = pmatch(ms,dfa,lp,bopat,eopat,stator))) lp = ep;
		  n = SETSKIP;
		  break;
	       case BOT:
		  if( (ep = pmatch(ms,dfa,lp,bopat,eopat,stator)) ) lp = ep;
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
	    n = (dfaScanForward(dfa,END,0,0) - dfa + 1);

	 if(i==M && !*lp){ dfa += n; break; } // {m,n}: only m matches

	 if(star && op!=BOT){	// a{2,}, N=2 --> aaa* == aa+
	    ms->pacman = pacman;
	    goto clo;	// fork a*, dfa --> CHR a, have consumed N "a"s
	 }

	 dfa += n;	// next op

	 if(M==N && !star) break;	// {n} and have matched n

	 if(op==BOT){
	    int wide = (flags & CLO_VWIDTH);
	    c	     = (flags & CLO_TAGS);
	    pacman   = (flags & CLO_PACMAN);
	    lp = are;		// in case of PACMAN
	    if(star) i = wide ? widerGlider( ms,efa,&lp,bopat,stator,dfa,    c)     :
				gliderGun(   ms,efa,&lp,bopat,stator,dfa,    c,pacman);
	    else     i = wide ? widerGliderN(ms,efa,&lp,bopat,stator,dfa,N-M,c) :
				gliderGunN(  ms,efa,&lp,bopat,stator,dfa,N-M,c,pacman);
	    if(i) break;
	    goto fail;	// are branches queued, our job is done
	 }

	 if(pacman) break;
	 goto fork;
      }
        break;
      case PACMAN: ms->pacman = 1; break;		// op_PACMAN
      case HOLDS:	// op_HOLDS: HOLDS hops-to-STR (base 1 in this Node)
      #if DO_HOLDS
	 for(n = *dfa, bp = dfa + 1; (bp = dfaScanForward(bp,STR,1,0)) && --n;
	     bp += (bp[1] + 2) ){}	// gotta single step bp over STR
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
      #else
         dfa++; break;		// fallback
      #endif // DO_HOLDS
      #if DO_DOTSTAR
      case DOTSTAR:				// op_DOTSTAR
      {
	 Byte	    c;
	 char	   *ep, *str = 0;
	 int	    n   = strlen((char *)lp), span = 1;
	 unsigned gid = ++ms->gid;

	 if(*dfa==CHR) c    = dfa[1];
	 else{	       c    = *(str = (char *)&dfa[2]); 
		       span = dfa[1] - ANDTHENULL; 
		       n   -= (span  - 1);
	     }
	 while(n > 0 && (ep = memrchr(lp,c,n)) ){
	    n = ep - (char *)lp;
	    if(str && memcmp(ep,str,span)) continue; // ep + span <= end of lp
	    if(forkk(ms,dfa,(UChar *)ep,bopat,stator,gid,0,0)) break;
	 }   
	 goto fail;
	 break;
      }
      case DOTSTAB:				// op_DOTSTAB
      {
	 Byte	    c = *dfa++;
	 char	   *ep;
	 int	    n   = strlen((char *)lp);
	 unsigned gid = ++ms->gid;

	 while(n > 0 && (ep = memrchr(lp,c,n)) ){
	    n = ep - (char *)lp;
	    if(forkk(ms,dfa,(UChar *)ep,bopat,stator,gid,0,0)) break;
	 }   
	 goto fail;
	 break;
      }
      #else	// fallbacks
      case DOTSTAR:				// op_DOTSTAR
      dotStarFallback:
	 are = lp;
	 lp += strlen((char *)lp);	// -->Eol
	 goto fork;
      case DOTSTAB:				// op_DOTSTAB
	 dfa++;
	 goto dotStarFallback;
      #endif // DO_DOTSTAR
      default: return _regExpFail(ms,"regExpMatch: bad dfa.",RE_ERROR_BAD_DFA,stator);
    }// switch, while
    stator->theEnd = 1;		// hit END
    dfa--;			// point at END in case somebody continues

    // success!
//success:
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
     * I do like debugging the ops better than the switch although GDB gets
     *   really confused about parameters.
     */

#define OP_ARG_LIST  MotherShip *ms, Byte *dfa, UChar *lp, \
	     char *bopat[], char *eopat[], Stator *stator

#define OP_ARGS	     ms, dfa,     lp, bopat,eopat,stator
#define OP_ARGSP     ms, dfa + 1, lp, bopat,eopat,stator

#define _H_ 
//#define _H_ [[clang::preserve_none]]	// clang 19, I'm running 18

#define OP_SIG(opName) _H_ static UChar *opName(OP_ARG_LIST)

typedef UChar *(*OpAddr)(OP_ARG_LIST);
static _H_ OpAddr re_ops[];		// jump table

    // jump/goto to next op:
#define JMP_NEXT_OP() TAIL_CALL(re_ops[*dfa],OP_ARGSP)
#define GOTO_OP(op)   TAIL_CALL(op,          OP_ARGS )

OP_SIG(pmatch){ JMP_NEXT_OP(); }
//static UChar *pmatch(OP_ARG_LIST){ return re_ops[*dfa](OP_ARGSP); }  // _H_

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
  // Hmmm, looks like in several engines, . doesn't match \n unless flag: DOTALL
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
#else	// the hope is that this is faster but usually really short
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
#if 0
OP_SIG(op_WB){	// "\b": word boundary: (?<=\W)(?=\W)|(?<=\w)(?=\w)
   if(!(lp != ms->bol && IS_WORD(lp[-1])) &&  IS_WORD(*lp)) JMP_NEXT_OP();
   if( (lp != ms->bol && IS_WORD(lp[-1])) && !IS_WORD(*lp)) JMP_NEXT_OP();
   GOTO_OP(op_fail);
}
#endif

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
   if(!(dfa = dfaScanForward(dfa + 2,EDON,1,0)))
      return _regExpFail(ms,"regExpMatch: AORB: No EDON.",RE_ERROR_BAD_DFA,stator);
   DEBUGCODE( printf("OR2: jumped to %ld\n",DFA_ADDR(ms,dfa)); )
   //JMP_NEXT_OP();	// --> EDON, == dfa++; GOTO_OP(op_EDON)
   dfa++; JMP_NEXT_OP();  // EDON == no-op so skip it
}
OP_SIG(op_NODE){
   lp  = doNode(ms,dfa,lp,bopat,stator,0);
   dfa = stator->dfa;
   //if(!lp) GOTO_OP(op_fail); JMP_NEXT_OP();		// next op: EDON + 1
   GOTO_OP(op_fail);	// doNode() forks
}
//OP_SIG(op_EDON){ stator->edon = 1; GOTO_OP(op_success); }
OP_SIG(op_EDON){ JMP_NEXT_OP(); }    // flowed off the end of last OR

    /* Handle * + sometimes ? {}
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
     * It would be nice if to just carry on for one of the branches,
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

   if(are==lp) JMP_NEXT_OP(); // a* match "b": no matches, don't fork, carry on
   gid = ++ms->gid;	// group this branch so I can prune
   for(; are <= lp; lp--)
      if(forkk(ms,dfa,lp,bopat,stator,gid,0,0)) break; // match found, stop

   GOTO_OP(op_fail);	// are branches queued, our job is done
}
OP_SIG(op_CLO){ // both CLO:[5] (*: none or more) & CLOP:[6] (+: one or more)
   		// CLO ANY|CHR|SET ... END
   UChar *are = lp, *ep;
   int    sz  = ANYSKIP, c,  clop = (dfa[-1]==CLOP), f;
   int    pacman = ms->pacman; ms->pacman = 0;	// for this op only

   switch(*dfa){
      case ANY:     lp += strlen((char *)lp);	      break; // -->Eol
      case DIGIT:   while( isdigit(*lp))	lp++; break;
      case N_DIGIT: while(!isdigit(*lp) && *lp) lp++; break;
      case SPACE:   while( isspace(*lp))	lp++; break;
      case N_SPACE: while(!isspace(*lp) && *lp) lp++; break;
      case ALPHA:   while( IS_WORD(*lp))	lp++; break;
      case N_ALPHA: while(!IS_WORD(*lp) && *lp) lp++; break;
      case CHR:
	 c = dfa[1];	// we know c != '\0', *lp can be
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
      #if 0
      case REF: 	// repeat REF n END
	 while(*lp && (ep = op_REF(OP_ARGSP))) lp = ep;
	 sz = CHRSKIP;
	 break;
      #endif
      case BOT: 
         // (ab)+ -->PACMAN CLOP BOT flags==5 sz==9 BOT n CHR a CHR b EOT n END
	 // Flags: 1 (PACMAN), 2 (wide), 4 (contains non-cosmetic tags)
	 f      = dfa[1];	// flags
	 sz     = dfa[2];	// size
	 dfa   += 3;
	 pacman = (f & CLO_PACMAN); // only way to test is to examine DFA or if forks

	 if(*dfa==NODE){
	 #if PAYLOAD
	    if(c) doNode( ms,dfa + 1,lp,bopat,stator, (clop ? 1 : 2));
	    else if(nodeGlider(ms,dfa,   &lp,bopat,stator, dfa + sz, clop,pacman))
			{ dfa += sz; JMP_NEXT_OP(); }
	 #else
	    if(nodeGlider(ms,dfa,&lp,bopat,stator, dfa + sz, clop,pacman))
	       { dfa += sz; JMP_NEXT_OP(); }
	 #endif
	 }else{
	    if(clop) lp = pmatch(OP_ARGS); // a+ == aa*
	    c  = (f & CLO_VWIDTH);	// wide
	    f &= CLO_TAGS;		// noncosmo tags
	    if(lp && (c ? widerGlider(ms,dfa,&lp,bopat,stator, dfa + sz,f)
			: gliderGun(  ms,dfa,&lp,bopat,stator, dfa + sz,f,pacman) ))
		{ dfa += sz; JMP_NEXT_OP(); }
	 }
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

   // PACMAN means a*b doesn't fork and fail a zillion times vs just
   // continuing at "b" (as there are no other choices).
   if(pacman) JMP_NEXT_OP();	// a*b, we have consumed all "a"s
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
     * CLOMN M N <dfa> END
     * ONE <dfa> END --> CLOMN 0 1 <dfa>
     * CLOMN M N BOT flags sz dfa END  : (abc){M,N}
     * 
     * It sure would have been a lot less code to compile *+ as CLOMN ala
     *   ONE although I use CLO as a fallback and CLOMN doesn't handle (a|b){}
     *   A quick test shows it works.
     */
OP_SIG(op_CLOMN){
   UChar *ep, *tp, *are; 	// for M==0
   int    c, sz = ANYSKIP, M,N, star = 0, i, op, Z, flags;
   OpAddr _H_ opaddr;
   Byte  *efa;
   int    pacman = ms->pacman; ms->pacman = 0;
   
   if(dfa[-1]==ONE){ M = 0;      N = 1;	     op = dfa[0]; c = dfa[1]; }
   else		   { M = dfa[0]; N = dfa[1]; op = dfa[2]; c = dfa[3]; dfa += 2;
      		     if(N==0){ star = 1; N = M; }
		   }
   Z = N;
   if(op==SET) opaddr = op_SET; else if(op==NSET) opaddr = op_NSET;
   if(op==BOT){
      flags = dfa[1];
      sz    = dfa[2];
      dfa  += 3;
      efa   = dfa;	// because I increment dfa below
      Z     = M;	// then let gliderGunN do the rest [of the matches]
   }
   for(are = tp = lp, i = 0; i < Z && *lp; i++){
      switch(op){  // I assume while(switch) is faster than while(pmatch())
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
	       "regExpMatch: {} closure: bad dfa.",RE_ERROR_BAD_DFA,stator);
     }//switch
     if(tp!=lp) tp  = lp;     // successful match
     else	break;	     // match fail
     if(i<M)	are = lp;   // first M matches are required, next are optional
   }//for
   if(i < M) GOTO_OP(op_fail);	// < min matches
   if(i==0 && sz==ANYSKIP && op!=BOT) // switch maybe not done, skip size unknown
      sz = (dfaScanForward(dfa,END,0,0) - dfa + 1);

   if(i==M && !*lp){ dfa += sz; JMP_NEXT_OP(); }   //  {m,n}: only m matches

   if(star && op!=BOT){	// a{2,}, N=2 == aaa* == aa+, have consumed aa
      ms->pacman = pacman;
      GOTO_OP(op_CLO);	// fork a*, dfa --> CHR a, have consumed N "a"s
   }

   dfa += sz;	// next op

   if(M==N && !star) JMP_NEXT_OP();  // {n} & matched n

   if(op==BOT){ // (abc){M,N}, matched M instances, now fork N - M more
      int wide = (flags & CLO_VWIDTH);
      c	       = (flags & CLO_TAGS);
      pacman   = (flags & CLO_PACMAN);
      lp       = are;		// in case of PACMAN
      if(star) i = wide ? widerGlider( ms,efa,&lp,bopat,stator,dfa,    c):
			  gliderGun(   ms,efa,&lp,bopat,stator,dfa,    c,pacman);
      else     i = wide ? widerGliderN(ms,efa,&lp,bopat,stator,dfa,N-M,c):
			  gliderGunN(  ms,efa,&lp,bopat,stator,dfa,N-M,c,pacman);
      if(i) JMP_NEXT_OP();
      GOTO_OP(op_fail);	// are branches queued, our job is done
   }

   if(pacman) JMP_NEXT_OP();
   ms->are = are; GOTO_OP(op_fork);
}
OP_SIG(op_PACMAN){
   /* PACMAN CLO|CLOP|ONE|CLOMN ..  tell closure to consume, !fork
    * Signal to *next* op 
    * This needs to reset before this Fiber yields so it can not be used by
    *   another Fiber. Setting a global resource.
    */
   ms->pacman = 1;
   JMP_NEXT_OP();
}
OP_SIG(op_HOLDS){	// HOLDS hops-to-STR (base 1 in this Node)
#if DO_HOLDS		//!!!??? only do this if searching?
   Byte *bp;
   int   n;

   for(n = *dfa, bp = dfa + 1; (bp = dfaScanForward(bp,STR,1,0)) && --n;
       bp += (bp[1] + 2) ){}	// gotta single step bp over STR
   if(!bp) return _regExpFail(ms,
		     "regExpMatch: HOLDS could not find STR",RE_ERROR_BAD_DFA,stator);
   dfa++;
   #if ANDTHENULL
      if(!strstr((char *)lp, (char *)(bp + 2)))	// strcasestr(3), Xwin
   #else
      if(!memmem(lp,strlen((char *)lp), bp + 2, bp[1]))
   #endif
      { stator->noHolds = 1; GOTO_OP(op_fail); }
   JMP_NEXT_OP();
#else		// fallback
   dfa++; JMP_NEXT_OP();
#endif // DO_HOLDS
}
OP_SIG(op_DOTSTAR){	// .*a --> DOTSTAR CHR a | DOTSTAR STR n text
#if DO_DOTSTAR
   /* Scan to farthest a, fork, next farthest, fork, ..
    * "(.*this |.*that )" match "well that went well"  (len == 19)
    *    Before: 38 Fibers, After: 7 Fibers (3 "t"s in text)
    * .*a match aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    *    same either way: fork until GC, done
    * A strpbrk reverse would be nice for .*(a|b), .*[a-z], .*\d, etc
    *   Would DOTSTAR 0 DOTSTAR 1 .. DOTSTAR 9 work? DOTSTAR 10 1234567890
    *   *.(a|b) ??--> DOTSTAR 2 ab
    * Note: .+ --> ..* so .+ gets DOTSTAR for free
    * !!!ARG: .*a.*b  match aaaaaaaaaaaaaaaaaaaabcd  --> dead lock
    *   The problem is DOTSTAR [!terminal] DOTSTAR
    *   !DOTSTAR dead locks with a lot more a's: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabcd
    *   A GC before deadlock might fix the problem in this case
    */
   Byte	    c;
   char	   *ep, *str = 0;
   int	    n   = strlen((char *)lp), span = 1;
   unsigned gid = ++ms->gid;

#if 0
   if(*dfa==REF){
      c    = dfa[1];
      str  = bopat[c];		// probably need some sanity checks
      span = eopat[c] - str;
      n   -= (span  - 1);
      c    = *str;
   }else
#endif
   if(*dfa==CHR) c    = dfa[1];		// will re-run CHR a  !!! should skip
   else{	 c    = *(str = (char *)&dfa[2]); 
		 span = dfa[1] - ANDTHENULL; 
		 n   -= (span  - 1);
       }
   while(n > 0 && (ep = memrchr(lp,c,n)) ){
      n = ep - (char *)lp;
      if(str && memcmp(ep,str,span)) continue; // ep + span <= end of lp
      if(forkk(ms,dfa,(UChar *)ep,bopat,stator,gid,0,0)) break;
   }   
   GOTO_OP(op_fail);   // .*a match "" -->fail
#else
   // fallback: do .* the hard way
   UChar *are = lp;
   lp += strlen((char *)lp);	// -->Eol
   ms->are = are;
   GOTO_OP(op_fork);
#endif  // DOTSTAR
}
OP_SIG(op_DOTSTAB){	// same as DOTSTAR but with char as part of op
#if DO_DOTSTAR
   Byte	    c = *dfa++;
   char	   *ep;
   int	    n   = strlen((char *)lp);
   unsigned gid = ++ms->gid;

   while(n > 0 && (ep = memrchr(lp,c,n)) ){
      n = ep - (char *)lp;
      if(forkk(ms,dfa,(UChar *)ep,bopat,stator,gid,0,0)) break;
   }   
   GOTO_OP(op_fail);   // .*a match "" -->fail
#else
   // fallback: do .* the hard way
   dfa++;	// skip over DOTSTAB b
   GOTO_OP(op_DOTSTAR);
#endif  // DOTSTAR

//OP_SIG(op_DOTSTAC){	 DOTSTAX?	// op_DOTSTAC: PREFIX in reverse
}

//////////////////////

_H_ static OpAddr re_ops[] = {
  op_END,
  op_CHR,   op_ANY,     op_SET,    op_NSET,	// 2 - 4
  op_BOL,   op_EOL,     op_BOT,    op_EOT,	// 5 - 8
  op_BOW,   op_EOW,     op_REF,   	 	// 9 - 11
  op_DIGIT, op_N_DIGIT, op_SPACE,  op_N_SPACE, op_ALPHA, op_N_ALPHA, // 12
  op_CLO,   op_CLOMN,   op_CLO,    op_CLOMN,			     // 18
  op_AORB,  op_NODE,    op_EDON,				     // 22
  op_STR,   op_HOLDS,   op_PACMAN, op_DOTSTAR, op_DOTSTAB,	     // 25
};

#endif	// TAIL_CALL


   /* Walk the DFA ops looking for specified op.
    * Starts looking at dfa, does not single step first. Because some requre
    *   that (doNode).
    * inThisNode: 1: Only stop if stopAt is in Node dfa starts in. Unless
    *   try to leave that Node, in which case fail.
    * Does not look inside CLO or CLOMN.
    * Returns: *dfa == stopAt or 0
    */
static Byte *dfaScanForward(
     Byte *dfa,int stopAt, int inThisNode, Byte **_tailCLO){
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
	       case SPACE: case N_SPACE:	// eg CLO SPACE END
	       case DIGIT: case N_DIGIT:
	       case ALPHA: case N_ALPHA:
	       case ANY:            n = ANYSKIP;    break;
	       case CHR: case REF:  n = CHRSKIP;    break; // CLO CHR chr END
	       case SET: case NSET: n = SETSKIP;    break;
	       case BOT:	    n = dfa[2] + 3; break;
	       #if DFA_DEBUG
	       default: return 0;
	       #endif
	    }
	    dfa += n;	// remember dfa++
	    break;
	 case HOLDS:
	 case CHR: case BOT: case EOT: case REF:
	 case DOTSTAB:			dfa++;	       break;
         case AORB:			dfa += 2;      break;
         case SET:  case NSET:		dfa += BITBLK; break;

	 case NODE: lvl++; break;
	 case EDON: 
	    if(--lvl < 0 && inThisNode) return 0; // DFA fail
	    break;
      }// switch
   }// while

#else
   // Arggh! slower. At best, not faster
   #define M42	0x30	// magic number
   // END      CHR     ANY     SET     NSET    BOL EOL BOT EOT BOW EOW REF       DIGIT   N_DIGIT SPACE   N_SPACE ALPHA   N_ALPHA  CLO  ONE CLOP CLOMN AORB NODE EDON STR HOLDS PACMAN DOTSTAR DOTSTAB
   static Byte opskp[] = {
      0,       1,      0,      BITBLK, BITBLK, 0,  0,  1,  1,  0,  0,  1,        0,      0,      0,      0,      0,      0,       M42, M42,M42, M42,  2,   0,   0,   0,  1,    0,     0,      1, };
   static Byte skp2[]  = { // op == CLO/CLOP/ANY/CLOMN: size of CLO op
      ANYSKIP, CHRSKIP,ANYSKIP,SETSKIP,SETSKIP,0,  0,  M42,0,  0,  0,  CHRSKIP,  ANYSKIP,ANYSKIP,ANYSKIP,ANYSKIP,ANYSKIP,ANYSKIP, 0,   0,  0,   0,    0,   0,   0,   0,  0,    0,     0,      0, };

   int n, lvl = 0, op;
Byte *tailCLO = 0;

   while(*dfa != END){
      if(*dfa == stopAt && (lvl==0 || !inThisNode)) return dfa;

      DEBUGCODE( if(*dfa > LAST_OP) return 0; )	// range check

      n = opskp[op = *dfa++];
      if(n==M42){	// a closure
tailCLO = dfa;
	 // CLO/CLOP/ONE  dfa  or  CLO       BOT flags sz dfa
	 // CLOMN m n     dfa  or  CLOMN m n BOT flags sz dfa
	 if(op==CLOMN) dfa += 2;
	 if( (n = skp2[*dfa]) == M42) dfa += dfa[2] + 3;	// (a*c)*
	 else			      dfa += n;
	 continue;
      }
if(op!=EOT && op!=EDON) tailCLO = 0;
      switch(op){
	 default:   dfa += n;	   break;
	 case STR:  dfa += (*dfa + 1); break;
	 case NODE: lvl++;		   break;
	 case EDON: 
	    if(--lvl < 0 && inThisNode) return 0; // DFA fail
	    break;
      }// switch
   }// while
#endif

if(_tailCLO && tailCLO) *_tailCLO = tailCLO - 1;

   if(END==stopAt) return dfa;	// op_CLOMN(): dfaScanForward(END)
   return 0;	// not found
}

    // Returns: 0 (error), size
int dfaSz(Byte *dfa){
   Byte *end = dfa + 1;	// flags

   if(*end==PREFIX) end += end[1] + 3;	// dfaScanForward doesn't know PREFIX
   end = dfaScanForward(end,0,0,0);
   if(!end) return 0;
   return end - dfa + 1;
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

// I dump DFAs a LOT (in gdb) so a pretty printer is worth the effort

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
		  n = dfa[1];	// flags
		  printf(" : BOT : tag size: %d : flags: %d: ",dfa[2],n);
		  if(n&CLO_PACMAN) printf("PACMAN ");
		  if(n&CLO_VWIDTH) printf("VWIDTH ");
		  printf((n & CLO_TAGS) ? "tags " : "cosmetic ");
		  dfa += 3;
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
	 case EOW: PRINTLN("EOW \\>");	  break;
	 case AORB: 
	    if((n = dfa[1])){
	       PRINT("OR: ");
	       if(n==0xff) printf("Tag(%d). Many hops to sibling OR",dfa[0]);
	       else        printf("Tag(%d). %d hop(s) to sibling OR",dfa[0],n);
	       dp = dfaScanForward(dfa + 2, AORB, 1,0);
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
	 case PACMAN:  PRINTLN("PACMAN");		     break;
	 case DOTSTAR: PRINTLN("DOTSTAR");		     break;
	 case DOTSTAB: PRINTLNc("DOTSTAB");		     break;
	 case HOLDS:   PRINTLNn("HOLDS: Top hops to STR: "); break;
	 case STAKE:   PRINTLN("STAKE");		     break;
	 case PREFIX:
	    PRINT("PREFIX: "); printf("%d %s\n",dfa[0],&dfa[1]);
	    dfa += dfa[0] + 2;
	    break;
	 default:
	    printf("Bad dfa. Opcode %d\n", dfa[-1]);
	    return 0;
      }// switch

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
// search this file(135,363 bytes): with PREFIX: 0.003635 sec, w/o: 0.068074
// 18x speed up --> 37mb/sec
