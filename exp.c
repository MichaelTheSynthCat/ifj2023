/** Projekt IFJ2023
 * @file exp.c
 * @brief Precedenčná syntaktická a sémantická analýza výrazov, generovanie cieľového kódu
 * @author Stanislav Letaši
 * @date 14.11.2023
 */

#include "stdio.h"
#include "exp.h"
#include "strR.h"
#include "symtable.h"
#include "generator.h"
#include "logErr.h"


/** Počas syntaktickej analýzy označuje, že ešte nebol spracovaný žiadny token*/
#define NO_PREV -1

/**************************************************************************************************
* Štruktúry
**************************************************************************************************/
/**
 * @brief Štruktúra pre uloženie informácií o tokene
*/
typedef struct parsed_token
{
    int type;       // typ tokenu
    char st_type;   // typ premennej, používa hodnoty SYM_TYPE_XXX
    str_T id;       // názov identifikátoru, zároveň kľúč v tabuľke
    str_T codename; // identifikátor v cieľovom kóde
    int ln;         // riadok tokenu
    int col;        // pozícia prvého charakteru tokenu v riadku
} ptoken_T;

/**
 * @brief Štruktúra zásobníka pre dátový typ ptoken_T
*/
typedef struct stack
{
    int size;           // Počet prvkov v zásobníku
    int capacity;       // Kapacita zásobníku
    ptoken_T **array;   // Dynamické pole pre uloženie tokenov

}stack_t;

/**************************************************************************************************
 *Funkcie zásobníka
**************************************************************************************************/

/**
 * @brief Inicializuje zásobník, alokuje pamäť pre 16 prvkov
*/
bool stack_init( stack_t *stack ) {

    stack->array = calloc(16, sizeof(ptoken_T*)); // Alokácia pamäte pre pole
	if(stack->array == NULL){ // Alokácia pamäte zlyhala
        return false;
	}
	stack->size = 0;        // 0 Iniciálnych položiek
    stack->capacity = 16;   // Iniciálna kapacita pre 16 položiek
	
    return true; // Inicializácia prebehla úspešne
}

/**
 * @brief Vracia prvok z vrcholu zásobníka
 * @returns Ukazateľ na hodnotu z vrcholu zásobníka. Ak je prázdny, vracia NULL
*/
ptoken_T *stack_top(stack_t *stack){
    if(stack->size > 0){
    return(stack->array[stack->size - 1]); // Vrátenie hodnoty z vrcholu zásobníka
    }
    return NULL;
}

/**
 * @brief Vloží parsed_token na zásobník
 * @returns 0 ak prebehlo vloženie tokenu úspešne, inak chybový kód
*/
int stack_push_ptoken(stack_t *stack, ptoken_T *token){
    if(stack->capacity == stack->size){         // Ak je zásobník plný
        stack->capacity = stack->capacity*2;    // Zdvojnásobenie kapacity
        stack->array = realloc(stack->array, stack->capacity*sizeof(token_T));

        if(stack->array == NULL){ // Realokácia pamäte zlyhala
            fprintf(stderr, "memory reallocation error\n");
            return COMPILER_ERROR;
        }
    }

    stack->array[stack->size] = token;  // Vloženie tokenu na zásobník
    stack->size = stack->size + 1;      // Zväčšenie počtu prvkov v zásobníku
    return 0;
}

/**
 * @brief Konvertuje token na parsed_token a zavolá stack_push_ptoken
 * @returns 0 ak prebehlo vloženie tokenu úspešne, inak chybový kód
*/
int stack_push_token(stack_t *stack, token_T *token){
    
    TSData_T *symtabData; // Premenná pre uloženie dát z tabuľky symbolov

    if(token->type == ID) // Ak je token identifikátor, musíme ho vyhľadať v tabuľke symbolov
    {
        symtabData = SymTabLookup(&symt, StrRead(&(token->atr))); // Získanie dát o premennej z tabuľky symbolov

        if(symtabData == NULL){ // Premenná nebola deklarovaná
            logErrCodeAnalysis(SEM_ERR_UNDEF, token->ln, token->col,"variable was not declared");
            return SEM_ERR_UNDEF;
        }
        if(symtabData->init == false){ // Premenná nebola inicializovaná
            logErrCodeAnalysis(SEM_ERR_UNDEF, token->ln, token->col,"variable was not initialised");
            return SEM_ERR_UNDEF;
        }
    }

    ptoken_T *parsed_token = malloc(sizeof(ptoken_T)); // Nový parsed token
    if(parsed_token == NULL){
        fprintf(stderr, "memory allocation error\n");
        return COMPILER_ERROR;
    }
    
    str_T id, codename;     // Reťazce pre identifikátor a názov premennej/konštanty v cieľovom kóde
    StrInit(&id);           // Inicializácia id
    StrInit(&codename);     // Inicializácia codename
    StrFillWith(&id, token->atr.data); // Vloženie názvu premennej do id

    parsed_token->id = id;              // Id parsed tokenu
    parsed_token->type = token->type;   // Typ tokenu
    parsed_token->ln = token->ln;       // Riadok tokenu
    parsed_token->col = token->col;     // Pozícia v riadku tokenu

    if(token->type == ID) // Operand je premenná
    {
        parsed_token->st_type = symtabData->type; // Identikátor v cieľovom kóde z tabuľky symbolov
        StrFillWith(&codename, StrRead(&(symtabData->codename))); 
        parsed_token->codename = codename; // Identifikátor v cieľovom kóde
    }

    if(token->type == INT_CONST || token->type == DOUBLE_CONST || token->type == STRING_CONST || token->type == NIL)// Operand je konštanta
    {
        genConstVal(token->type, StrRead(&(tkn->atr)), &codename); // Získanie identifikátoru v cieľovom kóde pre konštantu

        parsed_token->codename = codename; // Identifikátor v cieľovom kóde
        switch (parsed_token->type){
        case INT_CONST:
             parsed_token->st_type = 'i';
             break;

        case DOUBLE_CONST:
             parsed_token->st_type = 'd';
             break;

        case STRING_CONST:
             parsed_token->st_type = 's';
             break;

        case NIL:
             parsed_token->st_type = 'N';
             break;
        }
    }

    if(token->type != INT_CONST && token->type != DOUBLE_CONST && 
    token->type != STRING_CONST && token->type != NIL && token->type != ID) // Token je operátor
    {
        parsed_token->st_type = '0';        // Typ premennej (operátor nemá typ premennej)
        parsed_token->codename = codename;  // Prázdný inicializovaný StrR
    }
    
    return stack_push_ptoken(stack, parsed_token); // Vloženie parsed tokenu na zásobník a vrátenie return value
}

