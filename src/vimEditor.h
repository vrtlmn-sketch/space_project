#pragma once
#include <string>
#include <vector>

// Modal (vim-style) editing engine for a plain text buffer. Pure buffer/
// cursor logic — no ImGui here; the renderer feeds it typed characters and
// applies the resulting cursor/selection to the InputText widget state.
// Supported: NORMAL/INSERT/VISUAL/VISUAL-LINE, counts, h j k l w b e 0 ^ $
// gg G, f F t T ; , i a I A o O, d c y operators (+ dd cc yy, D C Y),
// x r p P J, u / Ctrl+R undo-redo. Yanks/deletes also copy to the system
// clipboard (see clipboardPending). No ":" commands yet.
class VimEditor {
public:
  enum class Mode { Normal, Insert, Visual, VisualLine };
  Mode mode{Mode::Normal};

  // Set when a yank/delete filled the register — the renderer copies
  // clipboardText to the system clipboard and clears the flag.
  bool        clipboardPending{false};
  std::string clipboardText;

  // Process printable characters typed this frame (NORMAL/VISUAL modes).
  // cursor is a byte offset into buf (in/out). Returns true if buf changed.
  bool HandleNormalKeys(std::string& buf, const std::vector<unsigned int>& chars,
                        bool ctrlR, int& cursor);

  // INSERT-mode line edits (Ctrl+W / Ctrl+U); return true if buf changed
  bool DeleteWordBefore(std::string& buf, int& cursor);
  bool DeleteToLineStart(std::string& buf, int& cursor);

  // ESC pressed in INSERT mode: back to NORMAL, cursor one left (vim rule)
  void ExitInsert(const std::string& buf, int& cursor);

  // ESC pressed in NORMAL/VISUAL: cancel pending count/operator/visual
  void CancelPending();

  // Full reset (used when vim mode is toggled on)
  void ResetState();

  // Current VISUAL selection as a [begin, end) byte range; false if none
  bool GetSelection(const std::string& buf, int cursor, int& begin, int& end) const;

  // Status line text, e.g. "NORMAL", "-- INSERT --", "VISUAL LINE"
  const char* ModeName() const;
  // Pending prefix for display, e.g. "12d"
  std::string PendingText() const;

private:
  int  count_{0};        // digits typed after the operator (or before it)
  int  opCount_{0};      // digits typed before the operator
  char op_{0};           // pending operator: 'd', 'c' or 'y'
  bool gPending_{false}; // first 'g' of "gg"
  bool rPending_{false}; // 'r' waiting for its replacement character
  bool ctrlRedo_{false}; // reserved
  char findPending_{0};  // f/F/t/T waiting for its target character
  char lastFindCmd_{0};  // last f/F/t/T for ; and ,
  char lastFindChar_{0};
  int  anchor_{0};       // visual mode anchor
  int  desiredCol_{-1};  // sticky column for j/k
  std::string reg_;      // yank register
  bool regLinewise_{false};

  struct Snap { std::string text; int cur; };
  std::vector<Snap> undo_, redo_;

  bool HandleChar(std::string& buf, char c, int& cursor);
  bool ApplyOperator(std::string& buf, char op, int a, int b, bool linewise,
                     int& cursor);
  void PushUndo(const std::string& buf, int cursor);
  bool Undo(std::string& buf, int& cursor);
  bool Redo(std::string& buf, int& cursor);
  int  TakeCount();      // combined count (opCount * count), min 1
};
