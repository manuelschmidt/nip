/* Definitions for the bison parser. $Id: parser.h,v 1.11 2004-05-31 10:36:54 mvkorpel Exp $
 */

#ifndef __PARSER_H__
#define __PARSER_H__

#define MAX_LINELENGTH 1000

/* Comment character in input files.
 * After this, the rest of the line is ignored.
 */
#define COMMENT_CHAR '#'

#include "Clique.h"
#include "Variable.h"
#include "potential.h"
#include "Graph.h"
#include <stdio.h>

struct varlist {
  Variable data;
  struct varlist *fwd;
  struct varlist *bwd;
};

typedef struct varlist varelement;
typedef varelement *varlink;

static varlink nip_first_var = NULL; /* global stuff, sad but true */
static varlink nip_last_var = NULL;
static int nip_vars_parsed = 0;

static varlink nip_first_temp_var = NULL;
static varlink nip_last_temp_var = NULL;
static int nip_symbols_parsed = 0;

static Graph *nip_graph = NULL;

struct doublelist {
  double data;
  struct doublelist *fwd;
  struct doublelist *bwd;
};

typedef struct doublelist doubleelement;
typedef doubleelement *doublelink;

static doublelink nip_first_double = 0;
static doublelink nip_last_double = 0;
static int nip_doubles_parsed = 0;


struct stringlist {
  char* data;
  struct stringlist *fwd;
  struct stringlist *bwd;
};

typedef struct stringlist stringelement;
typedef stringelement *stringlink;

static stringlink nip_first_string = 0;
static stringlink nip_last_string = 0;
static int nip_strings_parsed = 0;


struct initDataList {
  potential data;
  Variable child;
  Variable* parents;
  struct initDataList *fwd;
  struct initDataList *bwd;
};

typedef struct initDataList initDataElement;
typedef initDataElement *initDataLink;

static initDataLink nip_first_initData = 0;
static initDataLink nip_last_initData = 0;
static int nip_initData_parsed = 0;


/* The current input file */
static FILE *nip_parser_infile = NULL;

/* Is there a file open? 0 if no, 1 if yes. */
static int nip_file_open = 0;

/* Opens an input file. Returns 0 if file was opened or if some file was
 * already open. Returns ERROR_GENERAL if an error occurred
 * opening the file.
 */
int open_infile(const char *file);

/* Closes the current input file (if there is one).
 */
void close_infile();

/* Gets the next token from the input file.
 * Returns the token. token_length is the length of the token.
 * The token is a NULL terminated string.
 * *token_length doesn't include the NULL character.
 * After the token has been used, PLEASE free the memory allocated for it.
 * If token_length == 0, there are no more tokens.
 */
char *next_token(int *token_length);

/* Adds a variable into a list for creating an array. The variable is 
 * chosen from THE list of variables according to the given symbol. */
int add_symbol(char *symbol);

/* Gets the parsed variable according to the symbol. */
Variable get_variable(char *symbol);

/* Adds a potential and the correspondent variable references into a list.
 * The "ownership" of the vars[] array changes! */
int add_initData(potential p, Variable child, Variable* parents);

/* Adds a variable into THE list of variables. */
int add_pvar(Variable var);

/* Adds a number into the list of parsed numbers. */
int add_double(double d);

/* Adds a string into the list of parsed strings. */
int add_string(char* string);

/* Creates an array from the variable references in the temp list. 
 * The size will be symbols_parsed. */
Variable* make_variable_array();

/* Creates an array from the double values in the list. 
 * The size will be doubles_parsed. */
double* make_double_array();

/* Creates an array from the strings in the list. 
 * The size will be strings_parsed. */
char** make_string_array();

/* Removes everything from the list of doubles. This is likely to be used 
 * after the parser has parsed doubles to the list, created an array out 
 * of it and wants to reset the list for future use. */
int reset_doubles();

/* Removes everything from the list of strings and resets the counter. */
int reset_strings();

/* Removes everything from the temporary list of variables. */
int reset_symbols();

/* Frees some memory after parsing. */
int reset_initData();

#endif /* __PARSER_H__ */