/**
 * @brief Odstráni prvok z vrcholu zásobníka, ak nie je prázdny
*/
void stack_pop(stack_t *stack){
    if(stack->size > 0)
    {
        stack->size = stack->size-1;        // Zmenšenie počtu prvkov zásobníka
        stack->array[stack->size] = NULL;   // Vymazanie ukazateľa na prvok zo zásobníku
    }
}

/**
 * @brief Odstráni prvok z vrcholu zásobníka a zároveň uvoľní pamäť alokovanú pre prvok
*/
void stack_pop_destroy(stack_t *stack){
    ptoken_T *element;          // Pomocná premenná na vyprázdnenie zásobníku
    element = stack_top(stack); // Odstraňujeme prvok z vrcholu
    if(element != NULL)         // Prevencia double free
    {
        StrDestroy(&(element->id));         // Odstránenie strR id
        StrDestroy(&(element->codename));   // Odstránenie strR codename
        free(element);                      // Uvoľenie pamäte alokovanej pre prvok
    }
    stack_pop(stack); // Odstránenie prvku zo zásobníka
}

/**
 * @brief Vyprázdni zásobník a uvoľní alokovanú pamäť každého prvku a zásobníku
*/
void stack_clear(stack_t *stack){
    if(stack->array != NULL){       // Prevencia double free
        
        while(stack->size > 0)      // Kým sa nevymažú všetky položky
        {
            free(stack_top(stack)); // Odstránenie alokovanej pamäti prvku
            stack_pop(stack);       // Odstránenie prvku zo zásobníka
        }
    }
    free(stack->array);             // Uvoľnenie alokovanej pamäti zoznamu
    stack->size = 0;
}

/**
 * @brief Vyprázdni zásobník a uvoľní alokovanú pamäť prvku, jeho podštruktúr a zásobníku
*/
void stack_dispose(stack_t *stack){
    if(stack->array != NULL){           // Prevencia proti double free
        
        while(stack->size > 0)          // Kým sa nevymažú všetky položky
        {
            stack_pop_destroy(stack);   // Odstránenie prvku zo zásobníka a vymazanie
        }
        free(stack->array);             // Uvoľnenie alokovanej pamäti zoznamu
    }

    stack->capacity = stack->size = 0;  // Veľkosť a kapacita = 0
}

/**************************************************************************************************
* Overenie typu tokenu
**************************************************************************************************/

/**
 * @brief Vygeneruje kód pre konverziu int konštanty na double 
**/
void int2double(ptoken_T *var_a, ptoken_T *var_b){

    if(var_a->type == INT_CONST){   // Int konštanta je na zásobníku druhá z vrchu
        genCode("POPS","GF@!tmp2", NULL, NULL);             // Popnutie double premennej
        genCode("POPS","GF@!tmp1", NULL, NULL);             // Popnutie int premennej
        genCode("INT2FLOAT", "GF@!tmp3", "GF@!tmp1", NULL); // Konverzia int na double
        genCode("PUSHS", "GF@!tmp3", NULL, NULL);           // Pushnutie konvertovanej premennej späť na zásobník
        genCode("PUSHS", "GF@!tmp2", NULL, NULL);           // Pushnutie double premennej späť na zásobník
    }
    if(var_b->type == INT_CONST){ // Int konštanta je na vrchole zásobníku
        genCode("POPS","GF@!tmp1", NULL, NULL);             // Popnutie int premennej
        genCode("INT2FLOAT", "GF@!tmp3", "GF@!tmp1", NULL); // Konverzia int na double
        genCode("PUSHS", "GF@!tmp3", NULL, NULL);           // Pushnutie konvertovanej premennej späť na zásobník
    }
}

/**
 * @brief Overuje, či sa typ tokenu môže vyskytovať vo výraze.
 * @details Volaná pri každom tokene. Ak je false pri tokene na rovnakom riadku ako výraz => výraz nie je valídny. 
 * Inak indikuje koniec výrazu.
 * @returns true ak token môže byť súčasťou výrazu, inak false
**/
bool valid_token_type(int type){
    if (type == INVALID || type == INT_TYPE || type == DOUBLE_TYPE || type == ELSE ||
        type == STRING_TYPE || type == INT_NIL_YPE || type == DOUBLE_NIL_TYPE ||
        type == STRING_NIL_TYPE || type == VAR || type == LET || type == IF ||
        type == WHILE || type == FUNC || type == RETURN || type == BRT_CUR_R ||
        type == UNDERSCORE || type == ARROW || type == ASSIGN || type == BRT_CUR_L ||
        type == QUEST_MARK || type == COMMA || type == COLON || type == EOF_TKN) {
        return false;
    }
    else{
        return true; // Token môže byť časťou výrazu
    }
}

