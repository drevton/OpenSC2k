# Game text

Interface text is defined directly in C. There is no string-ID catalog or string preprocessing step.

- `src/assets/names.c`: calendar, tool, team, sport, tile, zone and rating names.
- `src/assets/data.c`: facility information templates and newspaper heading, title and price variants.
- `src/ui/dialogs/budget_host.c`: advisor recommendations, indexed by `CityBudgetAdvice`.
- `src/ui/text/status.c`: weather and disaster descriptions.
- `src/city/disasters/dispatch.c`: service alerts and severe weather warnings.
- `src/ui/text/text.c`: text for queued notices and command/file errors.
- Other window labels and formats live beside their drawing code.

Keep significant leading/trailing spaces, line breaks and substitution placeholders when editing.
Facility templates use `#0` through `#3`, `#G`, `#S`, `#T` and `#W` for live values. The interface
renderer uses Windows-1252 bytes; non-ASCII characters in C literals use octal byte escapes.
Error reasons start with `... `, use lowercase, and end with a period.

Numerical notice/error codes identify simulation outcomes and queued events, independently of text.
Original string resource IDs and recorded resource-read services exist only in `src/city/comparison`.
Native runtime does not read strings from the original executable or an extracted string archive.

Authored newspaper headlines and stories live directly in `src/assets/news.c`, alongside each
topic's severity and decay values. The native game borrows these immutable UTF-8 C strings;
there is no newspaper source parser, JSON file or build-time text compiler. `=` inserts the city
name. All 67 news topics have project-written headlines and bodies. Generic newspaper names
such as Times and Herald are retained; stock article templates are loaded only by comparison tools.
The historical wording review is recorded in `reports/openttd-string-matches.md`. Wording review
is not legal clearance for the project's strings or other assets.

## Compiler retained for future use

`tools/compile_strings.py` remains available as a standalone utility. Native interface text does
not use it or depend on a string catalog. To compile a future catalog:

```sh
python3 tools/compile_strings.py path/to/strings.txt /tmp/strings.c
```

Entries use `STR_123 : text`. Quote values with significant leading/trailing spaces or literal
line breaks. `\n` also represents a line break; lines beginning with `#` outside quoted values
are comments. Text is UTF-8 in the catalog and must fit Windows-1252 for the interface renderer.
