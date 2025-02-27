/** Projekt IFJ2023
 * @file parser.c
 * @brief Syntaktický a sémantický anayzátor
 * @author Michal Krulich (xkruli03)
 * @date 22.11.2023
 */

#include "parser.h"
#include "logErr.h"
#include "exp.h"
#include "generator.h"

token_T* tkn = NULL;

SymTab_T symt;

bool parser_inside_fn_def = false;

/**
 * @brief Názov funkcie, ktorej definícia je práve spracovávaná
*/
static str_T fn_name;

/**
 * @brief Zoznam mien funkcií, pri ktorých treba na konci sémantickej analýzy skontrovať, či boli definované.
*/
static DLLstr_T check_def_fns;

/**
 * @brief Indikuje, či sa parser nachádza vo vnútri cykla.
*/
static bool parser_inside_loop = false;

/**
 * @brief Meno náveštia na najvrchnejší cyklus
*/
static str_T first_loop_label;

/**
 * @brief Zoznam premenných,, ktoré musia byť dekalrované pred prvým nespracovaným cyklom
*/
static DLLstr_T variables_declared_inside_loop;

/**
 * @brief Bola volaná vstavaná funkcia "substring".
*/
static bool bifn_substring_called = false;

/* ----------- PRIVATE FUNKCIE ----------- */

/**
 * @brief Prekonvertuje dátový typ zahrňujúci nil na dátový typ nezahrňujúci nil.
 * @param nil_type dátový typ
 * @return dátový typ nezahrňujúci nil
*/
char convertNilTypeToNonNil(char nil_type) {
    switch (nil_type)
    {
    case SYM_TYPE_INT_NIL:
        return SYM_TYPE_INT;
    case SYM_TYPE_DOUBLE_NIL:
        return SYM_TYPE_DOUBLE;
    case SYM_TYPE_STRING_NIL:
        return SYM_TYPE_STRING;
    default:
        break;
    }
    return nil_type;
}

/**
 * @brief Zistí, či sa v zozname nachádzajú medzi sebou rôzne reťazce (pozor! reťazce "_" sú ignorované), resp. nie je možné nájsť dva totožné.
 * @param list zoznam
 * @param is_unique Ukazateľ na bool, kde sa zapíše false ak existujú dve položky zoznamu s rovnakým reťazcom, inak true
 * @warning Mení aktivitu zoznamu.
 * @return true v prípade úspechu, false v prípade chyby programu
*/
bool listHasUniqueValues(DLLstr_T* list, bool* is_unique) {
    str_T a, b; // reťazce položiek A a B
    StrInit(&a);
    StrInit(&b);

    DLLstr_First(list);
    for (size_t i = 0; DLLstr_IsActive(list); i++) {
        for (size_t j = 0; j < i; j++) {
            DLLstr_Next(list);
        }
        if (!DLLstr_IsActive(list)) break;
        if (!DLLstr_GetValue(list, &a)) return false; // načítanie položky A
        if (strcmp(StrRead(&a), "_") != 0) { // reťazce "_" sú ignorované
            DLLstr_Next(list);
            while (DLLstr_IsActive(list)) { // porovnávanie s položkami napravo od položky A
                if (!DLLstr_GetValue(list, &b)) return false; // načítanie položky B
                if (strcmp(StrRead(&a), StrRead(&b)) == 0) {
                    // totožné reťazce
                    StrDestroy(&a);
                    StrDestroy(&b);
                    *is_unique = false;
                    return true;
                }
                DLLstr_Next(list);
            }
        }
        DLLstr_First(list);
    }

    StrDestroy(&a);
    StrDestroy(&b);
    *is_unique = true;
    return true;
}

/**
 * Vstavané funkcie:
 *      "readString", "readInt", "readDouble",
 *      "write", "Int2Double", "Double2Int",
 *      "length", "substring", "ord", "chr"
 * @brief Zistí či funkcia je vstavaná.
 * @param function_name názov testovanej funkcie
 * @return true ak funkcia je vstavaná, inak false
*/
bool isBuiltInFunction(char* function_name) {
    static size_t total_num_of_bif = 10;
    static char* built_in_functions[] = {
        "readString", "readInt", "readDouble",
        "write", "Int2Double", "Double2Int",
        "length", "substring", "ord", "chr"
    };
    for (size_t i = 0; i < total_num_of_bif; i++) {
        if (strcmp(function_name, built_in_functions[i]) == 0) return true;
    }
    return false;
}

/**
 * Vlastný lokálny rámec si vytvárajú všetky uživateľom definované funkcie a vstavaná funkcia substring.
 * Pri ostatných vstavaných funkciach nie je potrebné popovať lokálny rámec
 *
 * @brief Určí či je potrebné pri volaní určitej funkcie pop-núť lokálny rámec
 * @param function_name Názov testovanej funkcie
 * @return true ak je potrebné zavolať popframe po vykonaní funkcie, inak false
*/
bool shouldPopFrame(char* function_name) {
    return !isBuiltInFunction(function_name) || strcmp(function_name, "substring") == 0;
}

/**
 * @brief Vloží danú signatúru vstavanej funkcie do TS
 * @param name      Názov funkcie
 * @param count_par Počet parametrov
 * @param ret_type  Návratový typ
 * @param par_names Pole názvov parametrov
 * @param par_types Reťazec označujúci dátové typy parametrov
*/
void loadSingleBIFnSig(char* name, size_t count_par, char ret_type,
    char* par_names[], char* par_types) {

    TSData_T* fn = SymTabCreateElement(name);
    fn->type = SYM_TYPE_FUNC;
    StrFillWith(&(fn->codename), name);
    fn->init = true;
    fn->let = false;
    fn->sig = SymTabCreateFuncSig();
    fn->sig->ret_type = ret_type;
    for (size_t i = 0; i < count_par; i++) {
        DLLstr_InsertLast(&(fn->sig->par_names), par_names[i]);
    }
    StrFillWith(&(fn->sig->par_types), par_types);

    SymTabInsertGlobal(&symt, fn);
}


/**
 * @brief Načíta signatúry vstavaných funkcií do TS
*/
void loadBuiltInFunctionSignatures() {
    loadSingleBIFnSig("readString", 0, SYM_TYPE_STRING_NIL, NULL, "");
    loadSingleBIFnSig("readInt", 0, SYM_TYPE_INT_NIL, NULL, "");
    loadSingleBIFnSig("readDouble", 0, SYM_TYPE_DOUBLE_NIL, NULL, "");

    loadSingleBIFnSig("write", 0, SYM_TYPE_VOID, NULL, "");

    char* empty_param[] = { "_" };
    loadSingleBIFnSig("Int2Double", 1, SYM_TYPE_DOUBLE, empty_param, "i");
    loadSingleBIFnSig("Double2Int", 1, SYM_TYPE_INT, empty_param, "d");
    loadSingleBIFnSig("length", 1, SYM_TYPE_INT, empty_param, "s");
    loadSingleBIFnSig("ord", 1, SYM_TYPE_INT, empty_param, "s");
    loadSingleBIFnSig("chr", 1, SYM_TYPE_STRING, empty_param, "i");

    char* substring_par_names[] = { "of", "startingAt", "endingBefore" };
    loadSingleBIFnSig("substring", 3, SYM_TYPE_STRING_NIL, substring_par_names, "sii");
}

