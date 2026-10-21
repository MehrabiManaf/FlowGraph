// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// Built-in nodes of the Time category. A point in time is a Timestamp: seconds since 1970-01-01 00:00:00 UTC (Unix time) as a
// float, with millisecond precision. Calendar dates are computed here (days from / to a civil date, proleptic Gregorian calendar)
// instead of with the C library: that is the same on every platform, thread safe, works before 1970, and a format text with an
// unknown % code cannot end the process (MSVC's strftime does that).
#include <FlowGraph/Builtins.hpp>
#include <FlowGraph/Vm.hpp>

#include <chrono>
#include <cmath>
#include <ctime>

namespace fg {

namespace {

enum class Weekday { Monday = 1, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };

int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// Days since 1970-01-01 of a civil date, and back (H. Hinnant, "chrono-compatible low-level date algorithms").
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
    y -= m <= 2;
    const int64_t era = floorDiv(y, 400);
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void civilFromDays(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
    z += 719468;
    const int64_t era = floorDiv(z, 146097);
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}
bool leapYear(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int64_t daysInMonth(int64_t y, int64_t m) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && leapYear(y) ? 29 : kDays[m - 1];
}

// The offset of the local time zone from UTC, in seconds, at a moment (daylight saving time included). 0 when the system does not know.
int64_t localOffsetAt(double timestamp) {
    if (!std::isfinite(timestamp)) return 0;
    const std::time_t t = static_cast<std::time_t>(std::floor(timestamp));
    std::tm tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &t) != 0) return 0;
#else
    if (!localtime_r(&t, &tm)) return 0;
#endif
    const int64_t local = daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return local - static_cast<int64_t>(t);
}

// Dates are supported for a million years either way: far beyond what a script needs, far below where the arithmetic would overflow.
constexpr int64_t kMaxYears = 1000000;
constexpr double kMaxSeconds = 3.2e13;            // about a million years

struct DateTime {
    int64_t year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, millisecond = 0;
    int64_t weekday = 4;        // ISO: Monday 1 ... Sunday 7 (1970-01-01 was a Thursday)
    int64_t dayOfYear = 1;
    int64_t offset = 0;         // seconds east of UTC of the time zone the fields are in
};

DateTime breakTimestamp(double timestamp, bool local) {
    if (!std::isfinite(timestamp)) throw ScriptError("the timestamp is not a number");
    if (std::fabs(timestamp) > kMaxSeconds) throw ScriptError("the timestamp is out of range (years -1000000 to 1000000)");
    DateTime dt;
    dt.offset = local ? localOffsetAt(timestamp) : 0;
    const int64_t totalMs = static_cast<int64_t>(std::llround(timestamp * 1000.0)) + dt.offset * 1000;
    const int64_t days = floorDiv(totalMs, 86400000);
    int64_t msOfDay = totalMs - days * 86400000;
    civilFromDays(days, dt.year, dt.month, dt.day);
    dt.hour = msOfDay / 3600000;
    msOfDay %= 3600000;
    dt.minute = msOfDay / 60000;
    msOfDay %= 60000;
    dt.second = msOfDay / 1000;
    dt.millisecond = msOfDay % 1000;
    dt.weekday = ((days + 3) % 7 + 7) % 7 + 1;
    dt.dayOfYear = days - daysFromCivil(dt.year, 1, 1) + 1;
    return dt;
}


