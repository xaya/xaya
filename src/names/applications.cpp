// Copyright (c) 2021 Jeremy Rand
// Copyright (c) 2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <names/applications.h>

#include <names/encoding.h>

#include <univalue.h>

namespace
{

NameNamespace
NamespaceFromString (const std::string& str)
{
    if (str == "g/")
        return NameNamespace::Game;
    if (str == "p/")
        return NameNamespace::Player;

    return NameNamespace::NonStandard;
}

std::string
NamespaceToString (NameNamespace ns)
{
    switch (ns)
    {
        case NameNamespace::Game:
            return "g/";
        case NameNamespace::Player:
            return "p/";
        case NameNamespace::NonStandard:
            return "";
    }

    /* NameNamespace values are only ever created internally in the binary
    and not received externally (except as string).  Thus it should never
    happen (and if it does, is a severe bug) that we see an unexpected
    value.  */
    assert (false);
}

NameNamespace
PurportedNamespaceFromName (const std::string& name)
{
    const auto slashPos = name.find("/");
    if (slashPos == std::string::npos)
    {
        return NameNamespace::NonStandard;
    }

    const std::string namespaceStr = name.substr(0, slashPos+1);
    return NamespaceFromString(namespaceStr);
}

} // anonymous namespace

NameNamespace
NamespaceFromName (const std::string& name)
{
    const NameNamespace purported = PurportedNamespaceFromName(name);
    const std::string purportedStr = NamespaceToString(purported);
    const auto purportedLen = purportedStr.length();

    const std::string label = name.substr(purportedLen);

    if (label.empty())
    {
        return NameNamespace::NonStandard;
    }

    /* In Xaya, we don't have to do any specific validation on the
       purported namespace, as there are no extra rules besides names
       being valid UTF8, which is already enforced both by consensus
       rules and also by redundantly by the decoding step in the
       valtype-accepting overload for NamespaceFromName.  */
    return purported;
}

NameNamespace
NamespaceFromName (const valtype& data)
{
    std::string name;

    try
    {
        name = EncodeName (data, NameEncoding::UTF8);
    }
    catch (const InvalidNameString& exc)
    {
        return NameNamespace::NonStandard;
    }

    return NamespaceFromName(name);
}

std::string
DescFromName (const valtype& name, NameNamespace ns)
{
    switch (ns)
    {
        default:
        {
            return EncodeNameForMessage(name);
        }
    }
}

bool
IsValidJSONOrEmptyString (const std::string& text){
    UniValue v;

    return text.empty() || v.read(text);
}
