#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "Graph.h"
#include "Variable.h"


Graph* test1(void)
{
    Graph *G;
    Variable v[5];
    Variable *w;
    char *symbol, *name;
    
    int i;

    printf("\tTest 1... new_graph and add_variable\n");

    G = new_graph(5);
    
    for (i = 0; i < 5; i++)
    {
        sprintf(symbol, "var%d", i);
        sprintf(name, "Variable %d", i);
        v[i] = new_variable(symbol, name, NULL, 2);
    
        assert(add_variable(G, v[i])==0);
    }

    w = get_variables(G);
    assert(get_size(G) == 5);
    for (i = 0; i<5; i++) {
        assert(equal_variables(v[i], w[i]));
    }
    
    printf("\tTest 1 done.\n");
    return G;
}

void test2(Graph* G)
{
    Variable* v = get_variables(G);
    
    printf("\tTest 2... add_child, is_child\n");

    /* v1 -> v3 -> v5; v2 -> v4 -> v5 */
    
    assert(add_child(G, v[1], v[3]) == 0);
    assert(add_child(G, v[3], v[5]) == 0);
    assert(add_child(G, v[2], v[4]) == 0);
    assert(add_child(G, v[4], v[5]) == 0);

    assert(is_child(G, v[1], v[3]));
    assert(is_child(G, v[2], v[4]));        
    assert(is_child(G, v[3], v[5]));        
    assert(is_child(G, v[4], v[5]));        
    
    assert(!is_child(G, v[1], v[1]));
    assert(!is_child(G, v[5], v[3]));
    assert(!is_child(G, v[4], v[2]));

    printf("\tTest 2 done.\n");    
}


void test3(Graph* G)
{
    Graph* Gc;
    Variable *v, *w;
    int i,j;
    
    printf("\tTest3... copy_graph\n");
    Gc = copy_graph(G);
    v = get_variables(G);
    w = get_variables(Gc);
    assert(get_size(G) == get_size(Gc));
    for (i = 0; i < get_size(G); i++) {
        assert(equal_variables(v[i],w[i]));
    }
    for (i = 0; i < get_size(G); i++)
        for (j = 0; j < get_size(G); j++) {
            assert(is_child(G, v[i], v[j]) == is_child(G,w[i], w[j]));
        }
    
}

void main(void)
{
    Graph* G;
    printf("-------------------------------\n");
    printf("Testing graphs:\n");
    
    G = test1();
    test2(G);
    test3(G);
    
}
