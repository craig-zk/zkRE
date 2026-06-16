# zkRE
Regular expression engine with ERE syntax for ASCII text.
A non-recursive back tracking regular expression engine.
Two C files: zkRE.[ch], thread safe, public domain
Syntax: ERE (extended regular expressions): {}[]()^$.|*+?\ \s\S \w\W \<\> \n (?:)
Limitations: NO support for non-ASCII text, results can differ from recursive engines (eg PCRE), some group closures not supported (eg (a+c)+, (a|b)+)
