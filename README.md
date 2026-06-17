# zkRE
Regular expression engine with ERE syntax for ASCII text.

A non-recursive back tracking regular expression engine.

Two C files: zkRE.[ch], thread safe, public domain

Syntax: ERE (extended regular expressions): {}[]()^$.|*+?\ \s\S \w\W \<\> \n (?:)

Limitations: NO support for non-ASCII text, results can differ from recursive engines (eg PCRE), some group closures not supported (eg (a+c)+, (a|b)+)

Example:
```
// clang eg.c zkRE.c
// clang will compile tail call VM, gcc & MSVC can't so get a big switch

#include <stdio.h>
#include <string.h>
#include "zkRE.h"

static void doRE(char *re, char *text){
   char *tags[2 * RE_MAX_TAG], *ptr;
   Byte  dfa[2000];
   int   n,s;

   n = sizeof(dfa);
   if( (ptr = regExpCompile(re,dfa,&n)) ){ printf("%s\n",ptr); return; }
   if(regExpMatch(dfa,text,0x0,tags,0)){
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
   doRE("(ab|a)bc","abc");              // --> match \1 == "a"
   doRE("(dog|cat)\\1","catcat");       // match
   doRE("(a.c){1,2}","abcadc"); // match, \\1 == adc
   doRE("a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaaaaa"); // match

   return 0;
}
```
