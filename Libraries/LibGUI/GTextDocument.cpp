#include <AK/StringBuilder.h>
#include <LibCore/CTimer.h>
#include <LibGUI/GTextDocument.h>
#include <ctype.h>

GTextDocument::GTextDocument(Client* client)
{
    if (client)
        m_clients.set(client);
    append_line(make<GTextDocumentLine>(*this));

    // TODO: Instead of a repating timer, this we should call a delayed 2 sec timer when the user types.
    m_undo_timer = CTimer::construct(
        2000, [this] {
            update_undo_timer();
        });
}

void GTextDocument::set_text(const StringView& text)
{
    m_client_notifications_enabled = false;
    m_spans.clear();
    remove_all_lines();

    int start_of_current_line = 0;

    auto add_line = [&](int current_position) {
        int line_length = current_position - start_of_current_line;
        auto line = make<GTextDocumentLine>(*this);
        if (line_length)
            line->set_text(*this, text.substring_view(start_of_current_line, current_position - start_of_current_line));
        append_line(move(line));
        start_of_current_line = current_position + 1;
    };
    int i = 0;
    for (i = 0; i < text.length(); ++i) {
        if (text[i] == '\n')
            add_line(i);
    }
    add_line(i);
    m_client_notifications_enabled = true;

    for (auto* client : m_clients)
        client->document_did_set_text();
}

int GTextDocumentLine::first_non_whitespace_column() const
{
    for (int i = 0; i < length(); ++i) {
        if (!isspace(m_text[i]))
            return i;
    }
    return length();
}

GTextDocumentLine::GTextDocumentLine(GTextDocument& document)
{
    clear(document);
}

GTextDocumentLine::GTextDocumentLine(GTextDocument& document, const StringView& text)
{
    set_text(document, text);
}

void GTextDocumentLine::clear(GTextDocument& document)
{
    m_text.clear();
    m_text.append(0);
    document.update_views({});
}

void GTextDocumentLine::set_text(GTextDocument& document, const StringView& text)
{
    if (text.length() == length() && !memcmp(text.characters_without_null_termination(), characters(), length()))
        return;
    if (text.is_empty()) {
        clear(document);
        return;
    }
    m_text.resize(text.length() + 1);
    memcpy(m_text.data(), text.characters_without_null_termination(), text.length() + 1);
    document.update_views({});
}

void GTextDocumentLine::append(GTextDocument& document, const char* characters, int length)
{
    int old_length = m_text.size() - 1;
    m_text.resize(m_text.size() + length);
    memcpy(m_text.data() + old_length, characters, length);
    m_text.last() = 0;
    document.update_views({});
}

void GTextDocumentLine::append(GTextDocument& document, char ch)
{
    insert(document, length(), ch);
}

void GTextDocumentLine::prepend(GTextDocument& document, char ch)
{
    insert(document, 0, ch);
}

void GTextDocumentLine::insert(GTextDocument& document, int index, char ch)
{
    if (index == length()) {
        m_text.last() = ch;
        m_text.append(0);
    } else {
        m_text.insert(index, move(ch));
    }
    document.update_views({});
}

void GTextDocumentLine::remove(GTextDocument& document, int index)
{
    if (index == length()) {
        m_text.take_last();
        m_text.last() = 0;
    } else {
        m_text.remove(index);
    }
    document.update_views({});
}

void GTextDocumentLine::truncate(GTextDocument& document, int length)
{
    m_text.resize(length + 1);
    m_text.last() = 0;
    document.update_views({});
}

void GTextDocument::append_line(NonnullOwnPtr<GTextDocumentLine> line)
{
    lines().append(move(line));
    if (m_client_notifications_enabled) {
        for (auto* client : m_clients)
            client->document_did_append_line();
    }
}

void GTextDocument::insert_line(int line_index, NonnullOwnPtr<GTextDocumentLine> line)
{
    lines().insert(line_index, move(line));
    if (m_client_notifications_enabled) {
        for (auto* client : m_clients)
            client->document_did_insert_line(line_index);
    }
}