/**
 * @brief Overuje, či je token operand.
 * @returns true ak je operand, inak false
**/
bool is_operand(int type){
    if(type == ID || type == INT_CONST || type == DOUBLE_CONST ||
    type == STRING_CONST || type == NIL){
        return true;
    }
    else{
        return false;
    }
}

/**
 * @brief Overuje, či je token binárny operátor.
 * @returns true ak je operátor, inak false
 * @note Operátor "minus" je v tejto implementácii len binárny
**/
bool is_binary_operator(int type){
    if (type == OP_PLUS || type == OP_MUL || type == OP_DIV || type == OP_MINUS ||
        type == ASSIGN || type == EQ || type == NEQ || type == GTEQ ||
        type == LT || type == LTEQ || type == TEST_NIL || type == GT) {
        return true;
    }
    else{
        return false;
    }
}

/**
 * @brief Overuje, či je token aritmetický operátor.
 * @returns true ak je aritmetický operátor, inak false
**/
bool is_arithmetic_operator(int operator){
    if(operator == OP_PLUS || operator == OP_MUL || 
    operator == OP_DIV || operator == OP_MINUS){
        return true;
    }
    else{
        return false;
    }
}
/**
 * @brief Overuje, či je token logický operátor.
 * @returns true ak je aritmetický operátor, inak false
**/
bool is_logical_operator(int operator){
    if(operator == EQ || operator == NEQ || operator == GTEQ ||
        operator == LT || operator == LTEQ || operator == GT){
            return true;
        }
    else{
        return false;
    }
}

/**
 * @brief Overuje, či sú dátové typy operandov kompatibilné pre logické operácie
 * @returns true ak sú kompatibilné, inak false
**/
bool are_compatible_l(ptoken_T *op1, ptoken_T *op2){

    if(op1->st_type == 'b'){ // Bool
        return (op2->st_type == 'b');
    }
    if(op1->st_type == 'i'){ // Integer
        return (op2->type == INT_CONST || op2->st_type == 'i');
    }
    if(op1->type == INT_CONST) // Integer konštanta - môže byť pretypovaná na double
    {
        if(op2->st_type == 'd' || op2->type == DOUBLE_CONST){ // Ak je druhý operand double
            int2double(op1, op2); // Vygenerovanie kódu pre konverziu na double
        }
        return (op2->type == INT_CONST || op2->st_type == 'i' || op2->st_type == 'd' || op2->type == DOUBLE_CONST);
    }
    if(op1->type == DOUBLE_CONST || op1->st_type == 'd') // Double
    {
        if(op2->type == INT_CONST){ // Druhý operand je int konštanta
            int2double(op1, op2);   // Vygenerovanie kódu pre konverziu na double
        }
        return (op2->type == DOUBLE_CONST || op2->st_type == 'd' || op2->type == INT_CONST); // Druhý op môže byť pretypovaný na double
    }
        
    if(op1->type == STRING_CONST || op1->st_type == 's'){ // String
        return (op2->type == STRING_CONST || op2->st_type == 's');
    }
    if(op1->type == NIL || op1->st_type == 'I'){ // Integer NIL
        return (op2->type == NIL || op2->st_type == 'I');
    }
    if(op1->type == NIL || op1->st_type == 'D'){ // Double NIL
        return (op2->type == NIL || op2->st_type == 'D');
    }
    if(op1->type == NIL || op1->st_type == 'S'){ // String NIL
        return (op2->type == NIL || op2->st_type == 'S');
    }
    
    return false;
}

/**
 * @brief Overuje, či sú dátové typy operandov kompatibilné pre "??"
 * @returns true ak sú kompatibilné, inak false
**/
bool are_compatible_n(ptoken_T *op1, ptoken_T *op2){

    if(op1->type == INT_CONST || op1->st_type == 'i' || op1->st_type == 'I'){ // Integer
        return (op2->type == INT_CONST || op2->st_type == 'i' || op2->st_type == 'I');
    }
    if(op1->type == DOUBLE_CONST || op1->st_type == 'd' || op1->st_type == 'D'){ // Double
        return (op2->type == DOUBLE_CONST || op2->st_type == 'd' || op2->st_type == 'D');
    }
    if(op1->type == STRING_CONST || op1->st_type == 's' || op1->st_type == 'S'){ // String
        return (op2->type == STRING_CONST || op2->st_type == 's' || op2->st_type == 'S');
    }

    return false;
}
/**
 * @brief Overuje, či je dátový typ operandu nil alebo môže byť nil
 * @returns true ak je nil alebo môže byť nil, false ak nie
**/
bool is_nil_type(ptoken_T *op){
    return (op->st_type == 'I' || op->st_type == 'D' || 
    op->st_type == 'S' || op->st_type == 'N');
}

/**
 * @brief Porovná prioritu operátorov
 * @returns true ak má type väčšiu prioritu ako top, inak false
**/
bool priority_cmp(int type, int top){
    if(type == EXCL){ // !
        type = 0;
    }
    if(top == EXCL){ // !
        top = 0;
    }
    if(type == OP_MUL || type == OP_DIV){ // * /
        type = 1;
    }
    if(top == OP_MUL || top == OP_DIV){ // * /
        top = 1;
    }
    if(type == OP_PLUS || type == OP_MINUS){ // + -
        type = 2;
    }
    if(top == OP_PLUS || top == OP_MINUS){ // + -
        top = 2;
    }
    if(type > 29 && type < 36){ // == != < > <= >= 
        type = 3;
    }
    if(top > 29 && top < 36){ // == != < > <= >= 
        top = 3;
    }
    if(type == TEST_NIL){ // ??
        type = 4;
    }
    if(top == TEST_NIL){ // ??
        top = 4;
    }

    if(top > type){ // Operátor na vrchole zásobníka má menšiu prioritu ako operátor type
        return true;
    }
    return false; // Operátor na vrchole zásobníka má väčšiu prioritu ako operátor type
}

