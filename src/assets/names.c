#include "assets/resource_types.h"
#include "city/tools/types.h"
#include <stdio.h>
#include <string.h>

#include "city/engine/city_types.h"
#include "assets/catalog.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "base/text.h"

static const char *const kMonthNames[] = {
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December",
};
static const char *const kShortMonthNames[] = {
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec",
};
static const char *const kToolNames[CITY_TOOL_GROUP_COUNT][CITY_TOOL_VARIANT_CAPACITY] = {
  {
    "Clear / Demolish",
    "Level Terrain",
    "Raise Terrain",
    "Lower Terrain",
    "Remove Zoning"
  },
  {
    "Trees",
    "Water"
  },
  {
    "Dispatch Police",
    "Dispatch Firefighters",
    "Dispatch Military"
  },
  {
    "Power Lines",
    "Power Plant",
    "Coal Power",
    "Hydroelectric Power",
    "Oil Power",
    "Natural Gas Power",
    "Nuclear Power",
    "Wind Power",
    "Solar Power",
    "Microwave Power",
    "Fusion Power"
  },
  {
    "Pipes",
    "Water Pump",
    "Water Tower",
    "Water Treatment",
    "Desalination"
  },
  {
    "Mayor's House",
    "City Hall",
    "Statue",
    "Wildlife Dome",
    "Arcologies",
    "Terraced Arcology",
    "Forest Arcology",
    "Tower Arcology",
    "Launch Arcology"
  },
  {
    "Road",
    "Highway",
    "Tunnel",
    "Highway Ramp",
    "Bus Depot"
  },
  {
    "Rail",
    "Subway",
    "Rail Depot",
    "Subway Station",
    "Subway-Rail Link"
  },
  {
    "Seaport",
    "Airport"
  },
  {
    "Light Residential",
    "Dense Residential"
  },
  {
    "Light Commercial",
    "Dense Commercial"
  },
  {
    "Light Industrial",
    "Dense Industrial"
  },
  {
    "School",
    "College",
    "Library",
    "Museum"
  },
  {
    "Police Station",
    "Fire Station",
    "Hospital",
    "Prison"
  },
  {
    "Small Park",
    "Large Park",
    "Zoo",
    "Stadium",
    "Marina"
  },
  {
    "Place Sign"
  },
  {
    "Inspect Tile"
  },
  {
    "Center View"
  },
};
const char *const kCityTeamNames[] = {
  "Foxes",
  "Falcons",
  "Otters",
  "Kestrels",
  "Badgers",
};
const char *const kCitySports[] = {
  "Football",
  "Baseball",
  "Soccer",
  "Cricket",
  "Rugby",
};

// Names

// These are presentation resources, not saved simulation strings.
bool InitializeCityResourceNames(CityAssets *resources) {
  memset(&resources->calendar_names, 0, sizeof(resources->calendar_names));
  memset(&resources->tool_names, 0, sizeof(resources->tool_names));
  for (int month = 0; month < CITY_MONTH_COUNT; ++month) {
    const char *full = kMonthNames[month];
    const char *abbreviated = kShortMonthNames[month];
    if (strlen(full) >= CITY_RESOURCE_NAME_CAPACITY || strlen(abbreviated) >= CITY_RESOURCE_NAME_CAPACITY) {
      return false;
    }
    CopyText(resources->calendar_names.full[month], CITY_RESOURCE_NAME_CAPACITY, full);
    CopyText(resources->calendar_names.abbreviated[month], CITY_RESOURCE_NAME_CAPACITY, abbreviated);
  }
  for (int group = 0; group < CITY_TOOL_GROUP_COUNT; ++group)
    for (int variant = 0; variant < CITY_TOOL_VARIANT_CAPACITY; ++variant) {
      const char *name = kToolNames[group][variant];
      if (!name) continue;
      if (strlen(name) >= CITY_RESOURCE_NAME_CAPACITY) {
        return false;
      }
      CopyText(resources->tool_names.name[group][variant], CITY_RESOURCE_NAME_CAPACITY, name);
    }
  return true;
}

// Name Lookup

