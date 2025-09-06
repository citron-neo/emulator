// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 1996 Arthur David Olson
// SPDX-License-Identifier: BSD-2-Clause

#include <climits>
#include <cstring>
#include <ctime>

#include "tz.h"

namespace Tz {

namespace {
#define EINVAL 22

static Rule gmtmem{};
static Rule* const gmtptr = &gmtmem;

struct TzifHeader {
    std::array<char, 4> tzh_magic; // "TZif"
    std::array<char, 1> tzh_version;
    std::array<char, 15> tzh_reserved;
    std::array<char, 4> tzh_ttisutcnt;
    std::array<char, 4> tzh_ttisstdcnt;
    std::array<char, 4> tzh_leapcnt;
    std::array<char, 4> tzh_timecnt;
    std::array<char, 4> tzh_typecnt;
    std::array<char, 4> tzh_charcnt;
};
static_assert(sizeof(TzifHeader) == 0x2C, "TzifHeader has the wrong size!");

struct local_storage {
    // Binary layout:
    // char buf[2 * sizeof(TzifHeader) + 2 * sizeof(Rule) + 4 * TZ_MAX_TIMES];
    std::span<const u8> binary;
    Rule state;
};
static local_storage tzloadbody_local_storage;

enum rtype : s32 {
    JULIAN_DAY = 0,
    DAY_OF_YEAR = 1,
    MONTH_NTH_DAY_OF_WEEK = 2,
};

struct tzrule {
    rtype r_type;
    int r_day;
    int r_week;
    int r_mon;
    s64 r_time;
};
static_assert(sizeof(tzrule) == 0x18, "tzrule has the wrong size!");

constexpr static char UNSPEC[] = "-00";
constexpr static char TZDEFRULESTRING[] = ",M3.2.0,M11.1.0";

enum {
    SECSPERMIN = 60,
    MINSPERHOUR = 60,
    SECSPERHOUR = SECSPERMIN * MINSPERHOUR,
    HOURSPERDAY = 24,
    DAYSPERWEEK = 7,
    DAYSPERNYEAR = 365,
    DAYSPERLYEAR = DAYSPERNYEAR + 1,
    MONSPERYEAR = 12,
    YEARSPERREPEAT = 400 /* years before a Gregorian repeat */
};

#define SECSPERDAY ((s64)SECSPERHOUR * HOURSPERDAY)

#define DAYSPERREPEAT ((s64)400 * 365 + 100 - 4 + 1)
#define SECSPERREPEAT ((int_fast64_t)DAYSPERREPEAT * SECSPERDAY)
#define AVGSECSPERYEAR (SECSPERREPEAT / YEARSPERREPEAT)

enum {
    TM_SUNDAY,
    TM_MONDAY,
    TM_TUESDAY,
    TM_WEDNESDAY,
    TM_THURSDAY,
    TM_FRIDAY,
    TM_SATURDAY,
};

enum {
    TM_JANUARY,
    TM_FEBRUARY,
    TM_MARCH,
    TM_APRIL,
    TM_MAY,
    TM_JUNE,
    TM_JULY,
    TM_AUGUST,
    TM_SEPTEMBER,
    TM_OCTOBER,
    TM_NOVEMBER,
    TM_DECEMBER,
};

constexpr s32 TM_YEAR_BASE = 1900;
constexpr s32 TM_WDAY_BASE = TM_MONDAY;
constexpr s32 EPOCH_YEAR = 1970;
constexpr s32 EPOCH_WDAY = TM_THURSDAY;

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

static constexpr std::array<std::array<int, MONSPERYEAR>, 2> mon_lengths = { {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
} };

static constexpr std::array<int, 2> year_lengths = {
    DAYSPERNYEAR,
    DAYSPERLYEAR,
};

constexpr static time_t leaps_thru_end_of_nonneg(time_t y) {
    return y / 4 - y / 100 + y / 400;
}

constexpr static time_t leaps_thru_end_of(time_t y) {
    return (y < 0 ? -1 - leaps_thru_end_of_nonneg(-1 - y) : leaps_thru_end_of_nonneg(y));
}

#define TWOS_COMPLEMENT(t) ((t) ~(t)0 < 0)

s32 detzcode(const char* const codep) {
    s32 result;
    int i;
    s32 one = 1;
    s32 halfmaxval = one << (32 - 2);
    s32 maxval = halfmaxval - 1 + halfmaxval;
    s32 minval = -1 - maxval;

    result = codep[0] & 0x7f;
    for (i = 1; i < 4; ++i) {
        result = (result << 8) | (codep[i] & 0xff);
    }

    if (codep[0] & 0x80) {
        /* Do two's-complement negation even on non-two's-complement machines.
            If the result would be minval - 1, return minval.  */
        result -= !TWOS_COMPLEMENT(s32) && result != 0;
        result += minval;
    }
    return result;
}

int_fast64_t detzcode64(const char* const codep) {
    int_fast64_t result;
    int i;
    int_fast64_t one = 1;
    int_fast64_t halfmaxval = one << (64 - 2);
    int_fast64_t maxval = halfmaxval - 1 + halfmaxval;
    int_fast64_t minval = -static_cast<int_fast64_t>(TWOS_COMPLEMENT(int_fast64_t)) - maxval;

    result = codep[0] & 0x7f;
    for (i = 1; i < 8; ++i) {
        result = (result << 8) | (codep[i] & 0xff);
    }

    if (codep[0] & 0x80) {
        /* Do two's-complement negation even on non-two's-complement machines.
            If the result would be minval - 1, return minval.  */
        result -= !TWOS_COMPLEMENT(int_fast64_t) && result != 0;
        result += minval;
    }
    return result;
}

/* Initialize *S to a value based on UTOFF, ISDST, and DESIGIDX.  */
constexpr void init_ttinfo(ttinfo* s, s64 utoff, bool isdst, int desigidx) {
    s->tt_utoff = static_cast<s32>(utoff);
    s->tt_isdst = isdst;
    s->tt_desigidx = desigidx;
    s->tt_ttisstd = false;
    s->tt_ttisut = false;
}

/* Return true if SP's time type I does not specify local time.  */
bool ttunspecified(struct Rule const* sp, int i) {
    char const* abbr = &sp->chars[sp->ttis[i].tt_desigidx];
    /* memcmp is likely faster than strcmp, and is safe due to CHARS_EXTRA.  */
    return memcmp(abbr, UNSPEC, sizeof(UNSPEC)) == 0;
}

bool typesequiv(const Rule* sp, int a, int b) {
    bool result;

    if (sp == nullptr || a < 0 || a >= sp->typecnt || b < 0 || b >= sp->typecnt) {
        result = false;
    }
    else {
        /* Compare the relevant members of *AP and *BP.
        Ignore tt_ttisstd and tt_ttisut, as they are
        irrelevant now and counting them could cause
        sp->goahead to mistakenly remain false.  */
        const ttinfo* ap = &sp->ttis[a];
        const ttinfo* bp = &sp->ttis[b];
        result = (ap->tt_utoff == bp->tt_utoff && ap->tt_isdst == bp->tt_isdst &&
            (strcmp(&sp->chars[ap->tt_desigidx], &sp->chars[bp->tt_desigidx]) == 0));
    }
    return result;
}

constexpr const char* getqzname(const char* strp, const int delim) {
    int c;

    while ((c = *strp) != '\0' && c != delim) {
        ++strp;
    }
    return strp;
}

/* Is C an ASCII digit?  */
constexpr bool is_digit(char c) {
    return '0' <= c && c <= '9';
}

/*
** Given a pointer into a timezone string, scan until a character that is not
** a valid character in a time zone abbreviation is found.
** Return a pointer to that character.
*/

constexpr const char* getzname(const char* strp) {
    char c;

    while ((c = *strp) != '\0' && !is_digit(c) && c != ',' && c != '-' && c != '+') {
        ++strp;
    }
    return strp;
}

static const char* getnum(const char* strp, int* const nump, const int min, const int max) {
    char c;
    int num;

    if (strp == nullptr || !is_digit(c = *strp)) {
        return nullptr;
    }
    num = 0;
    do {
        num = num * 10 + (c - '0');
        if (num > max) {
            return nullptr; /* illegal value */
        }
        c = *++strp;
    } while (is_digit(c));
    if (num < min) {
        return nullptr; /* illegal value */
    }
    *nump = num;
    return strp;
}

/*
** Given a pointer into a timezone string, extract a number of seconds,
** in hh[:mm[:ss]] form, from the string.
** If any error occurs, return NULL.
** Otherwise, return a pointer to the first character not part of the number
** of seconds.
*/

const char* getsecs(const char* strp, s64* const secsp) {
    int num;
    s64 secsperhour = SECSPERHOUR;

    /*
    ** 'HOURSPERDAY * DAYSPERWEEK - 1' allows quasi-Posix rules like
    ** "M10.4.6/26", which does not conform to Posix,
    ** but which specifies the equivalent of
    ** "02:00 on the first Sunday on or after 23 Oct".
    */
    strp = getnum(strp, &num, 0, HOURSPERDAY * DAYSPERWEEK - 1);
    if (strp == nullptr) {
        return nullptr;
    }
    *secsp = num * secsperhour;
    if (*strp == ':') {
        ++strp;
        strp = getnum(strp, &num, 0, MINSPERHOUR - 1);
        if (strp == nullptr) {
            return nullptr;
        }
        *secsp += num * SECSPERMIN;
        if (*strp == ':') {
            ++strp;
            /* 'SECSPERMIN' allows for leap seconds.  */
            strp = getnum(strp, &num, 0, SECSPERMIN);
            if (strp == nullptr) {
                return nullptr;
            }
            *secsp += num;
        }
    }
    return strp;
}

/*
** Given a pointer into a timezone string, extract an offset, in
** [+-]hh[:mm[:ss]] form, from the string.
** If any error occurs, return NULL.
** Otherwise, return a pointer to the first character not part of the time.
*/

const char* getoffset(const char* strp, s64* const offsetp) {
    bool neg = false;

    if (*strp == '-') {
        neg = true;
        ++strp;
    }
    else if (*strp == '+') {
        ++strp;
    }
    strp = getsecs(strp, offsetp);
    if (strp == nullptr) {
        return nullptr; /* illegal time */
    }
    if (neg) {
        *offsetp = -*offsetp;
    }
    return strp;
}

constexpr const char* getrule(const char* strp, tzrule* const rulep) {
    if (*strp == 'J') {
        /*
        ** Julian day.
        */
        rulep->r_type = JULIAN_DAY;
        ++strp;
        strp = getnum(strp, &rulep->r_day, 1, DAYSPERNYEAR);
    }
    else if (*strp == 'M') {
        /*
        ** Month, week, day.
        */
        rulep->r_type = MONTH_NTH_DAY_OF_WEEK;
        ++strp;
        strp = getnum(strp, &rulep->r_mon, 1, MONSPERYEAR);
        if (strp == nullptr) {
            return nullptr;
        }
        if (*strp++ != '.') {
            return nullptr;
        }
        strp = getnum(strp, &rulep->r_week, 1, 5);
        if (strp == nullptr) {
            return nullptr;
        }
        if (*strp++ != '.') {
            return nullptr;
        }
        strp = getnum(strp, &rulep->r_day, 0, DAYSPERWEEK - 1);
    }
    else if (is_digit(*strp)) {
        /*
        ** Day of year.
        */
        rulep->r_type = DAY_OF_YEAR;
        strp = getnum(strp, &rulep->r_day, 0, DAYSPERLYEAR - 1);
    }
    else {
        return nullptr;
    } /* invalid format */
    if (strp == nullptr) {
        return nullptr;
    }
    if (*strp == '/') {
        /*
        ** Time specified.
        */
        ++strp;
        strp = getoffset(strp, &rulep->r_time);
    }
    else {
        rulep->r_time = 2 * SECSPERHOUR; /* default = 2:00:00 */
    }
    return strp;
}

constexpr bool increment_overflow(int* ip, int j) {
    int const i = *ip;

    /*
    ** If i >= 0 there can only be overflow if i + j > INT_MAX
    ** or if j > INT_MAX - i; given i >= 0, INT_MAX - i cannot overflow.
    ** If i < 0 there can only be overflow if i + j < INT_MIN
    ** or if j < INT_MIN - i; given i < 0, INT_MIN - i cannot overflow.
    */
    if ((i >= 0) ? (j > INT_MAX - i) : (j < INT_MIN - i)) {
        return true;
    }
    *ip += j;
    return false;
}

constexpr bool increment_overflow32(s64* const lp, int const m) {
    s64 const l = *lp;

    if ((l >= 0) ? (m > INT_FAST32_MAX - l) : (m < INT_FAST32_MIN - l))
        return true;
    *lp += m;
    return false;
}

constexpr bool increment_overflow_time(time_t* tp, s64 j) {
    /*
    ** This is like
    ** 'if (! (TIME_T_MIN <= *tp + j && *tp + j <= TIME_T_MAX)) ...',
    ** except that it does the right thing even if *tp + j would overflow.
    */
    if (!(j < 0 ? (std::is_signed_v<time_t> ? TIME_T_MIN - j <= *tp : -1 - j < *tp)
        : *tp <= TIME_T_MAX - j)) {
        return true;
    }
    *tp += j;
    return false;
}

CalendarTimeInternal* timesub(const time_t* timep, s64 offset, const Rule* sp,
    CalendarTimeInternal* tmp) {
    time_t tdays;
    const int* ip;
    s64 idays, rem, dayoff, dayrem;
    time_t y;

    /* Calculate the year, avoiding integer overflow even if
        time_t is unsigned.  */
    tdays = *timep / SECSPERDAY;
    rem = *timep % SECSPERDAY;
    rem += offset % SECSPERDAY + 3 * SECSPERDAY;
    dayoff = offset / SECSPERDAY + rem / SECSPERDAY - 3;
    rem %= SECSPERDAY;
    /* y = (EPOCH_YEAR
            + floor((tdays + dayoff) / DAYSPERREPEAT) * YEARSPERREPEAT),
        sans overflow.  But calculate against 1570 (EPOCH_YEAR -
        YEARSPERREPEAT) instead of against 1970 so that things work
        for localtime values before 1970 when time_t is unsigned.  */
    dayrem = tdays % DAYSPERREPEAT;
    dayrem += dayoff % DAYSPERREPEAT;
    y = (EPOCH_YEAR - YEARSPERREPEAT +
        ((1ull + dayoff / DAYSPERREPEAT + dayrem / DAYSPERREPEAT - ((dayrem % DAYSPERREPEAT) < 0) +
            tdays / DAYSPERREPEAT) *
            YEARSPERREPEAT));
        /* idays = (tdays + dayoff) mod DAYSPERREPEAT, sans overflow.  */
    idays = tdays % DAYSPERREPEAT;
    idays += dayoff % DAYSPERREPEAT + 2 * DAYSPERREPEAT;
    idays %= DAYSPERREPEAT;
    /* Increase Y and decrease IDAYS until IDAYS is in range for Y.  */
    while (year_lengths[isleap(y)] <= idays) {
        s64 tdelta = idays / DAYSPERLYEAR;
        s64 ydelta = tdelta + !tdelta;
        time_t newy = y + ydelta;
        int leapdays;
        leapdays = static_cast<s32>(leaps_thru_end_of(newy - 1) - leaps_thru_end_of(y - 1));
        idays -= ydelta * DAYSPERNYEAR;
        idays -= leapdays;
        y = newy;
    }

    if constexpr (!std::is_signed_v<time_t> && y < TM_YEAR_BASE) {
        int signed_y = static_cast<s32>(y);
        tmp->tm_year = signed_y - TM_YEAR_BASE;
    }
    else if ((!std::is_signed_v<time_t> || std::numeric_limits<s32>::min() + TM_YEAR_BASE <= y) &&
        y - TM_YEAR_BASE <= std::numeric_limits<s32>::max()) {
        tmp->tm_year = static_cast<s32>(y - TM_YEAR_BASE);
    }
    else {
        // errno = EOVERFLOW;
        return nullptr;
    }

    tmp->tm_yday = static_cast<s32>(idays);
    /*
    ** The "extra" mods below avoid overflow problems.
    */
    tmp->tm_wday = static_cast<s32>(
        TM_WDAY_BASE + ((tmp->tm_year % DAYSPERWEEK) * (DAYSPERNYEAR % DAYSPERWEEK)) +
        leaps_thru_end_of(y - 1) - leaps_thru_end_of(TM_YEAR_BASE - 1) + idays);
    tmp->tm_wday %= DAYSPERWEEK;
    if (tmp->tm_wday < 0) {
        tmp->tm_wday += DAYSPERWEEK;
    }
    tmp->tm_hour = static_cast<s32>(rem / SECSPERHOUR);
    rem %= SECSPERHOUR;
    tmp->tm_min = static_cast<s32>(rem / SECSPERMIN);
    tmp->tm_sec = static_cast<s32>(rem % SECSPERMIN);

    ip = mon_lengths[isleap(y)].data();
    for (tmp->tm_mon = 0; idays >= ip[tmp->tm_mon]; ++(tmp->tm_mon)) {
        idays -= ip[tmp->tm_mon];
    }
    tmp->tm_mday = static_cast<s32>(idays + 1);
    tmp->tm_isdst = 0;
    return tmp;
}

CalendarTimeInternal* gmtsub([[maybe_unused]] Rule const* sp, time_t const* timep,
    s64 offset, CalendarTimeInternal* tmp) {
    CalendarTimeInternal* result;

    result = timesub(timep, offset, gmtptr, tmp);
    return result;
}

CalendarTimeInternal* localsub(Rule const* sp, time_t const* timep, s64 setname,
    CalendarTimeInternal* const tmp) {
    const ttinfo* ttisp;
    int i;
    CalendarTimeInternal* result;
    const time_t t = *timep;

    if (sp == nullptr) {
        /* Don't bother to set tzname etc.; tzset has already done it.  */
        return gmtsub(gmtptr, timep, 0, tmp);
    }
    if ((sp->goback && t < sp->ats[0]) || (sp->goahead && t > sp->ats[sp->timecnt - 1])) {
        time_t newt;
        time_t seconds;
        time_t years;

        if (t < sp->ats[0]) {
            seconds = sp->ats[0] - t;
        }
        else {
            seconds = t - sp->ats[sp->timecnt - 1];
        }
        --seconds;

        /* Beware integer overflow, as SECONDS might
            be close to the maximum time_t.  */
        years = seconds / SECSPERREPEAT * YEARSPERREPEAT;
        seconds = years * AVGSECSPERYEAR;
        years += YEARSPERREPEAT;
        if (t < sp->ats[0]) {
            newt = t + seconds + SECSPERREPEAT;
        }
        else {
            newt = t - seconds - SECSPERREPEAT;
        }

        if (newt < sp->ats[0] || newt > sp->ats[sp->timecnt - 1]) {
            return nullptr; /* "cannot happen" */
        }
        result = localsub(sp, &newt, setname, tmp);
        if (result) {
            int_fast64_t newy;

            newy = result->tm_year;
            if (t < sp->ats[0]) {
                newy -= years;
            }
            else {
                newy += years;
            }
            if (!(std::numeric_limits<s32>::min() <= newy &&
                newy <= std::numeric_limits<s32>::max())) {
                return nullptr;
            }
            result->tm_year = static_cast<s32>(newy);
        }
        return result;
    }
    if (sp->timecnt == 0 || t < sp->ats[0]) {
        i = sp->defaulttype;
    }
    else {
        int lo = 1;
        int hi = sp->timecnt;

        while (lo < hi) {
            int mid = (lo + hi) >> 1;

            if (t < sp->ats[mid])
                hi = mid;
            else
                lo = mid + 1;
        }
        i = sp->types[lo - 1];
    }
    ttisp = &sp->ttis[i];
    /*
    ** To get (wrong) behavior that's compatible with System V Release 2.0
    ** you'd replace the statement below with
    **	t += ttisp->tt_utoff;
    **	timesub(&t, 0L, sp, tmp);
    */
    result = timesub(&t, ttisp->tt_utoff, sp, tmp);
    if (result) {
        result->tm_isdst = ttisp->tt_isdst;

        if (ttisp->tt_desigidx > static_cast<s32>(sp->chars.size() - CHARS_EXTRA)) {
            return nullptr;
        }

        auto num_chars_to_copy{
            std::min(sp->chars.size() - ttisp->tt_desigidx, result->tm_zone.size()) - 1 };
        std::strncpy(result->tm_zone.data(), &sp->chars[ttisp->tt_desigidx], num_chars_to_copy);
        result->tm_zone[num_chars_to_copy] = '\0';

        auto original_size{ std::strlen(&sp->chars[ttisp->tt_desigidx]) };
        if (original_size > num_chars_to_copy) {
            return nullptr;
        }

        result->tm_utoff = ttisp->tt_utoff;
        result->time_index = i;
    }
    return result;
}

/*
** Given a year, a rule, and the offset from UT at the time that rule takes
** effect, calculate the year-relative time that rule takes effect.
*/

constexpr s64 transtime(const int year, const tzrule* const rulep,
    const s64 offset) {
    bool leapyear;
    s64 value;
    int i;
    int d, m1, yy0, yy1, yy2, dow;

    leapyear = isleap(year);
    switch (rulep->r_type) {
    case JULIAN_DAY:
        /*
        ** Jn - Julian day, 1 == January 1, 60 == March 1 even in leap
        ** years.
        ** In non-leap years, or if the day number is 59 or less, just
        ** add SECSPERDAY times the day number-1 to the time of
        ** January 1, midnight, to get the day.
        */
        value = (rulep->r_day - 1) * SECSPERDAY;
        if (leapyear && rulep->r_day >= 60) {
            value += SECSPERDAY;
        }
        break;

    case DAY_OF_YEAR:
        /*
        ** n - day of year.
        ** Just add SECSPERDAY times the day number to the time of
        ** January 1, midnight, to get the day.
        */
        value = rulep->r_day * SECSPERDAY;
        break;

    case MONTH_NTH_DAY_OF_WEEK:
        /*
        ** Mm.n.d - nth "dth day" of month m.
        */

        /*
        ** Use Zeller's Congruence to get day-of-week of first day of
        ** month.
        */
        m1 = (rulep->r_mon + 9) % 12 + 1;
        yy0 = (rulep->r_mon <= 2) ? (year - 1) : year;
        yy1 = yy0 / 100;
        yy2 = yy0 % 100;
        dow = ((26 * m1 - 2) / 10 + 1 + yy2 + yy2 / 4 + yy1 / 4 - 2 * yy1) % 7;
        if (dow < 0) {
            dow += DAYSPERWEEK;
        }

        /*
        ** "dow" is the day-of-week of the first day of the month. Get
        ** the day-of-month (zero-origin) of the first "dow" day of the
        ** month.
        */
        d = rulep->r_day - dow;
        if (d < 0) {
            d += DAYSPERWEEK;
        }
        for (i = 1; i < rulep->r_week; ++i) {
            if (d + DAYSPERWEEK >= mon_lengths[leapyear][rulep->r_mon - 1]) {
                break;
            }
            d += DAYSPERWEEK;
        }

        /*
        ** "d" is the day-of-month (zero-origin) of the day we want.
        */
        value = d * SECSPERDAY;
        for (i = 0; i < rulep->r_mon - 1; ++i) {
            value += mon_lengths[leapyear][i] * SECSPERDAY;
        }
        break;

    default:
        //UNREACHABLE();
        break;
    }

    /*
    ** "value" is the year-relative time of 00:00:00 UT on the day in
    ** question. To get the year-relative time of the specified local
    ** time on that day, add the transition time and the current offset
    ** from UT.
    */
    return value + rulep->r_time + offset;
}

bool tzparse(const char* name, Rule* sp) {
    const char* stdname{};
    const char* dstname{};
    s64 stdoffset;
    s64 dstoffset;
    char* cp;
    ptrdiff_t stdlen;
    ptrdiff_t dstlen{};
    ptrdiff_t charcnt;
    time_t atlo = TIME_T_MIN, leaplo = TIME_T_MIN;

    stdname = name;
    if (*name == '<') {
        name++;
        stdname = name;
        name = getqzname(name, '>');
        if (*name != '>') {
            return false;
        }
        stdlen = name - stdname;
        name++;
    }
    else {
        name = getzname(name);
        stdlen = name - stdname;
    }
    if (!(0 < stdlen && stdlen <= TZNAME_MAXIMUM)) {
        return false;
    }
    name = getoffset(name, &stdoffset);
    if (name == nullptr) {
        return false;
    }
    charcnt = stdlen + 1;
    if (charcnt > TZ_MAX_CHARS) {
        return false;
    }
    if (*name != '\0') {
        if (*name == '<') {
            dstname = ++name;
            name = getqzname(name, '>');
            if (*name != '>')
                return false;
            dstlen = name - dstname;
            name++;
        }
        else {
            dstname = name;
            name = getzname(name);
            dstlen = name - dstname; /* length of DST abbr. */
        }
        if (!(0 < dstlen && dstlen <= TZNAME_MAXIMUM)) {
            return false;
        }
        charcnt += dstlen + 1;
        if (charcnt > TZ_MAX_CHARS) {
            return false;
        }
        if (*name != '\0' && *name != ',' && *name != ';') {
            name = getoffset(name, &dstoffset);
            if (name == nullptr) {
                return false;
            }
        }
        else {
            dstoffset = stdoffset - SECSPERHOUR;
        }
        if (*name == '\0') {
            name = TZDEFRULESTRING;
        }
        if (*name == ',' || *name == ';') {
            struct tzrule start;
            struct tzrule end;
            int year;
            int timecnt;
            time_t janfirst;
            s64 janoffset = 0;
            int yearbeg, yearlim;

            ++name;
            if ((name = getrule(name, &start)) == nullptr) {
                return false;
            }
            if (*name++ != ',') {
                return false;
            }
            if ((name = getrule(name, &end)) == nullptr) {
                return false;
            }
            if (*name != '\0') {
                return false;
            }
            sp->typecnt = 2; /* standard time and DST */
            /*
            ** Two transitions per year, from EPOCH_YEAR forward.
            */
            init_ttinfo(&sp->ttis[0], -stdoffset, false, 0);
            init_ttinfo(&sp->ttis[1], -dstoffset, true, static_cast<s32>(stdlen + 1));
            sp->defaulttype = 0;
            timecnt = 0;
            janfirst = 0;
            yearbeg = EPOCH_YEAR;

            do {
                s64 yearsecs = year_lengths[isleap(yearbeg - 1)] * SECSPERDAY;
                yearbeg--;
                if (increment_overflow_time(&janfirst, -yearsecs)) {
                    janoffset = -yearsecs;
                    break;
                }
            } while (atlo < janfirst && EPOCH_YEAR - YEARSPERREPEAT / 2 < yearbeg);

            while (true) {
                s64 yearsecs = year_lengths[isleap(yearbeg)] * SECSPERDAY;
                int yearbeg1 = yearbeg;
                time_t janfirst1 = janfirst;
                if (increment_overflow_time(&janfirst1, yearsecs) ||
                    increment_overflow(&yearbeg1, 1) || atlo <= janfirst1) {
                    break;
                }
                yearbeg = yearbeg1;
                janfirst = janfirst1;
            }

            yearlim = yearbeg;
            if (increment_overflow(&yearlim, YEARSPERREPEAT + 1)) {
                yearlim = INT_MAX;
            }
            for (year = yearbeg; year < yearlim; year++) {
                s64 starttime = transtime(year, &start, stdoffset),
                    endtime = transtime(year, &end, dstoffset);
                s64 yearsecs = (year_lengths[isleap(year)] * SECSPERDAY);
                bool reversed = endtime < starttime;
                if (reversed) {
                    s64 swap = starttime;
                    starttime = endtime;
                    endtime = swap;
                }
                if (reversed || (starttime < endtime && endtime - starttime < yearsecs)) {
                    if (TZ_MAX_TIMES - 2 < timecnt) {
                        break;
                    }
                    sp->ats[timecnt] = janfirst;
                    if (!increment_overflow_time(reinterpret_cast<time_t*>(&sp->ats[timecnt]), janoffset + starttime) &&
                        atlo <= sp->ats[timecnt]) {
                        sp->types[timecnt++] = !reversed;
                    }
                    sp->ats[timecnt] = janfirst;
                    if (!increment_overflow_time(reinterpret_cast<time_t*>(&sp->ats[timecnt]), janoffset + endtime) &&
                        atlo <= sp->ats[timecnt]) {
                        sp->types[timecnt++] = reversed;
                    }
                }
                if (endtime < leaplo) {
                    yearlim = year;
                    if (increment_overflow(&yearlim, YEARSPERREPEAT + 1)) {
                        yearlim = INT_MAX;
                    }
                }
                if (increment_overflow_time(&janfirst, janoffset + yearsecs)) {
                    break;
                }
                janoffset = 0;
            }
            sp->timecnt = timecnt;
            if (!timecnt) {
                sp->ttis[0] = sp->ttis[1];
                sp->typecnt = 1; /* Perpetual DST.  */
            }
            else if (YEARSPERREPEAT < year - yearbeg) {
                sp->goback = sp->goahead = true;
            }
        }
        else {
            s64 theirstdoffset;
            s64 theirdstoffset;
            s64 theiroffset;
            bool isdst;
            int i;
            int j;

            if (*name != '\0') {
                return false;
            }
            /*
            ** Initial values of theirstdoffset and theirdstoffset.
            */
            theirstdoffset = 0;
            for (i = 0; i < sp->timecnt; ++i) {
                j = sp->types[i];
                if (!sp->ttis[j].tt_isdst) {
                    theirstdoffset = -sp->ttis[j].tt_utoff;
                    break;
                }
            }
            theirdstoffset = 0;
            for (i = 0; i < sp->timecnt; ++i) {
                j = sp->types[i];
                if (sp->ttis[j].tt_isdst) {
                    theirdstoffset = -sp->ttis[j].tt_utoff;
                    break;
                }
            }
            /*
            ** Initially we're assumed to be in standard time.
            */
            isdst = false;
            /*
            ** Now juggle transition times and types
            ** tracking offsets as you do.
            */
            for (i = 0; i < sp->timecnt; ++i) {
                j = sp->types[i];
                sp->types[i] = sp->ttis[j].tt_isdst;
                if (sp->ttis[j].tt_ttisut) {
                    /* No adjustment to transition time */
                }
                else {
                    /*
                    ** If daylight saving time is in
                    ** effect, and the transition time was
                    ** not specified as standard time, add
                    ** the daylight saving time offset to
                    ** the transition time; otherwise, add
                    ** the standard time offset to the
                    ** transition time.
                    */
                    /*
                    ** Transitions from DST to DDST
                    ** will effectively disappear since
                    ** POSIX provides for only one DST
                    ** offset.
                    */
                    if (isdst && !sp->ttis[j].tt_ttisstd) {
                        sp->ats[i] += dstoffset - theirdstoffset;
                    }
                    else {
                        sp->ats[i] += stdoffset - theirstdoffset;
                    }
                }
                theiroffset = -sp->ttis[j].tt_utoff;
                if (sp->ttis[j].tt_isdst) {
                    theirdstoffset = theiroffset;
                }
                else {
                    theirstdoffset = theiroffset;
                }
            }
            /*
            ** Finally, fill in ttis.
            */
            init_ttinfo(&sp->ttis[0], -stdoffset, false, 0);
            init_ttinfo(&sp->ttis[1], -dstoffset, true, static_cast<s32>(stdlen + 1));
            sp->typecnt = 2;
            sp->defaulttype = 0;
        }
    }
    else {
        dstlen = 0;
        sp->typecnt = 1; /* only standard time */
        sp->timecnt = 0;
        init_ttinfo(&sp->ttis[0], -stdoffset, false, 0);
        sp->defaulttype = 0;
    }
    sp->charcnt = static_cast<s32>(charcnt);
    cp = &sp->chars[0];
    memcpy(cp, stdname, stdlen);
    cp += stdlen;
    *cp++ = '\0';
    if (dstlen != 0) {
        memcpy(cp, dstname, dstlen);
        *(cp + dstlen) = '\0';
    }
    return true;
}

int tzloadbody(Rule* sp, local_storage& local_storage) {
    int i;
    int stored;
    size_t nread{ local_storage.binary.size_bytes() };
    int tzheadsize = sizeof(struct TzifHeader);
    TzifHeader header{};

    //ASSERT(local_storage.binary.size_bytes() >= sizeof(TzifHeader));
    std::memcpy(&header, local_storage.binary.data(), sizeof(TzifHeader));

    sp->goback = sp->goahead = false;

    for (stored = 8; stored <= 8; stored *= 2) {
        s64 datablock_size;
        s32 ttisstdcnt = detzcode(header.tzh_ttisstdcnt.data());
        s32 ttisutcnt = detzcode(header.tzh_ttisutcnt.data());
        s32 leapcnt = detzcode(header.tzh_leapcnt.data());
        s32 timecnt = detzcode(header.tzh_timecnt.data());
        s32 typecnt = detzcode(header.tzh_typecnt.data());
        s32 charcnt = detzcode(header.tzh_charcnt.data());
        /* Although tzfile(5) currently requires typecnt to be nonzero,
            support future formats that may allow zero typecnt
            in files that have a TZ string and no transitions.  */
        if (!(0 <= leapcnt && leapcnt < TZ_MAX_LEAPS && 0 <= typecnt && typecnt < TZ_MAX_TYPES &&
            0 <= timecnt && timecnt < TZ_MAX_TIMES && 0 <= charcnt && charcnt < TZ_MAX_CHARS &&
            0 <= ttisstdcnt && ttisstdcnt < TZ_MAX_TYPES && 0 <= ttisutcnt &&
            ttisutcnt < TZ_MAX_TYPES)) {
            return EINVAL;
        }
        datablock_size = (timecnt * stored         /* ats */
            + timecnt                /* types */
            + typecnt * 6            /* ttinfos */
            + charcnt                /* chars */
            + leapcnt * (stored + 4) /* lsinfos */
            + ttisstdcnt             /* ttisstds */
            + ttisutcnt);            /* ttisuts */
        if (static_cast<s32>(local_storage.binary.size_bytes()) < tzheadsize + datablock_size) {
            return EINVAL;
        }
        if (!((ttisstdcnt == typecnt || ttisstdcnt == 0) &&
            (ttisutcnt == typecnt || ttisutcnt == 0))) {
            return EINVAL;
        }

        char const* p = (const char*)local_storage.binary.data() + tzheadsize;

        sp->timecnt = timecnt;
        sp->typecnt = typecnt;
        sp->charcnt = charcnt;

        /* Read transitions, discarding those out of time_t range.
            But pretend the last transition before TIME_T_MIN
            occurred at TIME_T_MIN.  */
        timecnt = 0;
        for (i = 0; i < sp->timecnt; ++i) {
            int_fast64_t at = stored == 4 ? detzcode(p) : detzcode64(p);
            sp->types[i] = at <= TIME_T_MAX;
            if (sp->types[i]) {
                time_t attime =
                    ((std::is_signed_v<time_t> ? at < TIME_T_MIN : at < 0) ? TIME_T_MIN : at);
                if (timecnt && attime <= sp->ats[timecnt - 1]) {
                    if (attime < sp->ats[timecnt - 1])
                        return EINVAL;
                    sp->types[i - 1] = 0;
                    timecnt--;
                }
                sp->ats[timecnt++] = attime;
            }
            p += stored;
        }

        timecnt = 0;
        for (i = 0; i < sp->timecnt; ++i) {
            unsigned char typ = *p++;
            if (sp->typecnt <= typ) {
                return EINVAL;
            }
            if (sp->types[i]) {
                sp->types[timecnt++] = typ;
            }
        }
        sp->timecnt = timecnt;
        for (i = 0; i < sp->typecnt; ++i) {
            struct ttinfo* ttisp;
            unsigned char isdst, desigidx;

            ttisp = &sp->ttis[i];
            ttisp->tt_utoff = detzcode(p);
            p += 4;
            isdst = *p++;
            if (!(isdst < 2)) {
                return EINVAL;
            }
            ttisp->tt_isdst = isdst != 0;
            desigidx = *p++;
            if (!(desigidx < sp->charcnt)) {
                return EINVAL;
            }
            ttisp->tt_desigidx = desigidx;
        }
        for (i = 0; i < sp->charcnt; ++i) {
            sp->chars[i] = *p++;
        }
        /* Ensure '\0'-terminated, and make it safe to call
            ttunspecified later.  */
        memset(&sp->chars[i], 0, CHARS_EXTRA);

        for (i = 0; i < sp->typecnt; ++i) {
            struct ttinfo* ttisp;

            ttisp = &sp->ttis[i];
            if (ttisstdcnt == 0) {
                ttisp->tt_ttisstd = false;
            }
            else {
                if (*(bool*)p != true && *(bool*)p != false) {
                    return EINVAL;
                }
                ttisp->tt_ttisstd = *(bool*)p++;
            }
        }
        for (i = 0; i < sp->typecnt; ++i) {
            struct ttinfo* ttisp;

            ttisp = &sp->ttis[i];
            if (ttisutcnt == 0) {
                ttisp->tt_ttisut = false;
            }
            else {
                if (*(bool*)p != true && *(bool*)p != false) {
                    return EINVAL;
                }
                ttisp->tt_ttisut = *(bool*)p++;
            }
        }

        nread += (ptrdiff_t)local_storage.binary.data() - (ptrdiff_t)p;
        if (nread < 0) {
            return EINVAL;
        }
    }

    std::array<char, 256> buf{};
    if (nread > buf.size()) {
        //ASSERT(false);
        return EINVAL;
    }
    memmove(buf.data(), &local_storage.binary[local_storage.binary.size_bytes() - nread], nread);

    if (nread > 2 && buf[0] == '\n' && buf[nread - 1] == '\n' && sp->typecnt + 2 <= TZ_MAX_TYPES) {
        Rule* ts = &local_storage.state;

        buf[nread - 1] = '\0';
        if (tzparse(&buf[1], ts) && local_storage.state.typecnt == 2) {

            /* Attempt to reuse existing abbreviations.
                Without this, America/Anchorage would be right on
                the edge after 2037 when TZ_MAX_CHARS is 50, as
                sp->charcnt equals 40 (for LMT AST AWT APT AHST
                AHDT YST AKDT AKST) and ts->charcnt equals 10
                (for AKST AKDT).  Reusing means sp->charcnt can
                stay 40 in this example.  */
            int gotabbr = 0;
            int charcnt = sp->charcnt;
            for (i = 0; i < ts->typecnt; i++) {
                char* tsabbr = &ts->chars[ts->ttis[i].tt_desigidx];
                int j;
                for (j = 0; j < charcnt; j++)
                    if (strcmp(&sp->chars[j], tsabbr) == 0) {
                        ts->ttis[i].tt_desigidx = j;
                        gotabbr++;
                        break;
                    }
                if (!(j < charcnt)) {
                    int tsabbrlen = static_cast<s32>(strlen(tsabbr));
                    if (j + tsabbrlen < TZ_MAX_CHARS) {
                        strcpy(&sp->chars[j], tsabbr);
                        charcnt = j + tsabbrlen + 1;
                        ts->ttis[i].tt_desigidx = j;
                        gotabbr++;
                    }
                }
            }
            if (gotabbr == ts->typecnt) {
                sp->charcnt = charcnt;

                /* Ignore any trailing, no-op transitions generated
                    by zic as they don't help here and can run afoul
                    of bugs in zic 2016j or earlier.  */
                while (1 < sp->timecnt &&
                    (sp->types[sp->timecnt - 1] == sp->types[sp->timecnt - 2])) {
                    sp->timecnt--;
                }

                for (i = 0; i < ts->timecnt && sp->timecnt < TZ_MAX_TIMES; i++) {
                    time_t t = ts->ats[i];
                    if (0 < sp->timecnt && t <= sp->ats[sp->timecnt - 1]) {
                        continue;
                    }
                    sp->ats[sp->timecnt] = t;
                    sp->types[sp->timecnt] = static_cast<u8>(sp->typecnt + ts->types[i]);
                    sp->timecnt++;
                }
                for (i = 0; i < ts->typecnt; i++) {
                    sp->ttis[sp->typecnt++] = ts->ttis[i];
                }
            }
        }
    }
    if (sp->typecnt == 0) {
        return EINVAL;
    }

    if (sp->timecnt > 1) {
        if (sp->ats[0] <= TIME_T_MAX - SECSPERREPEAT) {
            time_t repeatat = sp->ats[0] + SECSPERREPEAT;
            int repeattype = sp->types[0];
            for (i = 1; i < sp->timecnt; ++i) {
                if (sp->ats[i] == repeatat && typesequiv(sp, sp->types[i], repeattype)) {
                    sp->goback = true;
                    break;
                }
            }
        }
        if (TIME_T_MIN + SECSPERREPEAT <= sp->ats[sp->timecnt - 1]) {
            time_t repeatat = sp->ats[sp->timecnt - 1] - SECSPERREPEAT;
            int repeattype = sp->types[sp->timecnt - 1];
            for (i = sp->timecnt - 2; i >= 0; --i) {
                if (sp->ats[i] == repeatat && typesequiv(sp, sp->types[i], repeattype)) {
                    sp->goahead = true;
                    break;
                }
            }
        }
    }

    /* Infer sp->defaulttype from the data.  Although this default
        type is always zero for data from recent tzdb releases,
        things are trickier for data from tzdb 2018e or earlier.

        The first set of heuristics work around bugs in 32-bit data
        generated by tzdb 2013c or earlier.  The workaround is for
        zones like Australia/Macquarie where timestamps before the
        first transition have a time type that is not the earliest
        standard-time type.  See:
        https://mm.icann.org/pipermail/tz/2013-May/019368.html */
    /*
    ** If type 0 does not specify local time, or is unused in transitions,
    ** it's the type to use for early times.
    */
    for (i = 0; i < sp->timecnt; ++i) {
        if (sp->types[i] == 0) {
            break;
        }
    }
    i = i < sp->timecnt && !ttunspecified(sp, 0) ? -1 : 0;
    /*
    ** Absent the above,
    ** if there are transition times
    ** and the first transition is to a daylight time
    ** find the standard type less than and closest to
    ** the type of the first transition.
    */
    if (i < 0 && sp->timecnt > 0 && sp->ttis[sp->types[0]].tt_isdst) {
        i = sp->types[0];
        while (--i >= 0) {
            if (!sp->ttis[i].tt_isdst) {
                break;
            }
        }
    }
    /* The next heuristics are for data generated by tzdb 2018e or
        earlier, for zones like EST5EDT where the first transition
        is to DST.  */
    /*
    ** If no result yet, find the first standard type.
    ** If there is none, punt to type zero.
    */
    if (i < 0) {
        i = 0;
        while (sp->ttis[i].tt_isdst) {
            if (++i >= sp->typecnt) {
                i = 0;
                break;
            }
        }
    }
    /* A simple 'sp->defaulttype = 0;' would suffice here if we
        didn't have to worry about 2018e-or-earlier data.  Even
        simpler would be to remove the defaulttype member and just
        use 0 in its place.  */
    sp->defaulttype = i;

    return 0;
}

constexpr int tmcomp(const CalendarTimeInternal* const atmp,
    const CalendarTimeInternal* const btmp) {
    int result;

    if (atmp->tm_year != btmp->tm_year) {
        return atmp->tm_year < btmp->tm_year ? -1 : 1;
    }
    if ((result = (atmp->tm_mon - btmp->tm_mon)) == 0 &&
        (result = (atmp->tm_mday - btmp->tm_mday)) == 0 &&
        (result = (atmp->tm_hour - btmp->tm_hour)) == 0 &&
        (result = (atmp->tm_min - btmp->tm_min)) == 0) {
        result = atmp->tm_sec - btmp->tm_sec;
    }
    return result;
}

/* Copy to *DEST from *SRC.  Copy only the members needed for mktime,
    as other members might not be initialized.  */
constexpr void mktmcpy(struct CalendarTimeInternal* dest, struct CalendarTimeInternal const* src) {
    dest->tm_sec = src->tm_sec;
    dest->tm_min = src->tm_min;
    dest->tm_hour = src->tm_hour;
    dest->tm_mday = src->tm_mday;
    dest->tm_mon = src->tm_mon;
    dest->tm_year = src->tm_year;
    dest->tm_isdst = src->tm_isdst;
    dest->tm_zone = src->tm_zone;
    dest->tm_utoff = src->tm_utoff;
    dest->time_index = src->time_index;
}

constexpr bool normalize_overflow(int* const tensptr, int* const unitsptr, const int base) {
    int tensdelta;

    tensdelta = (*unitsptr >= 0) ? (*unitsptr / base) : (-1 - (-1 - *unitsptr) / base);
    *unitsptr -= tensdelta * base;
    return increment_overflow(tensptr, tensdelta);
}

constexpr bool normalize_overflow32(s64* tensptr, int* unitsptr, int base) {
    int tensdelta;

    tensdelta = (*unitsptr >= 0) ? (*unitsptr / base) : (-1 - (-1 - *unitsptr) / base);
    *unitsptr -= tensdelta * base;
    return increment_overflow32(tensptr, tensdelta);
}

int time2sub(time_t* out_time, CalendarTimeInternal* const tmp,
    CalendarTimeInternal* (*funcp)(Rule const*, time_t const*, s64,
        CalendarTimeInternal*),
    Rule const* sp, const s64 offset, bool* okayp, bool do_norm_secs) {
    int dir;
    int i, j;
    int saved_seconds;
    s64 li;
    time_t lo;
    time_t hi;
    s64 y;
    time_t newt;
    time_t t;
    CalendarTimeInternal yourtm, mytm;

    *okayp = false;
    mktmcpy(&yourtm, tmp);

    if (do_norm_secs) {
        if (normalize_overflow(&yourtm.tm_min, &yourtm.tm_sec, SECSPERMIN)) {
            return 1;
        }
    }
    if (normalize_overflow(&yourtm.tm_hour, &yourtm.tm_min, MINSPERHOUR)) {
        return 1;
    }
    if (normalize_overflow(&yourtm.tm_mday, &yourtm.tm_hour, HOURSPERDAY)) {
        return 1;
    }
    y = yourtm.tm_year;
    if (normalize_overflow32(&y, &yourtm.tm_mon, MONSPERYEAR)) {
        return 1;
    }
    /*
    ** Turn y into an actual year number for now.
    ** It is converted back to an offset from TM_YEAR_BASE later.
    */
    if (increment_overflow32(&y, TM_YEAR_BASE)) {
        return 1;
    }
    while (yourtm.tm_mday <= 0) {
        if (increment_overflow32(&y, -1)) {
            return 1;
        }
        li = y + (1 < yourtm.tm_mon);
        yourtm.tm_mday += year_lengths[isleap(li)];
    }
    while (yourtm.tm_mday > DAYSPERLYEAR) {
        li = y + (1 < yourtm.tm_mon);
        yourtm.tm_mday -= year_lengths[isleap(li)];
        if (increment_overflow32(&y, 1)) {
            return 1;
        }
    }
    for (;;) {
        i = mon_lengths[isleap(y)][yourtm.tm_mon];
        if (yourtm.tm_mday <= i) {
            break;
        }
        yourtm.tm_mday -= i;
        if (++yourtm.tm_mon >= MONSPERYEAR) {
            yourtm.tm_mon = 0;
            if (increment_overflow32(&y, 1)) {
                return 1;
            }
        }
    }

    if (increment_overflow32(&y, -TM_YEAR_BASE)) {
        return 1;
    }
    if (!(INT_MIN <= y && y <= INT_MAX)) {
        return 1;
    }
    yourtm.tm_year = static_cast<s32>(y);

    if (yourtm.tm_sec >= 0 && yourtm.tm_sec < SECSPERMIN) {
        saved_seconds = 0;
    }
    else if (yourtm.tm_year < EPOCH_YEAR - TM_YEAR_BASE) {
        /*
        ** We can't set tm_sec to 0, because that might push the
        ** time below the minimum representable time.
        ** Set tm_sec to 59 instead.
        ** This assumes that the minimum representable time is
        ** not in the same minute that a leap second was deleted from,
        ** which is a safer assumption than using 58 would be.
        */
        if (increment_overflow(&yourtm.tm_sec, 1 - SECSPERMIN)) {
            return 1;
        }
        saved_seconds = yourtm.tm_sec;
        yourtm.tm_sec = SECSPERMIN - 1;
    }
    else {
        saved_seconds = yourtm.tm_sec;
        yourtm.tm_sec = 0;
    }
    /*
    ** Do a binary search (this works whatever time_t's type is).
    */
    lo = TIME_T_MIN;
    hi = TIME_T_MAX;
    for (;;) {
        t = lo / 2 + hi / 2;
        if (t < lo) {
            t = lo;
        }
        else if (t > hi) {
            t = hi;
        }
        if (!funcp(sp, &t, offset, &mytm)) {
            /*
            ** Assume that t is too extreme to be represented in
            ** a struct tm; arrange things so that it is less
            ** extreme on the next pass.
            */
            dir = (t > 0) ? 1 : -1;
        }
        else {
            dir = tmcomp(&mytm, &yourtm);
        }
        if (dir != 0) {
            if (t == lo) {
                if (t == TIME_T_MAX) {
                    return 2;
                }
                ++t;
                ++lo;
            }
            else if (t == hi) {
                if (t == TIME_T_MIN) {
                    return 2;
                }
                --t;
                --hi;
            }
            if (lo > hi) {
                return 2;
            }
            if (dir > 0) {
                hi = t;
            }
            else {
                lo = t;
            }
            continue;
        }

        if (yourtm.tm_isdst < 0 || mytm.tm_isdst == yourtm.tm_isdst) {
            break;
        }
        /*
        ** Right time, wrong type.
        ** Hunt for right time, right type.
        ** It's okay to guess wrong since the guess
        ** gets checked.
        */
        if (sp == nullptr) {
            return 2;
        }
        for (i = sp->typecnt - 1; i >= 0; --i) {
            if (sp->ttis[i].tt_isdst != static_cast<bool>(yourtm.tm_isdst)) {
                continue;
            }
            for (j = sp->typecnt - 1; j >= 0; --j) {
                if (sp->ttis[j].tt_isdst == static_cast<bool>(yourtm.tm_isdst)) {
                    continue;
                }
                if (ttunspecified(sp, j)) {
                    continue;
                }
                newt = (t + sp->ttis[j].tt_utoff - sp->ttis[i].tt_utoff);
                if (!funcp(sp, &newt, offset, &mytm)) {
                    continue;
                }
                if (tmcomp(&mytm, &yourtm) != 0) {
                    continue;
                }
                if (mytm.tm_isdst != yourtm.tm_isdst) {
                    continue;
                }
                /*
                ** We have a match.
                */
                t = newt;
                goto label;
            }
        }
        return 2;
    }
label:
    newt = t + saved_seconds;
    t = newt;
    if (funcp(sp, &t, offset, tmp) || *okayp) {
        *okayp = true;
        *out_time = t;
        return 0;
    }
    return 2;
}

int time2(time_t* out_time, struct CalendarTimeInternal* const tmp,
    struct CalendarTimeInternal* (*funcp)(struct Rule const*, time_t const*, s64,
        struct CalendarTimeInternal*),
    struct Rule const* sp, const s64 offset, bool* okayp) {
    int res;

