/*
 * Variable.c $Id: Variable.c,v 1.51 2005-03-16 12:14:17 jatoivol Exp $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Variable.h"
#include "potential.h"
#include "errorhandler.h"

static varlink nip_first_var = NULL;
static varlink nip_last_var = NULL;
static int nip_vars_parsed = 0;

static int variable_name(Variable v, const char *name);


/*
 * Gives the Variable a verbose name.
 */
static int variable_name(Variable v, const char *name){
  if(!name)
    return ERROR_NULLPOINTER; /* possibly a normal situation */
  strncpy(v->name, name, VAR_NAME_LENGTH);
  v->name[VAR_NAME_LENGTH] = '\0';
  return NO_ERROR;
}


Variable new_variable(const char* symbol, const char* name, 
		      char** states, int cardinality){
  /* NOTE: This id-stuff may overflow if variables are created and 
   * freed over and over again. */
  static long id = VAR_MIN_ID;
  int i, j;
  double *dpointer;
  Variable v;
  varlink new;

  v = (Variable) malloc(sizeof(vtype));
  if(!v){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  new = (varlink) malloc(sizeof(varelement));
  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(v);
    return NULL;
  }

  v->cardinality = cardinality;
  v->id = id++;
  v->previous = NULL;
  v->next = NULL;

  v->parents = NULL;
  v->num_of_parents = 0;
  v->family_clique = NULL;
  v->family_mapping = NULL;
 
  strncpy(v->symbol, symbol, VAR_SYMBOL_LENGTH);
  v->symbol[VAR_SYMBOL_LENGTH] = '\0';

  if(variable_name(v, name) == ERROR_NULLPOINTER)
    /* DANGER! The name can be omitted and consequently be NULL */
    v->name[0] = '\0';

  if(states){

    v->statenames = (char **) calloc(cardinality, sizeof(char *));
    if(!(v->statenames)){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free(v);
      free(new);
      return NULL;
    }

    for(i = 0; i < cardinality; i++){

      v->statenames[i] = (char *) calloc(strlen(states[i]) + 1, 
					   sizeof(char));
      if(!(v->statenames[i])){
	report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	for(j = 0; j < i; j++)
	  free(v->statenames[j]);
	free(v->statenames);
	free(v);
	free(new);
      }

      strcpy(v->statenames[i], states[i]);
    }
  }
  else
    report_error(__FILE__, __LINE__, ERROR_INVALID_ARGUMENT, 1);

  v->likelihood = (double *) calloc(cardinality, sizeof(double));
  if(!(v->likelihood)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    for(i = 0; i < v->cardinality; i++)
      free(v->statenames[i]);
    free(v->statenames);
    free(v);
    free(new);
  }

  /* initialise likelihoods to 1 */
  for(dpointer=v->likelihood, i=0; i < cardinality; *dpointer++ = 1, i++);

  new->data = v;
  new->fwd = NULL;
  new->bwd = nip_last_var;
  if(nip_first_var == NULL)
    nip_first_var = new;
  else
    nip_last_var->fwd = new;
  nip_last_var = new;
  nip_vars_parsed++;

  return v;
}


