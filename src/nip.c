/*
 * nip.c $Id: nip.c,v 1.47 2005-03-16 12:14:17 jatoivol Exp $
 */

#include "nip.h"
#include "parser.h"
#include "Clique.h"
#include "Variable.h"
#include "errorhandler.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
#define DEBUG_NIP
*/

/***** 
 * DONE:
 * + some sort of a function for the forward-backward algorithm
 *   + a data type that can be returned by the algorithm (how to index?)
 *   + some abstraction for a time series (structure and access to data..?)
 *   + NOTE: this turned out to need linear space requirement w.r.t. time 
 *           (assuming sepsets between timeslices have a constant size)

 * TODO: 
 * - Viterbi algorithm for the ML-estimate of the latent variables
 *   - another forward-like algorithm with elements of dynamic programming

 
 * - EM algorithm for estimating parameters of the model
 *   - Invent a concise and efficient way of computing each of the parameters.
 *     - in which order? ("Time-First" or "Family-First")
 *     - one kind of solution:
 *       1: run family_inference -> FamilySeries == P(c, PA(c) | t)
 *       2: sum up the "soft" frequencies of the (multidimensional) events
 *       3: normalise the results and create suitable potentials out of them
 *          -> P(x | pa(x))
 *
 *   - Find a neat way to replace the original parameters of the model.
 *     - gather a potential for each family of variables
 *     - initialise with saved potentials? (see parser.c line 1115)
 *
 *   - Determine the parameters of the algorithm
 *     - when to stop?
 *****/

extern int yyparse();

static int start_timeslice_message_pass(Nip model, int direction, 
					potential sepset);
static int finish_timeslice_message_pass(Nip model, int direction,
					 potential num, potential den);


void reset_model(Nip model){
  int i, retval;

  for(i = 0; i < model->num_of_vars; i++)
    reset_likelihood(model->variables[i]);
  retval = global_retraction(model->variables, model->num_of_vars, 
			     model->cliques, model->num_of_cliques);
  if(retval != NO_ERROR)
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
}


Nip parse_model(char* file){
  int i, j, retval;
  Variable temp;
  Variable_iterator it;
  Nip new = (Nip) malloc(sizeof(nip_type));

  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }
  
  /* 1. Parse */
  if(open_yyparse_infile(file) != NO_ERROR){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    return NULL;
  }

  retval = yyparse();

  close_yyparse_infile();

  if(retval != 0)
    return NULL;

  /* 2. Get the parsed stuff and make a model out of them */
  new->num_of_cliques = get_num_of_cliques();
  new->num_of_vars = total_num_of_vars();
  new->cliques = *get_cliques_pointer();

  new->variables = (Variable*) calloc(new->num_of_vars, sizeof(Variable));
  if(!(new->variables)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(new);
    return NULL;
  }
    
  it = get_first_variable();
  temp = next_Variable(&it);
  i = 0;
  while(temp != NULL){
    new->variables[i++] = temp;
    temp = next_Variable(&it);
  }
  
  /* count the number of nexts and children */
  for(i = 0; i < new->num_of_vars; i++){
    if(new->variables[i]->next)
      new->num_of_nexts++;

    if(new->variables[i]->parents)
      new->num_of_children++;
  }

  new->next = (Variable*) calloc(new->num_of_nexts, sizeof(Variable));
  new->previous = (Variable*) calloc(new->num_of_nexts, sizeof(Variable));
  new->children = (Variable*) calloc(new->num_of_children, sizeof(Variable));
  if(!(new->children && new->previous && new->next)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(new->variables);
    free(new->next);
    free(new->previous);
    free(new);
    return NULL;
  }
  
  j = 0;
  for(i = 0; i < new->num_of_vars; i++)  
    if(new->variables[i]->next){
      new->next[j] = new->variables[i];
      new->previous[j] = new->variables[i]->next;
      j++;
    }

  for(i = 0; i < new->num_of_vars; i++)
    if(new->variables[i]->parents)
      new->children[i] = new->variables[i]; /* set the children */
  
  new->front_clique = NULL;
  new->tail_clique = NULL;

  /* 4. Reset parser globals */
  it = get_last_variable(); /* free the list structure */
  while(it->bwd){
    it = it->bwd;
    free(it->fwd);
  }
  free(it);
  reset_Variable_list();
  reset_Clique_array();

  return new;
}


void free_model(Nip model){
  int i;

  /* 1. Free Cliques and adjacent Sepsets */
  for(i = 0; i < model->num_of_cliques; i++)
    free_Clique(model->cliques[i]);
  free(model->cliques);

  /* 2. Free the Variables */
  for(i = 0; i < model->num_of_vars; i++)
    free_variable(model->variables[i]);
  free(model->variables);
  free(model->children);
  free(model->next);
  free(model->previous);
  free(model);
}