static const char *const kTileNames[] = {
  "",
  "Rubble",
  "Radioactive Waste",
  "Trees",
  "Park",
  "Power Lines",
  "Road",
  "Railway",
  "Tunnel Entrance",
  "Road",
  "Railway",
  "Highway",
  "Suspension Bridge",
  "Bridge",
  "Power Lines",
  "Highway Ramp",
  "Highway",
  "Highway Bridge",
  "Subway-Rail Connection",
  "Modest Homes",
  "Standard Homes",
  "Luxury Homes",
  "Gas Station",
  "Bed & Breakfast Inn",
  "Convenience Store",
  "Gas Station",
  "Small Office Building",
  "Office Building",
  "Warehouse",
  "Toy Store",
  "Warehouse",
  "Chemical Storage",
  "Warehouse",
  "Industrial Substation",
  "Construction Site",
  "Construction Site",
  "Abandoned Building",
  "Abandoned Building",
  "Budget Apartments",
  "Apartments",
  "Apartments",
  "Upscale Apartments",
  "Upscale Apartments",
  "Condominium",
  "Condominium",
  "Condominium",
  "Shopping Center",
  "Grocery Store",
  "Office Building",
  "Resort Hotel",
  "Office Building",
  "Office and Retail",
  "Office Building",
  "Office Building",
  "Office Building",
  "Office Building",
  "Warehouse",
  "Chemical Processing Plant",
  "Factory",
  "Factory",
  "Factory",
  "Factory",
  "Factory",
  "Factory",
  "Construction Site",
  "Construction Site",
  "Construction Site",
  "Construction Site",
  "Abandoned Building",
  "Abandoned Building",
  "Abandoned Building",
  "Abandoned Building",
  "Large Apartment Building",
  "Large Apartment Building",
  "Condominium",
  "Condominium",
  "Office Park",
  "Office Tower",
  "Small Shopping Center",
  "Cinema Complex",
  "Drive-in Theater",
  "Office Tower",
  "Office Tower",
  "Parking Lot",
  "Historic Office Building",
  "Corporate Headquarters",
  "Chemical Processing Plant",
  "Large Factory",
  "Industrial Facility",
  "Factory",
  "Large Warehouse",
  "Warehouse",
  "Construction Site",
  "Construction Site",
  "Abandoned Building",
  "Abandoned Building",
  "Hydroelectric Plant",
  "Hydroelectric Plant",
  "Wind Turbine",
  "Natural Gas Power Plant",
  "Oil Power Plant",
  "Nuclear Power Plant",
  "Solar Power Plant",
  "Microwave Power Plant",
  "Fusion Power Plant",
  "Coal Power Plant",
  "City Hall",
  "Hospital",
  "Police Station",
  "Fire Station",
  "Museum",
  "Park",
  "School",
  "Stadium",
  "Prison",
  "College",
  "Zoo",
  "Statue",
  "Water Pump",
  "Runway",
  "Runway",
  "Pier",
  "Crane",
  "Control Tower",
  "Control Tower",
  "Warehouse",
  "Building",
  "Building",
  "Tarmac",
  "Fighter Aircraft",
  "Hangar",
  "Subway Station",
  "Radar",
  "Water Tower",
  "Bus Station",
  "Rail Station",
  "Parking Lot",
  "Parking Lot",
  "Loading Bay",
  "Military Research Facility",
  "Cargo Yard",
  "Mayor's House",
  "Water Treatment Plant",
  "Library",
  "Hangar",
  "Church",
  "Marina",
  "Missile Silo",
  "Desalination Plant",
  "Terraced Arcology",
  "Forest Arcology",
  "Tower Arcology",
  "Launch Arcology",
  "Wildlife Dome",
  "",
  "",
  "",
  "",
  "Clear Terrain",
  "Fresh Water",
  "Salt Water",
  "Sailboat",
};
static const char *const kZoneNames[] = {
  "Residential",
  "Residential",
  "Commercial",
  "Commercial",
  "Industrial",
  "Industrial",
  "Military",
  "Airport",
  "Seaport",
  "Seaport",
  "Airport",
  "",
  "",
  "",
  "",
  "",
  "Low-density",
  "High-density",
  "Low-density",
  "High-density",
  "Low-density",
  "High-density",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
};
static const char *const kRatingNames[] = {
  "High-density",
  "None",
  "Low",
  "Medium",
  "High",
  "Very High",
};

