/* iotest.c 
 *
 * A test for io functions. Reads from stdin.
 * Currently prints the number of characters, words and lines.
 * Also prints the word boundaries of each line, and prints a few
 * of the first words, showing the wonderful ability to split strings
 * at word boundaries.
 *
 * Author: Janne Toivola, Mikko Korpela
 * $Id: iotest.c,v 1.8 2010-12-03 17:21:28 jatoivol Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nipstring.h"

#define PRINTWORDS 20
#define BUF_SIZE 10000

/*
#define TOKENS
*/

int main(int argc, char **argv){

  char s[BUF_SIZE];
  int chars = 0, words = 0, lines = 0;
  int chartemp, wordtemp, i;
  int only_count = 0;
  int *wordbounds = NULL;
  char *splitwords[PRINTWORDS];
  char **wordarray;

  if(argc > 1)
    only_count = 1;

  while(fgets(s, BUF_SIZE, stdin) != NULL){

    printf("%s\n", s);

#if defined(TOKENS)
    wordtemp = nip_count_tokens(s, &chartemp, 1, "(){}=,;", 7, 1, 1);
#else
    wordtemp = nip_count_words(s, &chartemp);
#endif

    if(wordtemp == 0)
      break;

    if(!only_count){
#if defined(TOKENS)
      wordbounds = nip_tokenise(s, wordtemp, 1, "(){}=,;", 7, 1, 1);
#else
      wordbounds = nip_tokenise(s, wordtemp, 0, NULL, 0, 0, 1);
#endif

      if(words < PRINTWORDS){
	wordarray = nip_split(s, wordbounds, wordtemp);
	for(i = 0; i < wordtemp; i++){
	  if(words < PRINTWORDS)
	    splitwords[words] = wordarray[i];
	  else
	    free(wordarray[i]);
	  words++;
	}
      }
      else
	words += wordtemp;
    }
    else
      words += wordtemp;

    if(!only_count){
      /* Print the word bounds. */
      if(wordbounds != NULL){
	for(i = 0; i < 2 * wordtemp; i++)
	  printf("%d ", wordbounds[i]); 
	printf("\n");
	free(wordbounds);
      }
    }

    chars += chartemp + 1;
    lines++;
  }

  /* Print results */
  printf("%d lines, %d words, %d chars\n", lines, words, chars);

  if(!only_count){
    if(words < PRINTWORDS)
      wordtemp = words;
    else
      wordtemp = PRINTWORDS;
    printf("%d first words:\n", wordtemp);
    for(i = 0; i < wordtemp; i++)
      printf("%s ", splitwords[i]);
    printf("\n");
  }

  return 0;
}
