/*  NIP - Dynamic Bayesian Network library
    Copyright (C) 2012  Janne Toivola

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/* htmtest.c
 *
 * Experimental code for handling the inference with time slices. 
 * Prints marginal posterior probability distributions for each variable 
 * during the first time series in the given data file. 
 *
 * SYNOPSIS: HTMTEST <MODEL.NET> <DATA.TXT>
 * 
 * Author: Janne Toivola
 * Version: $Id: htmtest.c,v 1.64 2010-12-07 17:23:19 jatoivol Exp $
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "nipparsers.h"
#include "nipjointree.h"
#include "nipvariable.h"
#include "nippotential.h"
#include "niperrorhandler.h"
#include "nip.h"

#define VAR_OF_INTEREST(m,x) (((m)->variables[(x)]->interface_status & NIP_INTERFACE_OLD_OUTGOING) == 0)

int main(int argc, char *argv[]){

  int i, j, n, t = 0;

  double loglikelihood = 0;

  nip_model model = NULL;
  nip_variable temp = NULL;
  nip_variable* vars = NULL;
  int nvars = 0;

  time_series ts = NULL;
  time_series *ts_set = NULL;
  uncertain_series ucs = NULL;

  /*****************************************/
  /* Parse the model from a Hugin NET file */
  /*****************************************/
  /* -- Start parsing the network definition file */
  if(argc < 3){
    printf("Give the names of the net-file and data file, please!\n");
    return 0;
  }
  else{
    model = parse_model(argv[1]);
  }

  if(model == NULL)
    return -1;


  /*****************************/
  /* read the data from a file */
  /*****************************/
  n = read_timeseries(model, argv[2], &ts_set);

  if(n < 1){
    nip_report_error(__FILE__, __LINE__, NIP_ERROR_INVALID_ARGUMENT, 1);
    fprintf(stderr, "%s\n", argv[2]);
    free_model(model);
    return -1;
  }

  ts = ts_set[0];


  /* Determine variables of interest */
  nvars = 0;
  for(i = 0; i < model->num_of_vars; i++){
    if(VAR_OF_INTEREST(model,i))
      nvars++;
  }
  vars = (nip_variable*) calloc(nvars, sizeof(nip_variable));
  j = 0;
  for(i = 0; i < model->num_of_vars; i++){
    if(VAR_OF_INTEREST(model,i))
      vars[j++] = model->variables[i];
  }
  assert(j == nvars);


  /** DEBUG **/
  printf("Observed variables:\n  ");
  for(i = 0; i < model->num_of_vars - ts->num_of_hidden; i++)
    printf("%s ", ts->observed[i]->symbol);

  printf("\nHidden variables:\n  ");
  for(i = 0; i < ts->num_of_hidden; i++)
    printf("%s ", ts->hidden[i]->symbol);

  printf("\nVariables of interest:\n  ");
  for(i = 0; i < nvars; i++)
    printf("%s ", vars[i]->symbol);
  
  /*print_cliques(model);*/
  printf("\n");


  /* Make sure all the variables are marked not to be ignored 
   * in inserting evidence... */
  for(i = 0; i < model->num_of_vars; i++)
    nip_mark_variable(model->variables[i]);


  /*****************/
  /* Forward phase */
  /*****************/

  printf("## Forward phase ##\n");  

  ucs = forward_inference(ts, vars, nvars, &loglikelihood);

  for(t = 0; t < UNCERTAIN_SERIES_LENGTH(ucs); t++){ /* FOR EACH TIMESLICE */
    printf("-- t = %d --\n", t+1);

    /* Print some intermediate results */
    for(i = 0; i < ucs->num_of_vars; i++){
      temp = ucs->variables[i];            
      /* Print a value */
      for(j = 0; j < NIP_CARDINALITY(temp); j++)
	printf("P(%s=%s) = %f\n", nip_variable_symbol(temp),
	       (temp->state_names)[j], ucs->data[t][i][j]);
      printf("\n"); 
    }
  }  

  printf("  ln p(y(1:%d)) = %g\n\n", 
	 UNCERTAIN_SERIES_LENGTH(ucs), 
	 loglikelihood);
  
  /******************/
  /* Backward phase */
  /******************/

  printf("## Backward phase ##\n");  

  free_uncertainseries(ucs); /* REMEMBER THIS */

  ucs = forward_backward_inference(ts, vars, nvars, NULL);

  for(t = 0; t < UNCERTAIN_SERIES_LENGTH(ucs); t++){ /* FOR EACH TIMESLICE */
    printf("-- t = %d --\n", t+1);
    
    /* Print the final results */
    for(i = 0; i < ucs->num_of_vars; i++){
      temp = ucs->variables[i];
      /* Print a value */
      for(j = 0; j < NIP_CARDINALITY(temp); j++)
	printf("P(%s=%s) = %f\n", nip_variable_symbol(temp),
	       (temp->state_names)[j], ucs->data[t][i][j]);
      printf("\n"); 
    }
  }

  for(i = 0; i < n; i++)
    free_timeseries(ts_set[i]);
  free(ts_set);
  free_uncertainseries(ucs);
  free(vars);
  free_model(model);
  
  return 0;
}
