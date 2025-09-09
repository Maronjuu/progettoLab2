#! /usr/bin/env python3

import argparse, sys


Description = """
Compute pagerank for a directed graph represented by the list of its edges
following the Matrix Market format: https://math.nist.gov/MatrixMarket/formats.html#MMformat
Uses teleporting and the damping factor as in the original PageRank paper
Display the k highest ranked nodes (default k=3)
"""


# Matrix Market format for binary (asymmetric) matrices in brief:
#  lines starting with % are comments
#  the first non-comment line contains the number of rows, the number of 
#  columns and the number of non-zero matrix entries
#  the following lines contain the row and column each non-zero entry
#  the row and column id's are 1-based (not 0-based)
#  (when we read them we subtract one to each entry, so our results are 0-based)

# at the very begining of the file there may be a line like this:
# %%MatrixMarket matrix coordinate pattern general
# but it is not mandatory and we don't check this


# compute the pagerank of a directed graph
# graph is a list of tuples (tail, head) representing directed edges
# nodes must be integers and we assume that the number of nodes 
# is equal to the maximum node_id +1 (ie id's are 0-based) 
# code idea from http://dpk.io/pagerank
def pagerank(graph, damping=0.9, epsilon=1.0e-8, maxiter=100, verbose = False):
    inlink_map = {}     # inlink_map[i] = set of nodes with an arc leading to node i
    outlink_counts = {} # outlink_counts[i] number outgoing edges for node i
    
    # helper function to add a new node to the graph
    def new_node(node):
        if node not in inlink_map: inlink_map[node] = set()
        if node not in outlink_counts: outlink_counts[node] = 0
    
    # note: tail==origin, head==destination
    for tail_node, head_node in graph:
        new_node(tail_node)
        new_node(head_node)
        # skip self-loops (usually done in the web graph)
        if tail_node == head_node: continue
        # add the edge to the graph if it not a duplicate 
        if tail_node not in inlink_map[head_node]:
            inlink_map[head_node].add(tail_node)
            outlink_counts[tail_node] += 1
    
    # compute # nodes in the graph and set of all the dead end nodes 
    tot_nodes = 1+max( set(inlink_map.keys()) )
    # save nodes with no outgoing links (dead end nodes)
    de_nodes = set(n for n in range(tot_nodes) if (n not in outlink_counts or outlink_counts[n]==0))

    print(f"Number of nodes: {tot_nodes}")
    print(f"Number of dead-end nodes: {len(de_nodes)}")
    print(f"Number of valid arcs: {sum(outlink_counts.values())}")

    # show graph if required
    if verbose:
      print("--- incoming edges")
      print(inlink_map)
      print("--- outdegrees")
      print(outlink_counts)
    
    # pagerank computation 
    initial_value = 1 / tot_nodes
    # initial rank vector 
    ranks = tot_nodes * [initial_value]

    delta = 1.0 + epsilon
    n_iterations = 0
    # iterate until convergence or maximum number of iterations
    while delta >= epsilon and n_iterations < maxiter:
        # resetto il vettore XTnext
        new_ranks = tot_nodes * [0]
        # Somma St per ogni nodo in DE
        sum_de_ranks = sum(ranks[n] for n in de_nodes)
        # for(i=0; i<g->N; i++)
        for node in range(tot_nodes):
            # add contribution of inlinks, if any
            
            if node in inlink_map:
               inlinks  = inlink_map[node]
               # new_ranks è XTnext
               new_ranks[node] = damping * sum(ranks[inlink] / outlink_counts[inlink] for inlink in inlinks)
            # contribution of teleporting
            new_ranks[node] += ((1 - damping) / tot_nodes)
            # contribution of dead end nodes 
            new_ranks[node] += damping*sum_de_ranks/tot_nodes 
        # compute current delta
        delta = sum( abs(new_ranks[node] - ranks[node]) for node in range(tot_nodes))
        # update ranks and iteration counter
        ranks = new_ranks
        n_iterations += 1
    # return pagerank values and number of iterations     
    return ranks, n_iterations, delta


if __name__ == '__main__':
  # parsing della linea di comando vedere la guida
  #    https://docs.python.org/3/howto/argparse.html
  parser = argparse.ArgumentParser(description=Description, formatter_class=argparse.RawTextHelpFormatter)
  parser.add_argument('infile', help='input file', type = str) 
  parser.add_argument('-k', help='show top K nodes (default 3)', type = int, default=3) 
  parser.add_argument('-m', help='maximum number of iterations (default 100)', type = int, default=100) 
  parser.add_argument('-d', help='damping factor (default 0.9)', type = float, default=0.9)
  parser.add_argument('-e', help='max error (default 1.0e7)', type = float, default=1.0e-7)
  parser.add_argument('-v', help='verbose output', action='store_true') 
  args = parser.parse_args()
  # skip comments and reach description of the matrix
  with open(args.infile) as f:
    totlines = 0
    for line in f:
      totlines += 1
      if line[0] == '%': continue # skip comments
      try:
        rows, cols, entries = map(int, line.split())
      except ValueError:
        print(f"Error: wrong format at line {totlines} of the input file (3 ints expected)")
        sys.exit(1)
      break
    assert(rows > 0 and cols > 0), "Error: illegal number of rows or columns"
    assert(entries > 0), "Error: illegal number of entries"
    assert(rows==cols), "Error: number of rows and columns must be equal"
    # create list of arcs
    graph = []
    for line in f:
      totlines += 1
      if line[0] == '%': continue
      try:
        tail, head = map(int, line.split())
      except ValueError:
        print(f"Error: wrong format in line {totlines} of the input file (2 ints expected)")
        sys.exit(1)
      assert(1 <= tail <= rows), "Error: tail node out of range"
      assert(1 <= head <= cols), "Error: head node out of range"
      graph.append((tail-1, head-1))
    assert(len(graph) == entries), "Error: number of nonzeros does not match the number of arcs"
  # compute pagerank
  ranks, n_iterations, delta = pagerank(graph, verbose=args.v, damping=args.d, epsilon=args.e, maxiter=args.m)
  if n_iterations == args.m:
      print(f"Did not converge after {n_iterations} iterations")
      # better: if delta>args.e:
      #           print(f"Stopped after {n_iterations} iterations, delta={delta}")
  else:
      print(f"Converged after {n_iterations} iterations")
  print(f"Sum of ranks: {sum(ranks):.4f}   (should be 1)")
  print(f"Top {args.k} nodes:")
  # creates node ids and sort them according to rank
  ids = range(len(ranks))
  for node in sorted(ids, key=lambda x: ranks[x], reverse=True)[:args.k]:
      print(f"  {node} {ranks[node]:.6f}")
