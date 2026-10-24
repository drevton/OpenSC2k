#include "city/engine/city_name.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/text.h"
#include "city/state.h"
#include "game/errors.h"

// Stored City and Neighbor Names

// Replace the City's name with the supplied byte sequence. Text is borrowed for this call; SetCityNameText owns any
// overflow allocation and records allocation errors on Game.
void AssignCityName(City *city, int length, const u8 *text) {
  SetCityNameText(city->game, &city->name, length, text);
}

// Clear the City's name and release its overflow allocation through the normal storage path.
void EmptyCityName(City *city) {
  SetCityNameText(city->game, &city->name, 0, NULL);
}

// Saved neighbor indices select locally generated labels, with zero reserved for Ocean. Hash the city's name bytes
// explicitly so the result is independent of host byte order. Slot must be 0 through 3; writes a terminated fixed
// label without advancing City's simulation RNG streams.
void GenerateCityNeighborName(City *city, int slot) {
  char *name = (char *)city->geography.neighbor_names.name[slot];
  size_t capacity = sizeof(city->geography.neighbor_names.name[slot]);
  int index = city->account_names.value[slot];
  if (!index) {
    CopyText(name, capacity, "Ocean");
    return;
  }
  u32 seed = 2166136261;
  const char *city_name = CityNameText(&city->name);
  for (int character = 0; character < city->name.length; ++character)
    seed = (seed ^ (u8)city_name[character]) * 16777619;
  seed = (seed ^ (index & 255)) * 16777619;
  seed = (seed ^ (index >> 8)) * 16777619;
  // Retain distinct neighboring names even if two indices produce the same first candidate.
  for (int attempt = 0; attempt < 8; ++attempt) {
    if (!GenerateCityName(&seed, "", name, capacity)) continue;
    bool duplicate = false;
    for (int previous = 0; previous < slot; ++previous)
      if (!strcmp(name, (const char *)city->geography.neighbor_names.name[previous])) duplicate = true;
    if (!duplicate) return;
  }
  snprintf(name, capacity, "Neighbor %d", index);
}

// Regenerate all four neighboring-city labels from the current City name. The generated labels occupy City-owned fixed
// slots and require no caller cleanup.
void RefreshCityNeighborNames(City *city) {
  for (int slot = 0; slot < 4; ++slot) GenerateCityNeighborName(city, slot);
}

// Name Storage

// Copy length bytes into a terminated name, using inline storage where it fits and owned overflow otherwise. Negative
// lengths and allocation failure return false and set Game's error; nonempty input must provide length readable bytes.
bool SetCityNameText(Game *game, CityName *name, int length, const u8 *text) {
  if (length < 0) {
    SetGameError(game, SC2K_ERROR_GUEST_BUFFER_TOO_SMALL);
    return false;
  }
  if (length < CITY_NAME_CAPACITY) {
    if (length != 0)
      memmove(name->text, text, (size_t)length);
    name->text[length] = 0;
    if (name->overflow != NULL)
      free(name->overflow);
    name->overflow = NULL;
    name->overflow_capacity = 0;
  } else if (name->overflow != NULL && length <= name->overflow_capacity) {
    memmove(name->overflow, text, (size_t)length);
    name->overflow[length] = 0;
  } else {
    char *long_name = malloc((size_t)length + 1);
    if (long_name == NULL) {
      SetGameError(game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
      return false;
    }
    memcpy(long_name, text, (size_t)length);
    long_name[length] = 0;
    if (name->overflow != NULL)
      free(name->overflow);
    name->overflow = long_name;
    name->overflow_capacity = length;
    name->text[0] = 0;
  }
  name->length = length;
  return true;
}

// Release a name's overflow allocation and reset its length and capacity metadata. Requires an initialized CityName;
// its inline storage is part of the object and is never freed separately.
void FreeCityNameStorage(CityName *name) {
  free(name->overflow);
  *name = (CityName){0};
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
        root = kMediterraneanRoots[
            CityNameRandom(random) % (sizeof(kMediterraneanRoots) / sizeof(*kMediterraneanRoots))];
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
