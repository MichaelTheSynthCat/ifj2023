#include "../../generator.h"
#include "../../strR.h"
#include <stdio.h>
#include <string.h>

int failures = 0;

#define TEST(cond)                                    \
    if (!(cond))                                      \
    {                                                 \
        printf("FAIL[ln %d]\t%s\n", __LINE__, #cond); \
        failures++;                                   \
    }

int main()
{
    DLLstr_Init(&code_main);
    DLLstr_Init(&code_fn);

    str_T s;
    StrInit(&s);

    genUniqVar("GF", "x", &s);
    TEST(strcmp(StrRead(&s), "GF@x$1") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genUniqVar("GF", "x", &s);
    TEST(strcmp(StrRead(&s), "GF@x$2") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genUniqLabel("pow", "while", &s);
    TEST(strcmp(StrRead(&s), "pow&while1") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genUniqLabel("fib", "while", &s);
    TEST(strcmp(StrRead(&s), "fib&while2") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genConstVal(8, "123", &s);
    TEST(strcmp(StrRead(&s), "int@123") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genConstVal(9, "3.14", &s);
    TEST(strcmp(StrRead(&s), "float@0x1.91eb851eb851fp+1") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genConstVal(18, "", &s);
    TEST(strcmp(StrRead(&s), "nil@nil") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genConstVal(10, "a b", &s);
    TEST(strcmp(StrRead(&s), "string@a\\032b") == 0);

    StrDestroy(&s);
    StrInit(&s);

    genConstVal(-50, "true", &s);
    TEST(strcmp(StrRead(&s), "bool@true") == 0);

    StrDestroy(&s);

    genCode("ADD", "GF@x", "GF@y", "LF@z");
    if(parser_inside_fn_def) {
        TEST(strcmp(code_fn.last->string, "ADD GF@x GF@y LF@z") == 0);
    }
    else {
        TEST(strcmp(code_main.last->string, "ADD GF@x GF@y LF@z") == 0);
    }

    genCode("PUSHFRAME", NULL, NULL, NULL);
    if(parser_inside_fn_def) {
        TEST(strcmp(code_fn.last->string, "PUSHFRAME") == 0);
    }
    else {
        TEST(strcmp(code_main.last->string, "PUSHFRAME") == 0);
    }

    genCode("DEFVAR", NULL, "GF@x", NULL);
    if(parser_inside_fn_def) {
        TEST(strcmp(code_fn.last->string, "DEFVAR GF@x") == 0);
    }
    else {
        TEST(strcmp(code_main.last->string, "DEFVAR GF@x") == 0);
    }

    DLLstr_T *variables = malloc(sizeof(DLLstr_T));
    if(variables == NULL) {
        printf("Malloc failed\n");
        return 1;
    }

    DLLstr_Init(variables);
    DLLstr_InsertLast(variables, "LF@x$1");
    DLLstr_InsertLast(variables, "LF@y$2");

    genDefVarsBeforeLoop("&while25", variables);

    if(parser_inside_fn_def) {
        TEST(strcmp(code_fn.last->string, "DEFVAR LF@y$2") == 0);
        TEST(strcmp(code_fn.last->prev->string, "DEFVAR LF@x$1") == 0);
    }
    else {
        TEST(strcmp(code_main.last->string, "DEFVAR LF@y$2") == 0);
        TEST(strcmp(code_main.last->prev->string, "DEFVAR LF@x$1") == 0);
    }

    DLLstr_Dispose(variables);
    free(variables);

    DLLstr_T *variables2 = malloc(sizeof(DLLstr_T));
    DLLstr_Init(variables2);
    DLLstr_InsertLast(variables2, "a");
    DLLstr_InsertLast(variables2, "b");

    parser_inside_fn_def = true;

    genFnDefBegin("main", variables2);

    TEST(strcmp(code_fn.last->prev->prev->prev->prev->prev->prev->string, "LABEL main") == 0);
    TEST(strcmp(code_fn.last->prev->prev->prev->prev->prev->string, "CREATEFRAME") == 0);
    TEST(strcmp(code_fn.last->prev->prev->prev->prev->string, "PUSHFRAME") == 0);
    TEST(strcmp(code_fn.last->prev->prev->prev->string, "DEFVAR LF@a%") == 0);
    TEST(strcmp(code_fn.last->prev->prev->string, "POPS LF@a%") == 0);
    TEST(strcmp(code_fn.last->prev->string, "DEFVAR LF@b%") == 0);
    TEST(strcmp(code_fn.last->string, "POPS LF@b%") == 0);

    DLLstr_Dispose(variables2);
    free(variables2);

    DLLstr_T *variables3 = malloc(sizeof(DLLstr_T));
    DLLstr_Init(variables3);
    DLLstr_InsertLast(variables3, "int@6");
    DLLstr_InsertLast(variables3, "LF@x$1");

    genFnCall("sum", variables3);

    if(parser_inside_fn_def) {
        TEST(strcmp(code_fn.last->prev->prev->string, "PUSH LF@x$1") == 0);
        TEST(strcmp(code_fn.last->prev->string, "PUSH int@6") == 0);
        TEST(strcmp(code_fn.last->string, "CALL sum") == 0);
    }
    else {
        TEST(strcmp(code_main.last->prev->prev->string, "PUSH LF@x$1") == 0);
        TEST(strcmp(code_main.last->prev->string, "PUSH int@6") == 0);
        TEST(strcmp(code_main.last->string, "CALL sum") == 0);
    }

    DLLstr_Dispose(variables3);
    free(variables3);

    DLLstr_Dispose(&code_fn);
    DLLstr_Dispose(&code_main);

    if(failures != 0)
    {
        printf("Total tests failed: %d\n", failures);
    }
    else{
        printf("Everything OK\n");
    }

    return 0;
}