/*
 * Functions for the bison parser.
 * Also contains other functions for handling different files.
 * $Id: parser.c,v 1.111 2006-11-13 17:59:24 jatoivol Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "Graph.h"
#include "clique.h"
#include "lists.h"
#include "variable.h"
#include "fileio.h"
#include "errorhandler.h"

/* #define DEBUG_PARSER */

/* #define PRINT_TOKENS */

/* #define DEBUG_DATAFILE */

static varlink nip_first_temp_var = NULL;
static varlink nip_last_temp_var = NULL;
static int nip_symbols_parsed = 0;

static Graph *nip_graph = NULL;

/*
static doublelink nip_first_double = NULL;
static doublelink nip_last_double = NULL;
static int nip_doubles_parsed = 0;

TODO: Move the rest of the global variables into huginnet.y also...
      (with new implementations in lists.c)
*/

static stringlink nip_first_string = NULL;
static stringlink nip_last_string = NULL;
static int nip_strings_parsed = 0;

static initDataLink nip_first_initData = NULL;
static initDataLink nip_last_initData = NULL;
static int nip_initData_parsed = 0;

static time_init_link nip_first_timeinit = NULL;

static char** nip_statenames;
static char* nip_label;
static char* nip_persistence; /* The name included in the NIP_next field */

/* Should a new line be read in when the next token is requested? */
static int nip_read_line = 1;

/* The current input file */
static FILE *nip_yyparse_infile = NULL;

/* Is there a hugin net file open? 0 if no, 1 if yes. */
static int nip_yyparse_infile_open = 0;

static clique *nip_cliques = NULL;
static int nip_num_of_cliques = 0;
static int parser_node_position_x = 100;
static int parser_node_position_y = 100;
static int parser_node_size_x = 80;
static int parser_node_size_y = 60;

static int add_to_stringlink(stringlink *s, char* string);

static int search_stringlinks(stringlink s, char* string);

static int nullobservation(char *token);

static void free_datafile(datafile *f);

int open_yyparse_infile(const char *filename){
  if(!nip_yyparse_infile_open){
    nip_yyparse_infile = fopen(filename,"r");
    if (!nip_yyparse_infile){
      report_error(__FILE__, __LINE__, ERROR_IO, 1);
      return ERROR_IO; /* fopen(...) failed */
    }
    else{
      nip_yyparse_infile_open = 1;
      nip_read_line = 1;
    }
  }
  return NO_ERROR;
}


void close_yyparse_infile(){
  if(nip_yyparse_infile_open){
    fclose(nip_yyparse_infile);
    nip_yyparse_infile_open = 0;
  }
}


