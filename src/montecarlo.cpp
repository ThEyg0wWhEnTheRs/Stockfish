/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef> // For offsetof()
#include <cstring> // For std::memset, std::memcmp
#include <iomanip>
#include <iostream>
#include <sstream>

#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "types.h"
#include "uci.h"
#include "montecarlo.h"
#include "syzygy/tbprobe.h"


// UCT is a class implementing Monte-Carlo Tree Search for Stockfish.
// We are following the survey http://mcts.ai/pubs/mcts-survey-master.pdf
// for the Monte-Carlo algorithm description and the notations used.

// Bibliography:
//     http://mcts.ai/pubs/mcts-survey-master.pdf
//     https://www.ke.tu-darmstadt.de/lehre/arbeiten/bachelor/2012/Arenz_Oleg.pdf
//     https://dke.maastrichtuniversity.nl/m.winands/publications.html



using namespace std;
using std::string;


Node create_node(const Position& pos) {
   return nullptr;  // TODO, FIXME : this should create a Node !
}

UCTInfo* get_infos(Node node) {
  return node;
}

Move move_of(Node node) {
    return get_infos(node)->last_move();
}

Edge* get_list_of_edges(Node node) {
    return get_infos(node)->edges_list();
}

int number_of_sons(Node node) {
    return get_infos(node)->sons;
}

// UCT::search() is the main function of UCT algorithm.

Move UCT::search(Position& p) {

    create_root(p);

    while (computational_budget()) {
       Node node = tree_policy();
       Reward reward = playout_policy(node);
       backup(node, reward);
    }

    return best_move(root, 0.0);
}


/// UCT::UCT() is the constructor for the UCT class
UCT::UCT(Position& p) : pos(p) {
    create_root(p);
}

/// UCT::create_root() initializes the UCT tree with the given position
void UCT::create_root(Position& p) {

    // Initialize the global counters
    doMoveCnt  = 0;
    treeSize   = 0;
    descentCnt = 0;
    ply        = 0;

    // Prepare the stack to go down and up in the game tree
    std::memset(stackBuffer, 0, sizeof(stackBuffer));
    for (int i = 4; i > 0; i--)
      stack[-i].contHistory = &(pos.this_thread()->contHistory[NO_PIECE][0]); // Use as sentinel

    // TODO : what to do with killers ???

    // TODO : setupStates should probably come the caller, as a global ???
    StateListPtr setupStates(new std::deque<StateInfo>(1));

    // Save a hard copy of the root position
    StateInfo tmp = setupStates->back();
    rootPosition.set(pos.fen(), pos.is_chess960(), &setupStates->back(), pos.this_thread());
    setupStates->back() = tmp;

    // Erase the list of nodes, and set the root node
    std::memset(nodesBuffer, 0, sizeof(nodesBuffer));
    root = nodes[0] = create_node(pos);

    assert(ply == 0);
    assert(root == nodes[0]);
}


/// UCT::computational_budget() stops the search if the computational budget
/// has been reached (time limit, or number of nodes, etc.)
bool UCT::computational_budget() {
    return (treeSize < 5);
}


/// UCT::tree_policy() selects the next node to be expanded
Node UCT::tree_policy() {
    descentCnt++;
    return root;
}


/// UCT::playout_policy() plays a semi random game starting from the last extended node
Reward UCT::playout_policy(Node node) {
    playoutCnt++;
    return 1.0;
}


/// UCT::UCB() calculates the upper confidence bound formula for the son which
/// we reach from node "node" by following the edge "edge".
double UCT::UCB(Node node, Edge& edge, double C) {
    UCTInfo* father = get_infos(node);
    double result;


    result = edge.actionValue / edge.visits;
    result += C * edge.prior * sqrt(father->visits) / (1 + edge.visits);

    return result;
}

/// UCT::backup() implements the strategy for accumulating rewards up
/// the tree after a playout.
void UCT::backup(Node node, Reward r) {
}


/// UCT::best_move() selects the best child of a node according to the UCT formula
Move UCT::best_move(Node node, double C) {
    Edge* edges = get_list_of_edges(node);
    Move best = MOVE_NONE;

    double bestValue = -100000000.0;
    for (int k = 0 ; k < number_of_sons(node) ; k++)
    {
        double r = UCB(node, edges[k], C);
        if ( r > bestValue )
        {
            bestValue = r;
            best = edges[k].move;
        }
    }

    return best;
}

/// UCT::set_exploration_constant() changes the exploration constant of the UCB formula.
///
/// This constant sets the balance between the exploitation of past results and the
/// exploration of new branches in the UCT tree. The higher the constant, the more
/// likely is the algorithm to explore new parts of the tree, whereas lower values
/// of the constant makes an algorithm which focuses more on the already explored
/// parts of the tree. Default value is 10.0
///
void UCT::set_exploration_constant(double C) {
    explorationConstant = C;
}

/// UCT::get_exploration_constant() returns the exploration constant of the UCB formula
double UCT::get_exploration_constant() {
    return explorationConstant;
}

/// UCT::current_node() is the current node of our tree exploration
Node UCT::current_node() {
    return nodes[ply];
}