TimeSeries read_timeseries(Nip model, char* filename){
  int i, j, k, m;
  char** tokens = NULL;
  TimeSeries ts = NULL;
  datafile* df = NULL;
  
  ts = (TimeSeries) malloc(sizeof(time_series_type));
  if(!ts){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }
  ts->model = model;

  df = open_datafile(filename, ',', 0, 1);
  if(df == NULL){
    report_error(__FILE__, __LINE__, ERROR_FILENOTFOUND, 1);
    fprintf(stderr, "%s\n", filename);
    free(ts);
    return NULL;
  }  
  ts->length = df->datarows;

  /* Find out how many (totally) latent variables there are. */
  ts->num_of_hidden = model->num_of_vars - df->num_of_nodes;

  /* Allocate the array for the hidden variables. */
  ts->hidden = (Variable *) calloc(ts->num_of_hidden, sizeof(Variable));
  ts->observed = (Variable *) calloc(df->num_of_nodes, sizeof(Variable));
  if(!(ts->hidden && ts->observed)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(ts->hidden);
    free(ts);
    close_datafile(df);
    return NULL;
  }  

  /* Set the pointers to the hidden variables. */
  m = 0;
  for(k = 0; k < model->num_of_vars; k++){
    j = 1;
    for(i = 0; i < df->num_of_nodes; i++)
      if(equal_variables(model->variables[k], 
			 get_Variable(model, df->node_symbols[i])))
	j = 0;
    if(j)
      ts->hidden[m++] = model->variables[k];
  }

  for(i = 0; i < df->num_of_nodes; i++)
    ts->observed[i] = get_Variable(model, df->node_symbols[i]);

  /* Allocate some space for data */
  ts->data = (int**) calloc(ts->length, sizeof(int*));
  if(!(ts->data)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(ts->hidden);
    free(ts->observed);
    free(ts);
    close_datafile(df);
    return NULL;
  }
  for(i = 0; i < ts->length; i++){
    ts->data[i] = (int*) calloc(df->num_of_nodes, sizeof(int));
    if(!(ts->data[i])){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      for(j = 0; j < i; j++)
	free(ts->data[j]);
      free(ts->data);
      free(ts->hidden);
      free(ts->observed);
      free(ts);
      close_datafile(df);
      return NULL;
    }
  }

  /* Get the data */
  for(j = 0; j < ts->length; j++){
    m = nextline_tokens(df, ',', &tokens); /* 2. Read */
    assert(m == df->num_of_nodes);

    /* 3. Put into the data array */
    for(i = 0; i < m; i++){
      ts->data[j][i] = get_stateindex(get_Variable(model, 
						   df->node_symbols[i]), 
				      tokens[i]);

      /* Q: Should missing data be allowed?   A: Yes. */
      /* assert(data[j][i] >= 0); */
    }

    for(i = 0; i < m; i++) /* 4. Dump away */
      free(tokens[i]);
    free(tokens);
  }

  close_datafile(df);
  return ts;
}


void free_timeseries(TimeSeries ts){
  int t;
  if(ts){
    for(t = 0; t < ts->length; t++)
      free(ts->data[t]);
    free(ts->data);
    free(ts->hidden);
    free(ts->observed);
    free(ts);
  }
}


int timeseries_length(TimeSeries ts){
  return ts->length;
}


void free_uncertainseries(UncertainSeries ucs){
  int i, t;
  if(ucs){
    for(t = 0; t < ucs->length; t++){
      for(i = 0; i < ucs->num_of_vars; i++)
	free(ucs->data[t][i]);
      free(ucs->data[t]);
    }
    free(ucs->variables);
    free(ucs);
  }
}


int uncertainseries_length(UncertainSeries ucs){
  return ucs->length;
}


void free_familyseries(FamilySeries fs){
  int t, c;
  if(fs){
    for(t=0; t < fs->length; t++){
      for(c=0; c < fs->model->num_of_children; c++)
	free_potential(fs->families[t][c]);
      free(fs->families[t]);
    }
    free(fs->families);
    free(fs);
  }
}


int familyseries_length(FamilySeries fs){
  return fs->length;
}


char* get_observation(TimeSeries ts, Variable v, int time){
  int i, j = -1;
  for(i = 0; i < ts->model->num_of_vars - ts->num_of_hidden; i++)
    if(equal_variables(v, ts->observed[i]))
      j = i;

  if(j < 0)
    return NULL;

  return v->statenames[ts->data[time][j]];
}


int set_observation(TimeSeries ts, Variable v, int time, char* observation){
  int i, j = -1;
  for(i = 0; i < ts->model->num_of_vars - ts->num_of_hidden; i++)
    if(equal_variables(v, ts->observed[i]))
      j = i;

  i = get_stateindex(v, observation);
  /* a valid variable? a valid observation for that variable? */
  if((j < 0) || (i < 0))
    return ERROR_INVALID_ARGUMENT;

  ts->data[time][j] = i;
  return 0;
}


int insert_hard_evidence(Nip model, char* variable, char* observation){
  int ret;
  Variable v = get_Variable(model, variable);
  if(v == NULL)
    return ERROR_INVALID_ARGUMENT;
  ret = enter_observation(model->variables, model->num_of_vars, 
			  model->cliques, model->num_of_cliques, 
			  v, observation);
  if(ret != NO_ERROR)
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);

  make_consistent(model);
  return ret;
}


