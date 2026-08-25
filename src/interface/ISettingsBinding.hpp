#pragma once

#include <iosfwd>
#include <string_view>

class SettingsReader;

// !\brief One block of settings.ini, owned by the subsystem it configures.
//
// Loading and saving used to live in two long procedures inside Settings, which
// meant Settings knew the field names of every subsystem and a new key needed
// three separate edits that nothing checked against each other. A binding keeps
// the read and the write next to each other, in the class that owns the values;
// Settings only walks the registered bindings in order.
class ISettingsBinding
{
public:
    virtual ~ISettingsBinding() = default;

    // !\brief The section this binding owns, as it appears in settings.ini.
    // Reported when a load fails, so the user is told which block kept its
    // defaults.
    [[nodiscard]] virtual std::string_view SettingsSection() const = 0;

    // !\brief Read this subsystem's settings.
    // Throws when a required key is missing or malformed; Settings catches that
    // per binding, so one broken block no longer stops the rest of the file.
    virtual void LoadSettings(SettingsReader& reader) = 0;

    // !\brief Write this subsystem's block, section header and comments included.
    virtual void SaveSettings(std::ostream& out) const = 0;

    // !\brief Write the block for a settings.ini created from scratch, where
    // example content stands in for live state.
    //
    // Only the macro keys and the backup list have anything to show a user who
    // has none of their own yet, so this defaults to the live block. It used to
    // be a bool threaded through all sixteen bindings, fourteen of which took
    // it unnamed and ignored it.
    virtual void SaveDefaultSettings(std::ostream& out) const { SaveSettings(out); }
};