// Names are immutable literals. Unknown categories or indices return empty text;
// callers borrow the result and copy only when retaining an editable snapshot.
const char *ReadCityNameResource(int category, int index) {
  switch (category) {
  case CITY_NAME_CATEGORY_TILE:
    if (index >= 0 && index < (int)(sizeof(kTileNames) / sizeof(kTileNames[0]))) return kTileNames[index];
    break;
  case CITY_NAME_CATEGORY_ZONE:
    if (index >= 1 && index <= (int)(sizeof(kZoneNames) / sizeof(kZoneNames[0]))) return kZoneNames[index - 1];
    break;
  case CITY_NAME_CATEGORY_RATING:
    if (index >= 1 && index < (int)(sizeof(kRatingNames) / sizeof(kRatingNames[0]))) return kRatingNames[index];
    break;
  default: break;
  }
  return "";
}

// Generated City Names

// The caller owns this PRNG state; generated labels never consume simulation randomness.
static u32 CityNameRandom(u32 *random) {
  u32 value = *random;
  if (!value) value = 0x6D2B79F5;
  value ^= value << 13; value ^= value >> 17; value ^= value << 5;
  *random = value;
  return value;
}

// Avoid repeated stems such as Brookbrook or Bergbergsvik, including linking-s forms.
static bool CityNamePartsRepeat(const char *first, const char *second) {
  size_t first_length = strlen(first), second_length = strlen(second);
  if (first_length > 1 && first[first_length - 1] == 's') --first_length;
  if (second_length > 1 && second[second_length - 1] == 's') --second_length;
  if (first_length != second_length) return false;
  for (size_t i = 0; i < first_length; ++i) {
    int a = first[i], b = second[i];
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (a != b) return false;
  }
  return true;
}

// A doubled consonant at the join stays doubled rather than becoming three letters.
static const char *CityNameJoiningPart(const char *stem, const char *part) {
  size_t length = strlen(stem);
  return length >= 2 && stem[length - 1] == part[0] && stem[length - 2] == part[0] ? part + 1 : part;
}

