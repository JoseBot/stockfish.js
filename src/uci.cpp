/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

using namespace std;

extern void benchmark(const Position& pos, istream& is);

namespace {

  // FEN string of the initial position, normal chess
  const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  // Keep a track of the position keys along the setup moves (from the start position
  // to the position just before the search starts). This is needed by the repetition
  // draw detection code.
  Search::StateStackPtr SetupStates;


  // position() is called when engine receives the "position" UCI command.
  // The function sets up the position described in the given FEN string ("fen")
  // or the starting position ("startpos") and then makes the moves given in the
  // following move list ("moves").

  void position(Position& pos, istringstream& is) {

    Move m;
    string token, fen;

    is >> token;

    if (token == "startpos")
    {
        fen = StartFEN;
        is >> token; // Consume "moves" token if any
    }
    else if (token == "fen")
        while (is >> token && token != "moves")
            fen += token + " ";
    else
        return;

    pos.set(fen, Options["UCI_Chess960"], Threads.main());
    SetupStates = Search::StateStackPtr(new std::stack<StateInfo>());

    // Parse move list (if any)
    while (is >> token && (m = UCI::to_move(pos, token)) != MOVE_NONE)
    {
        SetupStates->push(StateInfo());
        pos.do_move(m, SetupStates->top());
    }
  }


  // setoption() is called when engine receives the "setoption" UCI command. The
  // function updates the UCI option ("name") to the given value ("value").

  void setoption(istringstream& is) {

    string token, name, value;

    is >> token; // Consume "name" token

    // Read option name (can contain spaces)
    while (is >> token && token != "value")
        name += string(" ", !name.empty()) + token;

    // Read option value (can contain spaces)
    while (is >> token)
        value += string(" ", !value.empty()) + token;

    if (Options.count(name))
        Options[name] = value;
    else
        sync_cout << "No such option: " << name << sync_endl;
  }


  // go() is called when engine receives the "go" UCI command. The function sets
  // the thinking time and other parameters from the input string, and starts
  // the search.

  void go(const Position& pos, istringstream& is) {

    Search::LimitsType limits;
    string token;

    while (is >> token)
    {
        if (token == "searchmoves")
            while (is >> token)
                limits.searchmoves.push_back(UCI::to_move(pos, token));

        else if (token == "wtime")     is >> limits.time[WHITE];
        else if (token == "btime")     is >> limits.time[BLACK];
        else if (token == "winc")      is >> limits.inc[WHITE];
        else if (token == "binc")      is >> limits.inc[BLACK];
        else if (token == "movestogo") is >> limits.movestogo;
        else if (token == "depth")     is >> limits.depth;
        else if (token == "nodes")     is >> limits.nodes;
        else if (token == "movetime")  is >> limits.movetime;
        else if (token == "mate")      is >> limits.mate;
        else if (token == "infinite")  limits.infinite = true;
        else if (token == "ponder")    limits.ponder = true;
    }

    Threads.start_thinking(pos, limits, SetupStates);
  }

} // namespace


/// Wait for a command from the user, parse this text string as an UCI command,
/// and call the appropriate functions. Also intercepts EOF from stdin to ensure
/// that we exit gracefully if the GUI dies unexpectedly. In addition to the UCI
/// commands, the function also supports a few debug commands.

///NOTE: This has been modified for Stockfish.js since we can't have an infinite loop.
Position pos;
  void UCI::commandInit() {
    pos = Position(StartFEN, false, Threads.main()); // The root position
  }
  void UCI::command(const string& cmd) {
      string token;
      istringstream is(cmd);

      is >> skipws >> token;

      if (token == "quit" || token == "stop" || token == "ponderhit")
      {
          // The GUI sends 'ponderhit' to tell us to ponder on the same move the
          // opponent has played. In case Signals.stopOnPonderhit is set we are
          // waiting for 'ponderhit' to stop the search (for instance because we
          // already ran out of time), otherwise we should continue searching but
          // switch from pondering to normal search.
          if (token != "ponderhit" || Search::Signals.stopOnPonderhit)
          {
              Search::Signals.stop = true;
              Threads.main()->notify_one(); // Could be sleeping
          }
          else
              Search::Limits.ponder = false;
      }
      else if (token == "perft")
      {
          int depth;
          stringstream ss;

          is >> depth;
          ss << Options["Hash"]    << " "
             << Options["Threads"] << " " << depth << " current " << token;

          benchmark(pos, ss);
      }
      else if (token == "key")
          sync_cout << hex << uppercase << setfill('0')
                    << "position key: "   << setw(16) << pos.key()
                    << "\nmaterial key: " << setw(16) << pos.material_key()
                    << "\npawn key:     " << setw(16) << pos.pawn_key()
                    << dec << nouppercase << setfill(' ') << sync_endl;

      else if (token == "uci")
          sync_cout << "id name " << engine_info(true)
                    << "\n"       << Options
                    << "\nuciok"  << sync_endl;

      else if (token == "ucinewgame") TT.clear();
      else if (token == "go")         go(pos, is);
      else if (token == "position")   position(pos, is);
      else if (token == "setoption")  setoption(is);
      else if (token == "flip")       pos.flip();
      else if (token == "bench")      benchmark(pos, is);
      else if (token == "d")          sync_cout << pos.pretty() << sync_endl;
      else if (token == "isready")    sync_cout << "readyok" << sync_endl;
      else if (token == "eval")       sync_cout << Eval::trace(pos) << sync_endl;
      else
          sync_cout << "Unknown command: " << cmd << sync_endl;

   ///} while (token != "quit" && argc == 1); // Passed args have one-shot behaviour /// Can't have an infinite loop in JS.

   ///Threads.wait_for_think_finished(); // Cannot quit whilst the search is running /// Don't need this either.
}

