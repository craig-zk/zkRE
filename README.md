# zkRE
Regular expression engine with ERE syntax for ASCII text.

__Syntax__: `{}[]()^$.|*+?\  \d\D  \s\S  \w\W \<\>  \n  (?:)`<br/>
d (digit), s (space), w (word), <> (begin/end word),  (?:) non-capturing<br/>
Documented at top of zkRE.c

A non-recursive back tracking regular expression engine.

Two __C files__: zkRE.[ch], thread safe, public domain

__Limitations__:
- NO support for non-ASCII text
- Results can differ from recursive engines (eg PCRE)
- Some group closures not supported (eg (a+b)+c, (a|b)+),
- The width of the match tree is limited: viewing a
match as a breadth first search, the number of nodes/level is limited
(`".*a"` match "1234a67890" is width 11, `".*(a|b)"` doubles the width. The
compiler tweaks and the VM prunes to control growth, not always
successfully).

__Tests__: 752 manually written tests, 221 are Henry Spencer's regular
expression tests (10 of which were modified).


__Examples__:
```
// clang eg.c zkRE.c
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
   doRE("(ab|a)bc","abc",0x0);          // match \1 == "a"    (21 byte DFA)
   doRE("(dog|cat)\\1","catcat",0x0);   // match              (25 byte DFA)
   doRE("(a.c){1,2}","abcadcaec",0x0);  // match \1 == "adc"  (17 byte DFA)
   doRE("(ab*c)+","abbbcacab",0x0);     // match \1 == "ac"   (20 byte DFA)
   doRE("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaaaaa",0x0);     // match               (102 byte DFA)
   doRE("(test\\w*)","it was a testing time",RE_SEARCH);  // \1-->"testing"
   return 0;
}
```
```
zkl: var d=File("VM/zkRE.c").read()   // has (65O) 253-0001. in last line
Data(103,515)
zkl: var r=RegExp(0''(\d{3}-|\(\d{3}\)\s+)(\d{3}-\d{4})')
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.057324
zkl: r.matched
L(L(103379,14),"(65O) ","253-0001")    // 65"O" is zero, don't match here

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZ$')
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
0.011371  // short circuits by noting that ABC..XYZ is not contained in text
         // ie strstr() and fail 2,786 times, up to this comment

zkl: r=RegExp(0''[ -~]*ABCDEFGHIJKLMNOPQRSTUVWXYZaaa$')  // aaa is AAA
zkl: t:=Time.Clock.runTime; r.search(d,True); Time.Clock.runTime-t
2e-05   // strstr(ABC...XYZAAA) ONCE and fail
  // Meta fail: this comment is in search text, aaa/AAA to avoid false match
```
