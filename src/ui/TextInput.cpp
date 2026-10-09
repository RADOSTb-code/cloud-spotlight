#include "ui/TextInput.h"

#include <cwctype>

#include "core/Str.h"

namespace cs::ui {

namespace {

bool IsHigh(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLow(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

bool IsWordChar(wchar_t c) {
  return c == L'_' || str::IsLetter(c) || str::IsDigit(c) || IsHigh(c) || IsLow(c) || std::iswalnum(wint_t(c));
}

}  // namespace

size_t TextInput::Snap(size_t p) const {
  if (p > text_.size()) p = text_.size();
  if (p > 0 && p < text_.size() && IsLow(text_[p]) && IsHigh(text_[p - 1])) --p;
  return p;
}

size_t TextInput::PrevChar(size_t p) const {
  if (p == 0) return 0;
  --p;
  if (p > 0 && IsLow(text_[p]) && IsHigh(text_[p - 1])) --p;
  return p;
}

size_t TextInput::NextChar(size_t p) const {
  if (p >= text_.size()) return text_.size();
  ++p;
  if (p < text_.size() && IsLow(text_[p]) && IsHigh(text_[p - 1])) ++p;
  return p;
}

// Windows-style Ctrl+Left: skip spaces, then a run of word chars (or a run of punctuation).
size_t TextInput::PrevWord(size_t p) const {
  while (p > 0 && str::IsSpace(text_[p - 1])) --p;
  if (p == 0) return 0;
  const bool word = IsWordChar(text_[p - 1]);
  while (p > 0 && !str::IsSpace(text_[p - 1]) && IsWordChar(text_[p - 1]) == word) --p;
  return p;
}

// Windows-style Ctrl+Right: skip the current run, then the spaces after it (lands on the next word start).
size_t TextInput::NextWord(size_t p) const {
  const size_t n = text_.size();
  if (p < n && !str::IsSpace(text_[p])) {
    const bool word = IsWordChar(text_[p]);
    while (p < n && !str::IsSpace(text_[p]) && IsWordChar(text_[p]) == word) ++p;
  }
  while (p < n && str::IsSpace(text_[p])) ++p;
  return p;
}

void TextInput::BeginEdit(Op op) {
  // Consecutive typing (or deleting) coalesces into one undo step.
  if (op == lastOp_ && op != Op::Other) return;
  undoText_ = text_;
  undoCaret_ = caret_;
  undoAnchor_ = anchor_;
  hasUndo_ = true;
  lastOp_ = op;
}

void TextInput::Replace(size_t from, size_t to, std::wstring_view s) {
  text_.replace(from, to - from, s);
  caret_ = anchor_ = from + s.size();
}

void TextInput::SetText(std::wstring_view t, bool selectAll) {
  if (t.size() > kMaxLength) t = t.substr(0, kMaxLength);
  if (t != text_) BeginEdit(Op::Other);
  text_.assign(t);
  caret_ = text_.size();
  anchor_ = selectAll ? 0 : caret_;
  lastOp_ = Op::None;
}

void TextInput::SelectAll() {
  anchor_ = 0;
  caret_ = text_.size();
  lastOp_ = Op::None;
}

bool TextInput::Insert(std::wstring_view s) {
  std::wstring clean;
  clean.reserve(s.size());
  for (wchar_t c : s) {
    if (c == L'\r' || c == L'\n' || c == L'\t') {
      if (clean.empty() || clean.back() != L' ') clean.push_back(L' ');
    } else if (c >= 0x20 && c != 0x7F) {
      clean.push_back(c);
    }
  }
  const size_t room = kMaxLength - (text_.size() - (SelEnd() - SelStart()));
  if (clean.size() > room) {
    clean.resize(room);
    if (!clean.empty() && IsHigh(clean.back())) clean.pop_back();
  }
  if (clean.empty() && !HasSelection()) return false;
  BeginEdit(s.size() == 1 && !HasSelection() ? Op::Typing : Op::Other);
  Replace(SelStart(), SelEnd(), clean);
  return true;
}

bool TextInput::DeleteSelection() {
  if (!HasSelection()) return false;
  BeginEdit(Op::Other);
  Replace(SelStart(), SelEnd(), {});
  return true;
}

bool TextInput::Backspace(bool word) {
  if (HasSelection()) return DeleteSelection();
  if (caret_ == 0) return false;
  const size_t from = word ? PrevWord(caret_) : PrevChar(caret_);
  BeginEdit(word ? Op::Other : Op::Deleting);
  Replace(from, caret_, {});
  return true;
}

bool TextInput::Delete(bool word) {
  if (HasSelection()) return DeleteSelection();
  if (caret_ >= text_.size()) return false;
  const size_t to = word ? NextWord(caret_) : NextChar(caret_);
  BeginEdit(word ? Op::Other : Op::Deleting);
  Replace(caret_, to, {});
  return true;
}

bool TextInput::Undo() {
  if (!hasUndo_) return false;
  // Swap, so a second Ctrl+Z redoes.
  std::swap(text_, undoText_);
  std::swap(caret_, undoCaret_);
  std::swap(anchor_, undoAnchor_);
  caret_ = Snap(caret_);
  anchor_ = Snap(anchor_);
  lastOp_ = Op::None;
  return text_ != undoText_;
}

void TextInput::MoveTo(size_t pos, bool extend) {
  caret_ = Snap(pos);
  if (!extend) anchor_ = caret_;
  lastOp_ = Op::None;
}

void TextInput::Left(bool word, bool extend) {
  if (!extend && !word && HasSelection()) return MoveTo(SelStart(), false);
  MoveTo(word ? PrevWord(caret_) : PrevChar(caret_), extend);
}

void TextInput::Right(bool word, bool extend) {
  if (!extend && !word && HasSelection()) return MoveTo(SelEnd(), false);
  MoveTo(word ? NextWord(caret_) : NextChar(caret_), extend);
}

void TextInput::SelectWordAt(size_t pos) {
  pos = Snap(pos);
  const size_t n = text_.size();
  if (n == 0) return;
  if (pos == n) pos = PrevChar(pos);
  size_t a = pos, b = pos;
  const wchar_t c = text_[pos];
  if (str::IsSpace(c)) {
    while (a > 0 && str::IsSpace(text_[a - 1])) --a;
    while (b < n && str::IsSpace(text_[b])) ++b;
  } else {
    const bool word = IsWordChar(c);
    while (a > 0 && !str::IsSpace(text_[a - 1]) && IsWordChar(text_[a - 1]) == word) --a;
    while (b < n && !str::IsSpace(text_[b]) && IsWordChar(text_[b]) == word) ++b;
  }
  anchor_ = a;
  caret_ = b;
  lastOp_ = Op::None;
}

}  // namespace cs::ui