/// UCT::do_move() plays a move in the search tree from the current position
void UCT::do_move(Move m) {

    stack[ply].ply         = ply;
    stack[ply].currentMove = m;
    stack[ply].contHistory = &(pos.this_thread()->contHistory[pos.moved_piece(m)][to_sq(m)]);

    pos.do_move(m, states[ply]);

    ply++;
}


/// UCT::undo_move() undo the current move in the search tree
void UCT::undo_move() {
    ply--;
    pos.undo_move(stack[ply].currentMove);
}

/// UCT::add_prior_to_node() adds the given (move,prior) pair as a new son for a node
void add_prior_to_node(Node node, Move m, Reward prior, int moveCount) {
   UCTInfo* s = get_infos(node);
   int n = s->sons;

   assert(n < MAX_SONS);

   if (n < MAX_SONS)
   {
       s->edges[n].visits         = 0;
       s->edges[n].move           = m;
       s->edges[n].prior          = prior;
       s->edges[n].actionValue    = 0.0;
       s->edges[n].meanAcionValue = 0.0;
       s->sons++;

       assert(s->sons == moveCount);
   }
   else
   {
   		std::cerr << "ERROR : too many sons (" << s->sons << ") in add_prior_to_node()" << std::endl;
   }
}


/// UCT::generate_moves() does some Stockfish gimmick to iterate over legal moves
/// of the current position, in a sensible order.
/// For historical reasons, it is not so easy to get a MovePicker object to
/// generate moves if we want to have a decent order (captures first, then
/// quiet moves, etc.). We have to pass various history tables to the MovePicker
/// constructor, like in the alpha-beta implementation of move ordering.
void UCT::generate_moves() {

    Thread*  thread      = pos.this_thread();
    Square   prevSq      = to_sq(stack[ply-1].currentMove);
    Move     countermove = thread->counterMoves[pos.piece_on(prevSq)][prevSq];
    Move     ttMove      = MOVE_NONE;  // FIXME
    Move*    killers     = stack[ply].killers;
    Depth    depth       = 30 * ONE_PLY;

    const CapturePieceToHistory* cph   = &thread->captureHistory;
    const ButterflyHistory* mh         = &thread->mainHistory;
    const PieceToHistory*   contHist[] = { stack[ply-1].contHistory,
                                           stack[ply-2].contHistory,
                                           nullptr,
                                           stack[ply-4].contHistory };

    MovePicker mp(pos, ttMove, depth, mh, cph, contHist, countermove, killers);

    Move move;
    Reward prior;
    int moveCount = 0;

    // Generate the legal moves and calculate their priors
    while ((move = mp.next_move()) != MOVE_NONE)
        if (pos.legal(move))
        {
            stack[ply].moveCount = ++moveCount;

            prior = calculate_prior(move, moveCount);
            add_prior_to_node(current_node(), move, prior, moveCount);
        }

    // Sort the moves according to their prior value
    int n = number_of_sons(current_node());
    if (n > 0)
    {
        Edge* edges = get_list_of_edges(current_node());
        std::sort(edges, edges + n, ComparePrior);
    }

    // Indicate that we have just expanded the current node
    UCTInfo* s = get_infos(current_node());
    s->visits       = 1;
    s->expandedSons = 0;
}

/// UCT::evaluate_with_minimax() evaluates the current position in the tree
/// with a small minimax search of the given depth. Use depth==DEPTH_ZERO
/// for a direct quiescence value.
Value UCT::evaluate_with_minimax(Depth depth) {
    return minimax_value(pos, &stack[ply], depth);
}

/// UCT::calculate_prior() returns the a-priori reward of the move leading to
/// the n-th son of the current node. Here we use the evaluation function to
/// estimate this prior, we could use other strategies too (like the rank n of
/// the son, or the type of the move (good capture/quiet/bad capture), etc).
Reward UCT::calculate_prior(Move move, int n) {
    Reward prior;

    do_move(move);
    prior = value_to_reward(evaluate_with_minimax(DEPTH_ZERO));
    undo_move();

    return prior;
}

/// UCT::value_to_reward() transforms a Stockfish value to a reward in [0..1]
/// We scale the logistic function such that a value of 600 (about three pawns)
/// is given a probability of win of 0.75, and a value of -600 is given a probability
/// of win of 0.25
Reward UCT::value_to_reward(Value v)
{
    const double k = -0.00183102048111;
    double r = 1.0 / (1 + exp(k * int(v)));
    return Reward(r);
}

/// UCT::reward_to_value() transforms a reward in [0..1] to a Stockfish value.
/// The scale is such that a reward of 0.75 corresponds to 600 (about three pawns),
/// and a reward of 0.25 corresponds to -600 (about minus three pawns).
Value UCT::reward_to_value(Reward r)
{
    if (r > 0.99) return  VALUE_KNOWN_WIN;
    if (r < 0.01) return -VALUE_KNOWN_WIN;

    const double g = 546.14353597715121;  //  this is 1 / k
    double v = g * log(r / (1.0 - r)) ;
    return Value(int(v));
}


/// UCT::test()
void UCT::test() {
   cerr << "---------------------------------------------------------------------------------" << endl;
   cerr << "Testing UCT for position..." << endl;
   cerr << pos << endl;
   cerr << "...end of UCT testing!" << endl;
   cerr << "---------------------------------------------------------------------------------" << endl;
}