void GTextDocument::remove_line(int line_index)
{
    lines().remove(line_index);
    if (m_client_notifications_enabled) {
        for (auto* client : m_clients)
            client->document_did_remove_line(line_index);
    }
}

void GTextDocument::remove_all_lines()
{
    lines().clear();
    if (m_client_notifications_enabled) {
        for (auto* client : m_clients)
            client->document_did_remove_all_lines();
    }
}

GTextDocument::Client::~Client()
{
}

void GTextDocument::register_client(Client& client)
{
    m_clients.set(&client);
}

void GTextDocument::unregister_client(Client& client)
{
    m_clients.remove(&client);
}

void GTextDocument::update_views(Badge<GTextDocumentLine>)
{
    notify_did_change();
}

void GTextDocument::notify_did_change()
{
    if (m_client_notifications_enabled) {
        for (auto* client : m_clients)
            client->document_did_change();
    }
}

void GTextDocument::set_all_cursors(const GTextPosition& position)
{
    if (m_client_notifications_enabled) {
        for (auto* client : m_clients)
            client->document_did_set_cursor(position);
    }
}

String GTextDocument::text_in_range(const GTextRange& a_range) const
{
    auto range = a_range.normalized();

    StringBuilder builder;
    for (int i = range.start().line(); i <= range.end().line(); ++i) {
        auto& line = lines()[i];
        int selection_start_column_on_line = range.start().line() == i ? range.start().column() : 0;
        int selection_end_column_on_line = range.end().line() == i ? range.end().column() : line.length();
        builder.append(line.characters() + selection_start_column_on_line, selection_end_column_on_line - selection_start_column_on_line);
        if (i != range.end().line())
            builder.append('\n');
    }

    return builder.to_string();
}

char GTextDocument::character_at(const GTextPosition& position) const
{
    ASSERT(position.line() < line_count());
    auto& line = lines()[position.line()];
    if (position.column() == line.length())
        return '\n';
    return line.characters()[position.column()];
}

GTextPosition GTextDocument::next_position_after(const GTextPosition& position, SearchShouldWrap should_wrap) const
{
    auto& line = lines()[position.line()];
    if (position.column() == line.length()) {
        if (position.line() == line_count() - 1) {
            if (should_wrap == SearchShouldWrap::Yes)
                return { 0, 0 };
            return {};
        }
        return { position.line() + 1, 0 };
    }
    return { position.line(), position.column() + 1 };
}

GTextPosition GTextDocument::previous_position_before(const GTextPosition& position, SearchShouldWrap should_wrap) const
{
    if (position.column() == 0) {
        if (position.line() == 0) {
            if (should_wrap == SearchShouldWrap::Yes) {
                auto& last_line = lines()[line_count() - 1];
                return { line_count() - 1, last_line.length() };
            }
            return {};
        }
        auto& prev_line = lines()[position.line() - 1];
        return { position.line() - 1, prev_line.length() };
    }
    return { position.line(), position.column() - 1 };
}

GTextRange GTextDocument::find_next(const StringView& needle, const GTextPosition& start, SearchShouldWrap should_wrap) const
{
    if (needle.is_empty())
        return {};

    GTextPosition position = start.is_valid() ? start : GTextPosition(0, 0);
    GTextPosition original_position = position;

    GTextPosition start_of_potential_match;
    int needle_index = 0;

    do {
        auto ch = character_at(position);
        if (ch == needle[needle_index]) {
            if (needle_index == 0)
                start_of_potential_match = position;
            ++needle_index;
            if (needle_index >= needle.length())
                return { start_of_potential_match, next_position_after(position, should_wrap) };
        } else {
            if (needle_index > 0)
                position = start_of_potential_match;
            needle_index = 0;
        }
        position = next_position_after(position, should_wrap);
    } while (position.is_valid() && position != original_position);

    return {};
}

