#include "assets/resources.h"
#include "assets/state.h"
#include "city/events/news.h"

#include <stdlib.h>
#include <string.h>

const char *const kCityNewspaperTitles[] = {
  "Times",
  "Herald",
  "Gazette",
  "Chronicle",
  "Tribune",
  "Journal",
};
const char *const kCityNewspaperWeatherHeadings[] = {
  "Local forecast",
  "Weather desk",
  "Around the city",
  "Skywatch",
  "The outlook",
  "Weather notes",
};
const char *const kCityNewspaperEditorialHeadings[] = {
  "From the editor",
  "Editorial desk",
  "Local commentary",
  "City opinion",
  "Public survey",
  "Advice desk",
};
const char *const kCityNewspaperStatistics[] = {
  "Traffic: %d%%",
  "Pollution: %d%%",
  "Crime: %d%%",
  "Tax rate: %d%%",
  "Unemployment: %d%%",
  "Education: %d%%",
  "Health: %d%%",
};
const char *const kCityNewspaperPrices[3][CITY_NEWSPAPER_PRICE_ERAS] = {
  {"1 cent", "5 cents", "25 cents", "1 dollar", "5 dollars"},
  {"Cover: 1¢", "Cover: 5¢", "Cover: 25¢", "Cover: $1", "Cover: $5"},
  {"Price: 1¢", "Price: 5¢", "Price: 25¢", "Price: $1", "Price: $5"},
};

