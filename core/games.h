// Game logic (no UI). Deterministic given a seed, so it is unit-testable.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace fai {

class Rng {  // xorshift32
 public:
  explicit Rng(uint32_t seed) : s_(seed ? seed : 1) {}
  uint32_t next() { s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5; return s_; }
  int below(int n) { return int(next() % uint32_t(n)); }
 private:
  uint32_t s_;
};

enum class Dir { Up, Down, Left, Right };

class Snake {
 public:
  Snake(int w, int h, uint32_t seed);
  void turn(Dir d);  // takes effect on the next step; a 180-degree reversal is ignored
  bool step();       // advance one cell; false once the game is over
  bool alive() const { return alive_; }
  int score() const { return score_; }
  std::vector<std::string> board() const;  // rows of '.', 'o' body, '@' head, '*' food
 private:
  void placeFood();
  int w_, h_;
  std::deque<std::pair<int, int>> body_;  // front = head, (x, y)
  Dir dir_ = Dir::Right, next_ = Dir::Right;
  std::pair<int, int> food_{0, 0};
  Rng rng_;
  bool alive_ = true;
  int score_ = 0;
};

class TicTacToe {  // human = X moves first, computer = O (wins, blocks, centre, else random)
 public:
  explicit TicTacToe(uint32_t seed) : rng_(seed) {}
  bool play(int cell);        // human move, then the computer replies; false if illegal
  char at(int i) const { return b_[i]; }  // 'X', 'O' or '.'
  char winner() const;        // 'X', 'O', 'D' (draw) or 0 while playing
  void reset() { b_ = "........."; }
 private:
  int aiMove();
  std::string b_ = ".........";
  Rng rng_;
};

class Guess {  // secret number in 1..100
 public:
  enum Result { Low, High, Right, Invalid };
  explicit Guess(uint32_t seed) : secret_(1 + Rng(seed).below(100)) {}
  Result guess(int n);
  int tries() const { return tries_; }
  int secret() const { return secret_; }
 private:
  int secret_, tries_ = 0;
};

}  // namespace fai