GTextRange GTextDocument::find_previous(const StringView& needle, const GTextPosition& start, SearchShouldWrap should_wrap) const
{
    if (needle.is_empty())
        return {};

    GTextPosition position = start.is_valid() ? start : GTextPosition(0, 0);
    position = previous_position_before(position, should_wrap);
    GTextPosition original_position = position;

    GTextPosition end_of_potential_match;
    int needle_index = needle.length() - 1;

    do {
        auto ch = character_at(position);
        if (ch == needle[needle_index]) {
            if (needle_index == needle.length() - 1)
                end_of_potential_match = position;
            --needle_index;
            if (needle_index < 0)
                return { position, next_position_after(end_of_potential_match, should_wrap) };
        } else {
            if (needle_index < needle.length() - 1)
                position = end_of_potential_match;
            needle_index = needle.length() - 1;
        }
        position = previous_position_before(position, should_wrap);
    } while (position.is_valid() && position != original_position);

    return {};
}

Vector<GTextRange> GTextDocument::find_all(const StringView& needle) const
{
    Vector<GTextRange> ranges;

    GTextPosition position;
    for (;;) {
        auto range = find_next(needle, position, SearchShouldWrap::No);
        if (!range.is_valid())
            break;
        ranges.append(range);
        position = range.end();
    }
    return ranges;
}

Optional<GTextDocumentSpan> GTextDocument::first_non_skippable_span_before(const GTextPosition& position) const
{
    for (int i = m_spans.size() - 1; i >= 0; --i) {
        if (!m_spans[i].range.contains(position))
            continue;
        while ((i - 1) >= 0 && m_spans[i - 1].is_skippable)
            --i;
        if (i <= 0)
            return {};
        return m_spans[i - 1];
    }
    return {};
}

Optional<GTextDocumentSpan> GTextDocument::first_non_skippable_span_after(const GTextPosition& position) const
{
    for (int i = 0; i < m_spans.size(); ++i) {
        if (!m_spans[i].range.contains(position))
            continue;
        while ((i + 1) < m_spans.size() && m_spans[i + 1].is_skippable)
            ++i;
        if (i >= (m_spans.size() - 1))
            return {};
        return m_spans[i + 1];
    }
    return {};
}

void GTextDocument::undo()
{
    if (!can_undo())
        return;
    m_undo_stack.undo();
    notify_did_change();
}

void GTextDocument::redo()
{
    if (!can_redo())
        return;
    m_undo_stack.redo();
    notify_did_change();
}

void GTextDocument::add_to_undo_stack(NonnullOwnPtr<GTextDocumentUndoCommand> undo_command)
{
    m_undo_stack.push(move(undo_command));
}

GTextDocumentUndoCommand::GTextDocumentUndoCommand(GTextDocument& document)
    : m_document(document)
{
}

GTextDocumentUndoCommand::~GTextDocumentUndoCommand()
{
}

InsertTextCommand::InsertTextCommand(GTextDocument& document, const String& text, const GTextPosition& position)
    : GTextDocumentUndoCommand(document)
    , m_text(text)
    , m_range({ position, position })
{
}

void InsertTextCommand::redo()
{
    auto new_cursor = m_document.insert_at(m_range.start(), m_text);
    // NOTE: We don't know where the range ends until after doing redo().
    //       This is okay since we always do redo() after adding this to the undo stack.
    m_range.set_end(new_cursor);
    m_document.set_all_cursors(new_cursor);
}

void InsertTextCommand::undo()
{
    m_document.remove(m_range);
    m_document.set_all_cursors(m_range.start());
}

RemoveTextCommand::RemoveTextCommand(GTextDocument& document, const String& text, const GTextRange& range)
    : GTextDocumentUndoCommand(document)
    , m_text(text)
    , m_range(range)
{
}

void RemoveTextCommand::redo()
{
    m_document.remove(m_range);
    m_document.set_all_cursors(m_range.start());
}

void RemoveTextCommand::undo()
{
    auto new_cursor = m_document.insert_at(m_range.start(), m_text);
    m_document.set_all_cursors(new_cursor);
}

void GTextDocument::update_undo_timer()
{
    m_undo_stack.finalize_current_combo();
}

GTextPosition GTextDocument::insert_at(const GTextPosition& position, const StringView& text)
{
    GTextPosition cursor = position;
    for (int i = 0; i < text.length(); ++i) {
        cursor = insert_at(cursor, text[i]);
    }
    return cursor;
}