datafile *open_datafile(char *filename, char separator,
			int write, int nodenames){

  char last_line[MAX_LINELENGTH];
  char *token;
  int length_of_name = 0;
  int num_of_tokens = 0;
  int *token_bounds;
  int linecounter = 0;
  int tscounter = 0;
  int dividend;
  int i, j, state;
  int empty_lines_read = 0;
  stringlink *statenames = NULL;
  stringlink temp, temp2;
  datafile *f = NULL;

  f = (datafile *) malloc(sizeof(datafile));

  if(f == NULL){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  f->name = NULL;
  f->separator = separator;
  f->file = NULL;
  f->is_open = 0;
  f->firstline_labels = 0;
  f->line_now = 0;
  f->label_line = -1;
  f->ndatarows = 0;
  f->datarows = NULL;
  f->node_symbols = NULL;
  f->num_of_nodes = 0;
  f->node_states = NULL;
  f->num_of_states = NULL;

  if(write)
    f->file = fopen(filename,"w");
  else
    f->file = fopen(filename,"r");

  if (!f->file){
    report_error(__FILE__, __LINE__, ERROR_IO, 1);
    free(f);
    return NULL; /* fopen(...) failed */
  }
  else
    f->is_open = 1;

  length_of_name = strlen(filename);

  f->name = (char *) calloc(length_of_name + 1, sizeof(char));
  if(!f->name){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    fclose(f->file);
    free(f);
    return NULL;
  }

  strcpy(f->name, filename);

  /*
   * If the file is opened in read mode, check the contents.
   * This includes names of nodes and their states.
   */
  if(!write){

    linecounter = 0;
    empty_lines_read = 0;
    /* tries to ignore the empty lines in the beginning of file
     * and between the node labels and data */
    state = 1; /* ignores empty lines before data */
    if(nodenames)
      state = 2; /* ignores empty lines before node labels */

    while(fgets(last_line, MAX_LINELENGTH, f->file)){
      /* treat the white space as separators */
      num_of_tokens = count_tokens(last_line, NULL, 0, &separator, 1, 0, 1);
      (f->line_now)++;

      /* JJT  1.9.2004: A sort of bug fix. Ignore empty lines */
      /* JJT 22.6.2005: Another fix. Ignore only the empty lines 
       * immediately after the node labels... and duplicate empty lines.
       * Otherwise start a new timeseries */
      if(num_of_tokens == 0){
	linecounter = 0;
	empty_lines_read++;
	if(state || empty_lines_read > 1)
	  continue; /* empty lines to be ignored */
      }
      else{
	linecounter++;
	empty_lines_read = 0;
	if(state > 0){
	  if(state > 1){
	    f->label_line = f->line_now;
	    linecounter = 0; /* reset for data lines */
	  }
	  state--; /* stop ignoring single empty lines and the node names */
	}
	if(state == 0 && linecounter == 1)
	  (f->ndatarows)++;;
      }
    }

    /* rewind */
    rewind(f->file);
    f->line_now = 0;
    linecounter = 0;
    state = 1;
    if(nodenames)
      state = 2;

    /* allocate f->datarows array */
    f->datarows = (int*) calloc(f->ndatarows, sizeof(int));
    /* NOTE: calloc resets the contents to zero! */
    if(!f->datarows){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      close_datafile(f);
      return NULL;
    }

    while(fgets(last_line, MAX_LINELENGTH, f->file)){
      /* treat the white space as separators */
      num_of_tokens = count_tokens(last_line, NULL, 0, &separator, 1, 0, 1);

      if(num_of_tokens > 0){
	token_bounds =
	  tokenise(last_line, num_of_tokens, 0, &separator, 1, 0, 1);
	if(!token_bounds){
	  report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	  close_datafile(f);
	  return NULL;
	}
      }
      (f->line_now)++;
      
      /* JJT  1.9.2004: A sort of bug fix. Ignore empty lines */
      /* JJT 22.6.2005: Another fix. Ignore only the empty lines 
       * immediately after the node labels... and duplicate empty lines.
       * Otherwise start a new timeseries */
      if(num_of_tokens == 0){
	empty_lines_read++;
	continue; /* empty lines to be ignored */
      }
      else{
	if(empty_lines_read){
	  linecounter = 1;
	  if(state < 1)
	    tscounter++;
	}
	else
	  linecounter++;
	empty_lines_read = 0;
	if(state > 0)
	  state--; /* stop ignoring single empty lines */
      }
      
      /* Read node names or make them up. */
      if(state){ 
	/* reading the first non-empty line and expecting node names */
	f->num_of_nodes = num_of_tokens;
	f->node_symbols = (char **) calloc(num_of_tokens, sizeof(char *));

	if(!f->node_symbols){
	  report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	  close_datafile(f);
	  free(token_bounds);
	  return NULL;
	}

	statenames = (stringlink *) calloc(num_of_tokens, sizeof(stringlink));

	if(!statenames){
	  report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	  close_datafile(f);
	  free(token_bounds);
	  return NULL;
	}

	f->num_of_states = (int *) calloc(num_of_tokens, sizeof(int));

	if(!f->num_of_states){
	  report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	  close_datafile(f);
	  free(statenames);
	  free(token_bounds);
	  return NULL;
	}

	f->node_states = (char ***) calloc(num_of_tokens, sizeof(char **));

	if(!f->node_states){
	  report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	  close_datafile(f);
	  free(statenames);
	  free(token_bounds);
	  return NULL;
	}

	if(nodenames){
	  f->firstline_labels = 1;

	  for(i = 0; i < num_of_tokens; i++){

	    f->node_symbols[i] =
	      (char *) calloc(token_bounds[2*i+1] - token_bounds[2*i] + 1,
			      sizeof(char));
	    if(!f->node_symbols[i]){
	      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	      close_datafile(f);
	      free(statenames);
	      free(token_bounds);
	      return NULL;
	    }

	    strncpy(f->node_symbols[i], &(last_line[token_bounds[2*i]]),
		    token_bounds[2*i+1] - token_bounds[2*i]);
	    f->node_symbols[i][token_bounds[2*i+1] - token_bounds[2*i]] = '\0';
	  }

	}
	else{
	  f->firstline_labels = 0;

	  for(i = 0; i < num_of_tokens; i++){

	    dividend = i + 1;
	    length_of_name = 5;

	    while((dividend /= 10) > 1)
	      length_of_name++;

	    f->node_symbols[i] = (char *) calloc(length_of_name, sizeof(char));
	    if(!f->node_symbols[i]){
	      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	      close_datafile(f);
	      free(statenames);
	      free(token_bounds);
	      return NULL; /* error horror */
	    }

	    sprintf(f->node_symbols[i], "node%d", i + 1);
	  }
	}
      }
      else{
	/* Read observations (just in order to see all the different
	   kinds of observations for each node). */

	f->datarows[tscounter]++;
	
	/* j == min(f->num_of_nodes, num_of_tokens) */
	if(f->num_of_nodes < num_of_tokens)
	  j = f->num_of_nodes;
	else
	  j = num_of_tokens;

	for(i = 0; i < j; i++){
	  token = (char *) calloc(token_bounds[2*i+1] - token_bounds[2*i] + 1,
				  sizeof(char));
	  if(!token){
	    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	    close_datafile(f);
	    free(statenames);
	    free(token_bounds);
	    return NULL;
	  }

	  strncpy(token, &(last_line[token_bounds[2*i]]),
		  token_bounds[2*i+1] - token_bounds[2*i]);

	  token[token_bounds[2*i+1] - token_bounds[2*i]] = '\0';

	  /* If the string has not yet been observed, add it to a list */
	  if(!(search_stringlinks(statenames[i], token) ||
	       nullobservation(token))){
	    if(add_to_stringlink(&(statenames[i]), token) != NO_ERROR){
	      report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	      close_datafile(f);
	      free(statenames);
	      free(token_bounds);
	      free(token);
	      return NULL;
	    }
	  }
	  free(token);
	}
      }
      free(token_bounds);
    }

    /* Count number of states in each variable. */
    for(i = 0; i < f->num_of_nodes; i++){
      j = 0;
      temp = statenames[i];
      while(temp != NULL){
	j++;
	temp = temp->fwd;
      }
      f->num_of_states[i] = j;
    }

    for(i = 0; i < f->num_of_nodes; i++){
      f->node_states[i] =
	(char **) calloc(f->num_of_states[i], sizeof(char *));

      if(!f->node_states[i]){
	report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
	/* JJT: Added 16.8.2004 because of possible memory leaks. */
	for(j = 0; j < f->num_of_nodes; j++){
	  temp = statenames[j];
	  while(temp != NULL){
	    temp2 = temp->fwd;
	    free(temp->data);
	    free(temp);
	    temp = temp2;
	  }
	}
	free(statenames);
	close_datafile(f);
	return NULL;
      }

      /* Copy statenames from the list */
      temp = statenames[i];
      for(j = 0; j < f->num_of_states[i]; j++){
	f->node_states[i][j] = temp->data;
	temp = temp->fwd;
      }
    }
  }

  /* JJT: Added 13.8.2004 because of possible memory leaks. */
  for(i = 0; i < f->num_of_nodes; i++){
    temp = statenames[i];
    while(temp != NULL){
      temp2 = temp->fwd;
      /* free(temp->data); */
      free(temp);
      temp = temp2;
    }
  }
  free(statenames);

  rewind(f->file);
  f->line_now = 0;

  return f;
}


/*
 * Tells if the given token indicates a missing value, a "null observation".
 * The token must be null terminated.
 */
static int nullobservation(char *token){

#ifdef DEBUG_DATAFILE
  printf("nullobservation called\n");
#endif

  if(token == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return 0;
  }

  else if((strcmp("N/A", token) == 0) ||
	  (strcmp("null", token) == 0) ||
	  (strcmp("<null>", token) == 0)){
    return 1;
  }
  else{

    return 0;
  }
}


void close_datafile(datafile *file){
  if(!file){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return;
  }
  if(file->is_open){
    fclose(file->file);
    file->is_open = 0;
  }
  /* Release the memory of file struct. */
  free_datafile(file);
}


/* Frees the memory used by (possibly partially allocated) datafile f. */
static void free_datafile(datafile *f){
  int i,j;

  if(!f)
    return;
  free(f->name);
  if(f->node_symbols){
    for(j = 0; j < f->num_of_nodes; j++)
      free(f->node_symbols[j]);
    free(f->node_symbols);
  }
  if(f->node_states){
    for(i = 0; i < f->num_of_nodes; i++){
      for(j = 0; j < f->num_of_states[i]; j++)
	free(f->node_states[i][j]);
      free(f->node_states[i]);
    }
    free(f->node_states);
  }
  free(f->num_of_states);
  free(f->datarows);
  free(f);
}


int nextline_tokens(datafile *f, char separator, char ***tokens){

  char line[MAX_LINELENGTH];
  char *token;
  int num_of_tokens;
  int *token_bounds;
  int i, j;

  if(!f || !tokens){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return -1;
  }

  if(!(f->is_open)){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    return -1;
  }

  /* seek the first line of data (starting from the current line) */
  num_of_tokens = 0;
  do{
    if(fgets(line, MAX_LINELENGTH, f->file) == NULL)
      break;
    else
      f->line_now++;
    
    /* treat the white space as separators */
    num_of_tokens = count_tokens(line, NULL, 0, &separator, 1, 0, 1);
    
    /* Skip the first line if it contains node labels. */
    if((f->line_now == f->label_line)  &&  f->firstline_labels)
      num_of_tokens = 0;

  }while(num_of_tokens < 1);

  if(num_of_tokens == 0)
    return 0;

  /* treat the white space as separators */
  token_bounds = tokenise(line, num_of_tokens, 0, &separator, 1, 0, 1);
  
  if(!token_bounds){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    return -1;
  }

  *tokens = (char **)
    calloc(f->num_of_nodes<num_of_tokens?f->num_of_nodes:num_of_tokens,
	   sizeof(char *));

  if(!tokens){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    free(token_bounds);
    return -1;
  }


  for(i = 0;
      i < (f->num_of_nodes<num_of_tokens?f->num_of_nodes:num_of_tokens);
      i++){

    token = (char *) calloc(token_bounds[2*i+1] - token_bounds[2*i] + 1,
			    sizeof(char));

    if(!token){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free_datafile(f);
      for(j = 0; j < i; j++)
	free(*tokens[j]);
      free(token_bounds);
      return -1;
    }

    strncpy(token, &(line[token_bounds[2*i]]),
	    token_bounds[2*i+1] - token_bounds[2*i]);

    token[token_bounds[2*i+1] - token_bounds[2*i]] = '\0';

    /* Children, remember what papa said: always use parentheses... */
    (*tokens)[i] = token;
  }

  free(token_bounds);

  /* Return the number of acquired tokens. */
  return f->num_of_nodes<num_of_tokens?f->num_of_nodes:num_of_tokens;
}


char *next_token(int *token_length){

  /* The last line read from the file */
  static char last_line[MAX_LINELENGTH];

  /* How many tokens left in the current line of input? */
  static int tokens_left;

  /* Pointer to the index array of token boundaries */
  static int *indexarray = NULL;

  /* Pointer to the index array of token boundaries
   * (not incremented, we need this for free() ) */
  static int *indexarray_original = NULL;

  /* The token we return */
  char *token;

  /* Return if some evil mastermind gives us a NULL pointer */
  if(!token_length){
    if(indexarray_original){
      free(indexarray_original);
      indexarray = NULL;
      indexarray_original = NULL;
    }

    return NULL;
  }

  /* Return if input file is not open */
  if(!nip_yyparse_infile_open){
    if(indexarray_original){
      free(indexarray_original);
      indexarray = NULL;
      indexarray_original = NULL;
    }

    return NULL;
  }

  /* Read new line if needed and do other magic... */
  while(nip_read_line){
    /* Read the line and check for EOF */
    if(!(fgets(last_line, MAX_LINELENGTH, nip_yyparse_infile))){

      if(indexarray_original){
	free(indexarray_original);
	indexarray = NULL;
	indexarray_original = NULL;
      }

      *token_length = 0;
      return NULL;
    }
    /* How many tokens in the line? */
    tokens_left = count_tokens(last_line, NULL, 1, "(){}=,;", 7, 1, 1);

    /* Check whether the line has any tokens. If not, read a new line
     * (go to beginning of loop) */
    if(tokens_left > 0){
      /* Adjust pointer to the beginning of token boundary index array */
      if(indexarray_original){
	free(indexarray_original);
	indexarray = NULL;
	indexarray_original = NULL;
      }

      indexarray = tokenise(last_line, tokens_left, 1, "(){}=,;", 7, 1, 1);
      indexarray_original = indexarray;

      /* Check if tokenise failed. If it failed, we have no other option
       * than to stop: return NULL, *token_length = 0.
       */
      if(!indexarray){
	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	*token_length = 0;

	return NULL;
      }

      /* Ignore lines that have COMMENT_CHAR as first non-whitespace char */
      if(last_line[indexarray[0]] == COMMENT_CHAR)
	nip_read_line = 1;
      else
	nip_read_line = 0;
    }
  }

  *token_length = indexarray[1] - indexarray[0];
  token = (char *) calloc(*token_length + 1, sizeof(char));
  if(!token){
    if(indexarray_original){
      free(indexarray_original);
      indexarray = NULL;
      indexarray_original = NULL;
    }
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    *token_length = -1;
    return NULL;
  }

  /* Copy the token */
  strncpy(token, &(last_line[indexarray[0]]), *token_length);

  /* NULL terminate the token. */
  token[*token_length] = '\0';

  indexarray += 2;
  
  /* If all the tokens have been handled, read a new line next time */
  if(--tokens_left == 0)
    nip_read_line = 1;

  /* Still some tokens left. Check for COMMENT_CHAR. */
  else if(last_line[indexarray[0]] == COMMENT_CHAR)
    nip_read_line = 1;

#ifdef PRINT_TOKENS
  printf("%s\n", token);
#endif

  return token;
}


/* Adds a variable into a temporary list for creating an array. 
 * The variable is chosen from THE list of variables 
 * according to the given symbol. */
int add_symbol(variable v){
  varlink new;

  if(v == NULL){
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return ERROR_NULLPOINTER;
  }

  new = (varlink) malloc(sizeof(varelement));
  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  new->data = v;
  new->fwd = NULL;
  new->bwd = nip_last_temp_var;

  if(nip_first_temp_var == NULL)
    nip_first_temp_var = new;
  else
    nip_last_temp_var->fwd = new;
    
  nip_last_temp_var = new;
  nip_symbols_parsed++;

  return NO_ERROR;
}


void set_parser_node_position(double x, double y){
  parser_node_position_x = abs((int)x);
  parser_node_position_y = abs((int)y);
}


void set_variable_position(variable v){
  set_position(v, parser_node_position_x, parser_node_position_y);
}


void set_parser_node_size(double x, double y){
  parser_node_size_x = abs((int)x);
  parser_node_size_y = abs((int)y);
}


void get_parser_node_size(int* x, int* y){
  *x = parser_node_size_x;
  *y = parser_node_size_y;
}


/* correctness? */
int add_initData(potential p, variable child, variable* parents){
  initDataLink new = (initDataLink) malloc(sizeof(initDataElement));

  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  new->data = p;
  new->child = child;
  new->parents = parents;
  new->fwd = NULL;
  new->bwd = nip_last_initData;
  if(nip_first_initData == NULL)
    nip_first_initData = new;
  else
    nip_last_initData->fwd = new;

  nip_last_initData = new;

  nip_initData_parsed++;
  return NO_ERROR;
}


int add_time_init(variable var, char* name){
  time_init_link new = (time_init_link) malloc(sizeof(time_init_element));

  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  new->var = var;
  new->previous = name;
  new->fwd = nip_first_timeinit;

  nip_first_timeinit = new;

  return NO_ERROR;
}


int add_string(char* string){
  stringlink new = (stringlink) malloc(sizeof(stringelement));

  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  new->data = string;
  new->fwd = NULL;
  new->bwd = nip_last_string;
  if(nip_first_string == NULL)
    nip_first_string = new;
  else
    nip_last_string->fwd = new;

  nip_last_string = new;
  nip_strings_parsed++;

  return NO_ERROR;
}


/*
 * Adds a string to the beginning of the list s.
 * Pointer s is altered so that it points to the new beginning of the list.
 */
static int add_to_stringlink(stringlink *s, char* string){
  stringlink new = (stringlink) malloc(sizeof(stringelement));

#ifdef DEBUG_DATAFILE
  printf("add_to_stringlink called\n");
#endif

  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  if(s == NULL){
    free(new);
    report_error(__FILE__, __LINE__, ERROR_NULLPOINTER, 1);
    return ERROR_NULLPOINTER;
  }

  /*  new->data = string; */ /* Let's copy the string instead */
  if(string){
    new->data = (char *) calloc(strlen(string) + 1, sizeof(char));
    if(!(new->data)){
      free(new);
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return ERROR_OUTOFMEMORY;
    }
    strcpy(new->data, string);
  }

  new->fwd = *s;
  new->bwd = NULL;

  if(*s != NULL)
    (*s)->bwd = new;

  *s = new;

  return NO_ERROR;
}


/*
 * Checks if the given string is in the list s (search forward).
 */
static int search_stringlinks(stringlink s, char* string){

  while(s != NULL){
    if(strcmp(string, s->data) == 0){
      return 1;
    }
    s = s->fwd;
  }

  return 0;
}

/* Creates an array from the variable in the list. 
 * NOTE: because of misunderstanding, the list is backwards. 
 * (Can't understand Hugin fellows...) */
variable* make_variable_array(){
  int i;
  variable* vars1 = (variable*) calloc(nip_symbols_parsed, sizeof(variable));
  varlink pointer = nip_last_temp_var;

  if(!vars1){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  for(i = 0; i < nip_symbols_parsed; i++){
    vars1[i] = pointer->data;
    pointer = pointer->bwd;
  }
  return vars1;
}


/* Creates an array from the strings in the list. 
 * The size will be strings_parsed. */
char** make_string_array(){
  int i;
  stringlink ln = nip_first_string;
  char** new = (char**) calloc(nip_strings_parsed, sizeof(char*));

  if(!new){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return NULL;
  }

  for(i = 0; i < nip_strings_parsed; i++){
    new[i] = ln->data; /* char[] references copied */
    ln = ln->fwd;
  }
  return new;
}


/* Removes everything from the list of strings and resets the counter. 
 * The actual memory for the strings is not freed also. */
void reset_strings(){
  int i;
  stringlink ln = nip_last_string;
  nip_last_string = NULL;
  while(ln != NULL){
    free(ln->fwd); /* free(NULL) is O.K. at the beginning */
    ln = ln->bwd;
  }
  free(nip_first_string);
  nip_first_string = NULL;

  for(i = 0; i < nip_strings_parsed; i++)
    free(nip_statenames[i]);
  free(nip_statenames);
  nip_statenames = NULL;

  nip_strings_parsed = 0;  
  return;
}


/* Removes everything from the temporary list of variables. */
void reset_symbols(){
  varlink ln = nip_last_temp_var;
  nip_last_temp_var = NULL;
  while(ln != NULL){
    free(ln->fwd); /* free(NULL) is O.K. at the beginning */
    ln = ln->bwd;
  }
  free(nip_first_temp_var);
  nip_first_temp_var = NULL;
  nip_symbols_parsed = 0;
  return;
}


/* Frees some memory after parsing. */
void reset_initData(){
  initDataLink ln = nip_last_initData;
  initDataLink temp;
  nip_last_initData = NULL;
  while(ln != NULL){
    free_potential(ln->data);
    free(ln->parents); /* calloc is in make_variable_array(); */
    temp = ln;
    ln = ln->bwd;
    free(temp);
  }
  nip_first_initData = NULL;
  nip_initData_parsed = 0;  
  return;
}


/* Frees some memory after parsing. 
 * JJT: DO NOT TOUCH THE ACTUAL DATA, OR IT WILL BE LOST. */
void reset_timeinit(){
  time_init_link ln = nip_first_timeinit;
  while(ln != NULL){
    nip_first_timeinit = ln->fwd;
    free(ln->previous); /* calloc is somewhere in the lexer */
    free(ln);
    ln = nip_first_timeinit;    
  }

  return;
}


void init_new_Graph(){
  nip_graph = new_graph(total_num_of_vars());
}


int parsedVars2Graph(){
  int i, retval;
  variable v;
  variable_iterator it;
  initDataLink initlist = nip_first_initData;

  it = get_first_variable();
  v = next_variable(&it);
  
  /* Add parsed variables to the graph. */
  while(v != NULL){
    retval = add_variable(nip_graph, v);
    if(retval != NO_ERROR){
      report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
      return ERROR_GENERAL;
    }
    v = next_variable(&it);
  }

  /* Add child - parent relations to the graph. */
  while(initlist != NULL){  
    for(i = 0; i < initlist->data->num_of_vars - 1; i++){
      retval = add_child(nip_graph, initlist->parents[i], initlist->child);
      if(retval != NO_ERROR){
	report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	return ERROR_GENERAL;
      }
    }
    
    /* Add child - parent relations to variables themselves also */
    set_parents(initlist->child, initlist->parents, 
		initlist->data->num_of_vars - 1);

    initlist = initlist->fwd;
  }
  return NO_ERROR;
}


int time2Vars(){
  int i, m;
  variable v1, v2;
  time_init_link initlist = nip_first_timeinit;
  variable_iterator it = get_first_variable();

  /* Add time relations to variables. */
  while(initlist != NULL){
    v1 = initlist->var;
    v2 = get_parser_variable(initlist->previous);
    if(v1->cardinality == v2->cardinality){
      /* check one thing */
      for(i = 0; i < v1->cardinality; i++){
	if(strcmp(get_statename(v1, i), get_statename(v2, i))){
	  fprintf(stderr, 
		  "Warning: Corresponding variables %s and %s\n", 
		  get_symbol(v1), get_symbol(v2));
	  fprintf(stderr, 
		  "have different kind of states %s and %s!\n", 
		  get_statename(v1,i), get_statename(v2,i));
	}
      }
      /* the core */
      v2->next = v1;     /* v2 belongs to V(t)   */
      v1->previous = v2; /* v1 belongs to V(t-1) */
    }
    else{
      fprintf(stderr, 
	      "NET parser: Invalid 'NIP_next' field for node %s!\n",
	      get_symbol(v1));
      report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
      return ERROR_GENERAL;
    }
    initlist = initlist->fwd;
  }

  /* Find out which variables belong to incoming/outgoing interfaces */
  v2 = next_variable(&it); /* get a variable */
  while(v2 != NULL){
    m = 0;

    for(i = 0; i < v2->num_of_parents; i++){ /* for each parent */
      v1 = v2->parents[i];

      /* Condition for belonging to the incoming interface */
      if(v1->previous != NULL && 
	 v2->previous == NULL){ 
	/* v2 has the parent v1 in the previous time slice */
	v1->if_status |= INTERFACE_OLD_OUTGOING;
	v1->previous->if_status |= INTERFACE_OUTGOING;
	v2->if_status |= INTERFACE_INCOMING;
	m = 1;
	/* break; ?? */

#ifdef DEBUG_PARSER
	fprintf(stdout, 
	      "NET parser: Node %s in I_{t}->\n",
	      get_symbol(v1->previous));
	fprintf(stdout, 
	      "NET parser: Node %s in I_{t-1}->\n",
	      get_symbol(v1));
	fprintf(stdout, 
	      "NET parser: Node %s in I_{t}<-\n",
	      get_symbol(v2));
#endif
      }
    }
    if(m){ /* parents of v2 in this time slice belong to incoming interface */
      for(i = 0; i < v2->num_of_parents; i++){
	v1 = v2->parents[i];
	if(v1->previous == NULL){ 
	  /* v1 (in time slice t) is married with someone in t-1 */
	  v1->if_status |= INTERFACE_INCOMING;

#ifdef DEBUG_PARSER
	fprintf(stdout, 
	      "NET parser: Node %s in I_{t}<-\n",
	      get_symbol(v1));
#endif
	}
      }
    }
    v2 = next_variable(&it);
  }

  return NO_ERROR;
}


int Graph2JTree(){
  int num_of_cliques;
  clique **cliques = &nip_cliques;

  /* Construct the cliques. */
  num_of_cliques = find_cliques(nip_graph, cliques);

  /* Error check */
  if(num_of_cliques < 0){
    report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
    free_graph(nip_graph);
    nip_graph = NULL;
    return ERROR_GENERAL;
  }

  nip_num_of_cliques = num_of_cliques;

  free_graph(nip_graph); /* Get rid of the graph (?) */
  nip_graph = NULL;

  return NO_ERROR; 
}


int parsedPots2JTree(){
  int i, nvars; 
  int retval;
  initDataLink initlist = nip_first_initData;
  variable* vars;
  variable var;
  variable_iterator it;
  clique fam_clique = NULL;;

  /* <ugly patch> */
  nvars = total_num_of_vars();
  vars = (variable*) calloc(nvars, sizeof(variable));
  if(!vars){
    report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
    return ERROR_OUTOFMEMORY;
  }

  i = 0;
  it = get_first_variable();
  var = next_variable(&it);
  while(var){
    vars[i++] = var;
    var = next_variable(&it);
  }
  /* <\ugly patch> */

  while(initlist != NULL){    
    fam_clique = find_family(nip_cliques, nip_num_of_cliques,
			     initlist->child);

    if(fam_clique != NULL){
      if(initlist->data->num_of_vars > 1){
	/* Conditional probability distributions are initialised into
	 * the jointree potentials */
	retval = initialise(fam_clique, initlist->child, 
			    initlist->data, 0); /* THE job */
	if(retval != NO_ERROR){
	  report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	  free(vars);
	  return ERROR_GENERAL;
	}
      }
      else{ 
	/* Priors of the independent variables are stored into the variable 
	 * itself, but NOT entered into the model YET. */
	/*retval = enter_evidence(vars, nvars, nip_cliques, 
	 *			nip_num_of_cliques, initlist->child, 
	 *			initlist->data->data);*/
	retval = set_prior(initlist->child, initlist->data->data);
	if(retval != NO_ERROR){
	  report_error(__FILE__, __LINE__, ERROR_GENERAL, 1);
	  free(vars);
	  return ERROR_GENERAL;
	}
      }
    }
    else
      fprintf(stderr, "In parser.c (%d): find_family failed!\n", __LINE__);

    initlist = initlist->fwd;
  }

  free(vars);
  return NO_ERROR;
}


void print_parsed_stuff(){
  int i, j;
  unsigned long temp;
  initDataLink list = nip_first_initData;

  /* Traverse through the list of parsed potentials. */
  while(list != NULL){
    int *indices; 
    int *temp_array;
    variable *variables;

    if((indices = (int *) calloc(list->data->num_of_vars,
				 sizeof(int))) == NULL){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      return;
    }

    if((temp_array = (int *) calloc(list->data->num_of_vars,
				    sizeof(int))) == NULL){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free(indices);
      return;
    }

    if((variables = (variable *) calloc(list->data->num_of_vars,
					sizeof(variable))) == NULL){
      report_error(__FILE__, __LINE__, ERROR_OUTOFMEMORY, 1);
      free(indices);
      free(temp_array);
      return;
    }
    
    variables[0] = list->child;
    for(i = 1; i < list->data->num_of_vars; i++)
      variables[i] = (list->parents)[i - 1];

    /* reorder[i] is the place of i:th variable (in the sense of this program) 
     * in the array variables[] */
    
    /* init (needed?) */
    for(i = 0; i < list->data->num_of_vars; i++)
      temp_array[i] = 0;
    
    /* Create the reordering table: O(num_of_vars^2) i.e. stupid but working.
     * Note the temporary use of indices array. */
    for(i = 0; i < list->data->num_of_vars; i++){
      temp = get_id(variables[i]);
      for(j = 0; j < list->data->num_of_vars; j++){
	if(get_id(variables[j]) > temp)
	  temp_array[j]++; /* counts how many greater variables there are */
      }
    }
    
    /* Go through every number in the potential array. */
    for(i = 0; i < list->data->size_of_data; i++){
      
      inverse_mapping(list->data, i, indices);

      printf("P( %s = %s", list->child->symbol, 
	     (list->child->statenames)[indices[temp_array[0]]]);

      if(list->data->num_of_vars > 1)
	printf(" |");

      for(j = 0; j < list->data->num_of_vars - 1; j++)
	printf(" %s = %s", ((list->parents)[j])->symbol,
	       (((list->parents)[j])->statenames)[indices[temp_array[j + 1]]]);
      
      printf(" ) = %.2f \n", (list->data->data)[i]);
    }
    list = list->fwd;
    
    free(indices);
    free(temp_array);
    free(variables);
  }
}


void set_nip_statenames(char **states){
  nip_statenames = states;
}


char** get_nip_statenames(){
  return nip_statenames;
}


void set_nip_label(char *label){
  nip_label = label;
}


char* get_nip_label(){
  return nip_label;
}


void set_nip_persistence(char *name){
  nip_persistence = name;
}


char* get_nip_persistence(){
  return nip_persistence;
}


int get_nip_symbols_parsed(){
  return nip_symbols_parsed;
}


int get_nip_strings_parsed(){
  return nip_strings_parsed;
}


int get_num_of_cliques(){
  return nip_num_of_cliques;
}


clique **get_cliques_pointer(){
  return &nip_cliques;
}

void reset_clique_array(){
  nip_cliques = NULL;
  nip_num_of_cliques = 0;
}