/// format_value() converts a Value to a string suitable for use with the UCI
/// protocol specifications:
///
/// cp <x>     The score from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies. If the engine is getting mated
///            use negative values for y.

string UCI::format_value(Value v, Value alpha, Value beta) {

  stringstream ss;

  if (abs(v) < VALUE_MATE_IN_MAX_PLY)
      ss << "cp " << v * 100 / PawnValueEg;
  else
      ss << "mate " << (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;

  ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

  return ss.str();
}


/// format_square() converts a Square to a string (g1, a7, etc.)

std::string UCI::format_square(Square s) {

  char ch[] = { 'a' + file_of(s), '1' + rank_of(s), 0 }; // Zero-terminating
  return ch;
}


/// format_move() converts a Move to a string in coordinate notation
/// (g1f3, a7a8q, etc.). The only special case is castling moves, where we print
/// in the e1g1 notation in normal chess mode, and in e1h1 notation in chess960
/// mode. Internally castling moves are always encoded as "king captures rook".

string UCI::format_move(Move m, bool chess960) {

  Square from = from_sq(m);
  Square to = to_sq(m);

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "0000";

  if (type_of(m) == CASTLING && !chess960)
      to = make_square(to > from ? FILE_G : FILE_C, rank_of(from));

  string move = format_square(from) + format_square(to);

  if (type_of(m) == PROMOTION)
      move += " pnbrqk"[promotion_type(m)];

  return move;
}


/// to_move() takes a position and a string representing a move in
/// simple coordinate notation and returns an equivalent legal Move if any.

Move UCI::to_move(const Position& pos, string& str) {

  if (str.length() == 5) // Junior could send promotion piece in uppercase
      str[4] = char(tolower(str[4]));

  for (MoveList<LEGAL> it(pos); *it; ++it)
      if (str == format_move(*it, pos.is_chess960()))
          return *it;

  return MOVE_NONE;
}

///READDED
/// move_to_san() takes a position and a legal Move as input and returns its
/// short algebraic notation representation.

static const char* PieceToChar[COLOR_NB] = { " PNBRQK", " pnbrqk" };

const string UCI::move_to_san(Position& pos, Move m) {

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "(null)";

  assert(MoveList<LEGAL>(pos).contains(m));

  Bitboard others, b;
  string san;
  Color us = pos.side_to_move();
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = pos.piece_on(from);
  PieceType pt = type_of(pc);

  if (type_of(m) == CASTLING)
      san = to > from ? "O-O" : "O-O-O";
  else
  {
      if (pt != PAWN)
      {
          san = PieceToChar[WHITE][pt]; // Upper case

          // A disambiguation occurs if we have more then one piece of type 'pt'
          // that can reach 'to' with a legal move.
          others = b = (pos.attacks_from(pc, to) & pos.pieces(us, pt)) ^ from;

          while (b)
          {
              Square s = pop_lsb(&b);
              if (!pos.legal(make_move(s, to), pos.pinned_pieces(us)))
                  others ^= s;
          }

          if (!others)
          { /* Disambiguation is not needed */ }

          else if (!(others & file_bb(from)))
              san += char(file_of(from) - FILE_A + 'a');

          else if (!(others & rank_bb(from)))
              san += char(rank_of(from) - RANK_1 + '1');

          else
              san += format_square(from);
      }
      else if (pos.capture(m))
          san = char(file_of(from) - FILE_A + 'a');

      if (pos.capture(m))
          san += 'x';

      san += format_square(to);

      if (type_of(m) == PROMOTION)
          san += string("=") + PieceToChar[WHITE][promotion_type(m)];
  }

  if (pos.gives_check(m, CheckInfo(pos)))
  {
      StateInfo st;
      pos.do_move(m, st);
      san += MoveList<LEGAL>(pos).size() ? "+" : "#";
      pos.undo_move(m);
  }

  return san;
}
