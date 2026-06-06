using System.Globalization;

namespace WinMacMenu.Config;

/// <summary>
/// Minimal INI reader matching the behaviour the Win32 app relies on via
/// <c>GetPrivateProfileString</c>: case-insensitive section/key lookup, the first
/// occurrence of a key wins, surrounding double quotes are stripped, and full lines
/// beginning with ';' or '#' are treated as comments (per the project README).
/// </summary>
public sealed class IniFile
{
    // section -> (key -> value), all keyed case-insensitively.
    private readonly Dictionary<string, Dictionary<string, string>> _sections =
        new(StringComparer.OrdinalIgnoreCase);

    public static IniFile Load(string path)
    {
        var ini = new IniFile();
        if (!File.Exists(path))
            return ini;

        Dictionary<string, string>? current = null;
        foreach (var raw in File.ReadAllLines(path))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line[0] == ';' || line[0] == '#')
                continue;

            if (line[0] == '[' && line[^1] == ']')
            {
                var name = line[1..^1].Trim();
                if (!ini._sections.TryGetValue(name, out current))
                {
                    current = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    ini._sections[name] = current;
                }
                continue;
            }

            int eq = line.IndexOf('=');
            if (eq < 0)
                continue;

            current ??= ini.GetOrAddSection(string.Empty);
            var key = line[..eq].Trim();
            var value = line[(eq + 1)..].Trim();
            if (value.Length >= 2 && value[0] == '"' && value[^1] == '"')
                value = value[1..^1];

            // First occurrence wins, matching GetPrivateProfileString.
            if (key.Length > 0 && !current.ContainsKey(key))
                current[key] = value;
        }

        return ini;
    }

    private Dictionary<string, string> GetOrAddSection(string name)
    {
        if (!_sections.TryGetValue(name, out var s))
        {
            s = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            _sections[name] = s;
        }
        return s;
    }

    public string GetString(string section, string key, string fallback = "")
    {
        if (_sections.TryGetValue(section, out var s) && s.TryGetValue(key, out var v))
            return v;
        return fallback;
    }

    public int GetInt(string section, string key, int fallback)
    {
        var v = GetString(section, key, string.Empty);
        if (string.IsNullOrEmpty(v))
            return fallback;
        // GetPrivateProfileInt parses a leading signed integer.
        int i = 0, sign = 1;
        if (i < v.Length && (v[i] == '+' || v[i] == '-')) { if (v[i] == '-') sign = -1; i++; }
        long acc = 0; bool any = false;
        for (; i < v.Length && char.IsDigit(v[i]); i++) { acc = acc * 10 + (v[i] - '0'); any = true; }
        return any ? (int)(sign * acc) : fallback;
    }

    public bool GetBool(string section, string key, bool fallback)
    {
        var v = GetString(section, key, string.Empty).Trim();
        if (string.IsNullOrEmpty(v))
            return fallback;
        return v.Equals("true", StringComparison.OrdinalIgnoreCase) || v == "1";
    }

    /// <summary>True only when the key is present (even if empty), used for back-compat fallbacks.</summary>
    public bool HasKey(string section, string key)
        => _sections.TryGetValue(section, out var s) && s.ContainsKey(key);

    public static int ToInt(string value, int fallback)
        => int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var r) ? r : fallback;
}