/**************************************************************************************************
 *Ostatné funkcie
**************************************************************************************************/


/**
 * @brief Funkcia zavolaná pred ukončením parseExpression počas syntaktickej analýzy
 * @details Vyprázdni zásobníky a uvoľní alokovanú pamäť každej položky v zásobníku
**/
void endParse_syn(stack_t *stack, stack_t *postfixExpr){
    stack_dispose(stack);
    stack_dispose(postfixExpr);
}
/**
 * @brief Funkcia zavolaná pred ukončením parseExpression počas sémantickej analýzy
 * @details Vyprázdni zásobníky a uvoľní alokovanú pamäť každej položky v zásobníku
**/
void endParse_sem(stack_t *stack, stack_t *postfixExpr){
    stack_clear(stack);
    stack_dispose(postfixExpr);
}

/**
 * @brief Konvertuje infixový výraz do postfixovej formy
 * @details Volaná počas syntaktickej analýzy výrazu pri každom tokene
 * @returns 0 ak prebehlo vloženie tokenu úspešne, inak chybový kód
**/
int infix2postfix(stack_t *stack, stack_t *postfixExpr, token_T *infix_token){
    
    int status = 0; // Pomocná premenná pre uloženie návratového kódu funkcií

    if(infix_token == NULL)     // Koniec výrazu => všetky prvky zo zásobníka vkladáme do postfixexpression
	{	
		while(stack->size > 0)  // Pokiaľ sa nevyprázdni celý zásobník
		{
			status = stack_push_ptoken(postfixExpr, stack_top(stack)); // Vloženie prvku z vrcholu zásobníka do postfixExpression
            if(status != 0){        // Push neprebehol úspešne
                return status;      // Vrátenie chybového kódu
            }
            stack_pop(stack); // Odstránenie prvku zo zásobníku
		}

		return 0;
	}
    if(infix_token->type == BRT_RND_L || infix_token->type == EXCL) // Ľavá zátvorka alebo "!"
	{
		return stack_push_token(stack, infix_token); // Vkladáme na zásobník
	}
	if(infix_token->type == BRT_RND_R) // Pravá zátvorka
	{
		while(true) // Pokým nenájdeme ľavú zátvorku
	    {
		    if(stack->size == 0){ // Ukončenie funkcie pri prázdnom zásobníku
		    	return 0;
		    }

		    if(stack_top(stack)->type == BRT_RND_L) // Ľavá zátvorka bola nájdená
		    {
		    	stack_pop_destroy(stack); // Odstránenie zátvorky zo zásobníku a z pamäte
		    	return 0;
		    }
		    else
		    {
		    	status = stack_push_ptoken(postfixExpr, stack_top(stack)); // Pridanie znaku zo zásobníku do výrazu
                if(status != 0){ 
                    return status; 
                }
		    	stack_pop(stack); // Odstránenie znaku zo zásobníku
		    }
	    }
		return 0;
	}
	
	if(is_binary_operator(infix_token->type))
	{
		while (true) // Opakovanie odstraňovania operátorov zo zásobníka až dokým nebude možné vložiť znak c na zásobník
		{
			if(stack->size == 0){// Zásobník je prázdny
				return stack_push_token(stack, infix_token); // Vloženie znaku na zásobník, koniec funkcie
			}
			else // Zásobník nie je prázdny
			{
                // Na vrchole zásobníka je ľavá zátvorka alebo operátor s nižšou prioritou ako infix_token
				if(stack_top(stack)->type == BRT_RND_L || priority_cmp(infix_token->type, stack_top(stack)->type))
                { 
					return stack_push_token(stack, infix_token); // Vloženie znaku na zásobník, koniec funkcie
				}
				else
                {
					status = stack_push_ptoken(postfixExpr, stack_top(stack)); // Vloženie operátoru s rovnakou/vyššou prioritou na koniec postfix výrazu
                    if(status != 0){ 
                        return status;
                    }
					stack_pop(stack); 
				}
			}
	    }
	}

	else // Žiadna predošlá podmienka nebola splnená => znak musí byť operand
	{
		return stack_push_token(postfixExpr, infix_token); // Vloženie operandu do postfixového výrazu, koniec funkcie
	}
    return 0;
}

/**
 * @brief Prekopíruje dáta tokenu do zadanej premennej
**/
void copy_data(ptoken_T *source, ptoken_T *destination){

    destination->st_type = source->st_type;
    destination->type = source->type;
    destination->codename = source->codename; 
    destination->id = source->id; 
    destination->ln = source->ln;
    destination->col = source->col;
}

/**************************************************************************************************
 *Hlavná funkcia
**************************************************************************************************/

