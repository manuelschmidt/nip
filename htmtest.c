#include <stdlib.h>
#include <assert.h>
#include "parser.h"
#include "Clique.h"
#include "Variable.h"
#include "potential.h"
#include "errorhandler.h"

/*
#define PRINT_CLIQUES
*/

/***********************************************************
 * The timeslice concept features some major difficulties 
 * because the actual calculations are done in the join tree
 * instead of the graph. The program should be able to 
 * figure out how the join tree repeats itself and store 
 * somekind of sepsets between the timeslices... Note that 
 * there can be only one sepset between two adjacent 
 * timeslices, because the join tree can't have loops.
 */


extern int yyparse();


void make_consistent(Clique* cliques, int n_cliques){
  int i;
  for (i = 0; i < n_cliques; i++)
    unmark_Clique(cliques[i]);
  collect_evidence(NULL, NULL, cliques[0]);
  for (i = 0; i < n_cliques; i++)
    unmark_Clique(cliques[i]);
  distribute_evidence(cliques[0]);
}


void forget_evidence(Clique c){
  Variable temp;
  Variable_iterator it = get_Variable_list();
  while((temp = next_Variable(&it)) != NULL)
    reset_likelihood(temp);
  global_retraction(c);
}


int main(int argc, char *argv[]){

  char** tokens;
  int** data;
  int* cardinalities;
  int* temp_vars = NULL;
  int i, j, k, m, n, retval, t = 0;
  int num_of_hidden = 0;
  int num_of_nexts = 0;
  int nip_num_of_cliques;
  double** result; /* probs of the hidden variables */
  
  Clique *nip_cliques;
  Clique clique_of_interest;
  
  potential *timeslice_sepsets;
  /* for reordering sepsets between timeslices
     potential temp_potential;
     potential reordered;
  */
  Variable *hidden;
  Variable *next;
  Variable *previous;
  Variable temp;
  Variable interesting;
  Variable_iterator it;

  datafile* timeseries;

  /*************************************/
  /* Some experimental timeslice stuff */
  /*************************************/
  
  /*****************************************/
  /* Parse the model from a Hugin NET file */
  /*****************************************/
  /* -- Start parsing the network definition file */
  if(argc < 3){
    printf("Give the names of the net-file and data file, please!\n");
    return 0;
  }
  else if(open_yyparse_infile(argv[1]) != NO_ERROR)
    return -1;

  retval = yyparse();

  close_yyparse_infile();

  if(retval != 0)
    return retval;
  /* The input file has been parsed. -- */


  /* Get references to the results of parsing */
  nip_cliques = *get_cliques_pointer();
  nip_num_of_cliques = get_num_of_cliques();


#ifdef PRINT_CLIQUES
  print_Cliques();
#endif

  /*****************************/
  /* read the data from a file */
  /*****************************/
  timeseries = open_datafile(argv[2], ',', 0, 1);
  if(timeseries == NULL){
    report_error(__FILE__, __LINE__, ERROR_FILENOTFOUND, 1);
    fprintf(stderr, "%s\n", argv[2]);
    return -1;
  }


  /* Figure out the number of hidden variables and variables 
   * that substitute some other variable in the next timeslice. */
  it = get_Variable_list();
  temp = next_Variable(&it);
  while(temp != NULL){
    j = 1;
    for(i = 0; i < timeseries->num_of_nodes; i++)
      if(equal_variables(temp, get_variable(timeseries->node_symbols[i])))
	j = 0;
    if(j)
      num_of_hidden++;

    if(temp->next)
      num_of_nexts++;

    temp = next_Variable(&it);
  }

  assert(num_of_hidden == (total_num_of_vars() - timeseries->num_of_nodes));

  /* Allocate arrays for hidden variables. */
  hidden = (Variable *) calloc(num_of_hidden, sizeof(Variable));
  next = (Variable *) calloc(num_of_nexts, sizeof(Variable));
  previous = (Variable *) calloc(num_of_nexts, sizeof(Variable));
  cardinalities = (int*) calloc(num_of_nexts, sizeof(int));
  if(!(hidden && next && previous && cardinalities)){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return 1;
  }

  /* Fill the arrays */
  it = get_Variable_list();
  temp = next_Variable(&it);
  k = 0;
  m = 0;
  n = 0;
  while(temp != NULL){
    j = 1;
    for(i = 0; i < timeseries->num_of_nodes; i++)
      if(equal_variables(temp, get_variable(timeseries->node_symbols[i]))){
	j = 0;
	break;
      }
    if(j)
      hidden[k++] = temp;

    if(temp->next){
      next[m] = temp;
      previous[m] = temp->next;
      cardinalities[m] = number_of_values(temp);
      m++;
    }

    temp = next_Variable(&it);
  }
  

  /* Check one little detail :) */
  j = 1;
  for(i = 1; i < num_of_nexts; i++)
    if(get_id(previous[i-1]) > get_id(previous[i]))
      j = 0;
  assert(j);


  /* Allocate some space for data */
  data = (int**) calloc(timeseries->datarows, sizeof(int*));
  if(!data){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return 1;
  }
  for(t = 0; t < timeseries->datarows; t++){
    data[t] = (int*) calloc(timeseries->num_of_nodes, sizeof(int));
    if(!(data[t])){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return 1;
    }
  }


  /* Allocate some space for filtering */
  result = (double**) calloc(num_of_hidden, sizeof(double*));
  if(!result){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return 1;
  }
  
  for(i = 0; i < num_of_hidden; i++){
    result[i] = (double*) calloc( number_of_values(hidden[i]), 
				     sizeof(double));
    if(!result[i]){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return 1;
    }
  }
  
  
  
  /* Fill the data array */
  for(t = 0; t < timeseries->datarows; t++){
    retval = nextline_tokens(timeseries, ',', &tokens); /* 2. Read */
    assert(retval == timeseries->num_of_nodes);

    /* 3. Put into the data array */
    for(i = 0; i < retval; i++){
      data[t][i] = 
	get_stateindex(get_variable((timeseries->node_symbols)[i]), tokens[i]);

      /* Q: Should missing data be allowed?   A: Yes. */
      /* assert(data[t][i] >= 0); */
    }

    for(i = 0; i < retval; i++) /* 4. Dump away */
      free(tokens[i]);
  }


  /* Allocate some space for the intermediate potentials */
  timeslice_sepsets = (potential *) calloc(timeseries->datarows, 
					sizeof(potential));  
  /* Initialise intermediate potentials */
  for(t = 0; t <= timeseries->datarows; t++){
    timeslice_sepsets[t] = make_potential(cardinalities, num_of_nexts, NULL);
  }
  free(cardinalities);


  /*****************/
  /* Forward phase */
  /*****************/
  printf("## Forward phase ##\n");  

  for(t = 0; t < timeseries->datarows; t++){ /* FOR EVERY TIMESLICE */
    
    printf("-- t = %d --\n", t+1);
    
    /* Put some data in */
    for(i = 0; i < timeseries->num_of_nodes; i++)
      if(data[t][i] >= 0)
	enter_i_observation(get_variable((timeseries->node_symbols)[i]), 
			    data[t][i]);
    
    /********************/
    /* Do the inference */
    /********************/
    
    make_consistent(nip_cliques, nip_num_of_cliques);
    
    
    /* an experimental forward phase (a.k.a. filtering)... */
    for(i = 0; i < num_of_hidden; i++){
      
      /*********************************/
      /* Check the result of inference */
      /*********************************/

      interesting = hidden[i];
      
      /* 1. Find the Clique that contains the family of 
       * the interesting Variables */
      clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				       &interesting, 1);
      assert(clique_of_interest != NULL);
      
      /* 2. Marginalisation (memory for the result must have been allocated) */
      marginalise(clique_of_interest, interesting, result[i]);
      
      /* 3. Normalisation */
      normalise(result[i], number_of_values(interesting));    
      
      /* 4. Print the result */
      for(j = 0; j < number_of_values(interesting); j++)
	printf("P(%s=%s) = %f\n", get_symbol(interesting),
	       (interesting->statenames)[j], result[i][j]);
      printf("\n");

    }

    /* Start a message pass between timeslices */
    /* NOTE: Let's hope the "next" variables are in the same clique! */
    clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				     next, num_of_nexts);
    assert(clique_of_interest != NULL);
    temp_vars = (int*) calloc(clique_of_interest->p->num_of_vars - 
			      num_of_nexts, sizeof(int));
    if(!temp_vars){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return 1;
    }
    /* NOTE: Let's hope the "next" variables are in correct order! */
    m = 0;
    n = 0;
    for(j=0; j < clique_of_interest->p->num_of_vars; j++){
      if(m < num_of_nexts &&
	 equal_variables((clique_of_interest->variables)[j], next[m]))
	m++;
      else {
	temp_vars[n] = j;
	n++;
      }
    }
    general_marginalise(clique_of_interest->p, timeslice_sepsets[t],
			temp_vars);
    
    /* Forget old evidence */
    forget_evidence(nip_cliques[0]);
    
    if(t < timeseries->datarows - 1){
      /* Finish the message pass between timeslices */
      clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				       previous, num_of_nexts);
      assert(clique_of_interest != NULL);
      j = 0; k = 0;
      for(i=0; i < clique_of_interest->p->num_of_vars; i++){
	if(j < num_of_nexts &&
	   equal_variables((clique_of_interest->variables)[i], previous[j]))
	  j++;
	else {
	  temp_vars[k] = i;
	  k++;
	}
      }
      update_potential(timeslice_sepsets[t], NULL, 
		       clique_of_interest->p, temp_vars);
    }
    free(temp_vars);
  }
  
  
  
  
  /******************/
  /* Backward phase */
  /******************/

  printf("## Backward phase ##\n");  
  
  /* forget old evidence */
  forget_evidence(nip_cliques[0]);
  
  
  for(t = timeseries->datarows - 1; t >= 0; t--){ /* FOR EVERY TIMESLICE */
    
    printf("-- t = %d --\n", t+1);


    for(i = 0; i < timeseries->num_of_nodes; i++)
      if(data[t][i] >= 0)
	enter_i_observation(get_variable((timeseries->node_symbols)[i]), 
			    data[t][i]);
    
    
    /* Message pass??? */
    clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				     previous, num_of_nexts);
    assert(clique_of_interest != NULL);
    temp_vars = (int*) calloc(clique_of_interest->p->num_of_vars - 
			      num_of_nexts, sizeof(int));
    if(!temp_vars){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return 1;
    }


    if(t > 0){
      j = 0; k = 0;
      for(i=0; i < clique_of_interest->p->num_of_vars; i++){
	if(j < num_of_nexts &&
	   equal_variables((clique_of_interest->variables)[i], previous[j]))
	  j++;
	else {
	  temp_vars[k] = i;
	  k++;
	}
      }
      update_potential(timeslice_sepsets[t-1], NULL,
		       clique_of_interest->p, temp_vars);
    }


    /* an inference */
    make_consistent(nip_cliques, nip_num_of_cliques);    
  

    if(t < timeseries->datarows - 1){
      /*******************************************/
      /* FIX ME: there's a bug here somewhere!!! */
      /*******************************************/
      clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				       next, num_of_nexts);
      assert(clique_of_interest != NULL);
      j = 0; k = 0;
      for(i=0; i < clique_of_interest->p->num_of_vars; i++){
	if(j < num_of_nexts && 
	   equal_variables((clique_of_interest->variables)[i], next[j]))
	  j++;
	else
	  temp_vars[k++] = i;
      }
      update_potential(timeslice_sepsets[t+1], timeslice_sepsets[t],
		       clique_of_interest->p, temp_vars);
    }


    /********************/
    /* Do the inference */
    /********************/
    
    make_consistent(nip_cliques, nip_num_of_cliques);
    
    
    /*********************************/
    /* Check the result of inference */
    /*********************************/
    for(i = 0; i < num_of_hidden; i++){
      
      /* 1. Decide which Variable you are interested in */
      interesting = hidden[i];
      
      /* 2. Find the Clique that contains the family of 
       *    the interesting Variable */
      clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				       &interesting, 1);
      assert(clique_of_interest != NULL);
      
      /* 3. Marginalisation (the memory must have been allocated) */
      marginalise(clique_of_interest, interesting, result[i]);
      
      /* 4. Normalisation */
      normalise(result[i], number_of_values(interesting));
      
      /* 5. Print the result */
      for(j = 0; j < number_of_values(interesting); j++)
	printf("P(%s=%s) = %f\n", get_symbol(interesting),
	       (interesting->statenames)[j], result[i][j]);
      printf("\n");
    }
    

    if(t > 0){
      /* Start a message pass between timeslices */
      clique_of_interest = find_family(nip_cliques, nip_num_of_cliques, 
				       previous, num_of_nexts);
      assert(clique_of_interest != NULL);
      /* NOTE: Let's hope the "previous" variables are in correct order */
      m = 0;
      n = 0;
      for(j=0; j < clique_of_interest->p->num_of_vars; j++){
	if(m < num_of_nexts &&
	   equal_variables((clique_of_interest->variables)[j], previous[m]))
	  m++;
	else {
	  temp_vars[n] = j;
	  n++;
	}
      }
      general_marginalise(clique_of_interest->p, timeslice_sepsets[t],
			  temp_vars);

      /* forget old evidence */
      forget_evidence(nip_cliques[0]);
    }

    free(temp_vars);
  }
  
  return 0;
}

