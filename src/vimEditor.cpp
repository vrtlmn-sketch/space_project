#include <algorithm>
#include <cctype>
#include "vimEditor.h"

// ─── Buffer helpers (byte offsets; ASCII-oriented) ───────────────────────────

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int LineStart(const std::string& s, int p) {
  p = Clamp(p, 0, (int)s.size());
  while (p > 0 && s[p - 1] != '\n') --p;
  return p;
}
static int LineEnd(const std::string& s, int p) {
  int n = (int)s.size();
  p = Clamp(p, 0, n);
  while (p < n && s[p] != '\n') ++p;
  return p;
}
static int NextLineStart(const std::string& s, int p) {
  int e = LineEnd(s, p);
  return (e < (int)s.size()) ? e + 1 : (int)s.size();
}
static int FirstNonBlank(const std::string& s, int p) {
  int b = LineStart(s, p), e = LineEnd(s, p);
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  return b;
}
static bool IsW(char c)  { return std::isalnum((unsigned char)c) || c == '_'; }
static bool IsSp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static int WordFwd(const std::string& s, int p) {
  int n = (int)s.size();
  if (p >= n) return n;
  char c = s[p];
  if (IsW(c))        while (p < n && IsW(s[p])) ++p;
  else if (!IsSp(c)) while (p < n && !IsW(s[p]) && !IsSp(s[p])) ++p;
  while (p < n && IsSp(s[p])) ++p;
  return p;
}
static int WordBack(const std::string& s, int p) {
  if (p <= 0) return 0;
  --p;
  while (p > 0 && IsSp(s[p])) --p;
  if (IsW(s[p])) { while (p > 0 && IsW(s[p - 1])) --p; }
  else if (!IsSp(s[p])) { while (p > 0 && !IsW(s[p - 1]) && !IsSp(s[p - 1])) --p; }
  return p;
}
static int WordEnd(const std::string& s, int p) {
  int n = (int)s.size();
  if (n == 0) return 0;
  if (p >= n - 1) return n - 1;
  ++p;
  while (p < n && IsSp(s[p])) ++p;
  if (p >= n) return n - 1;
  if (IsW(s[p])) { while (p + 1 < n && IsW(s[p + 1])) ++p; }
  else { while (p + 1 < n && !IsW(s[p + 1]) && !IsSp(s[p + 1])) ++p; }
  return p;
}

// Vertical move keeping a sticky column; returns new position
static int LineMove(const std::string& s, int p, int dir, int wantCol) {
  int start = LineStart(s, p);
  int col   = (wantCol >= 0) ? wantCol : p - start;
  int target = start;
  if (dir > 0) {
    int nls = NextLineStart(s, p);
    if (LineEnd(s, p) >= (int)s.size()) return p;   // already on last line
    target = nls;
  } else {
    if (start == 0) return p;                       // already on first line
    target = LineStart(s, start - 1);
  }
  return std::min(target + col, LineEnd(s, target));
}

// f/F/t/T: nth occurrence of ch on the current line. Returns the resting
// position, or -1 if not found. `inclusive` = the target char is part of the
// motion range (true for f/t forward, handled by the caller for operators).
static int FindOnLine(const std::string& s, int cursor, char cmd, char ch, int n) {
  int ls = LineStart(s, cursor), le = LineEnd(s, cursor);
  int start = cursor;
  if (cmd == 'f' || cmd == 't') {
    for (int k = 0; k < n; ++k) {
      int i = start + 1;
      // 't' repeated must step over the char it is already sitting before
      if (cmd == 't' && k == 0 && i < le && s[i] == ch && n == 1) i = start + 1;
      int found = -1;
      for (; i < le; ++i) if (s[i] == ch) { found = i; break; }
      if (found < 0) return -1;
      start = found;
    }
    return (cmd == 't') ? start - 1 : start;
  } else { // 'F' or 'T' backward
    for (int k = 0; k < n; ++k) {
      int i = start - 1, found = -1;
      for (; i >= ls; --i) if (s[i] == ch) { found = i; break; }
      if (found < 0) return -1;
      start = found;
    }
    return (cmd == 'T') ? start + 1 : start;
  }
}

