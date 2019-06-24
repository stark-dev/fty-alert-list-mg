#include <cxxtools/utf8codec.h>
#include <cxxtools/jsonserializer.h>
#include <cxxtools/jsondeserializer.h>
#include <fstream>
#include <algorithm>
#include <sstream>

#include "rule.h"

// 1, ..., 4 - # of utf8 octets
// -1 - error
static int8_t
utf8_octets (const std::string& s, std::string::size_type pos)
{
    assert (pos < s.length ());

    const char c = s[pos];
    if ((c & 0x80 ) == 0) {     // lead bit is zero, must be a single ascii
        return 1;
    }
    else
    if ((c & 0xE0 ) == 0xC0 ) { // 110x xxxx (2 octets)
        return 2;
    }
    else
    if ((c & 0xF0 ) == 0xE0 ) { // 1110 xxxx (3 octets)
        return 3;
    }
    else
    if ((c & 0xF8 ) == 0xF0 ) { // 1111 0xxx (4 octets)
        return 4;
    }
    else {
        log_error ("Unrecognized utf8 lead byte '%x' in string '%s'", c, s.c_str ());
        return -1;
    }
}

// 0 - same
// 1 - different
static int
utf8_compare_octets (const std::string& s1, std::string::size_type s1_pos, const std::string& s2, std::string::size_type s2_pos, uint8_t count)
{
    assert (count >= 1 && count <= 4);
    assert (s1_pos + count <= s1.length ());
    assert (s2_pos + count <= s2.length ());

    for (int i = 0; i < count; i++) {
        const char c1 = s1[s1_pos + i];
        const char c2 = s2[s2_pos + i];

        if ((count == 1 && tolower (c1) != tolower (c2)) ||
            (count > 1  && c1 != c2))
            return 1;
    }
    return 0;
}

int
utf8eq (const std::string& s1, const std::string& s2)
{
    if (s1.length () != s2.length ())
        return 0;

    std::string::size_type s1_pos = 0, s2_pos = 0;
    std::string::size_type length = s1.length ();


    while (s1_pos < length &&
           s2_pos < length)
    {
        uint8_t s1_octets = utf8_octets (s1, s1_pos);
        uint8_t s2_octets = utf8_octets (s2, s2_pos);

        if (s1_octets == -1 || s2_octets == -1)
            return -1;

        if (s1_octets != s2_octets)
            return 0;

        if (utf8_compare_octets (s1, s1_pos, s2, s2_pos, s1_octets) == 1)
            return 0;

        s1_pos = s1_pos + s1_octets;
        s2_pos = s2_pos + s1_octets;
    }
    return 1;
}

void
si_getValueUtf8 (const cxxtools::SerializationInfo& si, const std::string& membername_, std::string& result)
{
    std::basic_string <cxxtools::Char> cxxtools_Charname_;
    si.getMember (membername_).getValue (cxxtools_Charname_);
    result = cxxtools::Utf8Codec::encode (cxxtools_Charname_);
}

Rule::Rule (const std::string json) {
    std::istringstream iss (json);
    cxxtools::JsonDeserializer jd (iss);
    jd.deserialize (*this);
}

void Rule::setGlobalVariables (const VariableMap vars) {
    variables_.clear ();
    variables_.insert (vars.cbegin (), vars.cend ());
}

std::string Rule::getJsonRule (void) const {
    std::stringstream s;
    cxxtools::JsonSerializer js (s);
    js.beautify (true);
    cxxtools::SerializationInfo si;
    try {
        saveToSerializedObject (si);
    } catch (std::exception &e) {
        log_error ("unable to serialize rule due to %s", e.what ());
        throw std::runtime_error ("unable to serialize rule due to " + std::string (e.what ()));
    }
    js.serialize (si).finish ();
    return s.str ();
}

void Rule::save (const std::string &path) const {
    std::string fullname = path + name_ + ".rule";
    log_debug ("trying to save file : '%s'", fullname.c_str ());
    std::ofstream ofs (fullname, std::ofstream::out);
    ofs.exceptions (~std::ofstream::goodbit);
    ofs << getJsonRule ();
    ofs.close ();
}

int Rule::remove (const std::string &path) {
    std::string fullname = path + name_ + ".rule";
    log_debug ("trying to remove file : '%s'", fullname.c_str ());
    return std::remove (fullname.c_str ());
}

RuleNameMatcher::RuleNameMatcher (const std::string &name) :
    name_ (name) {
}

bool RuleNameMatcher::operator ()(const Rule &rule) {
    return rule.getName () == name_;
}

RuleAssetMatcher::RuleAssetMatcher (const std::string &asset) :
    asset_ (asset) {
}

