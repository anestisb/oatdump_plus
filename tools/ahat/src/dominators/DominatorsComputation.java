/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.ahat.dominators;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Queue;

/**
 * Generic DominatorsComputation.
 *
 * To use the dominators computation, have your graph nodes implement the
 * DominatorsComputation.Node interface, then call
 * DominatorsComputation.computeDominators on the single root node.
 */
public class DominatorsComputation {
  /**
   * Interface for a directed graph to perform the dominators computation on.
   */
  public interface Node {
    /**
     * Associate the given dominator state with this node.
     */
    void setDominatorsComputationState(Object state);

    /**
     * Get the most recent dominator state associated with this node using
     * setDominatorsComputationState. If setDominatorsComputationState has not
     * yet been called, this should return null.
     */
    Object getDominatorsComputationState();

    /**
     * Return a collection of nodes referenced from this node, for the
     * purposes of computing dominators.
     */
    Iterable<? extends Node> getReferencesForDominators();

    /**
     * Update this node's dominator based on the results of the dominators
     * computation.
     */
    void setDominator(Node dominator);
  }

  // NodeS is information associated with a particular node for the
  // purposes of computing dominators.
  // By convention we use the suffix 'S' to name instances of NodeS.
  private static class NodeS {
    // The node that this NodeS is associated with.
    public Node node;

    // Unique identifier for this node, in increasing order based on the order
    // this node was visited in a depth first search from the root. In
    // particular, given nodes A and B, if A.id > B.id, then A cannot be a
    // dominator of B.
    public long id;

    // Upper bound on the id of this node's dominator.
    // The true immediate dominator of this node must have id <= domid.
    // This upper bound is slowly tightened as part of the dominators
    // computation.
    public long domid;

    // The current candidate dominator for this node.
    // Invariant: (domid < domS.id) implies this node is on the queue of
    // nodes to be revisited.
    public NodeS domS;

    // A node with a reference to this node that is one step closer to the
    // root than this node.
    // Invariant: srcS.id < this.id
    public NodeS srcS;

    // The set of nodes X reachable by 'this' on a path of nodes from the
    // root with increasing ids (possibly excluding X) that this node does not
    // dominate (this.id > X.domid).
    // We can use a List instead of a Set for this because we guarentee that
    // we don't add the same node more than once to the list (see below).
    public List<NodeS> undom = new ArrayList<NodeS>();

    // The largest id of the node X for which we did X.undom.add(this).
    // This is an optimization to avoid adding duplicate node entries to the
    // undom set.
    //
    // The first time we see this node, we reach it through a path of nodes
    // with IDs 0,...,a,this. These form our src chain to the root.
    //
    // The next time we see this node, we reach it through a path of nodes
    // with IDS 0,...,b,c,...,d,this. Where all 0,...,b < a and all c,...,d > a.
    //
    // The next time we see this node, we reach it through a path of nodes
    // with IDS 0,...,e,f,...,g,this. With all 0,...,e < d and all f,...,g > d.
    // And so on.
    //
    // The first time we see this node, we set undomid to a.id. Nodes 0,...,a
    // will be added as undom in the 'revisit' phase of the node.
    //
    // The next times we see this node, we mark a+,...,d as undom and
    // change undomid to d. And so on.
    public long undomid;
  }

  private static class Link {
    public NodeS srcS;
    public Node dst;

    public Link(NodeS srcS, Node dst) {
      this.srcS = srcS;
      this.dst = dst;
    }
  }