// ─── Motion resolution ───────────────────────────────────────────────────────

namespace {
struct Motion {
  bool valid{false};
  int  target{0};
  bool linewise{false};
  bool inclusive{false};  // charwise motions that include the target char (e)
};
}

static Motion ResolveMotion(const std::string& s, int cursor, char m, bool gPending,
                            int n, int& desiredCol) {
  Motion r;
  int pos = cursor;
  switch (m) {
    case 'h': for (int i = 0; i < n; ++i) pos = std::max(pos - 1, LineStart(s, pos));
              r = {true, pos, false, false}; break;
    case 'l': for (int i = 0; i < n; ++i) pos = std::min(pos + 1, LineEnd(s, pos));
              r = {true, pos, false, false}; break;
    case 'j': {
      int col = (desiredCol >= 0) ? desiredCol : pos - LineStart(s, pos);
      for (int i = 0; i < n; ++i) pos = LineMove(s, pos, +1, col);
      desiredCol = col;
      r = {true, pos, true, false}; break;
    }
    case 'k': {
      int col = (desiredCol >= 0) ? desiredCol : pos - LineStart(s, pos);
      for (int i = 0; i < n; ++i) pos = LineMove(s, pos, -1, col);
      desiredCol = col;
      r = {true, pos, true, false}; break;
    }
    case 'w': for (int i = 0; i < n; ++i) pos = WordFwd(s, pos);
              r = {true, pos, false, false}; break;
    case 'b': for (int i = 0; i < n; ++i) pos = WordBack(s, pos);
              r = {true, pos, false, false}; break;
    case 'e': for (int i = 0; i < n; ++i) pos = WordEnd(s, pos);
              r = {true, pos, false, true}; break;
    case '0': r = {true, LineStart(s, pos), false, false}; break;
    case '^': r = {true, FirstNonBlank(s, pos), false, false}; break;
    case '$': r = {true, LineEnd(s, pos), false, false}; break;
    case 'G': {
      if (n > 1 || false) { /* count = line number */ }
      int p2 = 0;
      if (n > 1) { // count given: go to line n
        for (int line = 1; line < n && p2 < (int)s.size(); ++line) p2 = NextLineStart(s, p2);
      } else {
        p2 = LineStart(s, (int)s.size());
      }
      r = {true, FirstNonBlank(s, p2), true, false}; break;
    }
    case 'g': if (gPending) { // gg
        int p2 = 0;
        for (int line = 1; line < n && p2 < (int)s.size(); ++line) p2 = NextLineStart(s, p2);
        r = {true, FirstNonBlank(s, p2), true, false};
      }
      break;
    default: break;
  }
  if (m != 'j' && m != 'k') desiredCol = -1;
  return r;
}

// ─── VimEditor ───────────────────────────────────────────────────────────────

void VimEditor::ResetState() {
  mode = Mode::Normal;
  CancelPending();
  desiredCol_ = -1;
}

void VimEditor::CancelPending() {
  count_ = opCount_ = 0;
  op_ = 0;
  gPending_ = rPending_ = false;
  findPending_ = 0;
  if (mode == Mode::Visual || mode == Mode::VisualLine) mode = Mode::Normal;
}

int VimEditor::TakeCount() {
  int n = std::max(opCount_, 1) * std::max(count_, 1);
  count_ = opCount_ = 0;
  return n;
}

void VimEditor::ExitInsert(const std::string& buf, int& cursor) {
  mode = Mode::Normal;
  cursor = Clamp(cursor, 0, (int)buf.size());
  if (cursor > LineStart(buf, cursor)) --cursor;   // vim: cursor steps left
  desiredCol_ = -1;
}

const char* VimEditor::ModeName() const {
  switch (mode) {
    case Mode::Insert:     return "-- INSERT --";
    case Mode::Visual:     return "-- VISUAL --";
    case Mode::VisualLine: return "-- VISUAL LINE --";
    default:               return "NORMAL";
  }
}