bool RuleAssetMatcher::operator ()(const Rule &rule) {
    return std::find (rule.getAssets ().begin (), rule.getAssets ().end (), asset_) != rule.getAssets ().end ();
}

/*
 * \brief Deserialization
 */
/// deserialization of rule
void operator>>= (const cxxtools::SerializationInfo& si, Rule &rule) {
    rule.loadFromSerializedObject (si);
}
/// deserialization of outcome
void operator>>= (const cxxtools::SerializationInfo& si, Rule::Outcome& outcome)
{
    const cxxtools::SerializationInfo &actions = si.getMember ("action");
    outcome.actions_.clear ();
    outcome.actions_.reserve (actions.memberCount ());
    for ( const auto &a : actions) {
        std::string type, res;
        switch (a.category ()) {
        case cxxtools::SerializationInfo::Value:
            // old-style format ["EMAIL", "SMS"]
            outcome.actions_.resize (outcome.actions_.size () + 1);
            a >>= outcome.actions_.back ();
            break;
        case cxxtools::SerializationInfo::Object:
            // [{"action": "EMAIL"}, {"action": "SMS"}]
            a.getMember ("action") >>= type;
            if (type == "EMAIL" || type == "SMS") {
                res = type;
            } else if (type == "GPO_INTERACTION") {
                std::string asset, mode;
                a.getMember ("asset") >>= asset;
                a.getMember ("mode") >>= mode;
                res = type + ":" + asset + ":" + mode;
            } else {
                log_warning ("Unknown action type: \"%s\"", type.c_str ());
                res = type;
            }
            outcome.actions_.push_back (res);
            break;
        default:
            throw std::runtime_error ("Invalid format of action");
        }
    }
    const cxxtools::SerializationInfo *severity = si.findMember ("severity");
    if (severity != nullptr)
        *severity >>= outcome.severity_;
    const cxxtools::SerializationInfo *threshold_name = si.findMember ("threshold_name");
    if (threshold_name != nullptr)
        *threshold_name >>= outcome.threshold_name_;
    const cxxtools::SerializationInfo *description = si.findMember ("description");
    if (description != nullptr)
        *description >>= outcome.description_;
}
// TODO error handling mistakes can be hidden here
/// deserialization of variables (values)
void operator>>= (const cxxtools::SerializationInfo& si, Rule::VariableMap &values)
{
    /*
       "values":[ {"low_critical"  : "30"},
                  {"low_warning"   : "40"},
                  {"high_warning"  : "50"},
                  {"high_critical" : "60"} ]
    */
    for ( const auto &oneElement : si ) { // iterate through the array
        auto variableName = oneElement.getMember (0).name ();
        std::string valueString;
        oneElement.getMember (0) >>= valueString;
        try {
            values.emplace (variableName, valueString);
        }
        catch (const std::exception &e ) {
            log_error ("Value '%s' is not double", valueString.c_str ());
            throw std::runtime_error ("Value should be double");
        }
    }
}
// TODO error handling mistakes can be hidden here
/// deserialization of results
void operator>>= (const cxxtools::SerializationInfo& si, Rule::ResultsMap &outcomes)
{
    /*
        "results":[ {"low_critical"  : { "action" : [{ "action": "EMAIL"},{ "action": "SMS"}], "description" : "WOW low critical description" }},
                    {"low_warning"   : { "action" : [{ "action": "EMAIL"}], "description" : "wow LOW warning description"}},
                    {"high_warning"  : { "action" : [{ "action": "EMAIL"}], "description" : "wow high WARNING description" }},
                    {"high_critical" : { "action" : [{ "action": "EMAIL"}], "description" : "wow high critical DESCTIPRION" } } ]
    */
    for ( const auto &oneElement : si ) { // iterate through the array
        //we should ensure that only one member is present
        if (oneElement.memberCount ()!=1){
            throw std::runtime_error ("unexpected member count element in results");
        }
        auto outcomeName = oneElement.getMember (0).name ();
        Rule::Outcome outcome;
        oneElement.getMember (0) >>= outcome;
        if (outcome.severity_.empty ()) {
            if ( outcomeName == "low_critical" || outcomeName == "high_critical" ) {
                outcome.severity_ = "CRITICAL";
            }
            if ( outcomeName == "low_warning" || outcomeName == "high_warning" ) {
                outcome.severity_ = "WARNING";
            }
            if ( outcome.severity_.empty () ) {
                throw std::runtime_error ("unsupported result");
            }
        }
        outcomes.emplace (outcomeName, outcome);
    }
}
void Rule::loadMandatoryString (const cxxtools::SerializationInfo &si, const std::string name, std::string &target) {
    const cxxtools::SerializationInfo &elem = si.getMember (name);
    if (elem.category () != cxxtools::SerializationInfo::Value) {
        log_error ("%s property must be value type.", name.c_str ());
        throw std::runtime_error (name + " property must be value type.");
    }
    elem >>= target;
}
void Rule::loadOptionalString (const cxxtools::SerializationInfo &si, const std::string name, std::string &target) {
    const cxxtools::SerializationInfo *elem = si.findMember (name); // optional
    if (elem != nullptr) {
        if (elem->category () != cxxtools::SerializationInfo::Value) {
            log_error ("%s property must be value type.", name.c_str ());
            throw std::runtime_error (name + " property must be value type.");
        } else {
            (*elem) >>= target;
        }
    }
}
void Rule::loadOptionalInt (const cxxtools::SerializationInfo &si, const std::string name, int &target) {
    const cxxtools::SerializationInfo *elem = si.findMember (name); // optional
    if (elem != nullptr) {
        if (elem->category () != cxxtools::SerializationInfo::Value) {
            log_error ("%s property must be value type.", name.c_str ());
            throw std::runtime_error (name + " property must be value type.");
        } else {
            (*elem) >>= target;
        }
    }
}
void Rule::loadOptionalArray (const cxxtools::SerializationInfo &si, const std::string name, Rule::VectorStrings &target) {
    const cxxtools::SerializationInfo *elem = si.findMember (name); // optional
    if (elem != nullptr) {
        if (elem->category () != cxxtools::SerializationInfo::Array) {
            log_error ("%s property must be an array type.", name.c_str ());
            throw std::runtime_error (name + " property must be an array type.");
        }
        for (size_t i = 0; i < elem->memberCount (); ++i) {
            std::string val;
            elem->getMember (i).getValue (val);
            target.push_back (val);
        }
    }
}
void Rule::loadMandatoryArray (const cxxtools::SerializationInfo &si, const std::string name, Rule::VectorStrings &target) {
    const cxxtools::SerializationInfo &elem = si.getMember (name); // mandatory
    if (elem.category () != cxxtools::SerializationInfo::Array) {
        log_error ("%s property must be an array type.", name.c_str ());
        throw std::runtime_error (name + " property must be an array type.");
    }
    for (size_t i = 0; i < elem.memberCount (); ++i) {
        std::string val;
        elem.getMember (i).getValue (val);
        target.push_back (val);
    }
}
void Rule::loadMandatoryArrayOrValue (const cxxtools::SerializationInfo &si, const std::string name, Rule::VectorStrings &target) {
    const cxxtools::SerializationInfo &elem = si.getMember (name); // mandatory
    if (elem.category () == cxxtools::SerializationInfo::Value) {
        std::string val;
        elem >>= val;
        target.push_back (val);
    } else if (elem.category () == cxxtools::SerializationInfo::Array) {
        for (size_t i = 0; i < elem.memberCount (); ++i) {
            std::string val;
            elem.getMember (i).getValue (val);
            target.push_back (val);
        }
    } else {
        log_error ("%s property must be either an array type or value type.", name.c_str ());
        throw std::runtime_error (name + " property must be either an array type or value type.");
    }
}