bool GenerateCityName(u32 *random, const char *previous, char *name, size_t capacity) {
  // OpenTTD-inspired grammars with independently curated parts. Swedish,
  // Norwegian and Danish vocabulary shares one Nordic pool.
  static const char *const kEnglishRoots[] = {
    "Acorn", "Alder", "Amber", "Apple", "Ash", "Aspen", "Badger", "Beech", "Birch", "Black", "Blue", "Bramble",
    "Briar", "Bright", "Brook", "Broom", "Cedar", "Cherry", "Clay", "Clear", "Cloud", "Copper", "Crane", "Crow",
    "Deep", "Deer", "Dove", "Drift", "Eagle", "Elder", "Elm", "Fair", "Falcon", "Fern", "Fleet", "Flint", "Flower",
    "Fox", "Frost", "Glen", "Gold", "Goose", "Green", "Grey", "Hare", "Hawk", "Hawthorn", "Hazel", "Hearth",
    "Heather", "Hedge", "Heron", "Hickory", "High", "Holly", "Honey", "Iron", "Ivory", "Ivy", "Juniper", "Kings",
    "Lark", "Laurel", "Linden", "Low", "Maple", "Meadow", "Mill", "Moss", "Nettle", "Oak", "Otter", "Pebble",
    "Pine", "Raven", "Red", "Reed", "River", "Robin", "Rose", "Sage", "Silver", "Slate", "Snow", "Sparrow",
    "Spring", "Stone", "Summer", "Tawny", "Thorn", "Walnut", "Water", "Wheat", "White", "Wild", "Willow"
  };
  static const char *const kEnglishEndings[] = {
    "bank", "borough", "bourne", "bridge", "brook", "burn", "bury", "chester", "cliff", "combe", "croft", "cross",
    "dale", "den", "down", "fen", "field", "ford", "gate", "hall", "ham", "haven", "hill", "hurst", "lake", "land",
    "leigh", "marsh", "mere", "mill", "minster", "mont", "mouth", "pool", "port", "ridge", "shaw", "side", "stead",
    "stone", "stow", "thorpe", "ton", "vale", "well", "wick", "wood", "worth"
  };
  static const char *const kOnsets[] = {
    "Br", "Cl", "Cr", "D", "Dr", "F", "Fl", "Fr", "Gl", "H", "L", "M", "N", "P", "R", "St", "T", "Tr", "W", "Wh"
  };
  static const char *const kVowels[] = {"a", "e", "i", "o", "u"};
  static const char *const kCodas[] = {
    "b", "ck", "d", "ff", "l", "ll", "m", "n", "nd", "nn", "p", "r", "s", "sh", "t", "tt"
  };
  static const char *const kLandmarks[] = {
    "Bay", "Bend", "Bluffs", "Canyon", "Cape", "Cliffs", "Cove", "Creek", "Dunes", "Estuary", "Falls", "Fens",
    "Glade", "Grove", "Harbor", "Heights", "Hills", "Inlet", "Lake", "Landing", "Marsh", "Meadows", "Orchard",
    "Point", "Rapids", "Reach", "Ridge", "Shores", "Sound", "Springs", "Valley", "Waters"
  };
  static const char *const kEnglishPrefixes[] = {
    "East ", "Great ", "Little ", "Lower ", "New ", "North ", "Old ", "South ", "Upper ", "West ", "Fort ",
    "Mount ", "Port ", "Saint ", "Royal ", "Long "
  };
  static const char *const kEnglishSuffixes[] = {
    " Bay", " Falls", " Heights", " Point", " Springs", "-on-Sea", " Common", " Cross", " Heath", " Hill",
    " Marsh", " Sands", " Wells", "-on-the-Hill", "-in-the-Vale", "-by-the-Sea"
  };
  static const char *const kRejectedBeginnings[] = {"Clit", "Dick", "Fart", "Fuck", "Piss", "Tit"};
  static const char *const kNordicRoots[] = {
    "Alm", "Aske", "Berg", "Birk", "Bjork", "Bjorne", "Blom", "Bok", "Borg", "Bratt", "Brunn", "Dal", "Eke", "Elg",
    "En", "Fager", "Falk", "Fisk", "Fjell", "Flint", "Foss", "Furu", "Gran", "Grind", "Gull", "Haga", "Hassel",
    "Hav", "Hede", "Hjort", "Horn", "Hult", "Jarn", "Kastan", "Klint", "Koppar", "Kvarn", "Lang", "Lauv", "Linde",
    "Ljung", "Lyng", "Malm", "Myr", "Oxe", "Ravn", "Ronn", "Rosen", "Sand", "Skog", "Sol", "Sten", "Strom",
    "Svart", "Tall", "Tange", "Tegl", "Torn", "Tran", "Ull", "Vass", "Vinter", "Vold", "Vret"
  };
  static const char *const kNordicEndings[] = {
    "ager", "aker", "berg", "bo", "boda", "borg", "bro", "by", "dal", "eid", "fjell", "fjord", "fors", "gard",
    "havn", "heim", "holm", "hult", "land", "lund", "mark", "nas", "nes", "ness", "rud", "sjo", "stad", "sund",
    "torp", "tun", "vang", "vik"
  };
  static const char *const kGermanicRoots[] = {
    "Adler", "Amsel", "Apfel", "Biber", "Birken", "Blumen", "Buchen", "Dachs", "Donner", "Drossel", "Eber",
    "Eichen", "Erlen", "Falken", "Felsen", "Fichten", "Finken", "Flieder", "Fohren", "Forellen", "Fuchs", "Gold",
    "Habicht", "Hasel", "Hirsch", "Holunder", "Kastanien", "Kiefern", "Kirsch", "Korn", "Kranich", "Lerchen",
    "Linden", "Morgen", "Nebel", "Raben", "Reben", "Rosen", "Rot", "Schilf", "Silber", "Sonnen", "Stein", "Tannen",
    "Ulmen", "Wasser", "Wiesen", "Wolfen"
  };
  static const char *const kGermanicEndings[] = {
    "ach", "au", "bach", "berg", "born", "brunn", "burg", "dorf", "feld", "fels", "furt", "garten", "grund",
    "hagen", "hain", "hausen", "heim", "hof", "holz", "kirchen", "rode", "see", "stadt", "stein", "tal", "wald",
    "weide", "zell"
  };
  static const char *const kMediterraneanRoots[] = {
    "Borgo", "Bosco", "Campo", "Casale", "Castel", "Castello", "Colle", "Dosso", "Faro", "Fiume", "Fondo", "Forte",
    "Giardino", "Lago", "Lido", "Monte", "Mulino", "Orto", "Palazzo", "Parco", "Passo", "Piano", "Poggio", "Ponte",
    "Porto", "Prato", "Promontorio", "Rifugio", "Rio", "Rivo", "Roccolo", "Ronco", "Salto", "Sasso", "Scoglio",
    "Torrente", "Trivio", "Vallone", "Villaggio", "Vigneto"
  };
  static const char *const kMediterraneanEndings[] = {
    "Alto", "Antico", "Aperto", "Azzurro", "Basso", "Bello", "Bianco", "Calmo", "Chiaro", "Dorato", "Dolce",
    "Felice", "Fiorito", "Fresco", "Grande", "Largo", "Libero", "Lieto", "Lungo", "Marino", "Nuovo", "Ombroso",
    "Pacifico", "Piano", "Piccolo", "Quieto", "Rosso", "Rotondo", "Santo", "Secco", "Selvatico", "Sereno",
    "Silvestre", "Soleggiato", "Solido", "Tiepido", "Tranquillo", "Vecchio", "Verde", "Ventoso"
  };
  static const char *const kNordicMiddles[] = {
    "bergs", "brunns", "dals", "fjell", "fjords", "fors", "hammar", "holms", "kvarns", "lands", "lunds", "marks",
    "skogs", "stens", "strands", "vass"
  };
  static const char *const kNordicPrefixes[] = {
    "Gamla ", "Lilla ", "Nya ", "Stora ", "Norra ", "Sodra ", "Ovre ", "Nedre ", "Nordre ", "Sondre ", "Vester ",
    "Oster "
  };
  static const char *const kGermanicPrefixes[] = {
    "Alt-", "Neu-", "Gross ", "Klein ", "Ober-", "Unter-"
  };
  static const char *const kMediterraneanSuffixes[] = {
    " del Lago", " del Mare", " del Sole", " del Bosco", " del Colle", " del Fiume", " del Monte", " del Ponte",
    " della Costa", " della Fonte", " della Luna", " della Valle", " delle Rose", " dei Fiori", " dei Pini",
    " sul Mare"
  };
  enum { NAME_COMPOUND, NAME_SYLLABLES, NAME_LANDMARK, NAME_NORDIC, NAME_GERMANIC, NAME_MEDITERRANEAN };
  static const u8 kStyles[] = {
    NAME_COMPOUND, NAME_COMPOUND, NAME_COMPOUND, NAME_COMPOUND, NAME_COMPOUND, NAME_COMPOUND,
    NAME_SYLLABLES, NAME_SYLLABLES, NAME_SYLLABLES, NAME_LANDMARK, NAME_LANDMARK, NAME_LANDMARK,
    NAME_NORDIC, NAME_NORDIC, NAME_GERMANIC, NAME_MEDITERRANEAN
  };
  for (int attempt = 0; attempt < 8; ++attempt) {
    int style = kStyles[CityNameRandom(random) % (sizeof(kStyles) / sizeof(*kStyles))];
    const char *root, *ending;
    const char *prefix = "", *suffix = "";
    char base[CITY_NAME_CAPACITY];
    switch (style) {
      case NAME_NORDIC: {
        root = kNordicRoots[CityNameRandom(random) % (sizeof(kNordicRoots) / sizeof(*kNordicRoots))];
        ending = kNordicEndings[CityNameRandom(random) % (sizeof(kNordicEndings) / sizeof(*kNordicEndings))];
        const char *middle = "";
        int pattern = CityNameRandom(random) % 6;
        if (pattern < 2) {
          middle = kNordicMiddles[CityNameRandom(random) % (sizeof(kNordicMiddles) / sizeof(*kNordicMiddles))];
          if (CityNamePartsRepeat(root, middle) || CityNamePartsRepeat(middle, ending)) continue;
        } else if (pattern == 2) {
          prefix = kNordicPrefixes[CityNameRandom(random) % (sizeof(kNordicPrefixes) / sizeof(*kNordicPrefixes))];
        }
        if (middle[0]) middle = CityNameJoiningPart(root, middle);
        ending = CityNameJoiningPart(middle[0] ? middle : root, ending);
        snprintf(base, sizeof(base), "%s%s%s", root, middle, ending);
        break;
      }
      case NAME_GERMANIC:
        root = kGermanicRoots[CityNameRandom(random) % (sizeof(kGermanicRoots) / sizeof(*kGermanicRoots))];
        ending = kGermanicEndings[CityNameRandom(random) % (sizeof(kGermanicEndings) / sizeof(*kGermanicEndings))];
        snprintf(base, sizeof(base), "%s%s", root, ending);
        if (CityNameRandom(random) % 5 == 0)
          prefix = kGermanicPrefixes[CityNameRandom(random) % (sizeof(kGermanicPrefixes) / sizeof(*kGermanicPrefixes))];
        break;
      case NAME_MEDITERRANEAN:
        root = kMediterraneanRoots[CityNameRandom(random) % (sizeof(kMediterraneanRoots) / sizeof(*kMediterraneanRoots))];
        ending = kMediterraneanEndings[
            CityNameRandom(random) % (sizeof(kMediterraneanEndings) / sizeof(*kMediterraneanEndings))];
        snprintf(base, sizeof(base), "%s %s", root, ending);
        if (CityNameRandom(random) % 4 == 0)
          suffix = kMediterraneanSuffixes[
              CityNameRandom(random) % (sizeof(kMediterraneanSuffixes) / sizeof(*kMediterraneanSuffixes))];
        break;
      case NAME_SYLLABLES: {
        root = kOnsets[CityNameRandom(random) % (sizeof(kOnsets) / sizeof(*kOnsets))];
        const char *vowel = kVowels[CityNameRandom(random) % (sizeof(kVowels) / sizeof(*kVowels))];
        const char *coda = kCodas[CityNameRandom(random) % (sizeof(kCodas) / sizeof(*kCodas))];
        ending = kEnglishEndings[CityNameRandom(random) % (sizeof(kEnglishEndings) / sizeof(*kEnglishEndings))];
        snprintf(base, sizeof(base), "%s%s%s%s", root, vowel, coda, ending);
        break;
      }
      default:
        root = kEnglishRoots[CityNameRandom(random) % (sizeof(kEnglishRoots) / sizeof(*kEnglishRoots))];
        ending = style == NAME_LANDMARK ?
            kLandmarks[CityNameRandom(random) % (sizeof(kLandmarks) / sizeof(*kLandmarks))] :
            kEnglishEndings[CityNameRandom(random) % (sizeof(kEnglishEndings) / sizeof(*kEnglishEndings))];
        if (style == NAME_COMPOUND) ending = CityNameJoiningPart(root, ending);
        snprintf(base, sizeof(base), "%s%s%s", root, style == NAME_LANDMARK ? " " : "", ending);
        break;
    }
    if (CityNamePartsRepeat(root, ending)) continue;
    bool rejected = false;
    for (size_t i = 0; i < sizeof(kRejectedBeginnings) / sizeof(*kRejectedBeginnings); ++i)
      if (!strncmp(base, kRejectedBeginnings[i], strlen(kRejectedBeginnings[i]))) rejected = true;
    if (rejected) continue;
    if (style == NAME_COMPOUND || style == NAME_SYLLABLES || style == NAME_LANDMARK) {
      int qualifier = CityNameRandom(random) % 12;
      if (qualifier == 0)
        prefix = kEnglishPrefixes[CityNameRandom(random) % (sizeof(kEnglishPrefixes) / sizeof(*kEnglishPrefixes))];
      else if (qualifier == 1 && style != NAME_LANDMARK)
        suffix = kEnglishSuffixes[CityNameRandom(random) % (sizeof(kEnglishSuffixes) / sizeof(*kEnglishSuffixes))];
    }
    int length = snprintf(name, capacity, "%s%s%s", prefix, base, suffix);
    if (length >= 0 && (size_t)length < capacity && strcmp(name, previous)) {
      return true;
    }
  }
  return false;
}