// Fields out of range carry over, like a calendar would (and JavaScript's Date, C's mktime): month 13 is January of the next year,
// day 0 the last day of the month before, hour 36 noon of the next day.
double makeTimestamp(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s, int64_t ms, bool local) {
    auto tooFar = [] { throw ScriptError("the date is out of range (years -1000000 to 1000000)"); };
    if (y < -kMaxYears || y > kMaxYears || mo < -12 * kMaxYears || mo > 12 * kMaxYears) tooFar();
    y += floorDiv(mo - 1, 12);
    mo = (mo - 1) - floorDiv(mo - 1, 12) * 12 + 1;
    // the other fields are added as floating point numbers: any value carries over without an integer overflow
    const double utc = static_cast<double>(daysFromCivil(y, mo, 1)) * 86400.0 + static_cast<double>(d - 1) * 86400.0 +
                       static_cast<double>(h) * 3600.0 + static_cast<double>(mi) * 60.0 + static_cast<double>(s) + static_cast<double>(ms) / 1000.0;
    if (!(std::fabs(utc) <= kMaxSeconds)) tooFar();
    if (!local) return utc;
    // the offset depends on the moment itself (daylight saving time): two steps find it
    double t = utc - static_cast<double>(localOffsetAt(utc));
    t = utc - static_cast<double>(localOffsetAt(t));
    return t;
}

const char* const kMonths[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
const char* const kDays[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

std::string pad(int64_t v, int width) {
    std::string s = std::to_string(v < 0 ? -v : v);
    while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
    return v < 0 ? "-" + s : s;
}

// %Y year, %y two digit year, %m month, %d day, %H hour (24), %I hour (12), %p AM / PM, %M minute, %S second, %f milliseconds,
// %j day of the year, %u ISO weekday (1 = Monday), %A / %a weekday name / short, %B / %b month name / short, %z offset (+hhmm),
// %F = %Y-%m-%d, %T = %H:%M:%S, %% a percent sign. Anything else is copied as it is.
std::string formatDateTime(const DateTime& dt, const std::string& fmt) {
    std::string out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) { out += fmt[i]; continue; }
        const char c = fmt[++i];
        switch (c) {
        case 'Y': out += pad(dt.year, 4); break;
        case 'y': out += pad(((dt.year % 100) + 100) % 100, 2); break;
        case 'm': out += pad(dt.month, 2); break;
        case 'd': out += pad(dt.day, 2); break;
        case 'H': out += pad(dt.hour, 2); break;
        case 'I': out += pad(dt.hour % 12 == 0 ? 12 : dt.hour % 12, 2); break;
        case 'p': out += dt.hour < 12 ? "AM" : "PM"; break;
        case 'M': out += pad(dt.minute, 2); break;
        case 'S': out += pad(dt.second, 2); break;
        case 'f': out += pad(dt.millisecond, 3); break;
        case 'j': out += pad(dt.dayOfYear, 3); break;
        case 'u': out += std::to_string(dt.weekday); break;
        case 'A': out += kDays[dt.weekday - 1]; break;
        case 'a': out += std::string(kDays[dt.weekday - 1]).substr(0, 3); break;
        case 'B': out += kMonths[dt.month - 1]; break;
        case 'b': out += std::string(kMonths[dt.month - 1]).substr(0, 3); break;
        case 'z': {
            const int64_t m = (dt.offset < 0 ? -dt.offset : dt.offset) / 60;
            out += (dt.offset < 0 ? "-" : "+") + pad(m / 60, 2) + pad(m % 60, 2);
            break;
        }
        case 'F': out += pad(dt.year, 4) + "-" + pad(dt.month, 2) + "-" + pad(dt.day, 2); break;
        case 'T': out += pad(dt.hour, 2) + ":" + pad(dt.minute, 2) + ":" + pad(dt.second, 2); break;
        case '%': out += '%'; break;
        default: out += '%'; out += c; break;
        }
    }
    return out;
}

