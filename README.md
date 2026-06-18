# zkRE
Regular expression engine with ERE syntax for ASCII text.

A non-recursive back tracking regular expression engine.

Two C files: zkRE.[ch], thread safe, public domain

Syntax: ERE (extended regular expressions): `{}[]()^$.|*+?\  \s\S  \w\W  \<\>  \n  (?:)`<br/>
Documented at top of zkRE.c

Limitations: NO support for non-ASCII text, results can differ from recursive engines (eg PCRE), some group closures not supported (eg (a+c)+, (a|b)+)

Example:
```
// clang eg.c zkRE.c
// clang will compile tail call VM, gcc & MSVC won't so they get a big switch

#include <stdio.h>
#include <string.h>
#include "zkRE.h"

static void doRE(char *re, char *text, int flags){
   char *tags[2 * RE_MAX_TAG], *ptr;
   Byte  dfa[2000];
   int   n,s;

   n = sizeof(dfa);
   if( (ptr = regExpCompile(re,dfa,&n)) ){ printf("%s\n",ptr); return; }
   printf("%s --> %d byte DFA\n",re,n);
   if(regExpMatch(dfa,text,flags,tags,0)){
      printf("Match: %s  %s",re,text);
      if(tags[1]){
         strcpy((char *)dfa,tags[1]);
         dfa[tags[RE_MAX_TAG + 1] - tags[1]] = '\0';
         printf("\t\\1 == %s",dfa);
      }
      printf("\n");
   }
}
int main(int argc, char* argv[]){
   doRE("(ab|a)bc","abc",0x0);          // --> match, \1 == "a"  (25 byte DFA)
   doRE("(dog|cat)\\1","catcat",0x0);   // match        (25 byte DFA)
   doRE("(a.c){1,2}","abcadcaec",0x0);  // match, \1 == "adc"  (17 byte DFA)
   doRE("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaaaaa",0x0);     // match  (102 byte DFA)
   doRE("(test\\w*)","it was a testing time",RE_SEARCH);  // \1-->"testing"
   return 0;
}
```
