#include "games.h"

namespace fai {

Snake::Snake(int w, int h, uint32_t seed) : w_(w), h_(h), rng_(seed) {
  int y = h / 2, x = w / 2;
  for (int i = 0; i < 3; i++) body_.push_back({x - i, y});  // head first, heading right
  placeFood();
}

void Snake::turn(Dir d) {
  bool reverse = (d == Dir::Up && dir_ == Dir::Down) || (d == Dir::Down && dir_ == Dir::Up) ||
                 (d == Dir::Left && dir_ == Dir::Right) || (d == Dir::Right && dir_ == Dir::Left);
  if (!reverse) next_ = d;
}

void Snake::placeFood() {
  std::vector<std::pair<int, int>> free;
  for (int y = 0; y < h_; y++)
    for (int x = 0; x < w_; x++) {
      bool used = false;
      for (auto& c : body_) used |= c.first == x && c.second == y;
      if (!used) free.push_back({x, y});
    }
  if (free.empty()) { alive_ = false; return; }  // board full: nothing left to eat (a win)
  food_ = free[rng_.below(int(free.size()))];
}

bool Snake::step() {
  if (!alive_) return false;
  dir_ = next_;
  auto h = body_.front();
  if (dir_ == Dir::Up) h.second--;
  else if (dir_ == Dir::Down) h.second++;
  else if (dir_ == Dir::Left) h.first--;
  else h.first++;
  if (h.first < 0 || h.second < 0 || h.first >= w_ || h.second >= h_) return alive_ = false;
  bool eat = h == food_;
  if (!eat) body_.pop_back();  // the tail moves away first, so chasing it is legal
  for (auto& c : body_)
    if (c == h) return alive_ = false;
  body_.push_front(h);
  if (eat) { score_++; placeFood(); }
  return alive_;
}

std::vector<std::string> Snake::board() const {
  std::vector<std::string> rows(h_, std::string(w_, '.'));
  if (alive_ || score_ >= 0) rows[food_.second][food_.first] = '*';
  for (size_t i = 0; i < body_.size(); i++) rows[body_[i].second][body_[i].first] = i == 0 ? '@' : 'o';
  return rows;
}

static const int kLines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

char TicTacToe::winner() const {
  for (auto& l : kLines)
    if (b_[l[0]] != '.' && b_[l[0]] == b_[l[1]] && b_[l[1]] == b_[l[2]]) return b_[l[0]];
  return b_.find('.') == std::string::npos ? 'D' : 0;
}

int TicTacToe::aiMove() {
  for (char who : {'O', 'X'})  // first try to win, then to block
    for (auto& l : kLines) {
      int mine = 0, empty = -1;
      for (int i : l) { if (b_[i] == who) mine++; else if (b_[i] == '.') empty = i; }
      if (mine == 2 && empty >= 0) return empty;
    }
  if (b_[4] == '.') return 4;
  std::vector<int> free;
  for (int i = 0; i < 9; i++) if (b_[i] == '.') free.push_back(i);
  return free[rng_.below(int(free.size()))];
}

bool TicTacToe::play(int cell) {
  if (cell < 0 || cell > 8 || b_[cell] != '.' || winner()) return false;
  b_[cell] = 'X';
  if (!winner()) b_[aiMove()] = 'O';
  return true;
}

Guess::Result Guess::guess(int n) {
  if (n < 1 || n > 100) return Invalid;
  tries_++;
  return n < secret_ ? Low : n > secret_ ? High : Right;
}

}  // namespace fai