// Project-written news, indexed by the gameplay topic. Ranking values retain the existing event behavior.
static const CityNewspaperArticle kArticles[CITY_NEWS_EVENT_COUNT] = {
  [CITY_NEWS_WEATHER] = {
    .headline =
        "Weather desk tracks the changing skies",
    .body =
        "The weather office is monitoring local conditions and will update its outlook as the season develops. "
        "Travel and utility operators are reviewing their plans for changing temperatures and rainfall.",
    .severity = 0, .decay = 0,
  },
  [CITY_NEWS_LOCAL_NEWS] = {
    .headline =
        "Neighborhood reporters collect the latest news",
    .body =
        "Our reporters are following the decisions, projects and everyday events shaping the neighborhoods of =. "
        "Residents and local organizations continue to bring new stories to the newsroom.",
    .severity = 0, .decay = 0,
  },
  [CITY_NEWS_CITY_FOUNDED] = {
    .headline =
        "A new city opens its doors",
    .body =
        "The first chapter of = is being written. New roads, dependable utilities and well planned neighborhoods "
        "will shape how the community grows. The next challenge is to provide services as homes and workplaces "
        "arrive.",
    .severity = 1000, .decay = 500,
  },
  [CITY_NEWS_POPULATION_REWARD] = {
    .headline =
        "A growing city reaches another milestone",
    .body =
        "More residents are making their homes in =. Growth brings new opportunities, along with greater demand "
        "for transport, schools and public services. The planning office is reviewing where the next investments "
        "are needed.",
    .severity = 1000, .decay = 500,
  },
  [CITY_NEWS_INVENTION] = {
    .headline =
        "New technology changes the possibilities",
    .body =
        "Engineers and researchers have opened another route for city development. Officials will assess the "
        "costs and requirements before deciding where the new technology can help residents and businesses.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_BUILDING_AVAILABLE] = {
    .headline =
        "City builders gain a new option",
    .body =
        "An additional building option is now available to the city. Planners are considering suitable locations, "
        "connections and ongoing operating costs before recommending construction.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_INTERNATIONAL_NEWS] = {
    .headline =
        "News from beyond the city limits",
    .body =
        "Regional developments are drawing the attention of local officials. Trade, transport and public services "
        "connect = with communities well beyond its borders.",
    .severity = 360, .decay = 10,
  },
  [CITY_NEWS_MARKET] = {
    .headline =
        "Markets weigh the latest economic signals",
    .body =
        "Businesses are watching demand, employment and investment closely. Local conditions still matter: "
        "reliable services and useful connections help firms weather changes in the wider economy.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_SPORTS] = {
    .headline =
        "Local sport brings the community together",
    .body =
        "Players, supporters and volunteers are keeping the local sporting calendar busy. Community facilities "
        "provide space to train, compete and meet neighbors.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_INTEREST_RATE_INCREASE] = {
    .headline =
        "Borrowing costs are moving higher",
    .body =
        "Higher interest rates make new borrowing more expensive. The finance office recommends reviewing debt "
        "payments alongside essential services before committing to major projects.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_INTEREST_RATE_DECREASE] = {
    .headline =
        "Lower rates offer room to plan",
    .body =
        "Lower interest rates may ease future borrowing costs. The finance office is comparing investment needs "
        "with the long term cost of repayment.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_CONGRESS] = {
    .headline =
        "Lawmakers debate the next public investment",
    .body =
        "Public spending proposals are under discussion. Representatives are weighing immediate needs against "
        "projects intended to benefit communities over many years.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_DIPLOMACY] = {
    .headline =
        "Cities look for common ground",
    .body =
        "Officials are exploring cooperation on transport, infrastructure and trade. Useful regional connections "
        "can help both local residents and neighboring communities.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_OTHER_CITY_FIRE] = {
    .headline =
        "Neighboring community battles a fire",
    .body =
        "A nearby community is dealing with a serious fire. Local emergency planners are reviewing access routes, "
        "equipment and the ability to protect occupied neighborhoods.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_MEDICAL_RESEARCH] = {
    .headline =
        "Research teams report a medical advance",
    .body =
        "Medical researchers have reported progress in their latest work. Health officials caution that practical "
        "benefits depend on careful evaluation and adequate care services.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_COMMUNITY_NEWS] = {
    .headline =
        "Community groups turn plans into action",
    .body =
        "Volunteer organizations and neighborhood groups are working on practical improvements. Their efforts "
        "complement the services provided by the city.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_HIGH_CRIME] = {
    .headline =
        "Residents ask for safer streets",
    .body =
        "Residents have raised concerns about crime. Police coverage, service funding and access to affected "
        "neighborhoods are under review. Officials say prevention and a dependable response both require "
        "attention.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_HIGH_TRAFFIC] = {
    .headline =
        "Commuters call for a better journey",
    .body =
        "Congestion is making daily journeys harder. Transport planners are examining overloaded routes and "
        "alternatives to driving. New links should be coordinated with homes, jobs and public transport.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_HIGH_POLLUTION] = {
    .headline =
        "Air quality raises public concern",
    .body =
        "Residents are asking for cleaner air and less pollution. The planning office is reviewing industrial "
        "activity, traffic and the location of sensitive neighborhoods.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_LOW_EDUCATION] = {
    .headline =
        "Schools face a growing challenge",
    .body =
        "Schools and colleges need attention as the city develops. Education officials are reviewing capacity, "
        "funding and access to learning across the community.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_POOR_HEALTH] = {
    .headline =
        "Health services come under pressure",
    .body =
        "Health indicators suggest the city needs stronger care services. Officials are reviewing hospital "
        "access, pollution and the conditions affecting everyday wellbeing.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_HIGH_UNEMPLOYMENT] = {
    .headline =
        "Job seekers face a difficult market",
    .body =
        "Residents looking for work face a difficult period. The economic development office is reviewing the "
        "balance of homes and jobs, business demand and connections to outside markets.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_FIRE] = {
    .headline =
        "Fire crews respond across the city",
    .body =
        "Firefighters are working to contain active fires and protect nearby properties. Residents should keep "
        "response routes clear. City officials are assessing damage and the need for stronger local fire "
        "coverage.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_FLOOD] = {
    .headline =
        "Rising water threatens the neighborhood",
    .body =
        "Floodwater is damaging low lying areas. Emergency teams are monitoring threatened properties and access "
        "routes. Recovery work will need to account for the risk of further flooding.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_PLANE_CRASH] = {
    .headline =
        "Emergency crews respond to an aircraft crash",
    .body =
        "Emergency services have responded to an aircraft accident. The affected area is being assessed for fire, "
        "damage and risks to nearby properties.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_HELICOPTER_CRASH] = {
    .headline =
        "Helicopter accident prompts an emergency response",
    .body =
        "Emergency teams are attending the scene of a helicopter accident. Response crews are working to secure "
        "the area and limit further damage.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_TORNADO] = {
    .headline =
        "Tornado threatens homes and infrastructure",
    .body =
        "A dangerous storm is moving through the area. Emergency teams are tracking its progress and preparing to "
        "assess damage to homes, businesses and public infrastructure.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_EARTHQUAKE] = {
    .headline =
        "Earthquake damages the city",
    .body =
        "Strong ground movement has damaged parts of the city. Response teams are checking critical routes and "
        "utilities while officials assess the extent of the disruption.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_MONSTER] = {
    .headline =
        "A giant machine moves through the city",
    .body =
        "A huge mechanical crawler has entered the city and is damaging structures in its path. Emergency teams "
        "are keeping residents away from the affected districts and monitoring its movement.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_MELTDOWN] = {
    .headline =
        "Nuclear emergency requires urgent action",
    .body =
        "A nuclear incident has created a dangerous contaminated area. Emergency teams are assessing the damage "
        "and isolating affected ground. Recovery will require careful attention to public safety and utility "
        "supply.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_MICROWAVE] = {
    .headline =
        "Energy transmission accident sparks concern",
    .body =
        "An accident involving energy transmission has caused damage on the ground. Officials are assessing the "
        "affected area and the reliability of nearby infrastructure.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_VOLCANO] = {
    .headline =
        "Volcanic activity threatens nearby districts",
    .body =
        "Volcanic activity has damaged the landscape and nearby structures. Emergency crews are monitoring new "
        "hazards as the city begins to assess essential repairs.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_POLLUTION_DISASTER] = {
    .headline =
        "Pollution emergency strains local services",
    .body =
        "A severe pollution event is affecting the city. Health and environmental teams are identifying the areas "
        "most in need of a response.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_CHEMICAL_SPILL] = {
    .headline =
        "Chemical spill closes the affected area",
    .body =
        "A chemical release has made the affected area unsafe. Response teams are isolating the site and "
        "examining risks to nearby homes, businesses and water supplies.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_HURRICANE] = {
    .headline =
        "Hurricane brings dangerous conditions",
    .body =
        "High winds and heavy rain are affecting the city. Emergency services are monitoring damage, threatened "
        "properties and the availability of essential connections.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_RIOTS] = {
    .headline =
        "Unrest spreads through the streets",
    .body =
        "Unrest has disrupted streets and neighborhoods. Emergency teams are responding to immediate risks while "
        "city officials review the pressures behind the disturbance.",
    .severity = 1000, .decay = 250,
  },
  [CITY_NEWS_POWER_PLANT_AGING] = {
    .headline =
        "Aging power station needs a replacement plan",
    .body =
        "A power station is reaching the end of its useful life. Replacing capacity before it fails can reduce "
        "the risk of an electricity shortage.",
    .severity = 1000, .decay = 100,
  },
  [CITY_NEWS_PRISON_OVERCROWDING] = {
    .headline =
        "Prison capacity falls behind demand",
    .body =
        "Prison services are struggling to meet demand. Officials are reviewing capacity and funding alongside "
        "the wider needs of public safety.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_SCHOOL_FUNDING] = {
    .headline =
        "School funding draws fresh scrutiny",
    .body =
        "School budgets are under pressure. Education leaders are comparing available funding with the number of "
        "students and the cost of maintaining useful services.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_BRIDGE_COLLAPSE] = {
    .headline =
        "Bridge collapse disrupts a vital connection",
    .body =
        "A bridge failure has interrupted an important route. Engineers and emergency crews are examining the "
        "damage before repair plans can be developed.",
    .severity = 300, .decay = 50,
  },
  [CITY_NEWS_TREE_REMOVAL_PROTEST] = {
    .headline =
        "Residents seek protection for local trees",
    .body =
        "Residents have questioned the removal of established trees. Community groups are asking planners to "
        "consider greenery alongside new construction and transport links.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_ORDINANCE_PASSED] = {
    .headline =
        "Council adopts a new city ordinance",
    .body =
        "The council has approved a change to city policy. The ordinance will affect local services, revenue or "
        "the conditions faced by residents and businesses.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_FREDS_OPINION] = {
    .headline =
        "Readers debate the city priorities",
    .body =
        "Readers have shared different views about the future of =. Their letters focus on everyday services, the "
        "pace of growth and how public money should be spent.",
    .severity = 0, .decay = 0,
  },
  [CITY_NEWS_EDITOR_COMMENTARY] = {
    .headline =
        "From the editor: choosing the next steps",
    .body =
        "A successful city needs both dependable everyday services and a clear plan for the future. We encourage "
        "the council to publish its priorities and explain how the budget supports them.",
    .severity = 0, .decay = 0,
  },
  [CITY_NEWS_OPINION_SURVEY] = {
    .headline =
        "Residents give their view of city services",
    .body =
        "Residents have offered their views on city life. Their priorities reflect the services they use, the "
        "journeys they make and the opportunities available in their neighborhoods.",
    .severity = 0, .decay = 0,
  },
  [CITY_NEWS_ADVICE] = {
    .headline =
        "Planning desk: where investment is needed",
    .body =
        "The planning desk is reviewing the city needs. Investments work best when they address a clear shortage "
        "and account for both construction costs and long term maintenance.",
    .severity = 0, .decay = 0,
  },
  [CITY_NEWS_POWER_SHORTAGE] = {
    .headline =
        "Electricity supply struggles to meet demand",
    .body =
        "Demand for electricity is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_TRANSPORT_SHORTAGE] = {
    .headline =
        "Transport links need more capacity",
    .body =
        "Demand for transport is outgrowing the facilities available in =. City planners are reviewing coverage, "
        "connections and capacity. Any expansion will need adequate funding for both construction and future "
        "operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_POLICE_SHORTAGE] = {
    .headline =
        "Police coverage leaves gaps in the city",
    .body =
        "Demand for policing is outgrowing the facilities available in =. City planners are reviewing coverage, "
        "connections and capacity. Any expansion will need adequate funding for both construction and future "
        "operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_FIRE_STATION_SHORTAGE] = {
    .headline =
        "Fire protection needs a wider reach",
    .body =
        "Demand for fire protection is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_WATER_SHORTAGE] = {
    .headline =
        "Water supply needs further investment",
    .body =
        "Demand for water supply is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_HOSPITAL_SHORTAGE] = {
    .headline =
        "Healthcare capacity trails population growth",
    .body =
        "Demand for healthcare is outgrowing the facilities available in =. City planners are reviewing coverage, "
        "connections and capacity. Any expansion will need adequate funding for both construction and future "
        "operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_SCHOOL_SHORTAGE] = {
    .headline =
        "Classrooms struggle to keep pace",
    .body =
        "Demand for schools is outgrowing the facilities available in =. City planners are reviewing coverage, "
        "connections and capacity. Any expansion will need adequate funding for both construction and future "
        "operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_SEAPORT_SHORTAGE] = {
    .headline =
        "Freight businesses ask for port access",
    .body =
        "Demand for port facilities is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_AIRPORT_SHORTAGE] = {
    .headline =
        "Businesses seek better air connections",
    .body =
        "Demand for airport facilities is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_ZOO_SHORTAGE] = {
    .headline =
        "Residents ask for a city zoo",
    .body =
        "Demand for a zoo is outgrowing the facilities available in =. City planners are reviewing coverage, "
        "connections and capacity. Any expansion will need adequate funding for both construction and future "
        "operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_STADIUM_SHORTAGE] = {
    .headline =
        "Local teams ask for a sports venue",
    .body =
        "Demand for sports facilities is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_MARINA_SHORTAGE] = {
    .headline =
        "Boaters call for improved waterfront access",
    .body =
        "Demand for marina access is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_PARK_SHORTAGE] = {
    .headline =
        "Neighborhoods need more public green space",
    .body =
        "Demand for public parks is outgrowing the facilities available in =. City planners are reviewing "
        "coverage, connections and capacity. Any expansion will need adequate funding for both construction and "
        "future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_INDUSTRIAL_CONNECTION_SHORTAGE] = {
    .headline =
        "Industry needs stronger external connections",
    .body =
        "Demand for industrial connections is outgrowing the facilities available in =. City planners are "
        "reviewing coverage, connections and capacity. Any expansion will need adequate funding for both "
        "construction and future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_COMMERCIAL_CONNECTION_SHORTAGE] = {
    .headline =
        "Businesses seek links to neighboring markets",
    .body =
        "Demand for commercial connections is outgrowing the facilities available in =. City planners are "
        "reviewing coverage, connections and capacity. Any expansion will need adequate funding for both "
        "construction and future operation.",
    .severity = 500, .decay = 500,
  },
  [CITY_NEWS_LOW_CRIME] = {
    .headline =
        "Safer streets lift community confidence",
    .body =
        "Improved safety is encouraging residents and businesses. Keeping useful coverage and stable service "
        "funding will help protect the progress already made.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_LOW_TRAFFIC] = {
    .headline =
        "Improved journeys give commuters time back",
    .body =
        "Commuters are seeing less pressure on the road network. Planners are monitoring where capacity and "
        "alternatives to driving are making the greatest difference.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_LOW_POLLUTION] = {
    .headline =
        "Cleaner air marks a welcome change",
    .body =
        "Cleaner conditions are benefiting the city. Officials are reviewing how land use, transport and service "
        "decisions can preserve the improvement as the population grows.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_HIGH_EDUCATION] = {
    .headline =
        "Education outcomes continue to improve",
    .body =
        "Education indicators are improving. Teachers, students and service planners are building on the progress "
        "while preparing for future demand.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_GOOD_HEALTH] = {
    .headline =
        "Health indicators bring encouraging news",
    .body =
        "Health indicators are moving in an encouraging direction. Continued access to care and a healthy local "
        "environment will help sustain the improvement.",
    .severity = 200, .decay = 50,
  },
  [CITY_NEWS_LOW_UNEMPLOYMENT] = {
    .headline =
        "New opportunities strengthen the job market",
    .body =
        "More residents are finding useful employment. A balanced supply of workplaces, reliable services and "
        "regional connections can help maintain the gains.",
    .severity = 200, .decay = 50,
  },
};

// Bind immutable article text directly and initialize the topic groups and ranking inputs without allocating.
// Stock template buffers, when installed by comparison tools, remain separately owned and are released normally.
void InitializeCityNewsResources(CityAssets *assets) {
  assets->newspaper.token_expansions = (CityNewspaperTokenExpansions){0};
  assets->newspaper.articles = kArticles;
  memset(&assets->rankings, 0, sizeof(assets->rankings));
  for (int group = 0; group < 256; ++group) {
    assets->newspaper.group_starts.value[group] =
        group < CITY_NEWS_EVENT_COUNT ? group : CITY_NEWS_LOCAL_NEWS;
    assets->newspaper.group_counts.value[group] = 1;
    if (group >= CITY_NEWS_EVENT_COUNT) continue;
    assets->rankings.event_severity[group] = kArticles[group].severity;
    assets->rankings.event_decay[group] = kArticles[group].decay;
  }
  assets->news_resources_unavailable = 0;
}

void ReleaseCityNewspaperResources(CityNewspaperResources *resources) {
  free(resources->text);
  free(resources->offsets);
  resources->articles = NULL;
  resources->text = NULL;
  resources->offsets = NULL;
  resources->text_capacity = 0;
  resources->offsets_capacity = 0;
}
