#include "timezone.h"

#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace
{
struct TimezoneInfo
{
  const char *label;        // short user-facing label
  const char *iana;         // canonical IANA timezone name
  const char *posix;        // POSIX TZ rule used by setenv("TZ", ...)
  const char *const *aliases;
};

// -----------------------------------------------------------------------------
// Alias lists
// -----------------------------------------------------------------------------
static const char *const kAliasesUtc[] = {
    "UTC",
    "Etc/UTC",
    "Etc/GMT",
    nullptr};

static const char *const kAliasesUsEastern[] = {
    "America/New_York",
    "America/Detroit",
    "America/Indiana/Indianapolis",
    "America/Indiana/Marengo",
    "America/Indiana/Petersburg",
    "America/Indiana/Vevay",
    "America/Indiana/Vincennes",
    "America/Indiana/Winamac",
    "America/Kentucky/Louisville",
    "America/Kentucky/Monticello",
    "America/Toronto",
    "America/Nassau",
    nullptr};

static const char *const kAliasesUsCentral[] = {
    "America/Chicago",
    "America/Indiana/Knox",
    "America/Indiana/Tell_City",
    "America/Menominee",
    "America/North_Dakota/Beulah",
    "America/North_Dakota/Center",
    "America/North_Dakota/New_Salem",
    "America/Winnipeg",
    nullptr};

static const char *const kAliasesUsMountain[] = {
    "America/Denver",
    "America/Boise",
    "America/Edmonton",
    nullptr};

static const char *const kAliasesUsArizona[] = {
    "America/Phoenix",
    nullptr};

static const char *const kAliasesUsPacific[] = {
    "America/Los_Angeles",
    "America/Vancouver",
    nullptr};

static const char *const kAliasesUsAlaska[] = {
    "America/Anchorage",
    "America/Juneau",
    "America/Nome",
    "America/Sitka",
    "America/Yakutat",
    "America/Metlakatla",
    nullptr};

static const char *const kAliasesUsHawaii[] = {
    "Pacific/Honolulu",
    nullptr};

static const char *const kAliasesAtlanticCanada[] = {
    "America/Halifax",
    "America/Glace_Bay",
    "America/Moncton",
    "America/Goose_Bay",
    nullptr};

static const char *const kAliasesNewfoundland[] = {
    "America/St_Johns",
    nullptr};

static const char *const kAliasesMexicoCentral[] = {
    "America/Mexico_City",
    "America/Merida",
    "America/Monterrey",
    nullptr};

static const char *const kAliasesBrazil[] = {
    "America/Sao_Paulo",
    "America/Recife",
    nullptr};

static const char *const kAliasesArgentina[] = {
    "America/Argentina/Buenos_Aires",
    "America/Buenos_Aires",
    nullptr};

static const char *const kAliasesChile[] = {
    "America/Santiago",
    nullptr};

static const char *const kAliasesColombia[] = {
    "America/Bogota",
    "America/Lima",
    "America/Guayaquil",
    nullptr};

static const char *const kAliasesVenezuela[] = {
    "America/Caracas",
    nullptr};

static const char *const kAliasesUk[] = {
    "Europe/London",
    nullptr};

static const char *const kAliasesEuropeCentral[] = {
    "Europe/Paris",
    "Europe/Berlin",
    "Europe/Rome",
    "Europe/Madrid",
    "Europe/Amsterdam",
    "Europe/Brussels",
    "Europe/Vienna",
    "Europe/Zurich",
    "Europe/Prague",
    "Europe/Warsaw",
    "Europe/Stockholm",
    "Europe/Copenhagen",
    "Europe/Oslo",
    "Europe/Budapest",
    "Europe/Luxembourg",
    nullptr};

static const char *const kAliasesEuropeEastern[] = {
    "Europe/Helsinki",
    "Europe/Athens",
    "Europe/Bucharest",
    "Europe/Kyiv",
    "Europe/Sofia",
    "Europe/Riga",
    "Europe/Tallinn",
    "Europe/Vilnius",
    "Europe/Chisinau",
    nullptr};

static const char *const kAliasesMoscow[] = {
    "Europe/Moscow",
    nullptr};

static const char *const kAliasesTurkey[] = {
    "Europe/Istanbul",
    nullptr};

static const char *const kAliasesSouthAfrica[] = {
    "Africa/Johannesburg",
    "Africa/Maseru",
    "Africa/Mbabane",
    nullptr};

static const char *const kAliasesEastAfrica[] = {
    "Africa/Nairobi",
    "Africa/Addis_Ababa",
    "Africa/Kampala",
    "Africa/Dar_es_Salaam",
    nullptr};

static const char *const kAliasesWestAfrica[] = {
    "Africa/Lagos",
    "Africa/Luanda",
    "Africa/Douala",
    "Africa/Ndjamena",
    "Africa/Algiers",
    "Africa/Tunis",
    nullptr};

static const char *const kAliasesArabia[] = {
    "Asia/Riyadh",
    "Asia/Baghdad",
    "Asia/Qatar",
    "Asia/Bahrain",
    "Asia/Kuwait",
    nullptr};

static const char *const kAliasesGulf[] = {
    "Asia/Dubai",
    "Asia/Muscat",
    nullptr};

static const char *const kAliasesIran[] = {
    "Asia/Tehran",
    nullptr};

static const char *const kAliasesPakistan[] = {
    "Asia/Karachi",
    nullptr};

static const char *const kAliasesIndia[] = {
    "Asia/Kolkata",
    "Asia/Calcutta",
    nullptr};

static const char *const kAliasesBangladesh[] = {
    "Asia/Dhaka",
    nullptr};

static const char *const kAliasesThailand[] = {
    "Asia/Bangkok",
    "Asia/Ho_Chi_Minh",
    "Asia/Phnom_Penh",
    "Asia/Vientiane",
    nullptr};

static const char *const kAliasesSingapore[] = {
    "Asia/Singapore",
    "Asia/Kuala_Lumpur",
    nullptr};

static const char *const kAliasesPhilippines[] = {
    "Asia/Manila",
    nullptr};

static const char *const kAliasesChina[] = {
    "Asia/Shanghai",
    nullptr};

static const char *const kAliasesHongKong[] = {
    "Asia/Hong_Kong",
    "Asia/Macau",
    nullptr};

static const char *const kAliasesJapan[] = {
    "Asia/Tokyo",
    nullptr};

static const char *const kAliasesKorea[] = {
    "Asia/Seoul",
    nullptr};

static const char *const kAliasesIndonesiaWest[] = {
    "Asia/Jakarta",
    "Asia/Pontianak",
    nullptr};

static const char *const kAliasesSydney[] = {
    "Australia/Sydney",
    "Australia/Melbourne",
    "Australia/Hobart",
    "Australia/Currie",
    "Antarctica/Macquarie",
    nullptr};

static const char *const kAliasesBrisbane[] = {
    "Australia/Brisbane",
    "Australia/Lindeman",
    nullptr};

static const char *const kAliasesAdelaide[] = {
    "Australia/Adelaide",
    "Australia/Broken_Hill",
    nullptr};

static const char *const kAliasesPerth[] = {
    "Australia/Perth",
    nullptr};

static const char *const kAliasesNewZealand[] = {
    "Pacific/Auckland",
    nullptr};

// -----------------------------------------------------------------------------
// Canonical timezone table
// NOTE: keep US/Central at index 2 for release-safe compatibility.
// -----------------------------------------------------------------------------
static const TimezoneInfo kTimezones[] = {
    {"UTC", "UTC", "UTC0", kAliasesUtc},

    {"US/Eastern", "America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2", kAliasesUsEastern},
    {"US/Central", "America/Chicago", "CST6CDT,M3.2.0/2,M11.1.0/2", kAliasesUsCentral},
    {"US/Mountain", "America/Denver", "MST7MDT,M3.2.0/2,M11.1.0/2", kAliasesUsMountain},
    {"US/Arizona", "America/Phoenix", "MST7", kAliasesUsArizona},
    {"US/Pacific", "America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/2", kAliasesUsPacific},
    {"US/Alaska", "America/Anchorage", "AKST9AKDT,M3.2.0/2,M11.1.0/2", kAliasesUsAlaska},
    {"US/Hawaii", "Pacific/Honolulu", "HST10", kAliasesUsHawaii},
    {"Canada/Atlantic", "America/Halifax", "AST4ADT,M3.2.0/2,M11.1.0/2", kAliasesAtlanticCanada},
    {"Canada/Newfoundland", "America/St_Johns", "NST3:30NDT,M3.2.0/2,M11.1.0/2", kAliasesNewfoundland},

    {"Mexico/Central", "America/Mexico_City", "CST6", kAliasesMexicoCentral},
    {"Brazil", "America/Sao_Paulo", "BRT3", kAliasesBrazil},
    {"Argentina", "America/Argentina/Buenos_Aires", "ART3", kAliasesArgentina},
    {"Chile", "America/Santiago", "CLT4CLST,M9.1.6/24,M4.1.6/24", kAliasesChile},
    {"Colombia/Peru", "America/Bogota", "COT5", kAliasesColombia},
    {"Venezuela", "America/Caracas", "VET4:30", kAliasesVenezuela},

    {"Europe/UK", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0/2", kAliasesUk},
    {"Europe/Central", "Europe/Paris", "CET-1CEST,M3.5.0/2,M10.5.0/3", kAliasesEuropeCentral},
    {"Europe/Eastern", "Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4", kAliasesEuropeEastern},
    {"Europe/Moscow", "Europe/Moscow", "MSK-3", kAliasesMoscow},
    {"Europe/Turkey", "Europe/Istanbul", "TRT-3", kAliasesTurkey},

    {"Africa/South", "Africa/Johannesburg", "SAST-2", kAliasesSouthAfrica},
    {"Africa/East", "Africa/Nairobi", "EAT-3", kAliasesEastAfrica},
    {"Africa/West", "Africa/Lagos", "WAT-1", kAliasesWestAfrica},

    {"Arabia", "Asia/Riyadh", "AST-3", kAliasesArabia},
    {"Gulf/UAE", "Asia/Dubai", "GST-4", kAliasesGulf},
    {"Iran", "Asia/Tehran", "IRST-3:30", kAliasesIran},
    {"Pakistan", "Asia/Karachi", "PKT-5", kAliasesPakistan},
    {"India", "Asia/Kolkata", "IST-5:30", kAliasesIndia},
    {"Bangladesh", "Asia/Dhaka", "BST-6", kAliasesBangladesh},

    {"Thailand", "Asia/Bangkok", "ICT-7", kAliasesThailand},
    {"Singapore", "Asia/Singapore", "SGT-8", kAliasesSingapore},
    {"Philippines", "Asia/Manila", "PST-8", kAliasesPhilippines},
    {"China", "Asia/Shanghai", "CST-8", kAliasesChina},
    {"Hong Kong", "Asia/Hong_Kong", "HKT-8", kAliasesHongKong},
    {"Japan", "Asia/Tokyo", "JST-9", kAliasesJapan},
    {"Korea", "Asia/Seoul", "KST-9", kAliasesKorea},
    {"Indonesia/WIB", "Asia/Jakarta", "WIB-7", kAliasesIndonesiaWest},

    {"Australia/Sydney", "Australia/Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3", kAliasesSydney},
    {"Australia/Brisbane", "Australia/Brisbane", "AEST-10", kAliasesBrisbane},
    {"Australia/Adelaide", "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3", kAliasesAdelaide},
    {"Australia/Perth", "Australia/Perth", "AWST-8", kAliasesPerth},
    {"New Zealand", "Pacific/Auckland", "NZST-12NZDT,M9.5.0/2,M4.1.0/3", kAliasesNewZealand},
};

static constexpr uint8_t kTzCount = sizeof(kTimezones) / sizeof(kTimezones[0]);
static constexpr uint8_t kDefaultTzIndex = 2; // Keep US/Central as current release-safe default.

static bool tzMatchesIana(const TimezoneInfo &tz, const char *ianaName)
{
  if (!ianaName || !ianaName[0])
    return false;

  if (strcmp(tz.iana, ianaName) == 0)
    return true;

  if (!tz.aliases)
    return false;

  for (const char *const *p = tz.aliases; *p; ++p)
  {
    if (strcmp(*p, ianaName) == 0)
      return true;
  }

  return false;
}
} // namespace

uint8_t tzCount() { return kTzCount; }

uint8_t tzDefaultIndex() { return kDefaultTzIndex; }

bool tzIndexIsValid(int idx)
{
  return idx >= 0 && idx < (int)kTzCount;
}

const char *tzName(uint8_t idx)
{
  if (!tzIndexIsValid(idx))
    idx = kDefaultTzIndex;
  return kTimezones[idx].label;
}

const char *tzIanaName(uint8_t idx)
{
  if (!tzIndexIsValid(idx))
    idx = kDefaultTzIndex;
  return kTimezones[idx].iana;
}

const char *tzPosixRule(uint8_t idx)
{
  if (!tzIndexIsValid(idx))
    idx = kDefaultTzIndex;
  return kTimezones[idx].posix;
}

int tzFindIndexByIana(const char *ianaName)
{
  if (!ianaName || !ianaName[0])
    return -1;

  for (uint8_t i = 0; i < kTzCount; ++i)
  {
    if (tzMatchesIana(kTimezones[i], ianaName))
      return (int)i;
  }

  return -1;
}

void applyTimezoneIndex(uint8_t idx)
{
  if (!tzIndexIsValid(idx))
    idx = kDefaultTzIndex;

  setenv("TZ", kTimezones[idx].posix, 1);
  tzset();
}

static constexpr const char *NVS_NS = "rh_sys";
static constexpr const char *NVS_KEY = "tzIndex";

bool loadTzIndexFromNVS(uint8_t *outIdx)
{
  if (!outIdx)
    return false;

  Preferences p;
  if (!p.begin(NVS_NS, true))
    return false;

  const bool has = p.isKey(NVS_KEY);
  const uint8_t v = p.getUChar(NVS_KEY, kDefaultTzIndex);
  p.end();

  if (!has)
    return false;
  if (!tzIndexIsValid(v))
    return false;

  *outIdx = v;
  return true;
}

void saveTzIndexToNVS(uint8_t idx)
{
  if (!tzIndexIsValid(idx))
    idx = kDefaultTzIndex;

  Preferences p;
  if (!p.begin(NVS_NS, false))
    return;
  p.putUChar(NVS_KEY, idx);
  p.end();
}