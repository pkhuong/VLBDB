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

        vlbdb_register_function(unit, match, 1, NULL);
        vlbdb_register_function(unit, matchhere, 1, NULL);
        vlbdb_register_function(unit, matchstar, 2, NULL);

        char pattern[] = "as*b";
        char test[] = "assssb";
        char test2[] = "asss";

        matcher_t lazy    = vlbdb_specializef(unit, match, "%*p",
                                              (int)sizeof(pattern), pattern);
/*
 *  (gdb) x/19i 0x102300010
 *  0x102300010:	push   %r14
 *  0x102300012:	push   %rbx
 *  0x102300013:	push   %rax
 *  0x102300014:	mov    %rdi,%rbx
 *  0x102300017:	mov    $0x102380010,%r14
 *  0x102300021:	mov    %rbx,%rdi
 *  0x102300024:	callq  *%r14
 *  0x102300027:	mov    %eax,%ecx
 *  0x102300029:	mov    $0x1,%eax
 *  0x10230002e:	test   %ecx,%ecx
 *  0x102300030:	jne    0x102300045
 *  0x102300036:	xor    %eax,%eax
 *  0x102300038:	cmpb   $0x0,(%rbx)
 *  0x10230003b:	lea    0x1(%rbx),%rbx
 *  0x10230003f:	jne    0x102300021
 *  0x102300045:	add    $0x8,%rsp
 *  0x102300049:	pop    %rbx
 *  0x10230004a:	pop    %r14
 *  0x10230004c:	retq   
 *
 *  (gdb) x/2i 0x102380010
 *  0x102380010:	mov    $0x1023000c0,%r10
 *  0x10238001a:	jmpq   *%r10
 *
 *  [...]
 *
 *  0x1023000c0:	push   %r15
 *  0x1023000c2:	push   %r14
 *  0x1023000c4:	push   %r12
 *  0x1023000c6:	push   %rbx
 *  0x1023000c7:	push   %rax
 *  0x1023000c8:	mov    %rdi,%r15
 *  0x1023000cb:	xor    %edx,%edx
 *  0x1023000cd:	mov    $0x61,%dil
 *  0x1023000d0:	mov    $0x102390010,%r8
 *  0x1023000da:	mov    %dil,%bl
 *  0x1023000dd:	mov    $0x1,%eax
 *  0x1023000e2:	test   %bl,%bl
 *  0x1023000e4:	je     0x102300125
 *  0x1023000ea:	cmp    $0x1,%rdx
 *  0x1023000ee:	jne    0x102300131
 *  0x1023000f4:	jmpq   0x102300155
 *  0x1023000f9:	xor    %eax,%eax
 *  0x1023000fb:	test   %cl,%cl
 *  0x1023000fd:	je     0x102300125
 *  0x102300103:	lea    0x1(%rdx),%rsi
 *  0x102300107:	mov    0x1(%r8,%rdx,1),%dil
 *  0x10230010c:	xor    %eax,%eax
 *  0x10230010e:	cmp    $0x2e,%bl
 *  0x102300111:	mov    %rsi,%rdx
 *  0x102300114:	je     0x1023000da
 *  0x10230011a:	cmp    %cl,%bl
 *  0x10230011c:	mov    %rsi,%rdx
 *  0x10230011f:	je     0x1023000da
 *  0x102300125:	add    $0x8,%rsp
 *  0x102300129:	pop    %rbx
 *  0x10230012a:	pop    %r12
 *  0x10230012c:	pop    %r14
 *  0x10230012e:	pop    %r15
 *  0x102300130:	retq   
 *
 * [...]
 *
 *  It's specialised, but still not that good: inlining is useful.
 *
 */
        printf("%p %i %i\n", lazy, lazy(test), lazy(test2));

        matcher_t matcher = compile_match(pattern);

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

        printf("%p %i %i\n", matcher, matcher(test), matcher(test2));

        return 0;
}