void Rule::loadFromSerializedObject (const cxxtools::SerializationInfo &si) {
    try {
        auto elem_content = si.getMember (0);
        if (elem_content.category () != cxxtools::SerializationInfo::Object) {
            log_error ("Root of json must be an object with property 'single|pattern|threshold|flexible'.");
            throw std::runtime_error ("Root of json must be an object with property 'single|pattern|threshold|flexible'.");
        }
        loadMandatoryString (elem_content, "name", name_);
        loadOptionalString (elem_content, "description", description_);
        loadOptionalString (elem_content, "class", class_);
        loadMandatoryArray (elem_content, "categories", categories_);
        loadMandatoryArrayOrValue (elem_content, "metrics", metrics_);
        const cxxtools::SerializationInfo &elem_results = elem_content.getMember ("results"); // mandatory
        if ( elem_results.category () != cxxtools::SerializationInfo::Array ) {
            log_error ("results property must be an array type.");
            throw std::runtime_error ("results property must be an array type.");
        }
        elem_results >>= results_;
        loadOptionalString (elem_content, "source", source_);
        loadMandatoryArrayOrValue (elem_content, "assets", assets_);
        auto elem_values = elem_content.findMember ("values"); // optional for general rule
        if (elem_values != nullptr) {
            if (elem_values->category () != cxxtools::SerializationInfo::Array ) {
                log_error ("values property must be an array type.");
                throw std::runtime_error ("values property must be an array type.");
            }
            (*elem_values) >>= variables_;
        }
        loadOptionalString (elem_content, "values_unit", value_unit_);
        loadOptionalString (elem_content, "hierarchy", hierarchy_);
    } catch (std::exception &e) {
        std::ostringstream oss;
        si.dump (oss);
        log_error ("An error '%s' was caught while trying to read rule %s", e.what (), oss.str ().c_str ());
        throw e;
    }
}