/* Starts a message pass between timeslices */
static int start_timeslice_message_pass(Nip model, int direction,
					potential sepset){
  int i, j, k;
  int *mapping;
  Clique c;
  Variable *vars;
  int nvars = model->num_of_nexts;

  if(direction == FORWARD){
    vars = model->next;
    c = model->front_clique; /* null if not yet memoized */
  }
  else{
    vars = model->previous;
    c = model->tail_clique; /* null if not yet memoized */
  }

  /* some cliques are memoized */
  if(c == NULL){
    c = find_clique(model->cliques, model->num_of_cliques, vars, nvars);

    /* this saves the result */
    if(direction == FORWARD)
      model->front_clique = c; 
    else
      model->tail_clique = c;
  }
  assert(c != NULL);

  mapping = (int*) calloc(c->p->num_of_vars - nvars, sizeof(int));
  if(!mapping){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  k = 0;
  for(i=0; i < c->p->num_of_vars; i++){
    if(k == nvars)
      break; /* all pointers found */
    for(j=0; j < nvars; j++)
      if(equal_variables((c->variables)[i], vars[j])){
	mapping[j] = i;
	k++;
      }
  }    

  general_marginalise(c->p, sepset, mapping);
  free(mapping);

  return NO_ERROR;
}


/* Finishes the message pass between timeslices */
static int finish_timeslice_message_pass(Nip model, int direction,
					 potential num, potential den){
  int i, j, k;
  int *mapping;
  Clique c;
  Variable *vars;
  int nvars = model->num_of_nexts;

  /* This uses memoization for finding a suitable Clique c */
  if(direction == FORWARD){
    vars = model->previous;
    c = model->tail_clique;
  }
  else{
    vars = model->next;
    c = model->front_clique;
  }

  if(c == NULL){
    c = find_clique(model->cliques, model->num_of_cliques, vars, nvars);
    if(direction == FORWARD)
      model->tail_clique = c;
    else
      model->front_clique = c;
  }
  assert(c != NULL);


  mapping = (int*) calloc(c->p->num_of_vars - nvars, sizeof(int));
  if(!mapping){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }
  k = 0;
  for(i=0; i < c->p->num_of_vars; i++){
    if(k == nvars)
      break; /* all pointers found */
    for(j=0; j < nvars; j++)
      if(equal_variables((c->variables)[i], vars[j])){
	mapping[j] = i;
	k++;
      }
  }

  update_potential(num, den, c->p, mapping);
  free(mapping);
  
  return NO_ERROR;
}


/* forward-only inference consumes constant (1 time slice) amount of memory 
 * + the results (which is linear) */
UncertainSeries forward_inference(TimeSeries ts, Variable vars[], int nvars){
  int i, k, t;
  int *cardinalities = NULL;
  Variable temp;
  potential timeslice_sepset = NULL;
  Clique clique_of_interest;
  UncertainSeries results = NULL;
  Nip model = ts->model;
  
  /* Allocate an array */
  if(model->num_of_nexts > 0){
    cardinalities = (int*) calloc(ts->model->num_of_nexts, sizeof(int));
    if(!cardinalities){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return NULL;
    }
  }

  /* Fill the array */
  k = 0;
  for(i = 0; i < ts->num_of_hidden; i++){
    temp = ts->hidden[i];
    if(temp->next)
      cardinalities[k++] = number_of_values(temp);
  }

  /* Allocate some space for the results */
  results = (UncertainSeries) malloc(sizeof(uncertain_series_type));
  if(!results){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(cardinalities);
    return NULL;
  }
  
  results->variables = (Variable*) calloc(nvars, sizeof(Variable));
  if(!results->variables){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(results);
    free(cardinalities);
    return NULL;
  }

  /* Copy the references to the variables of interest */
  memcpy(results->variables, vars, nvars*sizeof(Variable));
  results->num_of_vars = nvars;
  results->length = ts->length;

  results->data = (double***) calloc(ts->length, sizeof(double**));
  if(!results->data){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(results->variables);
    free(results);
    free(cardinalities);
    return NULL;
  }
  
  for(t = 0; t < results->length; t++){
    results->data[t] = (double**) calloc(nvars, sizeof(double*));
    if(!results->data[t]){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      while(t > 0){
	t--;
	for(i = 0; i < nvars; i++)
	  free(results->data[t][i]); /* Fixed 15.3.2005. Did it help? */
	free(results->data[t]);
      }
      free(results->data); /* t == -1 */
      free(results->variables);
      free(results);
      free(cardinalities);
      return NULL;
    }

    for(i = 0; i < nvars; i++){
      results->data[t][i] = (double*) calloc(number_of_values(vars[i]),
					     sizeof(double));
      if(!results->data[t][i]){
	report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	while(i > 0)
	  free(results->data[t][--i]);
	free(results->data[t]);
	while(t > 0){
	  t--;
	  for(i = 0; i < nvars; i++)
	    free(results->data[t][i]);
	  free(results->data[t]);
	}
	free(results->data);
	free(results->variables); /* t == -1 */
	free(results);
	free(cardinalities);
	return NULL;
      }
    }
  }

  /* Initialise the intermediate potential */
  timeslice_sepset = make_potential(cardinalities, model->num_of_nexts, NULL);
  free(cardinalities);

  /*****************/
  /* Forward phase */
  /*****************/
  for(t = 0; t < ts->length; t++){ /* FOR EVERY TIMESLICE */
    
    /* Put some data in */
    for(i = 0; i < model->num_of_vars - ts->num_of_hidden; i++)
      if(ts->data[t][i] >= 0)
	enter_i_observation(model->variables, model->num_of_vars, 
			    model->cliques, model->num_of_cliques, 
			    ts->observed[i], ts->data[t][i]);
    
    
    if(t > 0)
      if(finish_timeslice_message_pass(model, FORWARD, 
				       timeslice_sepset, NULL) != NO_ERROR){
	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_uncertainseries(results);
	free_potential(timeslice_sepset);
	return NULL;
      }
    
    /* Do the inference */
    make_consistent(model);

    /* Write the results */
    for(i = 0; i < results->num_of_vars; i++){
      
      /* 1. Decide which Variable you are interested in */
      temp = results->variables[i];
      
      /* 2. Find the Clique that contains the family of 
       *    the interesting Variable */
      clique_of_interest = find_family(model->cliques, model->num_of_cliques, 
				       temp);
      assert(clique_of_interest != NULL);
      
      /* 3. Marginalisation (the memory must have been allocated) */
      marginalise(clique_of_interest, temp, results->data[t][i]);
      
      /* 4. Normalisation */
      normalise(results->data[t][i], number_of_values(temp));
    }
    
    /* Start a message pass between timeslices */
    if(start_timeslice_message_pass(model, FORWARD, 
				    timeslice_sepset) != NO_ERROR){
      report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
      free_uncertainseries(results);
      free_potential(timeslice_sepset);
      return NULL;
    }

    /* Forget old evidence */
    reset_model(model);
  }

  free_potential(timeslice_sepset);

  return results;
}


/* This consumes much more memory depending on the size of the 
 * sepsets between time slices. */
UncertainSeries forward_backward_inference(TimeSeries ts,
					   Variable vars[], int nvars){
  int i, k, t;
  int *cardinalities = NULL;
  Variable temp;
  potential *timeslice_sepsets = NULL;
  Clique clique_of_interest;
  UncertainSeries results = NULL;
  Nip model = ts->model;

  /* Allocate an array for describing the dimensions of timeslice sepsets */
  if(model->num_of_nexts > 0){
    cardinalities = (int*) calloc(model->num_of_nexts, sizeof(int));
    if(!cardinalities){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return NULL;
    }
  }

  /* Fill the array */
  k = 0;
  for(i = 0; i < ts->num_of_hidden; i++){
    temp = ts->hidden[i];
    if(temp->next)
      cardinalities[k++] = number_of_values(temp);
  }

  /* Allocate some space for the results */
  results = (UncertainSeries) malloc(sizeof(uncertain_series_type));
  if(!results){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(cardinalities);
    return NULL;
  }
  
  results->variables = (Variable*) calloc(nvars, sizeof(Variable));
  if(!results->variables){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(results);
    free(cardinalities);
    return NULL;
  }

  /* Copy the references to the variables of interest */
  memcpy(results->variables, vars, nvars*sizeof(Variable));
  results->num_of_vars = nvars;
  results->length = ts->length;

  results->data = (double***) calloc(ts->length, sizeof(double**));
  if(!results->data){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(results->variables);
    free(results);
    free(cardinalities);
    return NULL;
  }
  
  for(t = 0; t < results->length; t++){
    results->data[t] = (double**) calloc(nvars, sizeof(double*));
    if(!results->data[t]){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      while(t > 0){
	t--;
	for(i = 0; i < nvars; i++)
	  free(results->data[t][i]); /* Fixed 15.3.2005. Did it help? */
	free(results->data[t]);
      }
      free(results->data); /* t == 0 */
      free(results->variables);
      free(results);
      free(cardinalities);
      return NULL;
    }

    for(i = 0; i < nvars; i++){
      results->data[t][i] = (double*) calloc(number_of_values(vars[i]),
					     sizeof(double));
      if(!results->data[t][i]){
	report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	while(i > 0)
	  free(results->data[t][--i]);
	free(results->data[t]);
	while(t > 0){
	  t--;
	  for(i = 0; i < nvars; i++)
	    free(results->data[t][i]);
	  free(results->data[t]);
	}
	free(results->data); /* t == 0 */
	free(results->variables);
	free(results);
	free(cardinalities);
	return NULL;
      }
    }
  }


  /* Allocate some space for the intermediate potentials */
  timeslice_sepsets = (potential *) calloc(ts->length + 1, sizeof(potential));

  /* Initialise intermediate potentials */
  for(t = 0; t <= ts->length; t++){
    timeslice_sepsets[t] = make_potential(cardinalities, model->num_of_nexts, 
					  NULL);
  }
  free(cardinalities);


  /*****************/
  /* Forward phase */
  /*****************/
  for(t = 0; t < ts->length; t++){ /* FOR EVERY TIMESLICE */
    
    /* Put some data in */
    for(i = 0; i < model->num_of_vars - ts->num_of_hidden; i++)
      if(ts->data[t][i] >= 0)
	enter_i_observation(model->variables, model->num_of_vars, 
			    model->cliques, model->num_of_cliques, 
			    ts->observed[i], ts->data[t][i]);
    
    
    if(t > 0)
      if(finish_timeslice_message_pass(model, FORWARD, 
				       timeslice_sepsets[t-1], 
				       NULL)                   != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_uncertainseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }
    
    /* Do the inference */
    make_consistent(model);
    
    /* Start a message pass between timeslices */
    if(start_timeslice_message_pass(model, FORWARD,
				    timeslice_sepsets[t]) != NO_ERROR){
      report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
      free_uncertainseries(results);
      for(i = 0; i <= ts->length; i++)
	free_potential(timeslice_sepsets[i]);
      free(timeslice_sepsets);
      return NULL;
    }

    /* Forget old evidence */
    reset_model(model);
  }
  
  /******************/
  /* Backward phase */
  /******************/
  
  /* forget old evidence */
  reset_model(model);
  
  for(t = ts->length - 1; t >= 0; t--){ /* FOR EVERY TIMESLICE */
    
    /* Put some evidence in */
    for(i = 0; i < model->num_of_vars - ts->num_of_hidden; i++)
      if(ts->data[t][i] >= 0)
	enter_i_observation(model->variables, model->num_of_vars, 
			    model->cliques, model->num_of_cliques, 
			      ts->observed[i], ts->data[t][i]);

    /* Pass the message from the past */
    if(t > 0)
      if(finish_timeslice_message_pass(model, FORWARD, 
				       timeslice_sepsets[t-1], 
				       NULL)                   != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_uncertainseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }
    
    /* Pass the message from the future */
    if(t < ts->length - 1)
      if(finish_timeslice_message_pass(model, BACKWARD, 
				       timeslice_sepsets[t+1], 
				       timeslice_sepsets[t]) != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_uncertainseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }

    /* Do the inference */
    make_consistent(model);
    
    /* Write the results */
    for(i = 0; i < results->num_of_vars; i++){
      
      /* 1. Decide which Variable you are interested in */
      temp = results->variables[i];
      
      /* 2. Find the Clique that contains the family of 
       *    the interesting Variable */
      clique_of_interest = find_family(model->cliques, model->num_of_cliques, 
				       temp);
      assert(clique_of_interest != NULL);
      
      /* 3. Marginalisation (the memory must have been allocated) */
      marginalise(clique_of_interest, temp, results->data[t][i]);
      
      /* 4. Normalisation */
      normalise(results->data[t][i], number_of_values(temp));
    }
    

    if(t > 0)
      if(start_timeslice_message_pass(model, BACKWARD, 
				      timeslice_sepsets[t]) != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_uncertainseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }

    /* forget old evidence */
    reset_model(model);
  }

  /* free the intermediate potentials */
  for(t = 0; t <= ts->length; t++)
    free_potential(timeslice_sepsets[t]);
  free(timeslice_sepsets);

  return results;
}


/* My apologies: this function is probably the worst copy-paste case ever. */
FamilySeries family_inference(TimeSeries ts){
  int i, j, k, t, size;
  int *cardinalities = NULL;
  Variable temp;
  potential *timeslice_sepsets = NULL;
  Clique clique_of_interest;
  FamilySeries results = NULL;
  Nip model = ts->model;

  /* Allocate some space for the results */
  results = (FamilySeries) malloc(sizeof(family_series_type));
  if(!results){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }
  
  results->model = ts->model; /* Some shared properties... */
  results->length = ts->length;

  results->families = (potential**) calloc(ts->length, sizeof(potential*));
  if(!results->families){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(results);
    return NULL;
  }
  
  for(t = 0; t < results->length; t++){
    results->families[t] = 
      (potential*) calloc(results->model->num_of_children, sizeof(potential));
    if(!results->families[t]){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      while(t > 0)
	free(results->families[--t]);
      free(results->families);
      free(results);
      return NULL;
    }
  }

  for(i = 0; i < results->model->num_of_children; i++){
    temp = results->model->children[i];
    k = temp->num_of_parents + 1;
    cardinalities = (int *) calloc(k, sizeof(int));
    if(!cardinalities){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      while(i > 0){
	i--;
	for(t = 0; t < results->length; t++)
	  free_potential(results->families[t][i]); /* Not quite sure... */
      }
      for(t = 0; t < results->length; t++)
	free(results->families[t]);
      free(results->families);
      free(results);
      return NULL;
    }

    cardinalities[0] = temp->cardinality;
    for(t = 1; t < k; t++){
      cardinalities[t] = temp->parents[t]->cardinality;
    }

    for(t = 0; t < results->length; t++){
      results->families[t][i] = make_potential(cardinalities, k, NULL);
      if(!results->families[t][i]){
	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	while(t > 0)
	  free_potential(results->families[--t][i]);
	while(i > 0){
	  for(t = 0; t < results->length; t++)
	    free(results->families[t][i]); /* Not quite sure this works... */
	  i--;
	}
	for(t = 0; t < results->length; t++)
	  free(results->families[t]);
	free(results->families);
	free(results);
	free(cardinalities);
	return NULL;
      }
    }
    free(cardinalities);
  }


  /* Allocate some space for the intermediate potentials */
  timeslice_sepsets = (potential *) calloc(ts->length + 1, sizeof(potential));

  /* Allocate an array for describing the dimensions of timeslice sepsets */
  if(model->num_of_nexts > 0){
    cardinalities = (int*) calloc(model->num_of_nexts, sizeof(int));
    if(!cardinalities){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free_familyseries(results);
      free(timeslice_sepsets);
      return NULL;
    }
  }
  k = 0;
  for(i = 0; i < ts->num_of_hidden; i++){
    temp = ts->hidden[i];
    if(temp->next)
      cardinalities[k++] = number_of_values(temp);
  }

  /* Initialise intermediate potentials */
  for(t = 0; t <= ts->length; t++){
    timeslice_sepsets[t] = make_potential(cardinalities, model->num_of_nexts, 
					  NULL);
  }
  free(cardinalities);


  /*****************/
  /* Forward phase */
  /*****************/
  for(t = 0; t < ts->length; t++){ /* FOR EVERY TIMESLICE */
    
    /* Put some data in */
    for(i = 0; i < model->num_of_vars - ts->num_of_hidden; i++)
      if(ts->data[t][i] >= 0)
	enter_i_observation(model->variables, model->num_of_vars, 
			    model->cliques, model->num_of_cliques, 
			    ts->observed[i], ts->data[t][i]);
    
    
    if(t > 0)
      if(finish_timeslice_message_pass(model, FORWARD, 
				       timeslice_sepsets[t-1], 
				       NULL)                   != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_familyseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }
    
    /* Do the inference */
    make_consistent(model);
    
    /* Start a message pass between timeslices */
    if(start_timeslice_message_pass(model, FORWARD,
				    timeslice_sepsets[t]) != NO_ERROR){
      report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
      free_familyseries(results);
      for(i = 0; i <= ts->length; i++)
	free_potential(timeslice_sepsets[i]);
      free(timeslice_sepsets);
      return NULL;
    }

    /* Forget old evidence */
    reset_model(model);
  }
  
  /******************/
  /* Backward phase */
  /******************/
  
  /* forget old evidence */
  reset_model(model);
  
  for(t = ts->length - 1; t >= 0; t--){ /* FOR EVERY TIMESLICE */
    
    /* Put some evidence in */
    for(i = 0; i < model->num_of_vars - ts->num_of_hidden; i++)
      if(ts->data[t][i] >= 0)
	enter_i_observation(model->variables, model->num_of_vars, 
			    model->cliques, model->num_of_cliques, 
			      ts->observed[i], ts->data[t][i]);

    /* Pass the message from the past */
    if(t > 0)
      if(finish_timeslice_message_pass(model, FORWARD, 
				       timeslice_sepsets[t-1], 
				       NULL)                   != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_familyseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }
    
    /* Pass the message from the future */
    if(t < ts->length - 1)
      if(finish_timeslice_message_pass(model, BACKWARD, 
				       timeslice_sepsets[t+1], 
				       timeslice_sepsets[t]) != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_familyseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }

    /* Do the inference */
    make_consistent(model);
    

    /* Write the results of inference */
    for(i = 0; i < results->model->num_of_children; i++){

      /* NOTE: this would be a proper place for the rest of the EM-algorithm
       * in case we need to limit the memory consumption... */
      
      /* 1. Decide which Variable you are interested in */
      temp = results->model->children[i];
      
      /* 2. Find the Clique that contains the family of 
       *    the interesting Variable */
      clique_of_interest = find_family(results->model->cliques, 
				       results->model->num_of_cliques, 
				       temp);
      assert(clique_of_interest != NULL);
      
      /* 3. General Marginalisation */
      general_marginalise(clique_of_interest->p, 
			  results->families[t][i],
			  find_family_mapping(clique_of_interest, temp));
      
      /* 4. Normalisation ??? */
      size = results->families[t][i]->size_of_data;
      k = temp->cardinality;
      for(j = 0; j < size; j = j + k)
	normalise(results->families[t][i]->data + j, number_of_values(temp));
      /* Not so sure that this works correctly... */
    }
    /* Finished writing results for this t */


    if(t > 0)
      if(start_timeslice_message_pass(model, BACKWARD, 
				      timeslice_sepsets[t]) != NO_ERROR){

	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	free_familyseries(results);
	for(i = 0; i <= ts->length; i++)
	  free_potential(timeslice_sepsets[i]);
	free(timeslice_sepsets);
	return NULL;
      }

    /* forget old evidence */
    reset_model(model);
  }

  /* free the intermediate potentials */
  for(t = 0; t <= ts->length; t++)
    free_potential(timeslice_sepsets[t]);
  free(timeslice_sepsets);

  return results;
}


int insert_soft_evidence(Nip model, char* variable, double* distribution){
  int ret;
  Variable v = get_Variable(model, variable);
  if(v == NULL)
    return ERROR_INVALID_ARGUMENT;
  ret =  enter_evidence(model->variables, model->num_of_vars, 
			model->cliques, model->num_of_cliques, 
			v, distribution);
  make_consistent(model);
  return ret;
}


Variable get_Variable(Nip model, char* symbol){
  int i;

  if(!model){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return NULL;
  }

  for(i = 0; i < model->num_of_vars; i++)
    if(strcmp(symbol, model->variables[i]->symbol) == 0)
      return model->variables[i];

  return NULL;
}


void make_consistent(Nip model){
  int i;
  for (i = 0; i < model->num_of_cliques; i++)
    unmark_Clique(model->cliques[i]);

  if(collect_evidence(NULL, NULL, model->cliques[0]) != NO_ERROR){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    return;
  }

  for (i = 0; i < model->num_of_cliques; i++)
    unmark_Clique(model->cliques[i]);

  if(distribute_evidence(model->cliques[0]) != NO_ERROR)
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);

  return;
}