// ISO 8601: YYYY-MM-DD, then optionally T or a space and HH:MM[:SS[.fff]], then optionally Z or +HH:MM / -HH:MM / +HHMM.
// Without a zone the time is local time (or UTC when `local` is false).
bool parseIso(const std::string& text, bool local, double& out) {
    size_t p = 0;
    const std::string s = text;
    auto digits = [&](int n, int64_t& v) {
        if (p + static_cast<size_t>(n) > s.size()) return false;
        v = 0;
        for (int k = 0; k < n; ++k) {
            const char c = s[p + static_cast<size_t>(k)];
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        p += static_cast<size_t>(n);
        return true;
    };
    auto lit = [&](char c) {
        if (p < s.size() && s[p] == c) { ++p; return true; }
        return false;
    };
    while (p < s.size() && s[p] == ' ') ++p;
    int64_t y, mo, d, h = 0, mi = 0, sec = 0, ms = 0;
    if (!digits(4, y) || !lit('-') || !digits(2, mo) || !lit('-') || !digits(2, d)) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo)) return false;
    bool zone = false;
    int64_t offset = 0;
    if (p < s.size() && (s[p] == 'T' || s[p] == 't' || s[p] == ' ') && p + 1 < s.size() && s[p + 1] >= '0' && s[p + 1] <= '9') {
        ++p;
        if (!digits(2, h) || !lit(':') || !digits(2, mi)) return false;
        if (lit(':')) {
            if (!digits(2, sec)) return false;
            if (lit('.') || lit(',')) {
                size_t start = p;
                int64_t frac = 0;
                int n = 0;
                while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
                    if (n < 3) { frac = frac * 10 + (s[p] - '0'); ++n; }
                    ++p;
                }
                if (p == start) return false;
                while (n < 3) { frac *= 10; ++n; }
                ms = frac;
            }
        }
        if (h > 23 || mi > 59 || sec > 60) return false;
        if (lit('Z') || lit('z')) zone = true;
        else if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
            const bool minus = s[p++] == '-';
            int64_t oh, om = 0;
            if (!digits(2, oh)) return false;
            lit(':');
            if (p < s.size() && s[p] >= '0' && s[p] <= '9' && !digits(2, om)) return false;
            if (oh > 23 || om > 59) return false;
            offset = (oh * 3600 + om * 60) * (minus ? -1 : 1);
            zone = true;
        }
    }
    while (p < s.size() && s[p] == ' ') ++p;
    if (p != s.size()) return false;
    if (zone) out = makeTimestamp(y, mo, d, h, mi, sec, ms, false) - static_cast<double>(offset);
    else out = makeTimestamp(y, mo, d, h, mi, sec, ms, local);
    return true;
}

NodeDef pureTimeNode(std::string id, std::string title) {
    NodeDef d;
    d.id = std::move(id);
    d.title = std::move(title);
    d.category = "Time";
    d.pure = true;
    d.style = NodeStyle::Pure;
    return d;
}

} // namespace