int parseExpression(char* result_type, bool *literal) {

    stack_t stack;              // Zásobník pre konverziu výrazu na postfixovú formu
    stack_t postfixExpr;        // Zásobník pre uloženie postfixového výrazu
    stack_init(&stack);         // Inicializácia zásobníka
    stack_init(&postfixExpr);   // Inicializácia zásobníka

    int prevTokenType = NO_PREV;    // Pomocná premenná pre uloženie typu tokenu pred momentálne spracovaným
    int bracketCount = 0;           // Premenná na overenie korektnosti zátvoriek "()" vo výraze
    int status = 0;                 // Premenná na overenie priebehu volania funkcie


//======================================Syntaktická analýza======================================//
// Hľadanie porušenia syntaktických pravidiel. Ak žiadne nenastane, token je vložený do postfix výrazu.

    while(true) // Pokým sa nespracuje celý výraz
    {
        if(!valid_token_type(tkn->type)) // Ak token nemôže patriť do výrazu 
        {
            if(tkn->type == INVALID) // Token je typu INVALID
            {
                endParse_syn(&stack, &postfixExpr); // Upratanie pred skončením funkcie
                return LEX_ERR; // Lexikálna chyba
            }
            if(prevTokenType == NO_PREV){ // Token je prvý vo výraze
                logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expression is empty");
                break; // Výraz nie je valídny
            }
            else // Predpokladáme že token už nie je súčasťou výrazu => znamená to ukončenie výrazu
            {
                if(is_binary_operator(prevTokenType)) // Predošlý token je binárny operátor
                {
                    logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"binary operator expected a second operand");
                    prevTokenType = NO_PREV;
                    break; // Výraz nie je valídny
                }
                if(bracketCount != 0) // Vo výraze nie sú uzatvorené všetky zátvorky
                {
                    logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expression has unclosed brackets");
                    prevTokenType = NO_PREV;
                    break; // Výraz nie je valídny
                }
                if(infix2postfix(&stack, &postfixExpr, NULL) == COMPILER_ERROR){ // Ukončenie postfix výrazu
                    endParse_syn(&stack, &postfixExpr); // Upratenie pred ukončením pri chybovom stave
                    return COMPILER_ERROR; // Nastala chyba pri malloc/realloc
                }
                saveToken();    // Vloženie tokenu späť do input streamu
                break;          // Úspešný koniec syntaktickej analýzy výrazu
            }
        }

        if(is_binary_operator(tkn->type)) // Binárny operátor
        {
            if(prevTokenType == NO_PREV || is_binary_operator(prevTokenType) || prevTokenType == BRT_RND_L) 
            { // Token je prvý vo výraze, je za binárnym operátorom alebo ľavou zátvorkou

                logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"binary operator has no operands");
                prevTokenType = NO_PREV;
                break; // Výraz nie je valídny
            }
        }

        if(is_operand(tkn->type)) // Operand
        {
            if(prevTokenType != NO_PREV) // Operand nie je prvý token vo výraze
            {
                if(is_operand(prevTokenType) || prevTokenType == EXCL || (prevTokenType == BRT_RND_R)) // Predošlý token je operand, "!" alebo pravá zátvorka
                {
                    if(bracketCount != 0) // Ak nie sú uzavreté všetky zátvorky
                    {
                        logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expected an operator before operand");
                        prevTokenType = NO_PREV;
                        break; // Výraz nie je valídny
                    }
                    else
                    {
                        if(infix2postfix(&stack, &postfixExpr, NULL) == COMPILER_ERROR){ // Signalizuje ukončenie postfix výrazu
                            endParse_syn(&stack, &postfixExpr); // Upratenie pred ukončením
                            return COMPILER_ERROR; // Nastala chyba pri malloc/realloc
                        }
                        saveToken(); // Vloženie tokenu späť do input streamu
                        break; // Úspešný koniec syntaktickej analýzy výrazu
                    }
                }
            }
        }
        if(tkn->type == BRT_RND_L) // Ľavá zátvorka
        {
            if(prevTokenType != NO_PREV && !is_binary_operator(prevTokenType) && prevTokenType != BRT_RND_L) 
            { // Token nie je prvý vo výraze a predošlý token nie je binárny operátor
                logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expected an operator before opening bracket");
                prevTokenType = NO_PREV;
                break; // Výraz nie je valídny
            }
            else{
            bracketCount ++; // Zvýšenie počtu otvorených ľavých zátvoriek
            }
        }
        if(tkn->type == BRT_RND_R) // Pravá zátvorka
        {
            if(is_binary_operator(prevTokenType) || bracketCount == 0 || prevTokenType == BRT_RND_L) 
            { // Predošlý token je binárny operátor, ľavá zátvorka, alebo vo výraze nie je otvorená zátvorka
                if(bracketCount == 0){
                    logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expected an opened left bracket");
                }
                if(prevTokenType == BRT_RND_L){
                    logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"brackets without an operand");
                }
                if(is_binary_operator(prevTokenType)){
                    logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expected an operand before right bracket");
                }
                prevTokenType = NO_PREV;
                break; // Výraz nie je valídny
            }

            bracketCount--; // Uzatvorenie páru zátvoriek

        }
        if(tkn->type == EXCL) // Výkričník
        {
            if(!is_operand(prevTokenType) && prevTokenType != BRT_RND_R) // Predošlý token nie je operand alebo ")"
            {
                logErrCodeAnalysis(SYN_ERR, tkn->ln, tkn->col,"expected an operand or right bracket before '!'");
                prevTokenType = NO_PREV;
                break; // Výraz nie je valídny
            }
        }

        status = infix2postfix(&stack, &postfixExpr, tkn); // Pridanie tokenu do postfix výrazu
        if(status != 0) // Pridanie tokenu do postfix výrazu nebolo úspešné
        {
            endParse_syn(&stack, &postfixExpr); // Upratenie pred ukončením
            return status;                      // Vrátenie chybového kódu            
        }
        
        prevTokenType = tkn->type;      // Uloženie typu predošlého tokenu
        status = nextToken();           // Požiadanie o ďalší token z výrazu
        if(status == COMPILER_ERROR){   // nextToken vrátil compiler error
            fprintf(stderr, "nextToken: memory allocation error\n");
            endParse_syn(&stack, &postfixExpr); // Upratanie pred skončením funkcie
            return COMPILER_ERROR;              // Vrátenie compiler error
        }
        if(status == LEX_ERR){
            endParse_syn(&stack, &postfixExpr); // Upratanie pred skončením funkcie
            return LEX_ERR;                     // Vrátenie lexical error
        }

    } // Koniec while loopu

    if(prevTokenType == NO_PREV) // Symbolizuje chybnú syntax
    {
        endParse_syn(&stack, &postfixExpr); // Upratanie pred skončením funkcie
        return SYN_ERR; // Vrátenie chybového stavu
    }