GTextPosition GTextDocument::insert_at(const GTextPosition& position, char ch)
{
    // FIXME: We need these from GTextEditor!
    bool m_automatic_indentation_enabled = true;
    int m_soft_tab_width = 4;

    bool at_head = position.column() == 0;
    bool at_tail = position.column() == line(position.line()).length();
    if (ch == '\n') {
        if (at_tail || at_head) {
            String new_line_contents;
            if (m_automatic_indentation_enabled && at_tail) {
                int leading_spaces = 0;
                auto& old_line = lines()[position.line()];
                for (int i = 0; i < old_line.length(); ++i) {
                    if (old_line.characters()[i] == ' ')
                        ++leading_spaces;
                    else
                        break;
                }
                if (leading_spaces)
                    new_line_contents = String::repeated(' ', leading_spaces);
            }

            int row = position.line();
            Vector<char> line_content;
            for (int i = position.column(); i < line(row).length(); i++)
                line_content.append(line(row).characters()[i]);
            insert_line(position.line() + (at_tail ? 1 : 0), make<GTextDocumentLine>(*this, new_line_contents));
            notify_did_change();
            return { position.line() + 1, line(position.line() + 1).length() };
        }
        auto new_line = make<GTextDocumentLine>(*this);
        new_line->append(*this, line(position.line()).characters() + position.column(), line(position.line()).length() - position.column());

        Vector<char> line_content;
        for (int i = 0; i < new_line->length(); i++)
            line_content.append(new_line->characters()[i]);
        line(position.line()).truncate(*this, position.column());
        insert_line(position.line() + 1, move(new_line));
        notify_did_change();
        return { position.line() + 1, 0 };
    }
    if (ch == '\t') {
        int next_soft_tab_stop = ((position.column() + m_soft_tab_width) / m_soft_tab_width) * m_soft_tab_width;
        int spaces_to_insert = next_soft_tab_stop - position.column();
        for (int i = 0; i < spaces_to_insert; ++i) {
            line(position.line()).insert(*this, position.column(), ' ');
        }
        notify_did_change();
        return { position.line(), next_soft_tab_stop };
    }
    line(position.line()).insert(*this, position.column(), ch);
    notify_did_change();
    return { position.line(), position.column() + 1 };
}

void GTextDocument::remove(const GTextRange& unnormalized_range)
{
    if (!unnormalized_range.is_valid())
        return;

    auto range = unnormalized_range.normalized();

    // First delete all the lines in between the first and last one.
    for (int i = range.start().line() + 1; i < range.end().line();) {
        remove_line(i);
        range.end().set_line(range.end().line() - 1);
    }

    if (range.start().line() == range.end().line()) {
        // Delete within same line.
        auto& line = lines()[range.start().line()];
        bool whole_line_is_selected = range.start().column() == 0 && range.end().column() == line.length();

        if (whole_line_is_selected) {
            line.clear(*this);
        } else {
            auto before_selection = String(line.characters(), line.length()).substring(0, range.start().column());
            auto after_selection = String(line.characters(), line.length()).substring(range.end().column(), line.length() - range.end().column());
            StringBuilder builder(before_selection.length() + after_selection.length());
            builder.append(before_selection);
            builder.append(after_selection);
            line.set_text(*this, builder.to_string());
        }
    } else {
        // Delete across a newline, merging lines.
        ASSERT(range.start().line() == range.end().line() - 1);
        auto& first_line = lines()[range.start().line()];
        auto& second_line = lines()[range.end().line()];
        auto before_selection = String(first_line.characters(), first_line.length()).substring(0, range.start().column());
        auto after_selection = String(second_line.characters(), second_line.length()).substring(range.end().column(), second_line.length() - range.end().column());
        StringBuilder builder(before_selection.length() + after_selection.length());
        builder.append(before_selection);
        builder.append(after_selection);

        first_line.set_text(*this, builder.to_string());
        remove_line(range.end().line());
    }

    if (lines().is_empty()) {
        append_line(make<GTextDocumentLine>(*this));
    }

    notify_did_change();
}