  /**
   * Compute the dominator tree rooted at the given node.
   * There must not be any incoming references to the root node.
   */
  public static void computeDominators(Node root) {
    long id = 0;

    // List of all nodes seen. We keep track of this here to update all the
    // dominators once we are done.
    List<NodeS> nodes = new ArrayList<NodeS>();

    // The set of nodes N such that N.domid < N.domS.id. These nodes need
    // to be revisisted because their dominator is clearly wrong.
    // Use a Queue instead of a Set because performance will be better. We
    // avoid adding nodes already on the queue by checking whether it was
    // already true that N.domid < N.domS.id, in which case the node is
    // already on the queue.
    Queue<NodeS> revisit = new ArrayDeque<NodeS>();

    // Set up the root node specially.
    NodeS rootS = new NodeS();
    rootS.node = root;
    rootS.id = id++;
    root.setDominatorsComputationState(rootS);

    // 1. Do a depth first search of the nodes, label them with ids and come
    // up with intial candidate dominators for them.
    Deque<Link> dfs = new ArrayDeque<Link>();
    for (Node child : root.getReferencesForDominators()) {
      dfs.push(new Link(rootS, child));
    }

    while (!dfs.isEmpty()) {
      Link link = dfs.pop();
      NodeS dstS = (NodeS)link.dst.getDominatorsComputationState();
      if (dstS == null) {
        // This is the first time we have seen the node. The candidate
        // dominator is link src.
        dstS = new NodeS();
        dstS.node = link.dst;
        dstS.id = id++;
        dstS.domid = link.srcS.id;
        dstS.domS = link.srcS;
        dstS.srcS = link.srcS;
        dstS.undomid = dstS.domid;
        nodes.add(dstS);
        link.dst.setDominatorsComputationState(dstS);

        for (Node child : link.dst.getReferencesForDominators()) {
          dfs.push(new Link(dstS, child));
        }
      } else {
        // We have seen the node already. Update the state based on the new
        // potential dominator.
        NodeS srcS = link.srcS;
        boolean revisiting = dstS.domid < dstS.domS.id;

        while (srcS.id > dstS.domid) {
          if (srcS.id > dstS.undomid) {
            srcS.undom.add(dstS);
          }
          srcS = srcS.srcS;
        }
        dstS.undomid = link.srcS.id;

        if (srcS.id < dstS.domid) {
          // In this case, dstS.domid must be wrong, because we just found a
          // path to dstS that does not go through dstS.domid:
          // All nodes from root to srcS have id < domid, and all nodes from
          // srcS to dstS had id > domid, so dstS.domid cannot be on this path
          // from root to dstS.
          dstS.domid = srcS.id;
          if (!revisiting) {
            revisit.add(dstS);
          }
        }
      }
    }

    // 2. Continue revisiting nodes until they all satisfy the requirement
    // that domS.id <= domid.
    while (!revisit.isEmpty()) {
      NodeS nodeS = revisit.poll();
      NodeS domS = nodeS.domS;
      assert nodeS.domid < domS.id;
      while (domS.id > nodeS.domid) {
        if (domS.domS.id < nodeS.domid) {
          // In this case, nodeS.domid must be wrong, because there is a path
          // from root to nodeS that does not go through nodeS.domid:
          //  * We can go from root to domS without going through nodeS.domid,
          //    because otherwise nodeS.domid would dominate domS, not
          //    domS.domS.
          //  * We can go from domS to nodeS without going through nodeS.domid
          //    because we know nodeS is reachable from domS on a path of nodes
          //    with increases ids, which cannot include nodeS.domid, which
          //    has a smaller id than domS.
          nodeS.domid = domS.domS.id;
        }
        domS.undom.add(nodeS);
        domS = domS.srcS;
      }
      nodeS.domS = domS;
      nodeS.domid = domS.id;

      for (NodeS xS : nodeS.undom) {
        if (domS.id < xS.domid) {
          // In this case, xS.domid must be wrong, because there is a path
          // from the root to xX that does not go through xS.domid:
          //  * We can go from root to nodeS without going through xS.domid,
          //    because otherwise xS.domid would dominate nodeS, not domS.
          //  * We can go from nodeS to xS without going through xS.domid
          //    because we know xS is reachable from nodeS on a path of nodes
          //    with increasing ids, which cannot include xS.domid, which has
          //    a smaller id than nodeS.
          boolean revisiting = xS.domid < xS.domS.id;
          xS.domid = domS.id;
          if (!revisiting) {
            revisit.add(xS);
          }
        }
      }
    }

    // 3. Update the dominators of the nodes.
    root.setDominatorsComputationState(null);
    for (NodeS nodeS : nodes) {
      nodeS.node.setDominator(nodeS.domS.node);
      nodeS.node.setDominatorsComputationState(null);
    }
  }
}