std::string VimEditor::PendingText() const {
  std::string s;
  if (opCount_ > 0) s += std::to_string(opCount_);
  if (op_)          s += op_;
  if (count_ > 0)   s += std::to_string(count_);
  if (gPending_)    s += 'g';
  if (rPending_)    s += 'r';
  return s;
}

bool VimEditor::GetSelection(const std::string& buf, int cursor, int& begin, int& end) const {
  if (mode != Mode::Visual && mode != Mode::VisualLine) return false;
  int a = Clamp(anchor_, 0, (int)buf.size());
  int c = Clamp(cursor, 0, (int)buf.size());
  int lo = std::min(a, c), hi = std::max(a, c);
  if (mode == Mode::VisualLine) {
    begin = LineStart(buf, lo);
    end   = NextLineStart(buf, hi);
  } else {
    begin = lo;
    end   = std::min(hi + 1, (int)buf.size());
  }
  return end > begin;
}

void VimEditor::PushUndo(const std::string& buf, int cursor) {
  undo_.push_back({buf, cursor});
  if (undo_.size() > 200) undo_.erase(undo_.begin());
  redo_.clear();
}

bool VimEditor::Undo(std::string& buf, int& cursor) {
  if (undo_.empty()) return false;
  redo_.push_back({buf, cursor});
  buf    = undo_.back().text;
  cursor = Clamp(undo_.back().cur, 0, (int)buf.size());
  undo_.pop_back();
  return true;
}

bool VimEditor::Redo(std::string& buf, int& cursor) {
  if (redo_.empty()) return false;
  undo_.push_back({buf, cursor});
  buf    = redo_.back().text;
  cursor = Clamp(redo_.back().cur, 0, (int)buf.size());
  redo_.pop_back();
  return true;
}

// Delete/change/yank the range; updates the register and cursor
bool VimEditor::ApplyOperator(std::string& buf, char op, int a, int b, bool linewise,
                              int& cursor) {
  a = Clamp(a, 0, (int)buf.size());
  b = Clamp(b, 0, (int)buf.size());
  if (a > b) std::swap(a, b);

  reg_         = buf.substr(a, b - a);  // register holds the content range
  regLinewise_ = linewise;
  clipboardText    = reg_;   // yanks and deletes both feed the system clipboard
  clipboardPending = true;

  if (op == 'y') {
    if (a == b) return false;
    cursor = a;
    return false;  // buffer unchanged
  }

  // Delete range. For a linewise delete that reaches the end of the buffer
  // with no trailing newline (e.g. dd on the last line, even an empty one),
  // also remove the preceding newline so the line is truly gone — not left
  // behind as a blank line.
  int da = a, db = b;
  if (op == 'd' && linewise && db == (int)buf.size() && da > 0 && buf[da - 1] == '\n')
    da -= 1;

  if (da == db && op != 'c') return false;

  PushUndo(buf, cursor);
  buf.erase((size_t)da, (size_t)(db - da));
  cursor = Clamp(da, 0, (int)buf.size());

  if (op == 'c') {
    if (linewise) {
      // cc keeps an empty line to type into
      buf.insert((size_t)cursor, "\n");
    }
    mode = Mode::Insert;
  } else if (linewise) {
    cursor = FirstNonBlank(buf, cursor);
  } else {
    cursor = std::min(cursor, std::max(LineEnd(buf, cursor) - 0, LineStart(buf, cursor)));
  }
  return true;
}

// INSERT-mode Ctrl+W: delete the word (or run of blanks) before the cursor
bool VimEditor::DeleteWordBefore(std::string& buf, int& cursor) {
  cursor = Clamp(cursor, 0, (int)buf.size());
  if (cursor == 0) return false;
  int p = cursor;
  while (p > 0 && IsSp(buf[p - 1]) && buf[p - 1] != '\n') --p;   // trailing blanks
  if (p > 0 && buf[p - 1] != '\n') {
    if (IsW(buf[p - 1]))      while (p > 0 && IsW(buf[p - 1])) --p;
    else                      while (p > 0 && !IsW(buf[p - 1]) && !IsSp(buf[p - 1])) --p;
  }
  if (p == cursor) return false;
  PushUndo(buf, cursor);
  buf.erase((size_t)p, (size_t)(cursor - p));
  cursor = p;
  return true;
}