    /*
    ** First try without normalization of seconds
    ** (in case tm_sec contains a value associated with a leap second).
    ** If that fails, try with normalization of seconds.
    */
    res = time2sub(out_time, tmp, funcp, sp, offset, okayp, false);
    return *okayp ? res : time2sub(out_time, tmp, funcp, sp, offset, okayp, true);
}

int time1(time_t* out_time, CalendarTimeInternal* const tmp,
    CalendarTimeInternal* (*funcp)(Rule const*, time_t const*, s64,
        CalendarTimeInternal*),
    Rule const* sp, const s64 offset) {
    int samei, otheri;
    int sameind, otherind;
    int i;
    int nseen;
    char seen[TZ_MAX_TYPES];
    unsigned char types[TZ_MAX_TYPES];
    bool okay;

    if (tmp->tm_isdst > 1) {
        tmp->tm_isdst = 1;
    }
    auto res = time2(out_time, tmp, funcp, sp, offset, &okay);
    if (res == 0) {
        return res;
    }
    if (tmp->tm_isdst < 0) {
        return res;
    }
    /*
    ** We're supposed to assume that somebody took a time of one type
    ** and did some math on it that yielded a "struct tm" that's bad.
    ** We try to divine the type they started from and adjust to the
    ** type they need.
    */
    for (i = 0; i < sp->typecnt; ++i) {
        seen[i] = false;
    }

    if (sp->timecnt < 1) {
        return 2;
    }

    nseen = 0;
    for (i = sp->timecnt - 1; i >= 0; --i) {
        if (!seen[sp->types[i]] && !ttunspecified(sp, sp->types[i])) {
            seen[sp->types[i]] = true;
            types[nseen++] = sp->types[i];
        }
    }

    if (nseen < 1) {
        return 2;
    }

    for (sameind = 0; sameind < nseen; ++sameind) {
        samei = types[sameind];
        if (sp->ttis[samei].tt_isdst != static_cast<bool>(tmp->tm_isdst)) {
            continue;
        }
        for (otherind = 0; otherind < nseen; ++otherind) {
            otheri = types[otherind];
            if (sp->ttis[otheri].tt_isdst == static_cast<bool>(tmp->tm_isdst)) {
                continue;
            }
            tmp->tm_sec += (sp->ttis[otheri].tt_utoff - sp->ttis[samei].tt_utoff);
            tmp->tm_isdst = !tmp->tm_isdst;
            res = time2(out_time, tmp, funcp, sp, offset, &okay);
            if (res == 0) {
                return res;
            }
            tmp->tm_sec -= (sp->ttis[otheri].tt_utoff - sp->ttis[samei].tt_utoff);
            tmp->tm_isdst = !tmp->tm_isdst;
        }
    }
    return 2;
}

} // namespace

s32 ParseTimeZoneBinary(Rule& out_rule, std::span<const u8> binary) {
    tzloadbody_local_storage.binary = binary;
    if (tzloadbody(&out_rule, tzloadbody_local_storage)) {
        return 3;
    }
    return 0;
}

bool localtime_rz(CalendarTimeInternal* tmp, Rule const* sp, time_t* timep) {
    return localsub(sp, timep, 0, tmp) == nullptr;
}

u32 mktime_tzname(time_t* out_time, Rule const* sp, CalendarTimeInternal* tmp) {
    return time1(out_time, tmp, localsub, sp, 0);
}

} // namespace Tz
