# winter manual test file

Open with `winter tests/utf8_manual.md`. Each section says what to try and what
you should see. Cursor keys: h j k l, arrows, Home/End, PageUp/PageDown.
adllwabd
## 1. Two-byte, three-byte, four-byte
adwkjna
aé€😀z
Walk this line with `l` then `h`. The cursor should step one character at a
time (5 stops), never land inside a character. Backspace after the emoji should
delete the whole emoji in one press, and `u` should bring it back.

## 2. Wide characters (2 cells each)

中文字符 日本語 한국어
ＡＢＣ fullwidth
Cursor should sit at the start of a wide character and the status/cursor column
should jump by 2 cells. Text after it must line up (no drift to the right).

## 3. Emoji

😀😃😄 party 🎉 rocket 🚀 end
Backspace through the whole line. Each emoji removes in one press.

## 4. Combining marks

é (e + combining acute) ñ (n + combining tilde) ä
Known limit: `h`/`l` step over the mark as its own position. The mark must not
take a cell of its own on screen.

## 5. Tabs

    one tab at line start

a b c d
x 中 😀 end
Tab stops are every 8 cells. Wide characters before a tab must not break the
alignment of the next tab stop.

## 6. Horizontal scroll with wide characters

中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中
abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀abc😀
Move to the end of these lines with `End`. A wide character that is cut by the
left or right edge of the window should show as blank cells, not half a glyph.

## 7. Vertical movement

short
aé€😀z longer line with 中文
x
Go down from the middle of line 2 onto line 1 and 3. The cursor must always sit
on a character boundary. (Known limit: it keeps the byte column, not the
visual column.)

## 8. Search

Search for `€😀` with `/`, and for `中文`. The cursor should land on the first
byte of the match.

## 9. Line joins

join
this
Put the cursor at the start of the second line and press backspace in insert
mode. The lines should merge. `u` should split them again.

## 10. Typing

In insert mode type: é ñ ü € 日本 😀
Then press Esc and `u`. The whole typing session should disappear in one step.

## 11. Last line without trailing newline

the file ends here, no newline after this line