/**
 * Táto funkcia je používaná pre generovanie inštrukcií pre vstavané funkcie:
 *      "write", "Int2Double", "Double2Int", "length", "ord", "chr"
 *
 * @brief Vygeneruje cieľovú inštrukciu príslušnej vstavanej funkcie s argumentom v cieľovom kóde
 * @param bif_name
 * @param arg_codename
 * @return true ak bif_name je jedna z vstavaných funkcií
 *          {"write", "Int2Double", "Double2Int", "length", "ord", "chr"},
 *          inak false
*/
bool biFnGenInstruction(char* bif_name, char* arg_codename) {
    if (strcmp(bif_name, "write") == 0) {
        /*
            WRITE <arg>
        */
        genCode(INS_WRITE, arg_codename, NULL, NULL);
        return true;
    }
    else if (strcmp(bif_name, "Int2Double") == 0) {
        /*
            PUSHS <arg>
            INT2FLOATS
        */
        genCode(INS_PUSHS, arg_codename, NULL, NULL);
        genCode(INS_INT2FLOATS, NULL, NULL, NULL);
        return true;
    }
    else if (strcmp(bif_name, "Double2Int") == 0) {
        /*
            PUSHS <arg>
            FLOAT2INTS
        */
        genCode(INS_PUSHS, arg_codename, NULL, NULL);
        genCode(INS_FLOAT2INTS, NULL, NULL, NULL);
        return true;
    }
    else if (strcmp(bif_name, "length") == 0) {
        /*
            STRLEN GF@!tmp1 <arg>
            PUSHS GF@!tmp1
        */
        genCode(INS_STRLEN, VAR_TMP1, arg_codename, NULL);
        genCode(INS_PUSHS, VAR_TMP1, NULL, NULL);
        return true;
    }
    else if (strcmp(bif_name, "ord") == 0) {
        /*
            MOVE    GF@!tmp1    int@0
            STRLEN  GF@!tmp2    <arg>
            JUMPIFEQ  <label_empty_str> GF@!tmp2 int@0
            STR2INT GF@!tmp1    <arg>
            LABEL     <label_empty_str>
            PUSHS   GF@!tmp1
        */
        str_T label_empty_string;
        StrInit(&label_empty_string);
        genUniqLabel(StrRead(&fn_name), "ord", &label_empty_string);

        genCode(INS_MOVE, VAR_TMP1, "int@0", NULL);
        genCode(INS_STRLEN, VAR_TMP2, arg_codename, NULL);
        genCode("JUMPIFEQ", StrRead(&label_empty_string), VAR_TMP2, "int@0");
        genCode("STRI2INT", VAR_TMP1, arg_codename, "int@0");
        genCode(INS_LABEL, StrRead(&label_empty_string), NULL, NULL);
        genCode(INS_PUSHS, VAR_TMP1, NULL, NULL);
        StrDestroy(&label_empty_string);
        return true;
    }
    else if (strcmp(bif_name, "chr") == 0) {
        /*
            PUSHS <arg>
            INT2CHARS
        */
        genCode(INS_PUSHS, arg_codename, NULL, NULL);
        genCode("INT2CHARS", NULL, NULL, NULL);
        return true;
    }
    return false;
}