/*
 * \brief Serialization part
 */
/// serialization of outcome
void operator<<= (cxxtools::SerializationInfo& si, const Rule::Outcome& outcome)
{
    cxxtools::SerializationInfo &actions = si.addMember ("action");
    actions.setCategory (cxxtools::SerializationInfo::Array);
    for (auto &act : outcome.actions_) {
        if (act == "EMAIL" || act == "SMS") {
            cxxtools::SerializationInfo &one_action = actions.addMember (std::string ());
            one_action.setCategory (cxxtools::SerializationInfo::Object);
            one_action.addMember ("action") <<= act;
        } else if (0 == act.compare (0, strlen ("GPO_INTERACTION"), "GPO_INTERACTION")) {
            cxxtools::SerializationInfo &one_action = actions.addMember (std::string ());
            one_action.setCategory (cxxtools::SerializationInfo::Object);
            size_t action_action_end = strlen ("GPO_INTERACTION");
            size_t action_asset_end = act.find_last_of (":");
            std::string action_action = act.substr (0, action_action_end);
            std::string action_asset = act.substr (action_action_end + 1, action_asset_end - action_action_end - 1);
            std::string action_mode = act.substr (action_asset_end + 1);
            one_action.addMember ("action") <<= action_action;
            one_action.addMember ("asset") <<= action_asset;
            one_action.addMember ("mode") <<= action_mode;
        } else {
            log_warning ("Unable to serialize outcome action %s", act.c_str ());
        }
    }
    si.addMember ("severity") <<= outcome.severity_;
    si.addMember ("description") <<= outcome.description_;
    si.addMember ("threshold_name") <<= outcome.threshold_name_;
}
/// serialization of variables (values)
void operator<<= (cxxtools::SerializationInfo& si, const Rule::VariableMap &values)
{
    cxxtools::SerializationInfo &element = si.addMember ("values");
    element.setCategory (cxxtools::SerializationInfo::Array);
    for (auto &val : values) {
        cxxtools::SerializationInfo &item_parent = element.addMember (std::string ());
        item_parent.setCategory (cxxtools::SerializationInfo::Object);
        item_parent.addMember (val.first) <<= val.second;
    }
}
/// serialization of results
void operator<<= (cxxtools::SerializationInfo& si, const Rule::ResultsMap &outcomes)
{
    cxxtools::SerializationInfo &results = si.addMember ("results");
    results.setCategory (cxxtools::SerializationInfo::Array);
    for (auto &res : outcomes) {
        cxxtools::SerializationInfo &item_parent = results.addMember (std::string ());
        item_parent.setCategory (cxxtools::SerializationInfo::Object);
        cxxtools::SerializationInfo &item = item_parent.addMember (res.first);
        item.setCategory (cxxtools::SerializationInfo::Object);
        item <<= res.second;
    }
}

void Rule::saveToSerializedObject (cxxtools::SerializationInfo &si) const {
    cxxtools::SerializationInfo &root = si.addMember (whoami ());
    root.setCategory (cxxtools::SerializationInfo::Object);
    root.addMember ("name") <<= name_;
    if (!description_.empty ())
        root.addMember ("description") <<= description_;
    if (!class_.empty ())
        root.addMember ("class") <<= class_;
    root.addMember ("categories") <<= categories_;
    root.addMember ("metrics") <<= metrics_;
    root <<= results_;
    if (!source_.empty ())
        root.addMember ("source") <<= source_;
    root.addMember ("assets") <<= assets_;
    root <<= variables_;
    if (!value_unit_.empty ())
        root.addMember ("values_unit") <<= value_unit_;
    if (!hierarchy_.empty ())
        root.addMember ("hierarchy") <<= hierarchy_;
}

bool Rule::compare (const Rule &rule) const {
    return rule.name_ == name_ && rule.description_ == description_ && rule.class_ == class_ &&
        rule.categories_ == categories_ && rule.metrics_ == metrics_ && rule.results_ == results_ &&
        rule.source_ == source_ && rule.assets_ == assets_ && rule.variables_ == variables_ &&
        rule.value_unit_ == value_unit_ && rule.hierarchy_ == hierarchy_;
}