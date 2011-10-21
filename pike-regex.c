#include "vlbdb.h"

int match(char *regexp, char *text);
int matchhere(char *regexp, char *text);
int matchstar(int c, char *regexp, char *text);

typedef int (*matcher_t)(char *);
matcher_t compile_match (char *);
matcher_t compile_matchhere(char *);
matcher_t compile_matchstar(char, matcher_t);

static vlbdb_unit_t * unit;

/* match: search for regexp anywhere in text */
int match(char *regexp, char *text)
{
        if (regexp[0] == '^')
                return matchhere(regexp+1, text);
        do {    /* must look even if string is empty */
                if (matchhere(regexp, text))
                        return 1;
        } while (*text++ != '\0');
        return 0;
}

/* partial-application version of the above */
int match_papply(matcher_t fun, char * text)
{
        do {
                if (fun(text))
                        return 1;
        } while(*text++ != '\0');
        return 0;
}

matcher_t compile_match (char * regexp)
{
        if (regexp[0] == '^')
                return compile_matchhere(regexp+1);

        matcher_t inner = compile_matchhere(regexp);
        return vlbdb_specializef(unit, match_papply, "%p", inner);
}

/* matchhere: search for regexp at beginning of text */
int matchhere(char *regexp, char *text)
{
        if (regexp[0] == '\0')
                return 1;
        if (regexp[1] == '*')
                return matchstar(regexp[0], regexp+2, text);
        if (regexp[0] == '$' && regexp[1] == '\0')
                return *text == '\0';
        if (*text!='\0' && (regexp[0]=='.' || regexp[0]==*text))
                return matchhere(regexp+1, text+1);
        return 0;
}

int constant_papply(int x, char * text)
{
        (void)text;
        return x;
}

int matchend_papply(char * text)
{
        return *text == '\0';
}

int matchchar_papply(char constant, matcher_t k, char * text)
{
        if  (*text!='\0' && (constant=='.' || constant==*text))
                return k(text+1);
        return 0;
}

matcher_t compile_matchhere (char * regexp)
{
        if (regexp[0] == '\0')
                return vlbdb_specializef(unit, constant_papply, "%i", 1);
        if (regexp[1] == '*')
                return compile_matchstar(regexp[0], 
                                         compile_matchhere(regexp+2));
        
        return vlbdb_specializef(unit, matchchar_papply, "%i%p",
                                 regexp[0], compile_matchhere(regexp+1));
}

/* matchstar: search for c*regexp at beginning of text */
int matchstar(int c, char *regexp, char *text)
{
        do {    /* a * matches zero or more instances */
                if (matchhere(regexp, text))
                        return 1;
        } while (*text != '\0' && (*text++ == c || c == '.'));
        return 0;
}

int matchstar_papply (char c, matcher_t k, char * text)
{
        do {
                if (k(text)) return 1;
        } while (*text != '\0' && (*text++ == c || c == '.'));
        return 0;
}

matcher_t compile_matchstar (char c, matcher_t k)
{
        return vlbdb_specializef(unit, matchstar_papply, "%i%p", c, k);
}

#include <stdio.h>

int main ()
{
        unit = vlbdb_unit_from_bitcode("pike-regex.bc", NULL);
        vlbdb_register_all_functions(unit);
        matcher_t matcher = compile_match("as*b");

 /*
  *  (gdb) x/17i 0x1023001e0
  *  0x1023001e0:	inc    %rdi
  *  0x1023001e3:	mov    -0x1(%rdi),%sil
  *  0x1023001e7:	cmp    $0x61,%sil
  *  0x1023001eb:	mov    %rdi,%rdx
  *  0x1023001ee:	jne    0x102300210
  *  0x1023001f4:	mov    (%rdx),%cl
  *  0x1023001f6:	mov    $0x1,%eax
  *  0x1023001fb:	cmp    $0x62,%cl
  *  0x1023001fe:	je     0x10230021e
  *  0x102300204:	inc    %rdx
  *  0x102300207:	cmp    $0x73,%cl
  *  0x10230020a:	je     0x1023001f4
  *  0x102300210:	inc    %rdi
  *  0x102300213:	xor    %eax,%eax
  *  0x102300215:	test   %sil,%sil
  *  0x102300218:	jne    0x1023001e3
  *  0x10230021e:	retq
  */
        char test[] = "assssb";
        char test2[] = "asss";

        printf("%p %i %i\n", matcher, matcher(test), matcher(test2));

        return 0;
}