// INSERT-mode Ctrl+U: delete from the cursor back to the line start
bool VimEditor::DeleteToLineStart(std::string& buf, int& cursor) {
  cursor = Clamp(cursor, 0, (int)buf.size());
  int ls = LineStart(buf, cursor);
  if (ls == cursor) return false;
  PushUndo(buf, cursor);
  buf.erase((size_t)ls, (size_t)(cursor - ls));
  cursor = ls;
  return true;
}

bool VimEditor::HandleNormalKeys(std::string& buf, const std::vector<unsigned int>& chars,
                                 bool ctrlR, int& cursor) {
  bool changed = false;
  cursor = Clamp(cursor, 0, (int)buf.size());
  if (ctrlR && mode == Mode::Normal) changed |= Redo(buf, cursor);
  for (unsigned int uc : chars) {
    if (uc > 127) continue;
    changed |= HandleChar(buf, (char)uc, cursor);
    cursor = Clamp(cursor, 0, (int)buf.size());
  }
  // NORMAL/VISUAL cursor rests ON the last character, never past it (vim rule).
  // INSERT is exempt — appending at end-of-line is legal there.
  if (mode == Mode::Normal || mode == Mode::Visual) {
    int ls = LineStart(buf, cursor), le = LineEnd(buf, cursor);
    if (cursor >= le && le > ls) cursor = le - 1;
  }
  return changed;
}

