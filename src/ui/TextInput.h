#pragma once
// Editing model of the search field: text, caret, selection anchor, single-level undo. No rendering, no Win32.
// Positions are UTF-16 code unit offsets; movement never splits a surrogate pair.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cs::ui {

class TextInput {
 public:
  static constexpr size_t kMaxLength = 1000;

  const std::wstring& Text() const { return text_; }
  size_t Caret() const { return caret_; }
  size_t Anchor() const { return anchor_; }
  bool HasSelection() const { return caret_ != anchor_; }
  size_t SelStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
  size_t SelEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
  std::wstring SelectedText() const { return text_.substr(SelStart(), SelEnd() - SelStart()); }

  // Replaces the whole text (undoable). Caret at end, or everything selected.
  void SetText(std::wstring_view t, bool selectAll);
  void SelectAll();

  // Editing; each returns true if the text changed.
  bool Insert(std::wstring_view s);  // replaces the selection; control chars are dropped, newlines -> spaces
  bool Backspace(bool word);
  bool Delete(bool word);
  bool DeleteSelection();
  bool Undo();

  // Caret movement (extend = Shift held).
  void MoveTo(size_t pos, bool extend);
  void Left(bool word, bool extend);
  void Right(bool word, bool extend);
  void Home(bool extend) { MoveTo(0, extend); }
  void End(bool extend) { MoveTo(text_.size(), extend); }
  void SelectWordAt(size_t pos);

  size_t PrevChar(size_t p) const;
  size_t NextChar(size_t p) const;
  size_t PrevWord(size_t p) const;
  size_t NextWord(size_t p) const;
  size_t Snap(size_t p) const;  // clamps and moves off the middle of a surrogate pair

 private:
  enum class Op : uint8_t { None, Typing, Deleting, Other };
  void BeginEdit(Op op);
  void Replace(size_t from, size_t to, std::wstring_view s);

  std::wstring text_;
  size_t caret_ = 0;
  size_t anchor_ = 0;
  Op lastOp_ = Op::None;
  std::wstring undoText_;
  size_t undoCaret_ = 0;
  size_t undoAnchor_ = 0;
  bool hasUndo_ = false;
};

}  // namespace cs::ui
