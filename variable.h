/*
 * variable.h $Id: variable.h,v 1.1 2005-05-27 13:18:04 jatoivol Exp $
 */

#ifndef __VARIABLE_H__
#define __VARIABLE_H__

#include "potential.h"
#define VAR_SYMBOL_LENGTH 20
#define VAR_NAME_LENGTH 40
#define VAR_STATENAME_LENGTH 20
#define VAR_MIN_ID 1

/***
 * TODO: a set of mutators etc. for handling parents... 
 */

struct nip_var {
  char symbol[VAR_SYMBOL_LENGTH + 1]; /* short symbol for the node */
  char name[VAR_NAME_LENGTH + 1];     /* label in the Net language*/
  char **statenames;  /* a string array with <cardinality> strings */
  int cardinality;    /* number of possible values */
  unsigned long id;   /* unique id for every variable */
  double *likelihood; /* likelihood of each value */
  double *prior;      /* prior prob. of each value for an indep. variable */
  struct nip_var *previous; /* pointer to the variable which corresponds to
			     * this one in the previous timeslice */
  struct nip_var *next;     /* pointer to the variable which corresponds to
			     * this one in the next timeslice */
  struct nip_var **parents; /* array of pointers to the parents */
  int num_of_parents;       /* number of parents */
  void *family_clique;      /* possible reference to the family clique */
  int *family_mapping;      /* maps the variables in the family to the
			     * variables in the family clique */
};

typedef struct nip_var vtype;
typedef vtype *variable;


struct varlist {
  variable data;
  struct varlist *fwd;
  struct varlist *bwd;
};

typedef struct varlist varelement;
typedef varelement *varlink;
typedef varlink variable_iterator;

/* Creates a new variable:
 * - symbol is a short name e.g. A (= array [A, \0])
 * - name is a more verbose name e.g. "rain" or NULL 
 * - states is an array of strings containing the state names or NULL
 * - cardinality is the number of states/values the variable has */
variable new_variable(const char* symbol, const char* name, 
		      char** states, int cardinality);


/* Function for copying a variable (if needed). Handle with care.
 * v: the variable to be copied
 * Returns the copy.
 */
variable copy_variable(variable v);


/* Frees the memory used by the variable v. NOTE: REMEMBER TO REMOVE 
 * THE VARIABLE FROM ALL POSSIBLE LISTS TOO. */
void free_variable(variable v);


/* Method for testing variable equality. 
 * This may be needed to keep potentials in order. INEQUALITIES ???
 */
int equal_variables(variable v1, variable v2);


/* An alternative interface for keeping variables 
 * and thus the potentials in order.
 */
unsigned long get_id(variable v);


/* Returns the symbol of the variable (a reference). It is a string 
 * (or NULL if nullpointer given). */
char *get_symbol(variable v);


/* Gives the numerical representation of the variable state. 
 * Numbers are [0 ... <cardinality-1>] or -1 if the variable doesn't have
 * such a state. This function is needed when parsing data. */
int get_stateindex(variable v, char *state);


/* Tells the length of the list of variables. */
int total_num_of_vars();


/* Call this before searching through the list of variables. */
varlink get_first_variable();


varlink get_last_variable();


/* Call this after a model is parsed from a file and is ready. */
void reset_variable_list();


/* Gives the next variable in the list of variables. Returns NULL when 
 * the end of list is reached. */
variable next_variable(variable_iterator *it);


/* Gets the parsed variable according to the symbol. */
variable get_variable(variable* vars, int nvars, char *symbol);

/* Gets the variable according to the symbol (when parsing). */
variable get_parser_variable(char *symbol);

/* Gives v a new likelihood array. The size of the array
 * must match v->cardinality. Returns an error code.
 */
int update_likelihood(variable v, double likelihood[]);


/* Sets a uniform likelihood for v. */
void reset_likelihood(variable v);


/* Returns the number of possible values in the variable v. (-1 if v == NULL)
 */
int number_of_values(variable v);


/* Tells how many parents the variable has. */
int number_of_parents(variable v);


/* Sets the parents for the variable v. */
int set_parents(variable v, variable *parents, int nparents);


/* Tells which variables (*p) are the parents of the variable v. */
variable* get_parents(variable v);


/* Sets the prior for an (independent) variable v. You SHOULD not 
 * set a prior for a variable which has parents. */
int set_prior(variable v, double* prior);


/* Tells the prior distribution of a variable. 
 * (NULL if v depends on others) */
double* get_prior(variable v);


/* Returns a new array (allocates memory!) that contains the given variables
 * in ascending order according to their ID number.
 */
variable *sort_variables(variable *vars, int num_of_vars);

#endif /* __VARIABLE_H__ */