void registerTimeNodes(Registry& r) {
    r.registerEnum<Weekday>("Weekday", "Time")
        .value("Monday", Weekday::Monday).value("Tuesday", Weekday::Tuesday).value("Wednesday", Weekday::Wednesday)
        .value("Thursday", Weekday::Thursday).value("Friday", Weekday::Friday).value("Saturday", Weekday::Saturday).value("Sunday", Weekday::Sunday);

    // Exec nodes, like Random: each run reads the clock once, and every pin connected to the node sees that reading.
    r.addFunction("Time.Now", [] {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()) / 1000.0;
    }).withResultName("Timestamp").withKeywords("time date clock current today unix epoch")
      .withDoc("The current time: seconds since 1970-01-01 00:00 UTC, with milliseconds.");
    r.addFunction("Time.Seconds Since Start", [] {
        static const auto processStart = std::chrono::steady_clock::now();
        if (Instance* inst = currentInstance()) return inst->secondsSinceStart();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - processStart).count();
    }).withResultName("Seconds").withKeywords("time elapsed duration stopwatch measure")
      .withDoc("Seconds since the script instance started, from a steady clock (changes of the system time do not affect it): "
               "subtract two readings to measure how long something takes.");

    {
        NodeDef d = pureTimeNode("time.break", "Break Date Time");
        d.keywords = "date time year month day hour minute second weekday split";
        d.doc = "The calendar date and the time of day of a timestamp, in local time or in UTC. Weekday: Monday to Sunday.";
        d.pins = {dataIn("Timestamp", "float", Value(0.0)), dataIn("Local", "bool", Value(true)),
                  dataOut("Year", "int"), dataOut("Month", "int"), dataOut("Day", "int"), dataOut("Hour", "int"), dataOut("Minute", "int"),
                  dataOut("Second", "int"), dataOut("Millisecond", "int"), dataOut("Weekday", "Weekday"), dataOut("Day Of Year", "int")};
        d.pins[1].doc = "Local time (on) or UTC (off)";
        d.run = [](NodeCtx& c) {
            const DateTime dt = breakTimestamp(c.inFloat(0), c.inBool(1));
            const int64_t v[] = {dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.millisecond, dt.weekday, dt.dayOfYear};
            for (size_t i = 0; i < 9; ++i) c.out(i, Value(v[i]));
        };
        r.addNode(std::move(d));
    }
    {
        NodeDef d = pureTimeNode("time.make", "Make Date Time");
        d.keywords = "date time year month day hour minute second build compose";
        d.doc = "The timestamp of a calendar date and time of day, in local time or in UTC. Values out of range carry over: month 13 "
                "is January of the next year, minute 90 is half past the next hour.";
        d.pins = {dataIn("Year", "int", Value(2000)), dataIn("Month", "int", Value(1)), dataIn("Day", "int", Value(1)), dataIn("Hour", "int", Value(0)),
                  dataIn("Minute", "int", Value(0)), dataIn("Second", "int", Value(0)), dataIn("Millisecond", "int", Value(0)),
                  dataIn("Local", "bool", Value(true)), dataOut("Timestamp", "float")};
        d.pins[8].doc = "Local time (on) or UTC (off)";
        d.run = [](NodeCtx& c) {
            c.out(0, Value(makeTimestamp(c.inInt(0), c.inInt(1), c.inInt(2), c.inInt(3), c.inInt(4), c.inInt(5), c.inInt(6), c.inBool(7))));
        };
        r.addNode(std::move(d));
    }
    r.addFunction("Time.Format Date Time", [](double ts, const std::string& fmt, bool local) { return formatDateTime(breakTimestamp(ts, local), fmt); },
                  {"Timestamp", "Format", "Local"})
        .makePure().withDefault(1, Value("%Y-%m-%d %H:%M:%S")).withDefault(2, Value(true)).withKeywords("date time text string print strftime")
        .withDoc("Text of a timestamp. %Y year, %y two digit year, %m month, %d day, %H hour, %I hour (1-12), %p AM/PM, %M minute, "
                 "%S second, %f milliseconds, %j day of the year, %u weekday (1 = Monday), %A / %a weekday name, %B / %b month name, "
                 "%z offset from UTC (+hhmm), %F = %Y-%m-%d, %T = %H:%M:%S, %% a percent sign.");
    {
        NodeDef d = pureTimeNode("time.parse", "Parse Date Time");
        d.keywords = "date time text string read iso 8601 convert";
        d.doc = "The timestamp of an ISO 8601 text: 2026-10-18, 2026-10-18 14:30, 2026-10-18T14:30:05.250Z, 2026-10-18T14:30+02:00. "
                "Without a zone the time is local time (or UTC when Local is off). Valid is off, and Timestamp 0, when the text is "
                "not such a date.";
        d.pins = {dataIn("Text", "string", Value("2026-01-01 00:00:00")), dataIn("Local", "bool", Value(true)), dataOut("Timestamp", "float"),
                  dataOut("Valid", "bool")};
        d.run = [](NodeCtx& c) {
            double t = 0;
            const bool ok = parseIso(c.inString(0), c.inBool(1), t);
            c.out(0, Value(ok ? t : 0.0));
            c.out(1, Value(ok));
        };
        r.addNode(std::move(d));
    }
}

} // namespace fg