Variable copy_variable(Variable v){
  int i;
  Variable copy;

  if(v == NULL)
    return NULL;

  copy = (Variable) malloc(sizeof(vtype));
  if(!copy){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  copy->cardinality = v->cardinality;
  copy->id = v->id;

  strncpy(copy->name, v->name, VAR_NAME_LENGTH);
  copy->name[VAR_NAME_LENGTH] = '\0';

  copy->num_of_parents = v->num_of_parents;
  if(v->parents){
    copy->parents = (Variable*) calloc(v->num_of_parents, sizeof(Variable));
    if(!(copy->parents)){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free(copy);
      return NULL;
    }
  }
  else
    copy->parents = NULL;
  copy->family_clique = v->family_clique;

  copy->likelihood = (double *) calloc(copy->cardinality, sizeof(double));

  if(!(copy->likelihood)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(copy->parents);
    free(copy);
    return NULL;
  }  

  /* initialise likelihoods to 1 */
  for(i = 0; i < copy->cardinality; i++)
    copy->likelihood[i] = v->likelihood[i];

  return copy;
}


void free_variable(Variable v){
  int i;
  if(v == NULL)
    return;
  if(v->statenames)
    for(i = 0; i < v->cardinality; i++)
      free(v->statenames[i]);
  free(v->statenames);
  free(v->parents);
  free(v->family_mapping);
  free(v->likelihood);
  free(v);
}


int equal_variables(Variable v1, Variable v2){
  if(v1 && v2)
    return (v1->id == v2->id);
  return 0; /* FALSE if nullpointers */
}


unsigned long get_id(Variable v){
  if(v)
    return v->id;
  return 0;
}


char *get_symbol(Variable v){
  if(v)
    return v->symbol;
  return NULL;
}


int get_stateindex(Variable v, char *state){
  int i;
  if(!v->statenames)
    return -1;
  for(i = 0; i < v->cardinality; i++)
    if(strcmp(state, v->statenames[i]) == 0)
      return i;
  return -1;
}


int total_num_of_vars(){
  return nip_vars_parsed;
}


varlink get_first_variable(){
  return nip_first_var;
}


varlink get_last_variable(){
  return nip_last_var;
}


void reset_Variable_list(){
  nip_first_var = NULL;
  nip_last_var = NULL;
  nip_vars_parsed = 0;
}


Variable next_Variable(Variable_iterator* it){
  Variable v;
  if(*it){
    v = (*it)->data;
    *it = (*it)->fwd;
  }
  else
    v = NULL;
  return v;
}


Variable get_parser_variable(char *symbol){

  Variable v; 
  Variable_iterator it = nip_first_var; /* a private copy */
  v = next_Variable(&it);

  if(v == NULL)
    return NULL; /* didn't find the variable (possibly normal) */
  
  /* search for the variable reference */
  while(strcmp(symbol, v->symbol) != 0){
    v = next_Variable(&it);
    if(v == NULL){
      return NULL; /* didn't find the variable (a normal situation) */
    }
  }

  return v;

}

Variable get_variable(Variable* vars, int nvars, char *symbol){

  int i;
  Variable v; 
  
  /* search for the variable reference */
  for(i = 0; i < nvars; i++){
    v = vars[i];
    if(strcmp(symbol, v->symbol) == 0)
      return v;
  }

  return NULL;
}


int update_likelihood(Variable v, double likelihood[]){

  int i;
  if(v == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return ERROR_NULLPOINTER;
  }

  for(i = 0; i < v->cardinality; i++)
    (v->likelihood)[i] = likelihood[i];

  return NO_ERROR;
}


void reset_likelihood(Variable v){
  int i;
  if(v == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return;
  }
  
  for(i = 0; i < v->cardinality; i++)
    (v->likelihood)[i] = 1;
}


int number_of_values(Variable v){
  if(v == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return -1;
  }
  return v->cardinality;
}


int number_of_parents(Variable v){
  if(v == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return -1;
  }
  return v->num_of_parents;
}


int set_parents(Variable v, Variable *parents, int nparents){
  int i;
  if(v == NULL || parents == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return ERROR_NULLPOINTER;
  }
  
  v->parents = (Variable *) calloc(nparents, sizeof(Variable));
  if(!(v->parents)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  for(i = 0; i < nparents; i++)
    v->parents[i] = parents[i]; /* makes a copy of the array */
  
  v->num_of_parents = nparents;  
  return NO_ERROR;
}


Variable* get_parents(Variable v){
  if(v == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return NULL;
  }
  return v->parents;
}


Variable *sort_variables(Variable *vars, int num_of_vars){

  /* Selection sort (simple, fast enough) */

  int i, j;
  Variable *sorted;
  Variable temp;

  if(num_of_vars < 1){
    report_error(__FILE__, __LINE__, ERROR_INVALID_ARGUMENT, 1);
    return NULL;
  }

  sorted = (Variable *) calloc(num_of_vars, sizeof(Variable));

  if(!sorted){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  /* Fill the sorted array naively (unsorted) */
  for(i = 0; i < num_of_vars; i++)
    sorted[i] = vars[i];

  /* Sort in place (lots of swaps, I know...) */
  for(i = 0; i < num_of_vars - 1; i++)
    for(j = 1; j < num_of_vars; j++)
      if(sorted[j]->id < sorted[i]->id){
	temp = sorted[j];
	sorted[j] = sorted[i];
	sorted[i] = temp;
      }

  return sorted;

}