/* Most likely state sequence of the variables given the timeseries. */
TimeSeries mlss(Variable vars[], int nvars, TimeSeries ts){
  int i, j, k, l, t;
  TimeSeries mlss;

  /* Allocate some space for the results */
  mlss = (TimeSeries)malloc(sizeof(time_series_type));
  if(!mlss){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  mlss->model = ts->model;
  mlss->num_of_hidden = ts->model->num_of_vars - nvars;
  mlss->hidden = (Variable*) calloc(mlss->num_of_hidden, sizeof(Variable));
  mlss->observed = (Variable*) calloc(nvars, sizeof(Variable));
  mlss->length = ts->length;
  mlss->data = (int**) calloc(mlss->length, sizeof(int*));
  if(!(mlss->data && mlss->observed && mlss->hidden)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(mlss->data); /* useless? */
    free(mlss->observed);
    free(mlss->hidden);
    free(mlss);
    return NULL;
  }

  for(t=0; t < mlss->length; t++){
    mlss->data[t] = (int*) calloc(nvars, sizeof(int));
    if(!(mlss->data[t])){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      while(t > 0)
	free(mlss->data[--t]);
      free(mlss->data);
      free(mlss->observed);
      free(mlss->hidden);
      free(mlss);
      return NULL;
    }
  }

  /* Copy the variable references */
  memcpy(mlss->observed, vars, nvars*sizeof(Variable));

  /* Find out the "hidden", or more like uninteresting, variables */
  l = 0;
  for(i=0; i < ts->model->num_of_vars; i++){
    if(l == mlss->num_of_hidden)
      break;

    k = 1;
    for(j=0; j < nvars; j++){
      if(equal_variables(ts->model->variables[i], vars[j])){
	k = 0;
	break;
      }
    }
    
    if(k)
      mlss->hidden[l++] = ts->model->variables[i];
  }

  /* TODO: write the algorithm here 
   * - allocate a (massive?) chunk of memory for intermediate results 
   *   - find out the "geometry" of the intermediate data
   *
   * - do a sort of forward inference on the best states 
   *   - for each variable of interest separately? NO !?!?
   *   - by inserting the last state of a hidden variable as hard evidence?
   *     (or is the goal to maximize the probability of observations)
   *
   * - find out the result by iterating backwards the best choices
   */

  /* NOT IMPLEMENTED YET! */
  report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
  return mlss;
}


/* Teaches the given model (ts->model) according to the given time series 
 * (ts) with EM-algorithm. Returns an error code as an integer. */
int em_learn(TimeSeries ts){
  int i, n;
  int v;
  int t = 0;
  int *card;
  FamilySeries fs = NULL;
  potential *parameters;
  Variable c;

  /* Reserve some memory for calculation */
  parameters = (potential*) calloc(ts->model->num_of_children, 
				   sizeof(potential));
  if(!parameters){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }
  for(v = 0; v < ts->model->num_of_children; v++){
    n = ts->model->children[v]->num_of_parents + 1;
    card = (int*) calloc(n, sizeof(int));
    if(!card){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free(parameters);
      return ERROR_OUTOFMEMORY;
    }
    card[0] = ts->model->children[v]->cardinality;
    for(i = 1; i < n; i++)
      card[i] = ts->model->children[v]->parents[i]->cardinality;

    parameters[v] = make_potential(card, n, NULL);
    free(card);
  }

  while(0){ /* When should we stop? */

    /* E-Step: Now this is the heavy stuff..! */
    fs = family_inference(ts);

    /* FIXME: we need joint distributions of children and their parents. */

    /* M-Step: First the parameter estimation... */
    for(v = 0; v < ts->model->num_of_children; v++){
      c = ts->model->children[v];
      n = c->num_of_parents + 1;

      /* Traverse through the potentials with a flat index 
       * (+ flat->multidimensional conversion) */
      
      /* A special case can be found at HMModel.java lines 290-314 */
      
      for(t = 0; t < ts->length; t++){
	; /* unfinished... */
      }
      
      /* Normalisation of potentials? */
      ;
    }

    /* ... then the model modification */

    free_familyseries(fs);
  }

  for(v = 0; v < ts->model->num_of_children; v++){
    free_potential(parameters[v]);
  }
  free(parameters);
  
  /* NOT IMPLEMENTED YET! */
  report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
  return ERROR_GENERAL;
}


double *get_probability(Nip model, Variable v){
  Clique clique_of_interest;
  double *result;
  int cardinality;

  if(!model){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return NULL;
  }

  if(!v){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return NULL;
  }

  cardinality = number_of_values(v);
  result = (double *) calloc(cardinality, sizeof(double));
  if(!result){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  /* 1. Find the Clique that contains the interesting Variable */
  clique_of_interest = find_family(model->cliques, model->num_of_cliques, v);
  if(!clique_of_interest){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    free(result);
    return NULL;
  }

  /* 2. Marginalisation (the memory must have been allocated) */
  marginalise(clique_of_interest, v, result);

  /* 3. Normalisation */
  normalise(result, cardinality);

  /* 4. Return the result */
  return result;
}


/* TODO: Figure out if we need the stuff below this comment. (Probably in 
 *       some form yes, but...) */
double *get_joint_probability(Nip model, Variable *vars, int num_of_vars){
  Clique clique_of_interest;
  potential source, destination;
  int *cardinality;
  int *source_vars;
  int *indices = NULL;
  int i, j = 0, k = 0;
  int retval;
  int extra_vars;
  double *result;

  Variable *vars_sorted;

#ifdef DEBUG_NIP
  printf("In get_joint_probability()\n");
#endif

  /* Find the Clique that contains the interesting Variables */
  clique_of_interest = find_clique(model->cliques, model->num_of_cliques, 
				   vars, num_of_vars);
  if(!clique_of_interest){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    return NULL;
  }

  /* Sort the Variables to program order (ascending ID) */
  vars_sorted = sort_variables(vars, num_of_vars);
  if(!vars_sorted){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    return NULL;
  }

#ifdef DEBUG_NIP
  printf("Variables in given order:\n");
  for(i = 0; i < num_of_vars; i++)
    printf("Symbol: %s\tId: %ld\n", vars[i]->symbol, vars[i]->id);

  printf("\n");

  printf("Variables in sorted order:\n");
  for(i = 0; i < num_of_vars; i++)
    printf("Symbol: %s\tId: %ld\n", 
	   vars_sorted[i]->symbol, vars_sorted[i]->id);
#endif

  cardinality = (int *) calloc(num_of_vars, sizeof(int));
  if(!cardinality){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(vars_sorted);
    return NULL;
  }

  for(i = 0; i < num_of_vars; i++)
    cardinality[i] = vars_sorted[i]->cardinality;

  source = clique_of_interest->p;

  destination = make_potential(cardinality, num_of_vars, NULL);
  if(!destination){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    free(vars_sorted);
    free(cardinality);
    return NULL;
  }

  extra_vars = clique_of_interest->p->num_of_vars - num_of_vars;

  source_vars = (int *) calloc(extra_vars, sizeof(int));
  if(!source_vars){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(vars_sorted);
    free(cardinality);
    free_potential(destination);
    return NULL;
  }

  /* Select the variables that are in the Clique but not in the set of
   * Variables given to this function.
   * This relies on the order of variables. */
  for(i=0; i < clique_of_interest->p->num_of_vars; i++){
    if(j < num_of_vars &&
       equal_variables((clique_of_interest->variables)[i], vars_sorted[j]))
      j++;
    else {
      source_vars[k] = i;
      k++;
    }
  }

  retval = general_marginalise(source, destination, source_vars);
  if(retval != NO_ERROR){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    free(vars_sorted);
    free(cardinality);
    free(source_vars);
    return NULL;
  }

#ifdef DEBUG_NIP
  for(i = 0; i < destination->size_of_data; i++)
    printf("in get_joint_probability(): destination->data[%d] = %f\n",
	   i, destination->data[i]);
#endif

  result = reorder_potential(vars, destination);

#ifdef DEBUG_NIP
  for(i = 0; i < destination->size_of_data; i++)
    printf("in get_joint_probability(): result[%d] = %f\n",
	   i, result[i]);
#endif

  /* NORMALISE? */

  /* FREE THE MEMORY!!! */
  free_potential(destination);
  free(vars_sorted);
  free(cardinality);
  free(source_vars);
  free(indices);

  return result;

}


void print_Cliques(Nip model){

  int i;
  int num_of_cliques;
  Clique clique_of_interest;
  Sepset_link sepsetlist;
  Clique *cliques;

  if(!model){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return;
  }

  cliques = model->cliques;
  num_of_cliques = model->num_of_cliques;

  printf("Found Cliques:\n");
  for(i = 0; i < num_of_cliques; i++){
    clique_of_interest = cliques[i];
    print_Clique(clique_of_interest);
    sepsetlist = clique_of_interest->sepsets;
    while(sepsetlist){
      print_Sepset((Sepset)sepsetlist->data);
      sepsetlist = sepsetlist->fwd;
    }
    printf("\n");
  }
}
