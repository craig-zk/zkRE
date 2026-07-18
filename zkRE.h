/* 
 * zkRE.h : Regular Expressions, zkRE.c
 * 
 * Public Domain
 * Written 2006,2007,2008-13,2026 Craig Durland craigd@zenkinetic.com
 */

#ifndef __ZKRE_H
#define __ZKRE_H

#include <stdint.h>	// uint

#ifndef Byte
typedef unsigned char	UChar;
typedef uint8_t		Byte;	// or UChar if you don't have <stdint.h>
#endif

#define RE_MAX_TAG	20	// max number of tags: 10 is minimum: "\9"
			// don't just jack this up, big array in zkRE.c

	// regExpMatch flags:
#define RE_SEARCH	 2	// move as needed to match
#define RE_MID		 1	// text points to the middle of bigger text
			// ^ has no match in this text
			// If RE_MID, text[-1] MUST be at valid!

	// RegExp error codes
#define RE_ERROR_BAD_DFA	 1	// DFA null or bad/corrupt (eg open ref)
#define RE_ERROR_DEAD_LOCK	 2	// Not enough threads
#define RE_ERROR_EOM		 3	// Not enough memory
	
   // info on *why* regExpMatch() failed
typedef struct{ int errorCode; char *errorMsg; } ReErrorPacket;

char *regExpCompile(char *pattern, Byte dfa[], int *dfaSz);
int   regExpMatch(Byte *dfa, char *textToSearch, char *tags[],
                unsigned int flags, ReErrorPacket *);
int   regExpSubs(char *src, char *dst, char *tags[]);
void  dfaDump(Byte *dfa, int showSz);

/* tags:  char *tags[2 * RE_MAX_TAG] or 0, these are the "(" ptrs into text
 *   If tags[0..RE_MAX_TAG - 1] != 0 then
 *   To make it easy:    
 *      char **bopat = tags, **eopat = &tags[RE_MAX_TAG];
 *   bopat[n]-->start of \n match, eopat[n]-->end of \n match
 *   if bopat[n]==0, then bopat[>n] *should*==better be ignored.
 *   If tags==0, they are ignored and the search might be faster
 */

#endif // __ZKRE_H
