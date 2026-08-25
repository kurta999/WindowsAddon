#pragma once

#include <ostream>
#include <string_view>

// !\brief One block of settings.ini, written a key at a time.
//
// Sixteen SaveSettings implementations spelled the same four lines by hand:
// `out << "[Section]\n"` for the header, `out << "Key = " << value << "\n"` for
// a setting, the same with ` # note` glued on for a commented one, and
// `out << "\n"` between blocks. Nineteen section headers between them, and the
// spacing, the comment marker and the newline were re-decided at every line -
// which is how one section came to be written with '\n' and its neighbour with
// "\n", and how two of the nineteen ended without the blank line the rest have.
//
// The counterpart of SettingsReader, and header-only for the same reason: the
// subsystems that own their settings are spread across every headless target.
class SettingsWriter
{
public:
    SettingsWriter(std::ostream& out, std::string_view section) : m_out(out)
    {
        Section(section);
    }

    // !\brief For the one block that writes lines above its first section -
    // the macro keyword help, which stands at the top of the whole file.
    explicit SettingsWriter(std::ostream& out) : m_out(out) {}

    // !\brief Start another section on the same stream.
    //
    // Three blocks write more than one: the macro keys, the numbered backups,
    // and the App block that opens the file.
    SettingsWriter& Section(std::string_view section)
    {
        m_out << "[" << section << "]\n";
        return *this;
    }

    // !\brief `Name = value`, with an optional trailing `# note`.
    template <typename T>
    SettingsWriter& Key(std::string_view name, const T& value, std::string_view note = {})
    {
        m_out << name << " = " << value;
        if(!note.empty())
            m_out << " # " << note;
        m_out << "\n";
        return *this;
    }

    // !\brief A `# ...` line of its own, above the key it explains.
    SettingsWriter& Comment(std::string_view text)
    {
        m_out << "# " << text << "\n";
        return *this;
    }

    // !\brief A blank line, which is what separates one block from the next.
    SettingsWriter& Blank()
    {
        m_out << "\n";
        return *this;
    }

    // !\brief The stream underneath, for the two blocks that write a variable
    // number of sections and format the lines themselves.
    [[nodiscard]] std::ostream& Out() { return m_out; }

private:
    std::ostream& m_out;
};