/**
 * Stav tkn:
 *  - pred volaním: COLON alebo ARROW
 *  - po volaní:    NULL
 *
 * @brief Pravidlo pre spracovanie dátového typu, zapíše do data_type spracovaný typ
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseDataType(char* data_type) {
    /*
        39. <TYPE> -> Integer <QUESTMARK>
        40. <TYPE> -> Double <QUESTMARK>
        41. <TYPE> -> String <QUESTMARK>
    */
    TRY_OR_EXIT(nextToken());
    switch (tkn->type)
    {
    case INT_TYPE:
        *data_type = SYM_TYPE_INT;
        break;
    case DOUBLE_TYPE:
        *data_type = SYM_TYPE_DOUBLE;
        break;
    case STRING_TYPE:
        *data_type = SYM_TYPE_STRING;
        break;
    default:
        logErrSyntax(tkn, "data type");
        return SYN_ERR;
        break;
    }
    /*
        42. <QUESTMARK> -> ?
        43. <QUESTMARK> -> €
    */
    TRY_OR_EXIT(nextToken());
    if (tkn->type == QUEST_MARK) { // dátový typ zahrnǔjúci nil: <TYP>?
        switch (*data_type)
        {
        case SYM_TYPE_INT:
            *data_type = SYM_TYPE_INT_NIL;
            break;
        case SYM_TYPE_DOUBLE:
            *data_type = SYM_TYPE_DOUBLE_NIL;
            break;
        case SYM_TYPE_STRING:
            *data_type = SYM_TYPE_STRING_NIL;
            break;
        default:
            break;
        }
    }
    else { // za dátovým typom nebol token '?', token je vrátený naspäť
        saveToken();
    }
    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: token termu
 *  - po volaní:    token termu
 *
 * @brief Pravidlo pre spracovanie termu
 * @param term_type Dátový typ termu
 * @param term_codename Tvar termu v cieľovom kóde
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseTerm(char* term_type, str_T* term_codename) {
    switch (tkn->type)
    {
    case ID:; // premenná
        TSData_T* variable = SymTabLookup(&symt, StrRead(&(tkn->atr)));
        if (variable == NULL) {
            // v TS nie je záznam s daným identifikátorom => nedeklarovaná premenná
            logErrSemantic(tkn, "%s was undeclared", StrRead(&(tkn->atr)));
            return SEM_ERR_UNDEF;
        }
        if (variable->type == SYM_TYPE_FUNC) {
            // identifikátor označuje funkciu
            logErrSemantic(tkn, "%s is a function", StrRead(&(tkn->atr)));
            return SEM_ERR_RETURN;
        }
        if (!(variable->init)) {
            // premenná nebola inicializovaná
            logErrSemantic(tkn, "%s was uninitialized", StrRead(&(tkn->atr)));
            return SEM_ERR_UNDEF;
        }
        *term_type = variable->type;
        StrFillWith(term_codename, StrRead(&(variable->codename)));
        break;
    case INT_CONST: // konštanty
        *term_type = SYM_TYPE_INT;
        genConstVal(INT_CONST, StrRead(&(tkn->atr)), term_codename);
        break;
    case DOUBLE_CONST:
        *term_type = SYM_TYPE_DOUBLE;
        genConstVal(DOUBLE_CONST, StrRead(&(tkn->atr)), term_codename);
        break;
    case STRING_CONST:
        *term_type = SYM_TYPE_STRING;
        genConstVal(STRING_CONST, StrRead(&(tkn->atr)), term_codename);
        break;
    case NIL:
        *term_type = SYM_TYPE_NIL;
        genConstVal(NIL, "", term_codename);
        break;
    default:
        // nie je to term
        logErrSyntax(tkn, "term");
        return SYN_ERR;
        break;
    }
    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: prvý token argumentu
 *  - po volaní:    NULL alebo term
 *
 * Generuje cieľový kód pre vstavané funkcie:
 *      "write", "Int2Double", "Double2Int", "length", "ord", "chr",
 *  v ostatných prípadoch negeneruje inštrukcie, ale zapisuje argumenty v cieľovom kóde cez ukazateľ na inicializovaný zoznam.
 *
 * @brief Pravidlo pre spracovanie argumentu volanej funkcie, pričom cez svoje parametre vráti informácie o načítanom argumente.
 * @param par_name  načítaný názov parametru
 * @param term_type dátový typ termu
 * @param bif_name Ak volaná funkcia nie je vstavaná, potom NULL, inak názov vstavanej funkcie.
 * @param used_args Získané argumenty funkcie v cieľovom kóde.
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseFnArg(str_T* par_name, char* term_type, char* bif_name,
    DLLstr_T* used_args) {
    /*
        18. <PAR_IN> -> id : term
        19. <PAR_IN> -> term
    */
    str_T arg_codename; // Tvar argumentu v cieľovom kóde
    StrInit(&arg_codename);
    StrFillWith(par_name, StrRead(&(tkn->atr)));
    switch (tkn->type)
    {
    case ID:
        TRY_OR_EXIT(nextToken());
        if (tkn->type == COMMA || tkn->type == BRT_RND_R) {
            // 19. <PAR_IN> -> term
            // term je premenná a funkcia nemá názov pre parameter
            TSData_T* variable = SymTabLookup(&symt, StrRead(par_name));
            if (variable == NULL) {
                // v TS nie je záznam s daným identifikátorom => nedeklarovaná premenná
                logErrSemantic(tkn, "%s was undeclared", StrRead(par_name));
                return SEM_ERR_UNDEF;
            }
            if (variable->type == SYM_TYPE_FUNC) {
                // identifikátor označuje funkciu
                logErrSemantic(tkn, "%s is a function", StrRead(par_name));
                return SEM_ERR_RETURN;
            }
            if (!(variable->init)) {
                // premenná nebola inicializovaná
                logErrSemantic(tkn, "%s was uninitialized", StrRead(par_name));
                return SEM_ERR_UNDEF;
            }
            *term_type = variable->type;
            StrFillWith(par_name, "_"); // funkcia má vynechaný názvo pre parameter
            StrFillWith(&arg_codename, StrRead(&(variable->codename)));
            saveToken();
        }
        // inak prvý token musí byť názov parametra
        else if (tkn->type == COLON) {
            // 18. <PAR_IN> -> id : term
            TRY_OR_EXIT(nextToken());
            TRY_OR_EXIT(parseTerm(term_type, &arg_codename));
        }
        else {
            logErrSyntax(tkn, "',' or ':'");
            return SYN_ERR;
        }
        break;
    case INT_CONST: // 19. <PAR_IN> -> term, kde term je konštanta
    case DOUBLE_CONST:
    case STRING_CONST:
    case NIL:
        StrFillWith(par_name, "_");
        TRY_OR_EXIT(parseTerm(term_type, &arg_codename));
        break;
    default:
        logErrSyntax(tkn, "parameter identifier or term");
        return SYN_ERR;
    }

    // generácia inštrukcií pre niektoré vstavané funkcie
    if (!biFnGenInstruction(bif_name == NULL ? "" : bif_name, StrRead(&arg_codename))) {
        // inak je predaný identifikátor argumentu v cieľovom kóde naspäť volajúcemu
        DLLstr_InsertLast(used_args, StrRead(&arg_codename));
    }
    StrDestroy(&arg_codename);

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: BRT_RND_L '('
 *  - po volaní:    BRT_RND_R ')'
 *
 * @brief Pravidlo pre spracovanie argumentov volanej funkcie
 * @param defined Značí či bola funkcia definovaná
 * @param called_before Značí, či už bola daná funkcia predtým volaná
 * @param sig Signatúra funkcie, ktorá je volaná
 * @param bif_name Ak volaná funkcia nie je vstavaná, potom NULL, inak názov vstavanej funkcie.
 * @param used_args Získané argumenty funkcie v cieľovom kóde.
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseFnCallArgs(bool defined, bool called_before, func_sig_T* sig,
    char* bif_name, DLLstr_T* used_args) {
    /*
        13. <PAR_LIST> -> id : term <PAR_IN_NEXT>
        14. <PAR_LIST> -> term <PAR_IN_NEXT>
        15. <PAR_LIST> -> €
        16. <PAR_IN_NEXT> -> , <PAR_IN> <PAR_IN_NEXT>
        17. <PAR_IN_NEXT> -> €
    */

    size_t loaded_args = 0; // počet načítaných argumentov

    char arg_type;  // typ aktuálne načítaného argumentu
    str_T par_name, temp; // názov parametru, pomocné reťazcové úložisko
    StrInit(&par_name);
    StrInit(&temp);
    DLLstr_First(&(sig->par_names));

    // Špeciálny prístup sémantickej kontroly pri vstavanej funkcii "write",
    // pretože môže mať variabilný počet argumentov.
    bool write_function = false;
    if (bif_name != NULL) {
        if (strcmp(bif_name, "write") == 0) write_function = true;
    }

    TRY_OR_EXIT(nextToken());
    while (tkn->type != BRT_RND_R)
        // ( argument1, argument2, ..., argumentN )
    {
        // pred každým argumentom, okrem prvého, musí nasledovať čiarka
        if (loaded_args > 0) { // ??? dat pred kontrolu poctu argumentov ???
            if (tkn->type != COMMA) {
                logErrSyntax(tkn, "comma");
                return SYN_ERR;
            }
            TRY_OR_EXIT(nextToken());
        }
        else {
            /*  Táto extra syntaktická kokontrola zaručuje vyššiu prioritu
                syntaktickej chyby pred sémantickou. */
            switch (tkn->type)
            {
            case ID:
            case INT_CONST:
            case DOUBLE_CONST:
            case STRING_CONST:
            case NIL:
                break;
            default:
                logErrSyntax(tkn, "term or parameter name");
                return SYN_ERR;
                break;
            }
        }

        // kontrola, či nie je funkcia volaná s viacerými argumentami
        if ((defined || called_before) && !write_function) {
            if (strlen(StrRead(&(sig->par_types))) <= loaded_args) {
                logErrSemantic(tkn, "too many arguments in function call");
                return SEM_ERR_FUNC;
            }
        }

        TRY_OR_EXIT(parseFnArg(&par_name, &arg_type, bif_name, used_args)); // príkaz na spracovanie jedného argumentu

        // sémantická kontrola argumentu
        if (write_function) { // všetky argumenty vo funkcií "write" nemajú názov parametra
            if (strcmp(StrRead(&par_name), "_") != 0) {
                logErrSemantic(tkn, "function \"write\" does not use parameter names");
                return SEM_ERR_FUNC;
            }
        }
        else if (defined || called_before) {
            // kontrola dátového typu argumentu s predpisom funkcie
            if (!isCompatibleAssign(StrRead(&(sig->par_types))[loaded_args], arg_type)) {
                logErrSemantic(tkn, "different type in function call");
                return SEM_ERR_FUNC;
            }

            // kontrola názvu parametra s predpisom
            DLLstr_GetValue(&(sig->par_names), &temp);
            if (strcmp(StrRead(&par_name), StrRead(&temp)) != 0) {
                logErrSemantic(tkn, "different parameter name");
                return SEM_ERR_FUNC;
            }
        }
        else {  // funkcia ešte nebola volaná alebo definovaná, preto sa zapíšu informácie z jej prvého volania do predpisu
            switch (arg_type)
            {
                /*  Aj keď je predávaný argument typu nezahrňujúci nil, môže
                    funkcia mať v neskošej definícii v predpise typ zahrňujúci nil.
                */
            case SYM_TYPE_INT:
                StrAppend(&(sig->par_types), SYM_TYPE_INT_NIL);
                break;
            case SYM_TYPE_DOUBLE:
                StrAppend(&(sig->par_types), SYM_TYPE_DOUBLE_NIL);
                break;
            case SYM_TYPE_STRING:
                StrAppend(&(sig->par_types), SYM_TYPE_STRING_NIL);
                break;
            case SYM_TYPE_NIL:
                StrAppend(&(sig->par_types), SYM_TYPE_UNKNOWN);
                break;
            default:
                StrAppend(&(sig->par_types), arg_type);
                break;
            }

            // zápis názvu parametru
            DLLstr_InsertLast(&(sig->par_names), StrRead(&par_name));
        }

        TRY_OR_EXIT(nextToken());
        loaded_args++;
        DLLstr_Next(&(sig->par_names));
    }

    if ((defined || called_before) && !write_function) { // kontrola, či bola funkcia zavolaná so správnym počtom argumentov
        if (strlen(StrRead(&(sig->par_types))) != loaded_args) {
            logErrSemantic(tkn, "different count of arguments in function call");
            return SEM_ERR_FUNC;
        }
    }

    // dealokácia pomocných reťazcov
    StrDestroy(&par_name);
    StrDestroy(&temp);

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: identifikátor volanej funkcie
 *  - po volaní:    BRT_RND_R
 *
 * Generuje cieľový kód pre uživateľom definované funkcie a vstavané funkcie.
 *
 * @brief Pravidlo pre spracovanie volania funkcie
 * @param result_type návratový typ funkcie
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseFnCall(char* result_type) {
    // id ( <PAR_LIST> )

    // získanie informácii o funkcii z TS
    TSData_T* fn = SymTabLookupGlobal(&symt, StrRead(&(tkn->atr)));
    bool called_before = fn != NULL;
    bool built_in_fn = false;   // funkcia je vstavaná
    if (fn == NULL) // funkcia nebola definovaná a ani volaná
    {
        // vytvorí sa o nej záznam do TS
        fn = SymTabCreateElement(StrRead(&(tkn->atr)));
        if (fn == NULL) return COMPILER_ERROR;
        StrFillWith(&(fn->codename), StrRead(&(tkn->atr)));
        fn->type = SYM_TYPE_FUNC;
        fn->sig = SymTabCreateFuncSig();
        if (fn->sig == NULL) {
            return COMPILER_ERROR;
        }
        fn->sig->ret_type = SYM_TYPE_UNKNOWN;
        fn->let = false;
        fn->init = false;
        SymTabInsertGlobal(&symt, fn);

        // poznačiť názov funkcie do zoznamu nedefinovaných funkcií, pre kontrolu na koniec
        DLLstr_InsertLast(&check_def_fns, fn->id);
    }
    else {
        built_in_fn = isBuiltInFunction(fn->id);
    }

    TRY_OR_EXIT(nextToken());
    if (tkn->type != BRT_RND_L) {
        logErrSyntax(tkn, "'('");
        return SYN_ERR;
    }

    // spracovanie argumentov funkcie
    DLLstr_T args_codenames;
    DLLstr_Init(&args_codenames);
    TRY_OR_EXIT(parseFnCallArgs(fn->init, called_before, fn->sig, built_in_fn ? fn->id : NULL, &args_codenames));

    if (strcmp(fn->id, "substring") == 0) {
        // bude potrebné vložiť kód funkcie substring
        bifn_substring_called = true;
    }

    // Generovanie cieľového kódu
    if (!built_in_fn || strcmp(fn->id, "substring") == 0) {
        // generovanie vloženia argumentov na zásobník a volania funkcie
        genFnCall(fn->id, &args_codenames);
    }
    // špeciálne prípady generovania kódu pri týchto vstavaných funkciách
    else if (strcmp(fn->id, "readString") == 0) {
        genCode(INS_READ, VAR_TMP1, "string", NULL);
        genCode(INS_PUSHS, VAR_TMP1, NULL, NULL);
    }
    else if (strcmp(fn->id, "readInt") == 0) {
        genCode(INS_READ, VAR_TMP1, "int", NULL);
        genCode(INS_PUSHS, VAR_TMP1, NULL, NULL);
    }
    else if (strcmp(fn->id, "readDouble") == 0) {
        genCode(INS_READ, VAR_TMP1, "float", NULL);
        genCode(INS_PUSHS, VAR_TMP1, NULL, NULL);
    }
    DLLstr_Dispose(&args_codenames);

    *result_type = fn->sig->ret_type;

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: ´=´
 *  - po volaní:    NULL
 *
 * Generuje cieľový kód priradenia:
 *      { kód vygenerovaný vo funkciach parseFnCall alebo parseExpression }
 *      POPS <identifikátor premennej v IFJcode>
 *
 * @brief Pravidlo pre spracovanie priradenia
 * @param result_type Dátový typ výsledku
 * @param result_codename Identifikátor premennej v cieľovom kóde kam sa má uložiť výsledok
 * @param target_type Dátový typ premennej, ktorej je hodnota priraďovaná. Slúži len pre potreby implicitnej konverzie literálu Int na Double
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseAssignment(char* result_type, char* result_codename, char target_type) {
    bool possible_implicit_int_conversion = false;
    TRY_OR_EXIT(nextToken());
    token_T* first_tkn;
    switch (tkn->type) // treba rozlíšiť volanie funkcie a výraz
    {
    case BRT_RND_L:
    case INT_CONST:
    case DOUBLE_CONST:
    case STRING_CONST:
    case NIL:
        // 8. <ASSIGN> -> exp
        TRY_OR_EXIT(parseExpression(result_type, &possible_implicit_int_conversion));
        break;
    case ID:
        /*  Identifikátor môže byť názov premennej vo výraze alebo názov funkcie.
            Preto sa treba pozrieť na ďalší token. Ak ďalší token je ľavá zátvorka,
            potom je to volanie funkcie.
        */
        first_tkn = tkn;
        tkn = NULL; // ! Bez tohoto by bol first_tkn uvoľnený v nextToken().
        TRY_OR_EXIT(nextToken());
        if (tkn->type == BRT_RND_L) {
            // 9. <ASSIGN> -> id ( <PAR_LIST> )
            saveToken();
            tkn = first_tkn;
            bool popframe = shouldPopFrame(StrRead(&(tkn->atr))); // je potrebné zbaviť sa lokálneho rámca vytvoreného volanou funkciou
            TRY_OR_EXIT(parseFnCall(result_type));
            if (popframe) genCode(INS_POPFRAME, NULL, NULL, NULL);
        }
        else {
            saveToken();
            tkn = first_tkn;
            TRY_OR_EXIT(parseExpression(result_type, &possible_implicit_int_conversion));
        }
        break;
    default:
        logErrSyntax(tkn, "assignment or function call");
        return SYN_ERR;
    }

    if (possible_implicit_int_conversion && *result_type == SYM_TYPE_INT &&
        (target_type == SYM_TYPE_DOUBLE || target_type == SYM_TYPE_DOUBLE_NIL)) {
        // vo výraze sú celočíselné literály a výsledok má byť priradený do dátového typu Double(?)
        // musí byť vykonaná implicitná konverzia
        genCode(INS_INT2FLOATS, NULL, NULL, NULL);
        *result_type = SYM_TYPE_DOUBLE;
    }

    genCode(INS_POPS, result_codename, NULL, NULL); // priradenie výsledku do premennej

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: LET alebo VAR
 *  - po volaní:    NULL
 *
 * Generuje cieľový kód priradenia:
 *      DEFVAR <identifikátor premennej v IFJcode>
 *      { kód vygenerovaný vo funkcii parseAssignment }
 *
 * @brief Pravidlo pre spracovanie deklarácie/definície premennej
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseVariableDecl() {
    /*
        2. <STAT> -> let id <DEF_VAR> <STAT>
        3. <STAT> -> var id <DEF_VAR> <STAT>
    */
    bool let = tkn->type == LET ? true : false; // (ne)modifikovateľná premenná

    TRY_OR_EXIT(nextToken());
    if (tkn->type != ID) { // musí nasledovať názov premennej
        logErrSyntax(tkn, "identifier");
        return SYN_ERR;
    }

    // kontrola, či premenná s daným identifikátorom už nebola deklarovaná v tomto bloku
    if (SymTabLookupLocal(&symt, StrRead(&(tkn->atr))) != NULL) {
        logErrSemantic(tkn, "%s is already declared in this block", StrRead(&(tkn->atr)));
        return SEM_ERR_REDEF;
    }
    // zápis novej premennej do TS
    TSData_T* variable = SymTabCreateElement(StrRead(&(tkn->atr)));
    if (variable == NULL)
    {
        logErrCompilerMemAlloc();
        return COMPILER_ERROR;
    }
    variable->init = false;
    variable->let = let;
    variable->sig = NULL;
    variable->type = SYM_TYPE_UNKNOWN;

    // Generovanie cieľového kódu
    genUniqVar(parser_inside_fn_def ? "LF" : "GF", variable->id, &(variable->codename));
    if (parser_inside_loop) { // deklarácia premennej musí byť pred najvrchnejším cyklom
        DLLstr_InsertLast(&variables_declared_inside_loop, StrRead(&(variable->codename)));
    }
    else {
        // deklarácia premennej nie je v cykle, čiže môže byť hneď zapísaná
        genCode(INS_DEFVAR, StrRead(&(variable->codename)), NULL, NULL);
    }

    // ďalej musí nasledovať dátový typ alebo priradenie
    TRY_OR_EXIT(nextToken());
    switch (tkn->type)
    {
    case COLON: // 4. <DEF_VAR> -> : <TYPE> <INIT_VAL>
        // zistenie dátového typu
        TRY_OR_EXIT(parseDataType(&(variable->type)));

        // treba zistiť, či za deklaráciou dátového typu sa ešte nenachádza priradenie/inicializácia
        TRY_OR_EXIT(nextToken());
        if (tkn->type == ASSIGN) {
            // 6. <INIT_VAL> -> = <ASSIGN>
            char assign_type = SYM_TYPE_UNKNOWN;
            TRY_OR_EXIT(parseAssignment(&assign_type, StrRead(&(variable->codename)), variable->type));
            variable->init = true;

            // kontrola výsledného typu výrazu s deklarovaným dátovým typom
            if (!isCompatibleAssign(variable->type, assign_type)) {
                logErrSemantic(tkn, "incompatible data types");
                return SEM_ERR_TYPE;
            }
        }
        else { // bez počiatočnej inicializácie
            // <INIT_VAL> -> €
            switch (variable->type)
            {
            case SYM_TYPE_INT_NIL:
            case SYM_TYPE_DOUBLE_NIL:
            case SYM_TYPE_STRING_NIL:
                // implicitne inicializované na nil v prípade dátového typu zahrňujúceho nil
                variable->init = true;
                genCode(INS_MOVE, StrRead(&(variable->codename)), "nil@nil", NULL);
                break;
            default:
                break;
            }
            saveToken();
        }
        break;
    case ASSIGN: // 5. <DEF_VAR> -> = <ASSIGN>
        TRY_OR_EXIT(parseAssignment(&(variable->type), StrRead(&(variable->codename)), SYM_TYPE_UNKNOWN));
        variable->init = true;

        if (variable->type == SYM_TYPE_VOID) { // priradenie hodnoty z void funkcie
            logErrSemantic(tkn, "void function does not return a value");
            return SEM_ERR_TYPE;
        }
        else if (variable->type == SYM_TYPE_NIL) {  // len z nil nie je možné odvodiť typ
            logErrSemantic(tkn, "could not deduce the data type");
            return SEM_ERR_UKN_T;
        }
        break;
    default:
        logErrSyntax(tkn, "':' or '='");
        return SYN_ERR;
        break;
    }

    // vloženie záznamu o premennej do TS
    SymTabInsertLocal(&symt, variable);

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: BRT_CUR_L '{'
 *  - po volaní:    BRT_CUR_R '}'
 *
 * @brief Pravidlo pre spracovanie bloku kódu
 * @param had_return Ukazateľ na bool, ktorý bude značiť, či sa v bloku nachádzal príkaz return. Ak NULL, nič sa nezapisuje.
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseStatBlock(bool* had_return) {
    // 11. <STAT> -> { <STAT> } <STAT>

    // počiatočná ľavá zátvorka
    if (tkn->type != BRT_CUR_L) {
        logErrSyntax(tkn, "'{'");
        return SYN_ERR;
    }

    // vytvorenie nového lokálneho bloku v TS
    SymTabAddLocalBlock(&symt);

    TRY_OR_EXIT(nextToken());
    while (tkn->type != BRT_CUR_R)
    {
        TRY_OR_EXIT(parse());
        TRY_OR_EXIT(nextToken());
    }

    // vrátiť prítomnosť príkazu return a vymazať lokálny blok z TS
    if (had_return != NULL) *had_return = SymTabCheckLocalReturn(&symt);
    SymTabRemoveLocalBlock(&symt);

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: BRT_RND_L
 *  - po volaní:    BRT_RND_R
 *
 * @brief Pravidlo pre spracovanie definície parametrov funkcie, končí načítaním pravej zátvorky
 * @param compare_and_update Keď true: režim porovnávania s aktuálne zaznamenanou signatúrou volania, inak vytvára novú signatúru.
 * @param sig ukazateľ na dátovú štruktúru signatúry funkcie, kam sa zapíšu zistené informácie
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseFunctionSignature(bool compare_and_update, func_sig_T* sig) {
    /*
        21. <FN_SIG> -> id id : <TYPE> <FN_PAR_NEXT>
        22. <FN_SIG> -> _ id : <TYPE> <FN_PAR_NEXT>
        23. <FN_SIG> -> €
    */

    size_t loaded_params = 0; // počet načítaných parametrov
    DLLstr_First(&(sig->par_names));
    DLLstr_First(&(sig->par_ids));

    str_T tmp; // pomocný reťazec
    StrInit(&tmp);

    TRY_OR_EXIT(nextToken());
    while (tkn->type != BRT_RND_R)
    {
        /*
            24. <FN_PAR_NEXT> -> , <FN_PAR> <FN_PAR_NEXT>
            25. <FN_PAR_NEXT> -> €
            26. <FN_PAR> -> id id : <TYPE>
            27. <FN_PAR> -> _ id : <TYPE>
            28. <FN_PAR> -> id _ : <TYPE>
            29. <FN_PAR> -> _ _ : <TYPE>
        */

        if (loaded_params > 0) { // pred každým parametrom, okrem prvého, musí nasledovať čiarka
            if (tkn->type != COMMA) {
                logErrSyntax(tkn, "comma");
                return SYN_ERR;
            }
            TRY_OR_EXIT(nextToken());
        }

        if (tkn->type == ID || tkn->type == UNDERSCORE) { // názov parametra musí byť identifikátor alebo '_'

            if (compare_and_update) { // funkcia bola volaná pred jej definíciou
                /* Kontrola počtu parametrov s počtom argumentov v prvom volaní. */
                if (strlen(StrRead(&(sig->par_types))) <= loaded_params) { // funkcia bola volaná s menším počtom argumentov
                    logErrSemantic(tkn, "different number of parameters in function definition and first call");
                    return SEM_ERR_FUNC;
                }

                /*  Treba skontrolovať názov parametra s prvým volaním. */
                DLLstr_GetValue(&(sig->par_names), &tmp);
                if (strcmp(StrRead(&tmp), StrRead(&(tkn->atr))) != 0) {
                    logErrSemantic(tkn, "different parameter name in definition and first call");
                    return SEM_ERR_FUNC;
                }
            }
            else { // zápis názvu parametra do predpisu funkcie
                DLLstr_InsertLast(&(sig->par_names), StrRead(&(tkn->atr)));
            }
        }
        else {
            logErrSyntax(tkn, "parameter name or underscore");
            return SYN_ERR;
        }

        // nasleduje identifikátor parametra vo vnútri funkcie
        TRY_OR_EXIT(nextToken());
        if (tkn->type == ID) {
            compare_and_update ? DLLstr_GetValue(&(sig->par_names), &tmp) : DLLstr_GetLast(&(sig->par_names), &tmp);

            // názov parametra a identifikátor parametra sa musia líšiť
            if (strcmp(StrRead(&tmp), StrRead(&(tkn->atr))) == 0) {
                logErrSemantic(tkn, "parameter name and identifier must be different");
                return SEM_ERR_OTHER;
            }
            DLLstr_InsertLast(&(sig->par_ids), StrRead(&(tkn->atr)));
        }
        else if (tkn->type == UNDERSCORE) {
            DLLstr_InsertLast(&(sig->par_ids), "_");
        }
        else {
            logErrSyntax(tkn, "parameter identifier");
            return SYN_ERR;
        }

        // nasleduje dátový typ parametra
        TRY_OR_EXIT(nextToken()); // dvojbodka
        if (tkn->type != COLON) {
            logErrSyntax(tkn, "':'");
            return SYN_ERR;
        }
        char data_type;
        TRY_OR_EXIT(parseDataType(&data_type));
        if (compare_and_update) { // funkcia bola volaná pred jej definíciou
            // kontrola typu v definícii s typom argumentu v prvom volaní
            bool same_type = true;
            char type_before = StrRead(&(sig->par_types))[loaded_params];
            switch (data_type)
            {
            case SYM_TYPE_INT:
            case SYM_TYPE_INT_NIL:
                same_type = type_before == SYM_TYPE_INT || type_before == SYM_TYPE_INT_NIL;
                break;
            case SYM_TYPE_DOUBLE:
            case SYM_TYPE_DOUBLE_NIL:
                same_type = type_before == SYM_TYPE_DOUBLE || type_before == SYM_TYPE_DOUBLE_NIL;
                break;
            case SYM_TYPE_STRING:
            case SYM_TYPE_STRING_NIL:
                same_type = type_before == SYM_TYPE_STRING || type_before == SYM_TYPE_STRING_NIL;
                break;
            default:
                break;
            }
            if (type_before == SYM_TYPE_UNKNOWN) same_type = true;
            if (!same_type) {
                logErrSemanticFn(StrRead(&fn_name), "parameter types does not correspond to previous call");
                return SEM_ERR_FUNC;
            }
            sig->par_types.data[loaded_params] = data_type; // zapíše sa dátový typ zistení z definície
        }
        else {
            StrAppend(&(sig->par_types), data_type);
        }

        loaded_params++;
        DLLstr_Next(&(sig->par_names));
        DLLstr_Next(&(sig->par_ids));
        TRY_OR_EXIT(nextToken());
    }

    if (compare_and_update) { // kontrola počtu parametrov v definícii s počtom argumentov v prvom volaní
        if (strlen(StrRead(&(sig->par_types))) != loaded_params) {
            logErrSemantic(tkn, "different count of parameters in function definition and first call");
            return SEM_ERR_FUNC;
        }
    }

    // kontrola názvov rôznych názvov a identifikátorov parametrov 
    bool unique_names;
    if (!listHasUniqueValues(&(sig->par_names), &unique_names)) return COMPILER_ERROR;
    if (!unique_names) {
        logErrSemanticFn(StrRead(&fn_name), "parameter names don't have different names");
        return SEM_ERR_OTHER;
    }
    if (!listHasUniqueValues(&(sig->par_ids), &unique_names)) return COMPILER_ERROR;
    if (!unique_names) {
        logErrSemanticFn(StrRead(&fn_name), "parameter identifiers don't have different names");
        return SEM_ERR_OTHER;
    }

    // dealokácia pomocných štruktúr
    StrDestroy(&tmp);

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: FUNC
 *  - po volaní:    BRT_CUR_R
 *
 * Generuje cieľový kód uživateľskej funkcie:
 *      LABEL <fn_label>
 *      ...
 *      RETURN
 *
 * @brief Pravidlo pre spracovanie definície funkcie
 * @details Očakáva, že v globálnej premennej tkn je už načítaný token FUNC
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseFunction() {
    // 20. <STAT> -> func id ( <FN_SIG> ) <FN_RET_TYPE> { <STAT> } <STAT>
    bool code_inside_fn_def = parser_inside_fn_def;
    parser_inside_fn_def = true; // parser sa nachádza v definícií funkcie

    TRY_OR_EXIT(nextToken()); // názov funkcie
    if (tkn->type != ID) {
        logErrSyntax(tkn, "function identifier");
        return SYN_ERR;
    }

    TSData_T* fn = SymTabLookupGlobal(&symt, StrRead(&(tkn->atr)));
    bool already_called = fn != NULL; // funkcia bola volaná pred jej definíciou, pretože existuje záznam v TS
    if (fn == NULL) {
        // vytvorenie záznamu o funkcii do TS
        fn = SymTabCreateElement(StrRead(&(tkn->atr)));
        if (fn == NULL) return COMPILER_ERROR;
        SymTabInsertGlobal(&symt, fn);
        StrFillWith(&(fn->codename), StrRead(&(tkn->atr)));
        fn->type = SYM_TYPE_FUNC;
        fn->sig = SymTabCreateFuncSig();
        if (fn->sig == NULL) {
            return COMPILER_ERROR;
        }
        fn->sig->ret_type = SYM_TYPE_VOID;
    }
    else {
        if (fn->type != SYM_TYPE_FUNC) { // existuje globálna premenná s rovnakým názvom
            logErrSemantic(tkn, "identifier is already used as a variable");
            return SEM_ERR_OTHER;
        }
        if (fn->init) { // funkcia už bola definovaná
            logErrSemantic(tkn, "function was already defined");
            return SEM_ERR_REDEF;
        }
        // v ostatných prípadoch záznam o funkcii existuje preto, lebo bola už volaná 
    }
    fn->init = true; // funkcia je odteraz definovaná

    StrFillWith(&fn_name, fn->id); // zápis názvu aktuálne definovanej funkcie do globálnej premennej

    TRY_OR_EXIT(nextToken());
    if (tkn->type != BRT_RND_L) {
        logErrSyntax(tkn, "'('");
        return SYN_ERR;
    }

    // spracovanie predpisu, resp. definície parametrov
    TRY_OR_EXIT(parseFunctionSignature(already_called, fn->sig));

    // zistenie návratového typu funkcie
    TRY_OR_EXIT(nextToken());
    fn->sig->ret_type = SYM_TYPE_VOID;
    if (tkn->type == ARROW) {
        // 30. <FN_RET_TYPE> -> "->" <TYPE>
        TRY_OR_EXIT(parseDataType(&(fn->sig->ret_type)));
        TRY_OR_EXIT(nextToken());
    } // ak nenasleduje šípka '->', potom je to void funkcia

    if (tkn->type == BRT_CUR_L) {
        // 31. <FN_RET_TYPE> -> €
        TRY_OR_EXIT(nextToken());
    }
    else {
        logErrSyntax(tkn, "'{'");
        return SYN_ERR;
    }

    // Príprava parametrov pre telo funkcie
    // parametre budú vo vlastnom lokálnom bloku TS
    SymTabAddLocalBlock(&symt);
    DLLstr_First(&(fn->sig->par_ids));
    str_T par_id;
    StrInit(&par_id);
    for (size_t i = 0; StrRead(&(fn->sig->par_types))[i] != '\0'; i++) {
        DLLstr_GetValue(&(fn->sig->par_ids), &par_id);
        if (strcmp(StrRead(&par_id), "_") == 0) { // parametre s identifikátorom '_' sa nepoužívajú vo vnútri funkcie
            DLLstr_Next(&(fn->sig->par_ids));
            continue;
        }
        TSData_T* par = SymTabCreateElement(StrRead(&par_id));
        if (par == NULL) {
            logErrCompilerMemAlloc();
            return COMPILER_ERROR;
        }
        SymTabInsertLocal(&symt, par);
        par->init = true;
        par->let = true;
        par->type = StrRead(&(fn->sig->par_types))[i];
        /*
            Ak je identifikátor parametra napr. "a", v cieľovom kóde bude mať tvar "LF@%a".
        */
        StrFillWith(&(par->codename), "LF@");
        StrCatString(&(par->codename), par->id);
        StrAppend(&(par->codename), '%');
        DLLstr_Next(&(fn->sig->par_ids));
    }
    StrDestroy(&par_id);

    // vygenerovanie inštrukcií začiatku funkcie (náveštie, deklarácie parametrov a ich inicializácia)
    genFnDefBegin(StrRead(&fn_name), &(fn->sig->par_ids));

    // Spracovanie tela funkcie
    SymTabAddLocalBlock(&symt);
    while (tkn->type != BRT_CUR_R) {
        TRY_OR_EXIT(parse());
        TRY_OR_EXIT(nextToken());
    }
    // kontrola, či funkcia s návratovou hodnotou má všade kde je to treba, príkaz return
    if (!SymTabCheckLocalReturn(&symt) && fn->sig->ret_type != SYM_TYPE_VOID) {
        logErrSemanticFn(fn->id, "it is possible to exit function without return value");
        return SEM_ERR_FUNC;
    }
    if (!SymTabCheckLocalReturn(&symt)) {
        // aj void-funkcia musí mať na konci inštrukciu RETURN, pre vrátenie riadenie programu
        genCode(INS_RETURN, NULL, NULL, NULL);
    }
    SymTabRemoveLocalBlock(&symt);

    SymTabRemoveLocalBlock(&symt); // odstránenie lokálneho bloku s parametrami

    parser_inside_fn_def = code_inside_fn_def;
    StrFillWith(&fn_name, "");
    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: RETURN
 *  - po volaní:    NULL
 *
 * Generuje cieľový kód:
 *      { kód vygenerovaný v funkcii parseExpression }
 *      RETURN
 *
 * @brief Pravidlo pre spracovanie vrátenia návratovej hodnoty funkcie - return
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseReturn() {
    // 32. <STAT> -> return <RET_VAL> <STAT>
    if (!parser_inside_fn_def) { // return sa nachádza v hlavnom tele programu
        logErrSemantic(tkn, "return outside function definition");
        return SEM_ERR_OTHER;
    }

    bool possible_implicit_int_conversion = false;

    TRY_OR_EXIT(nextToken());
    char result_type = SYM_TYPE_UNKNOWN;
    switch (tkn->type)
    {
    case ID:; // id môže byť začiatok výrazu alebo sa za return môže nachádzať volanie funkcie
        TSData_T* id_data = SymTabLookup(&symt, StrRead(&(tkn->atr)));
        if (id_data == NULL) { // za return je funkcia, ktorá nebola ešte volaná
            saveToken();
            result_type = SYM_TYPE_VOID;
        }
        else {
            if (id_data->type == SYM_TYPE_FUNC) { // za return je funkcia
                // 34. <RET_VAL> -> €
                saveToken();
                result_type = SYM_TYPE_VOID;
            }
            else { // za return je výraz začínajúci premennou
                // 33. <RET_VAL> -> exp
                TRY_OR_EXIT(parseExpression(&result_type, &possible_implicit_int_conversion));
            }
        }
        break;
    case INT_CONST: // 33. <RET_VAL> -> exp
    case DOUBLE_CONST:
    case STRING_CONST:
    case NIL:
    case BRT_RND_L:
        // spracovanie výrazu a zistenie typu návratovej hodnoty v return
        TRY_OR_EXIT(parseExpression(&result_type, &possible_implicit_int_conversion));
        break;
    default: // 34. <RET_VAL> -> €
        // void return
        saveToken();
        result_type = SYM_TYPE_VOID;
        break;
    }

    // získanie informácii o predpise aktuálnej funkcie
    TSData_T* fn = SymTabLookupGlobal(&symt, StrRead(&fn_name));

    if (fn->sig->ret_type == SYM_TYPE_VOID) { // vo vnútri void funkcie
        if (result_type != SYM_TYPE_VOID) { // void funkcia nesmie vraciať hodnotu
            logErrSemanticFn(fn->id, "void function returns a value");
            return SEM_ERR_RETURN;
        }
    }
    else { // funkcia má vraciať hodnotu
        if (result_type == SYM_TYPE_VOID) { // return nič nevracia
            logErrSemanticFn(fn->id, "void return in non-void function");
            return SEM_ERR_RETURN;
        }
        if (possible_implicit_int_conversion && result_type == SYM_TYPE_INT &&
            (fn->sig->ret_type == SYM_TYPE_DOUBLE || fn->sig->ret_type == SYM_TYPE_DOUBLE_NIL)) {
            // vo výraze sú celočíselné literály a výsledok má byť priradený do dátového typu Double(?)
            // musí byť vykonaná implicitná konverzia
            genCode(INS_INT2FLOATS, NULL, NULL, NULL);
            result_type = SYM_TYPE_DOUBLE;
        }
        if (!isCompatibleAssign(fn->sig->ret_type, result_type)) { // návratový typ nesedí s predpisom funkcie
            logErrSemanticFn(fn->id, "different return type");
            return SEM_ERR_FUNC;
        }
    }

    SymTabModifyLocalReturn(&symt, true); // zapísať informáciu o prítomnosti return v aktuálnom bloku

    genCode(INS_RETURN, NULL, NULL, NULL); // vloženie inštrukcie RETURN

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: IF
 *  - po volaní:    BRT_CUR_R
 *
 * Generuje cieľový kód podmieneného bloku kódu:
 *      JUMPIFEQ/S  <if&XX!>
 *      ... { kód pre if } ...
 *      JUMP        <if&XX*>
 *      LABEL       <if&XX!>
 *      ... { kód pre else } ...
 *      LABEL       <if&XX*>
 *
 * @brief Pravidlo pre spracovanie podmieneného bloku kódu
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseIf() {
    // 35. <STAT> -> if <COND> { <STAT> } else { <STAT> } <STAT>
    TRY_OR_EXIT(nextToken());
    TSData_T* let_variable = NULL; // informácie o premennej v podmienke "let <premenná>"
    str_T cond_false;       // náveštie kam sa má skočiť, keď podmienka je false
    str_T skip_cond_false;  // náveštie kam sa skočí z if{} časti, aby sa preskočila časť else{}
    StrInit(&cond_false);
    StrInit(&skip_cond_false);

    genUniqLabel(StrRead(&fn_name), "if", &cond_false);
    StrFillWith(&skip_cond_false, StrRead(&(cond_false)));
    StrAppend(&cond_false, '!');
    StrAppend(&skip_cond_false, '*');

    switch (tkn->type) // rozlíšenie obyčajnej podmienky v tvare výrazu alebo test premennej na nil "let <premenná>"
    {
    case LET:;
        //  37. <COND> -> let id
        TRY_OR_EXIT(nextToken());   // identifikátor testovanej premennej
        if (tkn->type != ID) {
            logErrSyntax(tkn, "identifier");
            return SYN_ERR;
        }
        TSData_T* variable = SymTabLookup(&symt, StrRead(&(tkn->atr))); // informácie o premennej
        if (variable == NULL) { // premenná nebola deklarovaná
            logErrSemantic(tkn, "%s was undeclared", StrRead(&(tkn->atr)));
            return SEM_ERR_UNDEF;
        }
        if (variable->type == SYM_TYPE_FUNC) { // identifikátor označuje funkciu
            logErrSemantic(tkn, "%s is a function", StrRead(&(tkn->atr)));
            return SEM_ERR_RETURN;
        }
        if (!(variable->init)) { // premenná nebola inicializovaná
            logErrSemantic(tkn, "%s was uninitialized", StrRead(&(tkn->atr)));
            return SEM_ERR_UNDEF;
        }
        if (!(variable->let)) { // premenná musí byť nemodifikovateľná
            logErrSemantic(tkn, "%s must be unmodifiable variable", StrRead(&(tkn->atr)));
            return SEM_ERR_OTHER;
        }

        // premenná musí byť v samostatnom bloku, kde bude jej typ zmenený na typ nezahrňujúci nil
        SymTabAddLocalBlock(&symt);
        let_variable = SymTabCreateElement(StrRead(&(tkn->atr)));
        if (let_variable == NULL)
        {
            return COMPILER_ERROR;
        }
        let_variable->init = variable->init;
        let_variable->let = variable->let;
        let_variable->sig = NULL;
        let_variable->type = convertNilTypeToNonNil(variable->type);
        StrFillWith(&(let_variable->codename), StrRead(&(variable->codename)));

        SymTabInsertLocal(&symt, let_variable);

        genCode("JUMPIFEQ", StrRead(&cond_false), StrRead(&(variable->codename)), "nil@nil");
        break;
    case ID:;    // v podminke je obyčajný výraz
    case BRT_RND_L:;
    case INT_CONST:;
    case DOUBLE_CONST:;
    case STRING_CONST:;
    case NIL:;
        // 36. <COND> -> exp
        char exp_type = SYM_TYPE_UNKNOWN; // ???
        bool possible_implicit_int_conversion = false; // nie je využívané v if
        TRY_OR_EXIT(parseExpression(&exp_type, &possible_implicit_int_conversion));
        if (exp_type != SYM_TYPE_BOOL && exp_type != SYM_TYPE_UNKNOWN) {
            logErrSemantic(tkn, "condition must return a bool");
            return SEM_ERR_TYPE;
        }
        // na vrchole zásobníka je bool@true alebo bool@false
        genCode(INS_PUSHS, "bool@false", NULL, NULL);
        genCode(INS_JUMPIFEQS, StrRead(&cond_false), NULL, NULL);
        break;
    default:;
        logErrSyntax(tkn, "let or an expression");
        return SYN_ERR;
        break;
    }

    TRY_OR_EXIT(nextToken());
    bool if_had_return;
    TRY_OR_EXIT(parseStatBlock(&if_had_return)); // spracovanie príkazov keď podmienka je true
    if (let_variable != NULL) SymTabRemoveLocalBlock(&symt);

    genCode(INS_JUMP, StrRead(&skip_cond_false), NULL, NULL);

    TRY_OR_EXIT(nextToken());
    if (tkn->type != ELSE) {
        logErrSyntax(tkn, "else");
        return SYN_ERR;
    }

    genCode(INS_LABEL, StrRead(&cond_false), NULL, NULL);

    TRY_OR_EXIT(nextToken());
    bool else_had_return;
    TRY_OR_EXIT(parseStatBlock(&else_had_return)); // spracovanie príkazov keď podmienka je false

    if (if_had_return && else_had_return) {
        // pokiaľ sa v oboch častiach if aj else nachádzal return, potom bude return určite zastihnutý
        SymTabModifyLocalReturn(&symt, true);
    }

    genCode(INS_LABEL, StrRead(&skip_cond_false), NULL, NULL);

    StrDestroy(&cond_false);
    StrDestroy(&skip_cond_false);

    return COMPILATION_OK;
}

/**
 * Stav tkn:
 *  - pred volaním: WHILE
 *  - po volaní:    BRT_CUR_R
 *
 * Generuje cieľový kód cyklu:
 *      LABEL   <while&XX>
 *      PUSHS   <výsledok podmienky>
 *      PUSHS   bool@false
 *      JUMPIFEQS <while&XX!>
 *      ... { kód cyklu } ...
 *      JUMP    <while&XX>
 *      LABEL   <while&XX!>
 *
 * @brief Pravidlo pre spracovanie cyklu while
 * @return 0 v prípade úspechu, inak číslo chyby
*/
int parseWhile() {
    // 38. <STAT> -> while exp { <STAT> } <STAT>
    str_T loop_start;   // náveštie začiatku cyklu (spolu s podmienkou)
    str_T loop_end;     // náveštie za koniec cyklu (sem sa skočí keď podmienka nie je splnená)
    StrInit(&loop_start);
    StrInit(&loop_end);

    genUniqLabel(StrRead(&fn_name), "while", &loop_start);
    StrFillWith(&loop_end, StrRead(&(loop_start)));
    StrAppend(&loop_end, '!');
    genCode(INS_LABEL, StrRead(&loop_start), NULL, NULL);

    bool loop_inside_loop = parser_inside_loop; // cyklus v cykle
    if (!loop_inside_loop) {
        StrFillWith(&first_loop_label, StrRead(&loop_start));
    }
    parser_inside_loop = true;

    TRY_OR_EXIT(nextToken());
    switch (tkn->type) // syntaktická kontrola, či sa v podmienke nachádza výraz
    {
    case ID:;
    case BRT_RND_L:;
    case INT_CONST:;
    case DOUBLE_CONST:;
    case STRING_CONST:;
    case NIL:;
        // začiatok výrazu potvrdený
        break;
    default:
        logErrSyntax(tkn, "expression");
        return SYN_ERR;
    }

    char exp_type = SYM_TYPE_UNKNOWN; // ???
    bool possible_implicit_int_conversion = false; // nie je využívané vo while
    TRY_OR_EXIT(parseExpression(&exp_type, &possible_implicit_int_conversion));
    if (exp_type != SYM_TYPE_BOOL && exp_type != SYM_TYPE_UNKNOWN) {
        logErrSemantic(tkn, "condition must return a bool");
        return SEM_ERR_TYPE;
    }
    genCode(INS_PUSHS, "bool@false", NULL, NULL);
    genCode(INS_JUMPIFEQS, StrRead(&loop_end), NULL, NULL);

    TRY_OR_EXIT(nextToken());
    TRY_OR_EXIT(parseStatBlock(NULL));

    genCode(INS_JUMP, StrRead(&loop_start), NULL, NULL);
    genCode(INS_LABEL, StrRead(&loop_end), NULL, NULL);

    parser_inside_loop = loop_inside_loop;
    if (!parser_inside_loop) { // najvrchnejší cyklus bol opustený
        // inštrukcie pre definície premenných vo vnútri cyklu musia byť vložené pred samotným cyklom
        genDefVarsBeforeLoop(StrRead(&first_loop_label), &variables_declared_inside_loop);
        DLLstr_Dispose(&variables_declared_inside_loop);
    }

    StrDestroy(&loop_start);
    StrDestroy(&loop_end);

    return COMPILATION_OK;
}

/* --- FUNKCIE DEKLAROVANÉ V PARSER.H --- */

bool isCompatibleAssign(char dest, char src) {
    if (dest == SYM_TYPE_UNKNOWN) return true;
    if (src == SYM_TYPE_VOID) return false; // nemožno priradiť nič
    if (src == SYM_TYPE_UNKNOWN) return true;
    if (src == SYM_TYPE_NIL) {
        switch (dest) // samotný nil je možné priradiť len do dátového typu zahrňujúceho nil
        {
        case SYM_TYPE_INT_NIL:;
        case SYM_TYPE_DOUBLE_NIL:;
        case SYM_TYPE_STRING_NIL:;
            return true;
        default:
            return false;
        }
    }
    switch (src) // dátový typ nezahrňujúci nil je možné dosadiť aj do typu zahrňujúci nil
    {
    case SYM_TYPE_INT:
        return dest == SYM_TYPE_INT || dest == SYM_TYPE_INT_NIL;
    case SYM_TYPE_DOUBLE:
        return dest == SYM_TYPE_DOUBLE || dest == SYM_TYPE_DOUBLE_NIL;
    case SYM_TYPE_STRING:
        return dest == SYM_TYPE_STRING || dest == SYM_TYPE_STRING_NIL;
    case SYM_TYPE_BOOL:
        return dest == SYM_TYPE_BOOL;
    default:
        // dest je dátový typ zahrňujúci nil
        return dest == src;
        break;
    }
    return false;
}

int nextToken() {
    if (tkn != NULL) destroyToken(tkn);
    tkn = getToken();
    if (tkn == NULL) return COMPILER_ERROR;
    logErrUpdateTokenInfo(tkn);
    if (tkn->type == INVALID) {
        logErrCodeAnalysis(LEX_ERR, tkn->ln, tkn->col, "invalid token");
        return LEX_ERR;
    }
    return COMPILATION_OK;
}

void saveToken() {
    storeToken(tkn);
    tkn = NULL;
}

bool initializeParser() {
    SymTabInit(&symt);

    loadBuiltInFunctionSignatures();

    StrInit(&fn_name);

    DLLstr_Init(&check_def_fns);

    StrInit(&first_loop_label);
    DLLstr_Init(&variables_declared_inside_loop);

    DLLstr_Init(&code_fn);
    DLLstr_Init(&code_main);
    return true;
}

int parse() {
    switch (tkn->type)
    {
    case LET:;
    case VAR:;
        TRY_OR_EXIT(parseVariableDecl());
        break;
    case ID:;
        // 10. <STAT> -> id = <ASSIGN> <STAT>
        // 12. <STAT> -> id ( <PAR_LIST> ) <STAT>
        char result_type;
        token_T* first_tkn = tkn;
        tkn = NULL;
        TRY_OR_EXIT(nextToken());
        if (tkn->type == BRT_RND_L) {
            // 12. <STAT> -> id ( <PAR_LIST> ) <STAT>
            saveToken();
            tkn = first_tkn;
            bool popframe = shouldPopFrame(StrRead(&(tkn->atr)));
            TRY_OR_EXIT(parseFnCall(&result_type));
            if (popframe) genCode(INS_POPFRAME, NULL, NULL, NULL);
            genCode(INS_CLEARS, NULL, NULL, NULL); // volaná funkcia môže zanechať návratovú hodnotu na zásobníku
        }
        else if (tkn->type == ASSIGN) {
            // 10. <STAT> -> id = <ASSIGN> <STAT>
            TSData_T* variable = SymTabLookup(&symt, StrRead(&(first_tkn->atr)));
            if (variable == NULL) {
                // v TS nie je záznam s daným identifikátorom => nedeklarovaná premenná
                logErrSemantic(first_tkn, "%s was undeclared", StrRead(&(first_tkn->atr)));
                return SEM_ERR_UNDEF;
            }
            if (variable->init && variable->let) {
                // nemodifikovateľná premenná
                logErrSemantic(first_tkn, "%s is unmodifiable and was already initialised", StrRead(&(first_tkn->atr)));
                return SEM_ERR_OTHER;
            }
            TRY_OR_EXIT(parseAssignment(&result_type, StrRead(&(variable->codename)), variable->type));
            if (!isCompatibleAssign(variable->type, result_type)) {
                // nekompatibilný typ výsledku a premennej
                logErrSemantic(first_tkn, "incompatible data types");
                return SEM_ERR_TYPE;
            }
            variable->init = true;
            destroyToken(first_tkn);
        }
        else {
            logErrSyntax(tkn, "'(' or '='");
            return SYN_ERR;
        }
        break;
    case BRT_CUR_L:;
        // 11. <STAT> -> { <STAT> } <STAT>
        bool block_had_return;
        TRY_OR_EXIT(parseStatBlock(&block_had_return));
        if (block_had_return) SymTabModifyLocalReturn(&symt, block_had_return);
        break;
    case FUNC:;
        // 20. <STAT> -> func id ( <FN_SIG> ) <FN_RET_TYPE> { <STAT> } <STAT>
        TRY_OR_EXIT(parseFunction());
        break;
    case RETURN:;
        // 32. <STAT> -> return <RET_VAL> <STAT>
        TRY_OR_EXIT(parseReturn());
        break;
    case IF:;
        // 35. <STAT> -> if <COND> { <STAT> } else { <STAT> } <STAT>
        TRY_OR_EXIT(parseIf());
        break;
    case WHILE:;
        // 38. <STAT> -> while exp { <STAT> } <STAT>
        TRY_OR_EXIT(parseWhile());
        break;
    default:;
        logErrSyntax(tkn, "beginning of a statement");
        return SYN_ERR;
        break;
    }

    return COMPILATION_OK;
}

int checkIfAllFnDef() {
    DLLstr_First(&check_def_fns);
    while (DLLstr_IsActive(&check_def_fns))
    {
        DLLstr_GetValue(&check_def_fns, &fn_name);
        TSData_T* fn_info = SymTabLookupGlobal(&symt, StrRead(&fn_name));
        if (!fn_info->init) {
            logErrSemanticFn(StrRead(&fn_name), "was not defined");
            return SEM_ERR_REDEF;
        }
        DLLstr_Next(&check_def_fns);
    }
    return COMPILATION_OK;
}

void printOutCompiledCode() {
    printf(".IFJcode23\n"); // povinná hlavička

    // pomocné premenné
    printf("DEFVAR %s\n", VAR_TMP1);
    printf("DEFVAR %s\n", VAR_TMP2);
    printf("DEFVAR %s\n", VAR_TMP3);

    printf("JUMP !main\n"); // skok do hlavného tela programu

    // dogenerovať inštrukcie pre vstavanú funkciu substring pokiaľ bola použitá
    if (bifn_substring_called) genSubstring();

    DLLstr_printContent(&code_fn); // kód uživateľských funkcií

    // hlavné telo programu
    printf("LABEL !main\n");
    DLLstr_printContent(&code_main);
    printf("EXIT int@0\n");
}

void destroyParser() {
    if (tkn != NULL) {
        destroyToken(tkn);
        tkn = NULL;
    }
    SymTabDestroy(&symt);

    StrDestroy(&fn_name);
    DLLstr_Dispose(&check_def_fns);

    StrDestroy(&first_loop_label);
    DLLstr_Dispose(&variables_declared_inside_loop);

    DLLstr_Dispose(&code_main);
    DLLstr_Dispose(&code_fn);
}

/* Koniec súboru parser.c */