//=====================================Sémantická analýza=========================================/

    ptoken_T *var_a, *var_b; // Pomocné premenné pre sémantickú analýzu
    status = 0;

    for(int index = 0; index<postfixExpr.size; index++) // Kým sa nespracuje celý postfix výraz
    {
        if(is_operand(postfixExpr.array[index]->type))  // Operand
        {
            ptoken_T *new_token = malloc(sizeof(ptoken_T)); // Vytvorenie nového tokenu kvôli zachovaniu hodnôt v pôvodnom
            copy_data(postfixExpr.array[index], new_token); // Skopírovanie hodnôt z pôvodného tokenu

            if(stack_push_ptoken(&stack, new_token) == COMPILER_ERROR) // Operand sa vloží na zásobník
            {
                endParse_sem(&stack, &postfixExpr); // Upratanie pred skončením funkcie
                return COMPILER_ERROR; // Vrátenie chybového stavu
            }

            genCode("PUSHS",StrRead(&(postfixExpr.array[index]->codename)),NULL, NULL); // Vloženie premennej na zásobník
        }
        if(is_binary_operator(postfixExpr.array[index]->type)) // Binárny operátor
        {
            var_b = stack_top(&stack);
            stack_pop(&stack);
            var_a = stack_top(&stack);
            stack_pop(&stack);
            // Popneme 2 premenné zo zásobníka, vo výraze sú v poradí "a b"

            if(is_arithmetic_operator(postfixExpr.array[index]->type)) // Aritmetický operátor
            {
                if(var_a->st_type == 'I' || var_a->st_type == 'D' || var_a->st_type == 'S' || 
                var_a->st_type == 'N' || var_b->st_type == 'I' || var_b->st_type == 'D' || 
                var_b->st_type == 'S' || var_b->st_type == 'N')
                { // Jeden z operandov je nil alebo nil typ
                    if(var_a->st_type == 'I' || var_a->st_type == 'D' || var_a->st_type == 'S' || var_a->st_type == 'N'){
                        logErrCodeAnalysis(SEM_ERR_TYPE, var_a->ln, var_a->col,"operand is a nil type");
                    }
                    else{
                        logErrCodeAnalysis(SEM_ERR_TYPE, var_b->ln, var_b->col,"operand is a nil type");
                    }
                    status = SEM_ERR_TYPE;
                    break;
                }

                if(var_a->st_type == 's' || var_a->type == STRING_CONST) // Prvý operand je reťazec
                {
                    if(postfixExpr.array[index]->type == OP_PLUS) // Operátor je "+"
                    {
                        if(var_b->st_type == 's' || var_b->type == STRING_CONST) // Druhý operand je tiež reťazec
                        {
                            var_a->st_type = 's';   // Výsledok konkatenácie je typu string
                            free(var_b);            // Vymazanie tokenu
                            if((status = stack_push_ptoken(&stack, var_a)) != 0){  // Vloženie tokenu na zásobník
                                break;
                            }

                            genCode("POPS","GF@!tmp2", NULL, NULL);                 // Popnutie reťazca do pomocnej premennej
                            genCode("POPS","GF@!tmp1", NULL, NULL);                 // Popnutie reťazca do pomocnej premennej
                            genCode("CONCAT", "GF@!tmp3", "GF@!tmp1", "GF@!tmp2");  // Konkatenácia reťazcov
                            genCode("PUSHS", "GF@!tmp3", NULL, NULL);               // Pushnutie konkatenovaného reťazca na stack

                            continue; // Posúvame sa na ďalší znak v postfix výraze
                        }
                        else{ // Druhý operand nie je reťazec
                            logErrCodeAnalysis(SEM_ERR_TYPE, var_b->ln, var_b->col,"expected operand of type string");
                            status = SEM_ERR_TYPE;
                            break;
                        }
                    }
                    else{ // Operátor nie je "+"
                        logErrCodeAnalysis(SEM_ERR_TYPE, postfixExpr.array[index]->ln, postfixExpr.array[index]->col,"expected the '+' operator");
                        status = SEM_ERR_TYPE;
                        break;
                    }
                }
                if((var_a->st_type == 'i' || var_a->type == INT_CONST) && (var_b->st_type == 'i' || var_b->type == INT_CONST)) 
                { // 2 Inty
                    var_a->st_type = 'i'; // Výsledok operácie je typu int
                    
                    if(var_b->type != INT_CONST){   // Ak je jeden z operandov premenná, výsledok operácie sa nebude implicitne konvertovať na double
                        var_a->type = ID;           // V tomto prípade musíme na zásobník vložiť výsledok operácie ako typ ID, čiže premenná
                    }

                    free(var_b); // Vymazanie tokenu
                    if(stack_push_ptoken(&stack, var_a) != 0){  // Vloženie tokenu na zásobník
                        free(var_a); // Vymazanie tokenu
                        endParse_sem(&stack, &postfixExpr);     // Upratanie pred skončením funkcie
                        return COMPILER_ERROR;
                    }
                    switch (postfixExpr.array[index]->type){
                    case OP_PLUS:
                        genCode("ADDS",NULL, NULL, NULL);   // Sčítanie hodnôt na vrchole zásobníka
                        break;
                    case OP_MINUS:
                        genCode("SUBS",NULL, NULL, NULL);   // Odčítanie hodnôt na vrchole zásobníka
                        break;
                    case OP_DIV:
                        genCode("IDIVS",NULL, NULL, NULL);  // Podiel integer hodnôt na vrchole zásobníka
                        break;
                    case OP_MUL:
                        genCode("MULS",NULL, NULL, NULL);   // Vynásobenie hodnôt na vrchole zásobníka
                        break;
                    }

                    continue; // Posúvame sa na ďalší znak v postfix výraze
                }
                if((var_a->st_type == 'd' || var_a->type == DOUBLE_CONST) && (var_b->st_type == 'd' || var_b->type == DOUBLE_CONST))
                { // 2 Double
                    var_a->st_type = 'd'; // Výsledok operácie je typu double
                    if((status = stack_push_ptoken(&stack, var_a)) != 0){  // Vloženie tokenu na zásobník
                        break;
                    }
                    free(var_b); // Vymazanie tokenu

                    switch (postfixExpr.array[index]->type){
                    case OP_PLUS:
                        genCode("ADDS",NULL, NULL, NULL); // Sčítanie hodnôt na vrchole zásobníka
                        break;
                    case OP_MINUS:
                        genCode("SUBS",NULL, NULL, NULL); // Odčítanie hodnôt na vrchole zásobníka
                        break;
                    case OP_DIV:
                        genCode("DIVS",NULL, NULL, NULL); // Podiel hodnôt na vrchole zásobníka
                        break;
                    case OP_MUL:
                        genCode("MULS",NULL, NULL, NULL);  //  Vynásobenie hodnôt na vrchole zásobníka
                        break;
                    }
                    continue; // Posúvame sa na ďalší znak v postfix výraze
                }
                if( (var_a->type == INT_CONST && (var_b->type == DOUBLE_CONST || var_b->st_type == 'd') ) || 
                (var_b->type == INT_CONST && (var_a->type == DOUBLE_CONST || var_a->st_type == 'd') ) ) 
                { // Int konštanta a Double
                    int2double(var_a, var_b); // Konverzia int typu na double typ

                    var_a->st_type = 'd';   // Výsledok operácie je typu double
                    free(var_b);            // Vymazanie tokenu
                    if((status = stack_push_ptoken(&stack, var_a)) != 0){  // Vloženie tokenu na zásobník
                        break;
                    }

                    switch (postfixExpr.array[index]->type){
                    case OP_PLUS:
                        genCode("ADDS",NULL, NULL, NULL); // Sčítanie hodnôt na vrchole zásobníka
                        break;
                    case OP_MINUS:
                        genCode("SUBS",NULL, NULL, NULL); // Odčítanie hodnôt na vrchole zásobníka
                        break;
                    case OP_DIV:
                        genCode("DIVS",NULL, NULL, NULL); // Podiel hodnôt na vrchole zásobníka
                        break;
                    case OP_MUL:
                        genCode("MULS",NULL, NULL, NULL); // Vynásobenie hodnôt na vrchole zásobníka
                        break;
                    }
                    continue; // Posúvame sa na ďalší znak v postfix výraze

                }
                else{ // Typy nie sú kompatibilné
                    logErrCodeAnalysis(SEM_ERR_TYPE, var_b->ln, var_b->col,"data type of operand is not compatible");
                    status = SEM_ERR_TYPE;
                    break;
                }
            }
            if(is_logical_operator(postfixExpr.array[index]->type)) // Logický operátor
            {
                if(are_compatible_l(var_a, var_b)) // Overenie, či sú dátové typy kompatibilné pre logickú operáciu
                { // Ak sú int a double, are_compatible_l vykoná implicitnú konverzia

                    if(var_a->st_type == 'b' && var_b->st_type == 'b' &&
                    (postfixExpr.array[index]->type != EQ && postfixExpr.array[index]->type != NEQ))
                    {// Bool operandy môžu byť porovnané iba operátorom "==" alebo "!="

                        logErrCodeAnalysis(SEM_ERR_TYPE, postfixExpr.array[index]->ln, postfixExpr.array[index]->col,"expected the '==' or '!=' operator");
                        status = SEM_ERR_TYPE;
                        break;
                    }

                    var_a->st_type = 'b'; // Výsledný token bude typu boolean
                    if((status = stack_push_ptoken(&stack, var_a)) != 0){ // Pushnutie nového tokenu na stack
                        break;
                    }
                    free(var_b); // Vymazanie druhého tokenu

                    switch (postfixExpr.array[index]->type){
                    case EQ:
                        genCode("EQS",NULL, NULL, NULL); // Rovnosť hodnôt
                        break;
                    case NEQ:
                        genCode("EQS",NULL, NULL, NULL); // Rovnosť hodnôt
                        genCode("NOTS",NULL, NULL, NULL); // => nerovnosť hodnôt
                        break;
                    case GT:
                        genCode("GTS",NULL, NULL, NULL); // A > B
                        break;
                    case LT:
                        genCode("LTS",NULL, NULL, NULL); // A<B
                        break;
                    case LTEQ:
                        genCode("GTS",NULL, NULL, NULL); // A > B
                        genCode("NOTS",NULL, NULL, NULL); // A <= B
                        break;
                    case GTEQ:
                        genCode("LTS",NULL, NULL, NULL); // A < B
                        genCode("NOTS",NULL, NULL, NULL); // A >= B
                        break;
                    }
                    continue; // Posúvame sa na ďalší znak v postfix výraze
                }
                
                else{ // Typy nie sú kompatibilné
                    logErrCodeAnalysis(SEM_ERR_TYPE, var_b->ln, var_b->col,"data type of operand is not compatible");
                    status = SEM_ERR_TYPE;
                    break;
                }
            }
            if(postfixExpr.array[index]->type == TEST_NIL) // Test nil hodnoty "??"
            {
                if(is_nil_type(var_b)) // Druhý operand je nil alebo nil typ
                {
                    logErrCodeAnalysis(SEM_ERR_TYPE, var_b->ln, var_b->col,"operand is a nil type");
                    status = SEM_ERR_TYPE;
                    break;
                }
                if(var_a->st_type == 'N')// Prvý operand je nil
                {
                    if((status = stack_push_ptoken(&stack, var_b)) != 0){  // Pushnutie druhého tokenu na stack
                        break;
                    }
                    free(var_a); // Vymazanie prvého tokenu
                    genCode("POPS","GF@!tmp1", NULL, NULL);     // Popnutie non-nil premennej do pomocnej premennej
                    genCode("POPS","GF@!tmp2", NULL, NULL);     // Odstránenie nil zo zásobníka
                    genCode("PUSHS","GF@!tmp1", NULL, NULL);    // Vrátenie non-nil premennej späť na zásobník
                    continue; // Posúvame sa na ďalší token
                }
                if(are_compatible_n(var_a, var_b)) // Ak majú tokeny kompatibilný dátový typ
                {
                    if(!is_nil_type(var_a)) // Prvý operand nikdy nebude nil => je výsledok výrazu
                    {
                        if((status = stack_push_ptoken(&stack, var_a)) != 0){  // Pushnutie druhého tokenu na stack
                        break;
                        }
                        free(var_b); // Vymazanie druhého tokenu
                        genCode("POPS","GF@!tmp1", NULL, NULL); // Odstránenie nil zo zásobníka
                        continue;
                    }
                    else // Prvý operand môže byť nil
                    {
                        if((status = stack_push_ptoken(&stack, var_b)) != 0){  // Pushnutie druhého tokenu na stack
                        break;
                        }
                        free(var_a); // Vymazanie druhého tokenu

                        str_T label1, label2;
                        StrInit(&label1);
                        StrInit(&label2);

                        genUniqLabel("testnil","l1",&label1); // Vygenerovanie labelu pre podmienený skok
                        genUniqLabel("testnil","l2",&label2); // Vygenerovanie labelu pre podmienený skok

                        genCode("POPS","GF@!tmp2", NULL, NULL); // Popnutie non-nil premennej do pomocnej premennej
                        genCode("POPS","GF@!tmp1", NULL, NULL); // Popnutie possible-nil premennej do pomocnej premennej
                        genCode("JUMPIFEQ", StrRead(&label1),"GF@!tmp1", "nil@nil"); // Ak sa prvá premenná rovná nil, skok na náveštie 1
                        genCode("PUSHS","GF@!tmp1", NULL, NULL); // V tomto prípade prvá premenná nie je nil, pushnutie prvej premennej na zásobník
                        genCode("JUMP", StrRead(&label2), NULL, NULL); // Skok na koniec funkcie
                        genCode("LABEL", StrRead(&label1), NULL, NULL); // Náveštie 1
                        genCode("PUSHS","GF@!tmp2", NULL, NULL); // V tomto prípade prvá premenná je nil, pushnutie 2. premennej na zásobník
                        genCode("LABEL", StrRead(&label2), NULL, NULL); // Náveštie 2 - koniec funkcie

                        StrDestroy(&label1);
                        StrDestroy(&label2);
                        continue;
                    }
                }
                else
                {
                    logErrCodeAnalysis(SEM_ERR_TYPE, var_b->ln, var_b->col,"operand types are not compatible");
                    status = SEM_ERR_TYPE;
                    break;
                }
            }
        }
        
        if(postfixExpr.array[index]->type == EXCL) // Výkričník
        {
            if(stack_top(&stack)->st_type == 'I'){ // typ Int? 
                stack_top(&stack)->st_type = 'i'; // pretypovanie na Int
            }
            if(stack_top(&stack)->st_type == 'D'){ // typ Double?
                stack_top(&stack)->st_type = 'd'; // pretypovanie na Double
            }
            if(stack_top(&stack)->st_type == 'S'){ // typ String?
                stack_top(&stack)->st_type = 's'; // pretypovanie na String
            }
            if(stack_top(&stack)->type == NIL){ // Výraz "nil!"
                logErrCodeAnalysis(SEM_ERR_TYPE, stack_top(&stack)->ln, stack_top(&stack)->col,"not possible to make a non-nil value from 'nil'");
                status = SEM_ERR_OTHER;
                break;
            }
            // Pre konštanty operátor "!" nemá efekt
        }
    }// Koniec for loopu

    if(status != COMPILATION_OK){ // Počas sémantickej analýzy bola zistená chyba
        free(var_a);
        free(var_b);
        endParse_sem(&stack, &postfixExpr); // Upratanie pred skončením funkcie
        return status; // Koniec 
    }

    if(stack.size == 1){ // Výsledný typ je na vrchole zásobníka
        *result_type = stack_top(&stack)->st_type; // Zapísanie výsledného typu výrazu
        if(stack_top(&stack)->type == INT_CONST){
            *literal = true; // Výsledok je int literál, je možné ho implicitne pretypovať na double
        }
        endParse_sem(&stack, &postfixExpr); // Upratanie pred skončením funkcie

        return COMPILATION_OK; // Úspešný koniec
    }
    else{
        endParse_sem(&stack, &postfixExpr); // Upratanie pred skončením funkcie
        return SEM_ERR_OTHER;
    }
    
    return COMPILATION_OK;
}


/* Koniec súboru exp.c */