bool VimEditor::HandleChar(std::string& buf, char c, int& cursor) {
  const bool visual = (mode == Mode::Visual || mode == Mode::VisualLine);

  // r<char> — replace character under cursor
  if (rPending_) {
    rPending_ = false;
    if (c >= 32 && cursor < (int)buf.size() && buf[cursor] != '\n') {
      PushUndo(buf, cursor);
      buf[cursor] = c;
      return true;
    }
    return false;
  }

  // f/F/t/T<char> — the target character just arrived
  if (findPending_) {
    char cmd = findPending_; findPending_ = 0;
    if (c < 32) { op_ = 0; count_ = opCount_ = 0; return false; }
    lastFindCmd_ = cmd; lastFindChar_ = c;
    int n = std::max(count_, 1) * std::max(opCount_, 1);
    count_ = 0;
    int tgt = FindOnLine(buf, cursor, cmd, c, n);
    if (tgt < 0) { op_ = 0; opCount_ = 0; return false; }
    if (op_ && !visual) {
      char op = op_; op_ = 0; opCount_ = 0;
      // forward: include the resting char; backward: [tgt, cursor)
      int a = cursor, b = tgt;
      if (cmd == 'f' || cmd == 't') b = std::min(tgt + 1, (int)buf.size());
      return ApplyOperator(buf, op, a, b, false, cursor);
    }
    opCount_ = 0;
    cursor = tgt;
    return false;
  }

  // Count prefix ('0' is a motion when no count has started)
  if ((c >= '1' && c <= '9') || (c == '0' && count_ > 0)) {
    count_ = count_ * 10 + (c - '0');
    return false;
  }

  // gg handling
  if (gPending_ && c != 'g') gPending_ = false;

  // ── Operator-pending: same-key doubles (dd/cc/yy) and motions ──
  if (op_ && !visual) {
    if (c == op_) {  // linewise on N lines
      int n = TakeCount();
      char op = op_; op_ = 0;
      int a = LineStart(buf, cursor);
      int b = cursor;
      for (int i = 0; i < n; ++i) b = NextLineStart(buf, b);
      return ApplyOperator(buf, op, a, b, true, cursor);
    }
    if (c == 'g' && !gPending_) { gPending_ = true; return false; }
    if (c == 'f' || c == 'F' || c == 't' || c == 'T') { findPending_ = c; return false; }
    int n = 0;
    { // peek count before consuming
      n = std::max(opCount_, 1) * std::max(count_, 1);
    }
    Motion m = ResolveMotion(buf, cursor, c, gPending_, n, desiredCol_);
    gPending_ = false;
    if (!m.valid) { op_ = 0; count_ = opCount_ = 0; return false; }
    count_ = opCount_ = 0;
    char op = op_; op_ = 0;
    int a = cursor, b = m.target;
    if (m.linewise) {
      a = LineStart(buf, std::min(cursor, m.target));
      b = NextLineStart(buf, std::max(cursor, m.target));
      return ApplyOperator(buf, op, a, b, true, cursor);
    }
    if (m.inclusive && b >= a) b = std::min(b + 1, (int)buf.size());
    // cw acts like ce (vim quirk): don't eat trailing whitespace
    if (op == 'c' && c == 'w') {
      Motion e = ResolveMotion(buf, cursor, 'e', false, n, desiredCol_);
      if (e.valid) b = std::min(e.target + 1, (int)buf.size());
    }
    return ApplyOperator(buf, op, a, b, false, cursor);
  }

  // ── VISUAL mode commands ──
  if (visual) {
    switch (c) {
      case 'v': mode = (mode == Mode::Visual) ? Mode::Normal : Mode::Visual; return false;
      case 'V': mode = (mode == Mode::VisualLine) ? Mode::Normal : Mode::VisualLine; return false;
      case 'o': std::swap(anchor_, cursor); return false;
      case 'd': case 'x': case 'y': case 'c': {
        int a, b;
        bool linewise = (mode == Mode::VisualLine);
        GetSelection(buf, cursor, a, b);
        mode = Mode::Normal;
        char op = (c == 'x') ? 'd' : c;
        return ApplyOperator(buf, op, a, b, linewise, cursor);
      }
      case 'f': case 'F': case 't': case 'T': findPending_ = c; return false;
      case ';': case ',': {
        if (!lastFindCmd_) return false;
        char cmd = lastFindCmd_;
        if (c == ',') {  // reverse direction
          cmd = (cmd == 'f') ? 'F' : (cmd == 'F') ? 'f'
              : (cmd == 't') ? 'T' : 'f' /*T->t*/;
          if (lastFindCmd_ == 'T') cmd = 't';
        }
        int n = TakeCount();
        int tgt = FindOnLine(buf, cursor, cmd, lastFindChar_, n);
        if (tgt >= 0) cursor = tgt;
        return false;
      }
      default: {
        int n = TakeCount();
        Motion m = ResolveMotion(buf, cursor, c, gPending_, n, desiredCol_);
        if (c == 'g' && !gPending_ && !m.valid) { gPending_ = true; return false; }
        gPending_ = false;
        if (m.valid) cursor = m.target;
        return false;
      }
    }
  }

  // ── NORMAL mode ──
  switch (c) {
    // insert entries
    case 'i': mode = Mode::Insert; TakeCount(); return false;
    case 'a': cursor = std::min(cursor + 1, LineEnd(buf, cursor));
              mode = Mode::Insert; TakeCount(); return false;
    case 'I': cursor = FirstNonBlank(buf, cursor); mode = Mode::Insert; TakeCount(); return false;
    case 'A': cursor = LineEnd(buf, cursor); mode = Mode::Insert; TakeCount(); return false;
    case 'o': PushUndo(buf, cursor);
              cursor = LineEnd(buf, cursor);
              buf.insert((size_t)cursor, "\n");
              ++cursor;
              mode = Mode::Insert; TakeCount(); return true;
    case 'O': PushUndo(buf, cursor);
              cursor = LineStart(buf, cursor);
              buf.insert((size_t)cursor, "\n");
              mode = Mode::Insert; TakeCount(); return true;

    // operators
    case 'd': case 'c': case 'y':
      op_ = c; opCount_ = count_; count_ = 0; return false;

    // simple edits
    case 'x': {
      int n = TakeCount();
      int end = std::min(cursor + n, LineEnd(buf, cursor));
      if (end <= cursor) return false;
      return ApplyOperator(buf, 'd', cursor, end, false, cursor);
    }
    case 'D': { TakeCount(); return ApplyOperator(buf, 'd', cursor, LineEnd(buf, cursor), false, cursor); }
    case 'C': { TakeCount(); return ApplyOperator(buf, 'c', cursor, LineEnd(buf, cursor), false, cursor); }
    case 'Y': {
      int n = TakeCount();
      int b = cursor;
      for (int i = 0; i < n; ++i) b = NextLineStart(buf, b);
      return ApplyOperator(buf, 'y', LineStart(buf, cursor), b, true, cursor);
    }
    case 'r': rPending_ = true; return false;
    case 'J': {
      int n = std::max(TakeCount(), 2) - 1;
      bool did = false;
      for (int i = 0; i < n; ++i) {
        int e = LineEnd(buf, cursor);
        if (e >= (int)buf.size()) break;
        if (!did) { PushUndo(buf, cursor); did = true; }
        int ws = e + 1;
        while (ws < (int)buf.size() && (buf[ws] == ' ' || buf[ws] == '\t')) ++ws;
        buf.replace((size_t)e, (size_t)(ws - e), " ");
        cursor = e;
      }
      return did;
    }
    case 'p': case 'P': {
      if (reg_.empty()) { TakeCount(); return false; }
      int n = TakeCount();
      PushUndo(buf, cursor);
      std::string ins;
      for (int i = 0; i < n; ++i) ins += reg_;
      if (regLinewise_) {
        int at = (c == 'p') ? NextLineStart(buf, cursor) : LineStart(buf, cursor);
        if (c == 'p' && at == (int)buf.size() && (!buf.empty() && buf.back() != '\n')) {
          // pasting below the LAST line: the separating newline goes in front
          if (!ins.empty() && ins.back() == '\n') ins.pop_back();
          buf.insert((size_t)at, "\n" + ins);
          cursor = FirstNonBlank(buf, std::min(at + 1, (int)buf.size()));
        } else {
          if (ins.empty() || ins.back() != '\n') ins += '\n';
          buf.insert((size_t)at, ins);
          cursor = FirstNonBlank(buf, at);
        }
      } else {
        int at = cursor;
        if (c == 'p') at = std::min(cursor + 1, LineEnd(buf, cursor));
        buf.insert((size_t)at, ins);
        cursor = std::max(at, at + (int)ins.size() - 1);
      }
      return true;
    }

    // undo / redo
    case 'u': TakeCount(); return Undo(buf, cursor);

    // visual modes
    case 'v': anchor_ = cursor; mode = Mode::Visual; TakeCount(); return false;
    case 'V': anchor_ = cursor; mode = Mode::VisualLine; TakeCount(); return false;

    // find character on line (target arrives next char)
    case 'f': case 'F': case 't': case 'T': findPending_ = c; return false;
    case ';': case ',': {
      if (!lastFindCmd_) { TakeCount(); return false; }
      char cmd = lastFindCmd_;
      if (c == ',') {
        cmd = (lastFindCmd_ == 'f') ? 'F' : (lastFindCmd_ == 'F') ? 'f'
            : (lastFindCmd_ == 't') ? 'T' : 't';
      }
      int n = TakeCount();
      int tgt = FindOnLine(buf, cursor, cmd, lastFindChar_, n);
      if (tgt >= 0) cursor = tgt;
      return false;
    }

    // motions (fall through to shared resolver)
    default: {
      if (c == 'g' && !gPending_) { gPending_ = true; return false; }
      int n = TakeCount();
      Motion m = ResolveMotion(buf, cursor, c, gPending_, n, desiredCol_);
      gPending_ = false;
      if (m.valid) cursor = m.target;
      return false;
    }
  }
}
