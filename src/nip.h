/*
 * nip.h $Id: nip.h,v 1.8 2004-08-31 08:41:36 mvkorpel Exp $
 */

#ifndef __NIP_H__
#define __NIP_H__

#include "Clique.h"
#include "Variable.h"
#include "errorhandler.h"
#include <stdlib.h>

typedef struct{
  int num_of_cliques;
  int num_of_vars;
  Clique *cliques;
  varlink first_var;
  varlink last_var;
}nip_type;

typedef nip_type *Nip;


/* Makes the model forget all the given evidence. */
void reset_model(Nip model);


/* Creates a model according to the net file. 
 * The single parameter is the name of the net file as a string. */
Nip parse_model(char* file);


/* This provides a way to get rid of a model and free some memory. */
void free_model(Nip model);


/* This is a function for telling the model about observations. 
 * In case of an error, a non-zero value is returned. */
int insert_hard_evidence(Nip model, char* variable, char* observation);


/* If an observation has some uncertainty, the evidence can be inserted 
 * with this procedure. The returned value is 0 if everything went well. */
int insert_soft_evidence(Nip model, char* variable, double* distribution);

Variable get_Variable(Nip model, char* symbol);

void make_consistent(Nip model);

/********************************************************************
 * TODO: a set of functions for reading the results of inference from 
 *       the model 
 */

/*
 * Calculates the probability distribution of a Variable.
 * The join tree MUST be consistent before calling this.
 * Parameters:
 * - model: the model that contains the Variable
 * - v: the Variable whose distribution we want
 * - print: zero if we don't want the result printed. Non-zero is the opposite.
 * Returns an array of doubles (remember to free the result when not needed).
 * The returned array is of size v->cardinality.
 * In case of problems, NULL is returned.
 */
double *get_probability(Nip model, Variable v, int print);


/*
 * Calculates the joint probability distribution of a set of Variables.
 * The Variables MUST be in the SAME CLIQUE.
 * The join tree MUST be consistent before calling this.
 * Parameters:
 * - model: the model that contains the Variable
 * - vars: the Variables whose distribution we want
 * - num_of_vars: the number of Variables (size of "vars")
 * - print: zero if we don't want the result printed. Non-zero is the opposite.
 * Returns an array of doubles (remember to free the result when not needed).
 * The returned array is of size
 *   vars[0]->cardinality * ... * vars[num_of_vars - 1]->cardinality.
 * The array can be addressed with multiple indices,
 *   i.e. array[i0][i1][i2], where the indices are in the same order as
 *   the array "vars".
 * In case of problems, NULL is returned.
 */
double *get_joint_probability(Nip model, Variable *vars, int num_of_vars,
			      int print);


/*
 * Prints the Cliques in the given Nip model.
 */
void print_Cliques(Nip model);

#endif /* __NIP_H__ */
