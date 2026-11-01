# zkRE
Regular expression engine with ERE syntax for ASCII text.</br>

__Syntax__: `{}[]()^$.|*+?\  \d\D  \s\S  \w\W \<\>  \0 .. \9  (?:)`<br/>
    &nbsp; &nbsp; &nbsp;&nbsp; &nbsp; d (digit), s (space), w (word), <> (begin/end word),  (?:) non-capturing<br/>
Documented at top of zkRE.c

A non-recursive back tracking regular expression engine.

Two __C files__: zkRE.[ch], no memory allocation, thread safe, public domain

__Limitations__:
- NO support for non-ASCII (8 bit) text
- Case insensitive search not implemented.
- Some group closures not supported, eg (a+)+a, (.+a)+b, (a+|(b|c))+</br>
   To close a group, the group must have an "unambiguous" stopping point and
   no nested alternations.
- Group values can differ from recursive engines (eg PCRE) or POSIX RE
    * Count and index can differ
      eg "a(b)|c(d)|a(e)f" match"aef" --> \1=="e", not \1==\2=="", \3=="e"
- Alternation is longest match wins:
    (ab|abc) and (ab|abc)+ match "abc" --> "abc" vs "ab" PCRE
- The width of the match tree is limited: viewing a
match as a breadth first search, the number of nodes/level is limited:
`".*a"` match "1234a67890" is width 11, `".*(a|b)"` doubles the width. The
compiler tweaks and the VM prunes to control growth, not always
successfully.

__Tests__: 1,200+ hand written tests, 221 are Henry Spencer's regular
expression tests (10 of which were modified).


__Examples__:
```
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
   if( (ptr = regExpCompile(re,0x00,dfa,&n,0)) ){ printf("%s\n",ptr); return; }
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
   doRE("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaa",0x0);         // match             (106 byte DFA)
   doRE("(test\\w*)","it was a testing time",RE_SEARCH);  // \1 == "testing"
   return 0;
}
```
```
zkl: var r=RegExp(0''^(aa|a)+')		# 23 byte DFA
zkl: t:=Time.Clock.runTime; r.search("a"*100_000); Time.Clock.runTime-t
0.01142
zkl: r.matched
L(L(0,100000),"aa")

zkl: var r=RegExp(0''^(?:a+)+b')	// (?:a+)+a won't compile, ambiguous
zkl: t:=Time.Clock.runTime; r.search("a"*100_000 + "b"); Time.Clock.runTime-t
0.000759
zkl: r.matched
L(L(0,100001))

zkl: var r=RegExp(0''(.*) (.*) (.*) (.*) (.*)')    	# 42 byte DFA
zkl: var txt = ["a".."e"].apply('*(10_000)).concat(" ")  # "aaa bbb ccc ddd eee"
zkl: t:=Time.Clock.runTime; r.search(txt); Time.Clock.runTime-t
4e-05
zkl: r.matchedNs
L(L(0,50004),L(0,10000),L(10001,10000),L(20002,10000),L(30003,10000),L(40004,10000))

zkl: t:=Time.Clock.runTime; r.search("a"*50_000); Time.Clock.runTime-t
0.000283	  # no match. Actually searching: same

//////////

zkl: var d=File("VM/zkRE.c").read()   // has (65O) 253-0001. in last line
Data(135,326)

zkl: var r=RegExp(0''(\d{3}-|\(\d{3}\)\s+)(\d{3}-\d{4})')	# 61 bytes
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.003635
zkl: r.matched
L(L(135190,14),"(65O) ","253-OOO1")    // 65"O" is zero, don't match here

zkl: var r=RegExp(0''.*(\d{3}-|\(\d{3}\)\s+)(\d{3}-\d{4})')	# 48 bytes
zkl: t:=Time.Clock.runTime; r.search(d); Time.Clock.runTime-t
0.030823
zkl: r.matched
L(L(0,135204),"(65O) ","253-OOO1")

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$')	# 69 bytes
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.016997 // short circuits by noting that ABC...XYZ is not contained in text
     // ie strstr() and fail 2,786 times, up to this comment @ character 2,786

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZaaa$')  // aaa is AAA
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
2e-05 // strstr(ABCDE...XYZAAA) ONCE and fail
 // Meta fail: this comment is in search text, aaa/AAA to avoid false match
```
